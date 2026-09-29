#include "MiniTask13ProbeActor.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameModes/MiniGameMode.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "TimerManager.h"

AMiniTask13ProbeActor::AMiniTask13ProbeActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
}

AMiniPlayerController* AMiniTask13ProbeActor::GetProbeController() const
{
	return Cast<AMiniPlayerController>(GetOwner());
}

AController* AMiniTask13ProbeActor::GetOtherController() const
{
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (*It != GetProbeController() && It->GetPawn())
		{
			return *It;
		}
	}
	return nullptr;
}

AMiniGameMode* AMiniTask13ProbeActor::GetMiniGameMode() const
{
	return GetWorld() ? GetWorld()->GetAuthGameMode<AMiniGameMode>() : nullptr;
}

UMiniAbilitySystemComponent* AMiniTask13ProbeActor::GetProbeASC() const
{
	const AMiniPlayerController* Controller = GetProbeController();
	const AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetMiniAbilitySystemComponent() : nullptr;
}

UMiniHealthSet* AMiniTask13ProbeActor::GetProbeHealthSet() const
{
	const AMiniPlayerController* Controller = GetProbeController();
	const AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetHealthSet() : nullptr;
}

void AMiniTask13ProbeActor::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask13Probe FAIL: %s Phase=%d"),
			Reason, static_cast<int32>(Phase));
	}
}

int32 AMiniTask13ProbeActor::GetAbilityCount(const UMiniAbilitySystemComponent* ASC) const
{
	return ASC ? ASC->GetActivatableAbilities().Num() : INDEX_NONE;
}

bool AMiniTask13ProbeActor::CheckAliveBaseline(AMiniCharacter* Pawn, float ExpectedHealth) const
{
	const UMiniAbilitySystemComponent* ASC = GetProbeASC();
	const UMiniHealthSet* HealthSet = GetProbeHealthSet();
	const UMiniHealthComponent* HealthComponent = Pawn ? Pawn->GetHealthComponent() : nullptr;
	return IsValid(Pawn) && ASC && HealthSet && HealthComponent &&
		ASC->GetAvatarActor() == Pawn && !HealthComponent->IsDead() &&
		!ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) &&
		FMath::IsNearlyEqual(HealthSet->GetHealth(), ExpectedHealth, 0.01f) &&
		FMath::IsNearlyEqual(HealthSet->GetMaxHealth(), 100.0f, 0.01f);
}

void AMiniTask13ProbeActor::ServerApplyNonlethal_Implementation()
{
	AMiniPlayerController* Controller = GetProbeController();
	AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	AMiniGameMode* GameMode = GetMiniGameMode();
	AController* OtherController = GetOtherController();
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	UMiniHealthSet* HealthSet = GetProbeHealthSet();
	if (bFailed || Phase != EPhase::Initial || !GameMode || !Controller || !OtherController ||
		!CheckAliveBaseline(Pawn, 100.0f) || !ASC || !HealthSet)
	{
		Fail(TEXT("initial damage preconditions failed"));
		return;
	}
	OriginalASC = ASC;
	OriginalAbilityCount = GetAbilityCount(ASC);
	if (OriginalAbilityCount < 3)
	{
		Fail(TEXT("initial Pawn lacks the baseline abilities"));
		return;
	}
	if (GameMode->TryApplyTestDamage(Controller, Pawn, 10.0f) ||
		GameMode->TryApplyTestDamage(OtherController, nullptr, 10.0f) ||
		!CheckAliveBaseline(Pawn, 100.0f))
	{
		Fail(TEXT("self or invalid-target damage was accepted"));
		return;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask13Probe SERVER_INVALID_PASS: Self=1 Null=1 Health=%.1f"),
		HealthSet->GetHealth());
	if (!GameMode->TryApplyTestDamage(OtherController, Pawn, 25.0f) ||
		!CheckAliveBaseline(Pawn, 75.0f))
	{
		Fail(TEXT("authoritative nonlethal damage failed"));
		return;
	}
	Phase = EPhase::NonlethalApplied;
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask13Probe SERVER_DAMAGE: Cycle=1 Health=%.1f Max=%.1f Pawn=%s"),
		HealthSet->GetHealth(), HealthSet->GetMaxHealth(), *Pawn->GetPathName());
}

void AMiniTask13ProbeActor::ServerApplyLethal_Implementation(int32 Cycle)
{
	const EPhase ExpectedPhase = Cycle == 1 ? EPhase::NonlethalApplied : EPhase::FirstRespawnVerified;
	const float ExpectedHealth = Cycle == 1 ? 75.0f : 100.0f;
	AMiniPlayerController* Controller = GetProbeController();
	AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	AMiniGameMode* GameMode = GetMiniGameMode();
	AController* OtherController = GetOtherController();
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	UMiniHealthSet* HealthSet = GetProbeHealthSet();
	if (bFailed || (Cycle != 1 && Cycle != 2) || Phase != ExpectedPhase ||
		!GameMode || !OtherController || !ASC || ASC != OriginalASC.Get() ||
		GetAbilityCount(ASC) != OriginalAbilityCount || !HealthSet ||
		!CheckAliveBaseline(Pawn, ExpectedHealth))
	{
		Fail(TEXT("lethal damage preconditions failed"));
		return;
	}
	LastDeadPawn = Pawn;
	LastDeadPawnPath = Pawn->GetPathName();
	LastDeathTime = GetWorld()->GetTimeSeconds();
	if (!GameMode->TryApplyTestDamage(OtherController, Pawn, 100.0f) ||
		!FMath::IsNearlyZero(HealthSet->GetHealth(), 0.01f))
	{
		Fail(TEXT("authoritative lethal damage failed"));
		return;
	}
	if (!IsValid(Pawn) || GameMode->TryApplyTestDamage(OtherController, Pawn, 20.0f) ||
		!FMath::IsNearlyZero(HealthSet->GetHealth(), 0.01f))
	{
		Fail(TEXT("dead target accepted repeated damage"));
		return;
	}
	Phase = Cycle == 1 ? EPhase::FirstDead : EPhase::SecondDead;
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask13Probe SERVER_DEAD: Cycle=%d Health=%.1f RepeatRejected=1 Pawn=%s"),
		Cycle, HealthSet->GetHealth(), *LastDeadPawnPath);
}

void AMiniTask13ProbeActor::ServerVerifyRespawn_Implementation(int32 Cycle)
{
	const EPhase ExpectedPhase = Cycle == 1 ? EPhase::FirstDead : EPhase::SecondDead;
	AMiniPlayerController* Controller = GetProbeController();
	AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	UMiniHealthSet* HealthSet = GetProbeHealthSet();
	const float Delay = GetWorld() ? GetWorld()->GetTimeSeconds() - LastDeathTime : 0.0f;
	if (bFailed || (Cycle != 1 && Cycle != 2) || Phase != ExpectedPhase ||
		!Controller || !Pawn || Pawn == LastDeadPawn.Get() ||
		Pawn->GetPathName() == LastDeadPawnPath || !ASC || ASC != OriginalASC.Get() ||
		GetAbilityCount(ASC) != OriginalAbilityCount || !HealthSet ||
		!CheckAliveBaseline(Pawn, 100.0f) || Delay < 2.5f || Delay > 10.0f)
	{
		Fail(TEXT("replacement Pawn, ASC, health, or respawn timing invalid"));
		return;
	}
	// Any delayed cleanup from the previous Pawn must leave this Avatar intact.
	if (AMiniCharacter* OldPawn = LastDeadPawn.Get())
	{
		OldPawn->GetPawnExtensionComponent()->UninitializeAbilitySystem(true);
		if (ASC->GetAvatarActor() != Pawn)
		{
			Fail(TEXT("old Pawn cleanup cleared the replacement Avatar"));
			return;
		}
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask13Probe SERVER_RESPAWN: Cycle=%d OldPawn=%s NewPawn=%s ASC=%s Abilities=%d Health=%.1f Delay=%.2f"),
		Cycle, *LastDeadPawnPath, *Pawn->GetPathName(), *ASC->GetPathName(),
		GetAbilityCount(ASC), HealthSet->GetHealth(), Delay);
	Phase = Cycle == 1 ? EPhase::FirstRespawnVerified : EPhase::SecondRespawnVerified;
}

void AMiniTask13ProbeActor::ServerForceRespawnAfterDeath_Implementation()
{
	AMiniPlayerController* Controller = GetProbeController();
	AMiniCharacter* OldPawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	AMiniGameMode* GameMode = GetMiniGameMode();
	AController* OtherController = GetOtherController();
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	UMiniHealthSet* HealthSet = GetProbeHealthSet();
	if (bFailed || Phase != EPhase::SecondRespawnVerified || !Controller || !GameMode ||
		!OtherController || !ASC || ASC != OriginalASC.Get() ||
		GetAbilityCount(ASC) != OriginalAbilityCount || !HealthSet ||
		!CheckAliveBaseline(OldPawn, 100.0f))
	{
		Fail(TEXT("forced-death preconditions failed"));
		return;
	}
	ForcedOldPawn = OldPawn;
	ForcedOldPawnPath = OldPawn->GetPathName();
	if (!GameMode->TryApplyTestDamage(OtherController, OldPawn, 100.0f) ||
		!FMath::IsNearlyZero(HealthSet->GetHealth(), 0.01f) ||
		!OldPawn->GetHealthComponent()->IsDead() ||
		!ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead))
	{
		Fail(TEXT("forced lethal hit did not enter server death state"));
		return;
	}
	Controller->UnPossess();
	if (Controller->GetPawn() || ASC->GetAvatarActor() ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) ||
		GetAbilityCount(ASC) != OriginalAbilityCount)
	{
		Fail(TEXT("forced UnPossess retained Avatar or Death GameplayEffect"));
		return;
	}
	Phase = EPhase::ForcedRestartPending;
	ForcedRestartTime = GetWorld()->GetTimeSeconds();
	GameMode->RestartPlayer(Controller);
	VerifyForcedRespawn();
}

void AMiniTask13ProbeActor::VerifyForcedRespawn()
{
	if (bFailed || Phase != EPhase::ForcedRestartPending)
	{
		return;
	}
	AMiniPlayerController* Controller = GetProbeController();
	AMiniCharacter* OldPawn = ForcedOldPawn.Get();
	AMiniCharacter* NewPawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	UMiniHealthSet* HealthSet = GetProbeHealthSet();
	if (!Controller || !IsValid(OldPawn) || !ASC || ASC != OriginalASC.Get() ||
		GetAbilityCount(ASC) != OriginalAbilityCount || !HealthSet ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead))
	{
		Fail(TEXT("forced restart lost controller, old Pawn, ASC, abilities, or death cleanup"));
		return;
	}
	if (!NewPawn || NewPawn == OldPawn || NewPawn->GetPathName() == ForcedOldPawnPath ||
		!CheckAliveBaseline(NewPawn, 100.0f))
	{
		if (GetWorld()->GetTimeSeconds() - ForcedRestartTime > 2.0f)
		{
			Fail(TEXT("forced restart did not restore health and a new Avatar"));
			return;
		}
		FTimerHandle RetryTimer;
		GetWorld()->GetTimerManager().SetTimer(RetryTimer,
			FTimerDelegate::CreateUObject(this, &ThisClass::VerifyForcedRespawn), 0.1f, false);
		return;
	}
	// Teardown callbacks may arrive after possession of the replacement Pawn.
	OldPawn->GetHealthComponent()->UninitializeAbilitySystem();
	OldPawn->GetPawnExtensionComponent()->UninitializeAbilitySystem(true);
	if (!CheckAliveBaseline(NewPawn, 100.0f))
	{
		Fail(TEXT("late old-Pawn cleanup damaged the replacement Avatar"));
		return;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask13Probe SERVER_FORCED_CLEANUP: OldPawn=%s NewPawn=%s ASC=%s Abilities=%d Health=%.1f DeadTag=0"),
		*ForcedOldPawnPath, *NewPawn->GetPathName(), *ASC->GetPathName(),
		GetAbilityCount(ASC), HealthSet->GetHealth());
	OldPawn->Destroy();
	Phase = EPhase::Done;
}
