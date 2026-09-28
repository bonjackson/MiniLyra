#include "MiniTask12ProbeActor.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Character/MiniCharacter.h"
#include "Engine/World.h"
#include "GameModes/MiniGameMode.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

namespace
{
const FGameplayAbilitySpec* FindInputSpec(const UMiniAbilitySystemComponent* ASC, FGameplayTag InputTag)
{
	if (ASC)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				return &Spec;
			}
		}
	}
	return nullptr;
}

bool IsProbeAbilityInput(FGameplayTag Tag)
{
	return Tag == MiniGameplayTags::InputTag_Jump || Tag == MiniGameplayTags::InputTag_Aim ||
		Tag == MiniGameplayTags::InputTag_Fire;
}
}

AMiniTask12ProbeActor::AMiniTask12ProbeActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
}

AMiniPlayerController* AMiniTask12ProbeActor::GetProbeController() const
{
	return Cast<AMiniPlayerController>(GetOwner());
}

UMiniAbilitySystemComponent* AMiniTask12ProbeActor::GetProbeASC() const
{
	const AMiniPlayerController* Controller = GetProbeController();
	const AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetMiniAbilitySystemComponent() : nullptr;
}

bool AMiniTask12ProbeActor::IsAllowedBlockingTag(FGameplayTag Tag) const
{
	const FString Name = Tag.ToString();
	return Name == TEXT("State.Reloading") || Name == TEXT("State.Dead") ||
		Name == TEXT("Gameplay.AbilityInputBlocked");
}

void AMiniTask12ProbeActor::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask12Probe FAIL: %s"), Reason);
		bFailed = true;
	}
}

void AMiniTask12ProbeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || bFailed)
	{
		return;
	}
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	if (PendingCancelTag.IsValid())
	{
		PendingSeconds += DeltaSeconds;
		const FGameplayAbilitySpec* Spec = FindInputSpec(ASC, PendingActiveInputTag);
		if (Spec && Spec->IsActive())
		{
			CancelCheckTag = PendingCancelTag;
			CancelCheckInputTag = PendingActiveInputTag;
			PendingCancelTag = FGameplayTag();
			PendingActiveInputTag = FGameplayTag();
			PendingSeconds = 0.0f;
			MulticastSetTag(CancelCheckTag, true);
		}
		else if (PendingSeconds > 8.0f)
		{
			Fail(TEXT("server did not observe the ability before status cancellation"));
		}
	}
	else if (CancelCheckTag.IsValid())
	{
		PendingSeconds += DeltaSeconds;
		const FGameplayAbilitySpec* Spec = FindInputSpec(ASC, CancelCheckInputTag);
		if (ASC && ASC->HasMatchingGameplayTag(CancelCheckTag) && Spec && !Spec->IsActive())
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask12Probe SERVER_CANCEL_PASS: Blocker=%s Ability=%s Avatar=%s"),
				*CancelCheckTag.ToString(), *CancelCheckInputTag.ToString(),
				*GetPathNameSafe(ASC->GetAvatarActor()));
			CancelCheckTag = FGameplayTag();
			CancelCheckInputTag = FGameplayTag();
			PendingSeconds = 0.0f;
		}
		else if (PendingSeconds > 8.0f)
		{
			Fail(TEXT("status did not cancel the active server ability"));
		}
	}
	if (bPendingRespawn)
	{
		PendingSeconds += DeltaSeconds;
		const FGameplayAbilitySpec* Aim = FindInputSpec(ASC, MiniGameplayTags::InputTag_Aim);
		if (Aim && Aim->IsActive())
		{
			AMiniPlayerController* Controller = GetProbeController();
			AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
			if (!Pawn || !ASC || ASC->GetAvatarActor() != Pawn)
			{
				Fail(TEXT("server cannot switch a valid aiming Pawn"));
				return;
			}
			bPendingRespawn = false;
			OldPawn = Pawn;
			Controller->UnPossess();
			const FGameplayAbilitySpec* AfterUnpossess = FindInputSpec(ASC, MiniGameplayTags::InputTag_Aim);
			if (ASC->GetAvatarActor() == Pawn ||
				(AfterUnpossess && AfterUnpossess->IsActive()) ||
				ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Aiming))
			{
				Fail(TEXT("old Pawn retained Aim or State.Aiming after unpossess"));
				return;
			}
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe OLD_PAWN_CLEAN: Pawn=%s"),
				*Pawn->GetPathName());
			GetWorldTimerManager().SetTimer(RespawnTimer, this, &ThisClass::FinishRespawn, 1.25f, false);
		}
		else if (PendingSeconds > 8.0f)
		{
			Fail(TEXT("server did not observe Aim before Pawn switch"));
		}
	}
}

void AMiniTask12ProbeActor::MulticastSetTag_Implementation(FGameplayTag Tag, bool bEnabled)
{
	if (!IsAllowedBlockingTag(Tag))
	{
		Fail(TEXT("invalid probe status tag"));
		return;
	}
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	if (!ASC)
	{
		Fail(TEXT("probe status has no ASC"));
		return;
	}
	if (bEnabled)
	{
		ASC->AddLooseGameplayTag(Tag);
	}
	else
	{
		ASC->RemoveLooseGameplayTag(Tag);
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe STATUS: Blocker=%s Active=%d Role=%d"),
		*Tag.ToString(), bEnabled, static_cast<int32>(GetLocalRole()));
}

void AMiniTask12ProbeActor::ServerCancelWithTag_Implementation(
	FGameplayTag BlockingTag, FGameplayTag ActiveAbilityInputTag)
{
	if (!IsAllowedBlockingTag(BlockingTag) || !IsProbeAbilityInput(ActiveAbilityInputTag) ||
		PendingCancelTag.IsValid() || CancelCheckTag.IsValid() || bPendingRespawn)
	{
		Fail(TEXT("invalid or overlapping status cancellation request"));
		return;
	}
	PendingCancelTag = BlockingTag;
	PendingActiveInputTag = ActiveAbilityInputTag;
	PendingSeconds = 0.0f;
}

void AMiniTask12ProbeActor::ServerCheckRejected_Implementation(
	FGameplayTag BlockingTag, FGameplayTag AbilityInputTag)
{
	if (!IsAllowedBlockingTag(BlockingTag) || !IsProbeAbilityInput(AbilityInputTag))
	{
		Fail(TEXT("invalid server rejection request"));
		return;
	}
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	const FGameplayAbilitySpec* Spec = FindInputSpec(ASC, AbilityInputTag);
	if (!ASC || !ASC->HasMatchingGameplayTag(BlockingTag) || !Spec || Spec->IsActive())
	{
		Fail(TEXT("server rejection preconditions were not met"));
		return;
	}
	FGameplayTagContainer AbilityTags;
	AbilityTags.AddTag(AbilityInputTag == MiniGameplayTags::InputTag_Jump
		? MiniGameplayTags::Ability_Jump
		: (AbilityInputTag == MiniGameplayTags::InputTag_Aim
			? MiniGameplayTags::Ability_Aim : MiniGameplayTags::Ability_Fire));
	if (ASC->AreAbilityTagRequirementsMet(AbilityTags))
	{
		Fail(TEXT("server status relationship did not block the requested ability"));
		return;
	}
	const FGameplayAbilitySpecHandle Handle = Spec->Handle;
	FGameplayTagContainer FailureTags;
	if (!ASC->AbilityActorInfo.IsValid() ||
		Spec->Ability->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get(),
			nullptr, nullptr, &FailureTags))
	{
		Fail(TEXT("server CanActivateAbility accepted a blocked ability"));
		return;
	}
	// On a remote-owned LocalPredicted spec, the default call dispatches
	// ClientTryActivateAbility and reports dispatch success before checking tags.
	if (ASC->TryActivateAbility(Handle, false))
	{
		ASC->CancelAbilityHandle(Handle);
		Fail(TEXT("server activated an ability under its blocking status"));
		return;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask12Probe SERVER_REJECT_PASS: Blocker=%s Ability=%s Avatar=%s"),
		*BlockingTag.ToString(), *AbilityInputTag.ToString(),
		*GetPathNameSafe(ASC->GetAvatarActor()));
}

void AMiniTask12ProbeActor::ServerClearTag_Implementation(FGameplayTag BlockingTag)
{
	UMiniAbilitySystemComponent* ASC = GetProbeASC();
	if (!IsAllowedBlockingTag(BlockingTag) || !ASC || !ASC->HasMatchingGameplayTag(BlockingTag))
	{
		Fail(TEXT("invalid status clear request"));
		return;
	}
	MulticastSetTag(BlockingTag, false);
}

void AMiniTask12ProbeActor::ServerRespawnWhileAiming_Implementation()
{
	if (bPendingRespawn || OldPawn.IsValid() || PendingCancelTag.IsValid() || CancelCheckTag.IsValid())
	{
		Fail(TEXT("overlapping Pawn switch request"));
		return;
	}
	bPendingRespawn = true;
	PendingSeconds = 0.0f;
}

void AMiniTask12ProbeActor::FinishRespawn()
{
	AMiniPlayerController* Controller = GetProbeController();
	AMiniGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMiniGameMode>() : nullptr;
	AMiniCharacter* Previous = OldPawn.Get();
	if (!Controller || !GameMode || !Previous)
	{
		Fail(TEXT("server could not finish the Pawn switch"));
		return;
	}
	GameMode->RestartPlayer(Controller);
	AMiniCharacter* Replacement = Cast<AMiniCharacter>(Controller->GetPawn());
	if (!Replacement || Replacement == Previous)
	{
		Fail(TEXT("server did not create a replacement Pawn"));
		return;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask12Probe SERVER_RESPAWN: OldPawn=%s NewPawn=%s"),
		*Previous->GetPathName(), *Replacement->GetPathName());
	Previous->Destroy();
	OldPawn.Reset();
}
