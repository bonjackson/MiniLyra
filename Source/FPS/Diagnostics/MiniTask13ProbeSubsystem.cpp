#include "MiniTask13ProbeSubsystem.h"

#include "MiniTask13ProbeActor.h"
#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Camera/MiniCameraComponent.h"
#include "Camera/MiniCameraMode.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

namespace
{
void SendTask13Key(AMiniPlayerController* Controller, const FKey& Key, EInputEvent Event)
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(
		Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}

bool IsTask13CameraMode(AMiniCharacter* Pawn, bool bAim)
{
	UMiniCameraComponent* Camera = Pawn ? Pawn->GetMiniCameraComponent() : nullptr;
	const UMiniPawnData* Data = Pawn ? Pawn->GetPawnData() : nullptr;
	if (!Camera || !Data || !Data->DefaultCameraMode || !Data->AimCameraMode)
	{
		return false;
	}
	FMinimalViewInfo View;
	Camera->GetCameraView(0.25f, View);
	return Camera->GetCurrentCameraMode().Get() ==
		(bAim ? Data->AimCameraMode.Get() : Data->DefaultCameraMode.Get());
}

int32 GetTask13AbilityCount(const UMiniAbilitySystemComponent* ASC)
{
	return ASC ? ASC->GetActivatableAbilities().Num() : INDEX_NONE;
}
}

bool UMiniTask13ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask13"));
#else
	return false;
#endif
}

TStatId UMiniTask13ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask13ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask13ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask13Probe FAIL: %s Stage=%d"),
			Reason, static_cast<int32>(Stage));
	}
}

void UMiniTask13ProbeSubsystem::SetStage(EStage NewStage)
{
	Stage = NewStage;
	StageSeconds = 0.0f;
}

void UMiniTask13ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || Stage == EStage::Done || !GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer)
	{
		TickServer();
	}
	else if (GetWorld()->GetNetMode() == NM_Client)
	{
		TickClient(DeltaTime);
	}
}

void UMiniTask13ProbeSubsystem::TickServer()
{
	if (bActorSpawned)
	{
		return;
	}
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		AMiniPlayerController* Controller = *It;
		AMiniCharacter* Pawn = Controller && !Controller->IsLocalController()
			? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
		AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
		UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
		const UMiniHealthSet* HealthSet = State ? State->GetHealthSet() : nullptr;
		if (!Pawn || !Pawn->GetHealthComponent() || !ASC || !HealthSet ||
			ASC->GetAvatarActor() != Pawn || GetTask13AbilityCount(ASC) < 3)
		{
		continue;
		}
		FActorSpawnParameters Params;
		Params.Owner = Controller;
		AMiniTask13ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask13ProbeActor>(
			AMiniTask13ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Probe)
		{
			Fail(TEXT("could not spawn replicated authority probe"));
			return;
		}
		bActorSpawned = true;
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask13Probe SERVER_READY: Pawn=%s ASC=%s Actor=%s"),
			*Pawn->GetPathName(), *ASC->GetPathName(), *Probe->GetPathName());
		return;
	}
}

void UMiniTask13ProbeSubsystem::TickClient(float DeltaTime)
{
	StageSeconds += DeltaTime;
	if (StageSeconds > (Stage == EStage::WaitReady ? 90.0f : 20.0f))
	{
		Fail(TEXT("probe stage timed out"));
		return;
	}
	AMiniPlayerController* Controller = nullptr;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsLocalController())
		{
			Controller = *It;
			break;
		}
	}
	if (!Controller)
	{
		return;
	}
	AMiniTask13ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask13ProbeActor> It(GetWorld()); It; ++It)
	{
		if (It->GetOwner() == Controller)
		{
			Probe = *It;
			break;
		}
	}
	AMiniPlayerState* PlayerState = Controller->GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetMiniAbilitySystemComponent() : nullptr;
	UMiniHealthSet* HealthSet = PlayerState ? PlayerState->GetHealthSet() : nullptr;
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(Controller->GetPawn());
	if (!Probe || !ASC || !HealthSet)
	{
		return;
	}
	const bool bDeadTag = ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead);
	const bool bAiming = ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Aiming);
	const bool bHealthFull = FMath::IsNearlyEqual(HealthSet->GetHealth(), 100.0f, 0.01f) &&
		FMath::IsNearlyEqual(HealthSet->GetMaxHealth(), 100.0f, 0.01f);
	const bool bPawnReady = Pawn && Pawn->GetHealthComponent() &&
		!Pawn->GetHealthComponent()->IsDead() && Pawn->GetHeroComponent() &&
		Pawn->GetHeroComponent()->IsInputActive() && Controller->GetViewTarget() == Pawn &&
		ASC->GetAvatarActor() == Pawn && GetTask13AbilityCount(ASC) == OriginalAbilityCount &&
		bHealthFull && !bDeadTag && !bAiming && IsTask13CameraMode(Pawn, false);
	switch (Stage)
	{
	case EStage::WaitReady:
		if (Pawn && Pawn->GetHealthComponent() && !Pawn->GetHealthComponent()->IsDead() &&
			Pawn->GetHeroComponent() && Pawn->GetHeroComponent()->IsInputActive() &&
			Controller->GetViewTarget() == Pawn && ASC->GetAvatarActor() == Pawn &&
			GetTask13AbilityCount(ASC) >= 3 && bHealthFull && !bDeadTag && IsTask13CameraMode(Pawn, false))
		{
			OriginalASC = ASC;
			OriginalAbilityCount = GetTask13AbilityCount(ASC);
			LastPawn = Pawn;
			LastPawnPath = Pawn->GetPathName();
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_READY: Pawn=%s ASC=%s Abilities=%d Health=%.1f Max=%.1f"),
				*LastPawnPath, *ASC->GetPathName(), OriginalAbilityCount,
				HealthSet->GetHealth(), HealthSet->GetMaxHealth());
			Probe->ServerApplyNonlethal();
			SetStage(EStage::WaitNonlethal);
		}
		break;
	case EStage::WaitNonlethal:
		if (Pawn == LastPawn.Get() && ASC == OriginalASC.Get() &&
			FMath::IsNearlyEqual(HealthSet->GetHealth(), 75.0f, 0.01f) && !bDeadTag)
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_DAMAGE: Cycle=1 Health=%.1f Max=%.1f Pawn=%s"),
				HealthSet->GetHealth(), HealthSet->GetMaxHealth(), *LastPawnPath);
			Probe->ServerApplyLethal(1);
			SetStage(EStage::WaitFirstDeath);
		}
		break;
	case EStage::WaitFirstDeath:
	case EStage::WaitSecondDeath:
		if (ASC == OriginalASC.Get() && FMath::IsNearlyZero(HealthSet->GetHealth(), 0.01f) &&
			bDeadTag && LastPawn.IsValid() && LastPawn->GetHealthComponent() &&
			LastPawn->GetHealthComponent()->IsDead() &&
			(!Pawn || Pawn == LastPawn.Get()))
		{
			const int32 Cycle = Stage == EStage::WaitFirstDeath ? 1 : 2;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_DEAD: Cycle=%d Health=%.1f DeadTag=1 Pawn=%s"),
				Cycle, HealthSet->GetHealth(), *LastPawnPath);
			SetStage(Cycle == 1 ? EStage::WaitFirstRespawn : EStage::WaitSecondRespawn);
		}
		break;
	case EStage::WaitFirstRespawn:
	case EStage::WaitSecondRespawn:
		if (bPawnReady && Pawn != LastPawn.Get() && Pawn->GetPathName() != LastPawnPath &&
			ASC == OriginalASC.Get())
		{
			const int32 Cycle = Stage == EStage::WaitFirstRespawn ? 1 : 2;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_RESPAWN: Cycle=%d OldPawn=%s NewPawn=%s ASC=%s Abilities=%d Health=%.1f"),
				Cycle, *LastPawnPath, *Pawn->GetPathName(), *ASC->GetPathName(),
				GetTask13AbilityCount(ASC), HealthSet->GetHealth());
			Probe->ServerVerifyRespawn(Cycle);
			LastPawn = Pawn;
			LastPawnPath = Pawn->GetPathName();
			SendTask13Key(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(Cycle == 1 ? EStage::WaitFirstAim : EStage::WaitSecondAim);
		}
		break;
	case EStage::WaitFirstAim:
	case EStage::WaitSecondAim:
		if (Pawn == LastPawn.Get() && ASC == OriginalASC.Get() && bAiming &&
			IsTask13CameraMode(Pawn, true))
		{
			const int32 Cycle = Stage == EStage::WaitFirstAim ? 1 : 2;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_INPUT_AIM: Cycle=%d Pawn=%s"),
				Cycle, *LastPawnPath);
			SendTask13Key(Controller, EKeys::RightMouseButton, IE_Released);
			SetStage(Cycle == 1 ? EStage::WaitFirstAimReleased : EStage::WaitSecondAimReleased);
		}
		break;
	case EStage::WaitFirstAimReleased:
	case EStage::WaitSecondAimReleased:
		if (bPawnReady && Pawn == LastPawn.Get() && ASC == OriginalASC.Get())
		{
			const int32 Cycle = Stage == EStage::WaitFirstAimReleased ? 1 : 2;
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_INPUT_RELEASED: Cycle=%d Pawn=%s"),
				Cycle, *LastPawnPath);
			SetStage(Cycle == 1 ? EStage::WaitFirstStable : EStage::WaitSecondStable);
		}
		break;
	case EStage::WaitFirstStable:
	case EStage::WaitSecondStable:
		if (StageSeconds >= 0.75f)
		{
			if (!bPawnReady || Pawn != LastPawn.Get() || ASC != OriginalASC.Get())
			{
				Fail(TEXT("new Avatar, abilities, input, or camera became unstable"));
				return;
			}
			if (Stage == EStage::WaitFirstStable)
			{
				Probe->ServerApplyLethal(2);
				SetStage(EStage::WaitSecondDeath);
			}
			else
			{
				Probe->ServerForceRespawnAfterDeath();
				SetStage(EStage::WaitForcedRespawn);
			}
		}
		break;
	case EStage::WaitForcedRespawn:
		if (bPawnReady && Pawn != LastPawn.Get() && Pawn->GetPathName() != LastPawnPath &&
			ASC == OriginalASC.Get())
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_FORCED_RECOVERY: OldPawn=%s NewPawn=%s ASC=%s Abilities=%d Health=%.1f DeadTag=0"),
				*LastPawnPath, *Pawn->GetPathName(), *ASC->GetPathName(),
				GetTask13AbilityCount(ASC), HealthSet->GetHealth());
			LastPawn = Pawn;
			LastPawnPath = Pawn->GetPathName();
			SendTask13Key(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(EStage::WaitForcedAim);
		}
		break;
	case EStage::WaitForcedAim:
		if (Pawn == LastPawn.Get() && ASC == OriginalASC.Get() && bAiming &&
			IsTask13CameraMode(Pawn, true))
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_FORCED_AIM: Pawn=%s"), *LastPawnPath);
			SendTask13Key(Controller, EKeys::RightMouseButton, IE_Released);
			SetStage(EStage::WaitForcedAimReleased);
		}
		break;
	case EStage::WaitForcedAimReleased:
		if (bPawnReady && Pawn == LastPawn.Get() && ASC == OriginalASC.Get())
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe CLIENT_FORCED_RELEASED: Pawn=%s"), *LastPawnPath);
			SetStage(EStage::WaitForcedStable);
		}
		break;
	case EStage::WaitForcedStable:
		// Outlive the original death timer as well as the immediate replacement.
		// The delayed callback must leave the new Pawn and its ASC intact.
		if (StageSeconds >= 3.5f)
		{
			if (!bPawnReady || Pawn != LastPawn.Get() || ASC != OriginalASC.Get())
			{
				Fail(TEXT("forced replacement Avatar became unstable"));
				return;
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask13Probe PASS: Damage=1 Invalid=1 Deaths=3 Respawns=3 Reuse=1 InputCamera=3 ForcedCleanup=1"));
			SetStage(EStage::Done);
		}
		break;
	case EStage::Done:
		break;
	}
}
