#include "MiniGamePhaseSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "GameModes/MiniGamePhaseAbility.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "Engine/World.h"

bool UMiniGamePhaseSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UMiniGamePhaseSubsystem::IsAuthority() const
{
	const AMiniGameState* State = GetWorld() ? GetWorld()->GetGameState<AMiniGameState>() : nullptr;
	return State && State->HasAuthority() && GetWorld()->GetNetMode() != NM_Client;
}

UMiniAbilitySystemComponent* UMiniGamePhaseSubsystem::GetPhaseASC() const
{
	const AMiniGameState* State = GetWorld() ? GetWorld()->GetGameState<AMiniGameState>() : nullptr;
	return State ? State->GetPhaseAbilitySystemComponent() : nullptr;
}

bool UMiniGamePhaseSubsystem::StartPhase(UMiniArenaRulesComponent* Source,
	TSubclassOf<UMiniGamePhaseAbility> AbilityClass, float DurationSeconds,
	FOnMiniGamePhaseComplete Completion, FGameplayAbilitySpecHandle& OutHandle)
{
	OutHandle = FGameplayAbilitySpecHandle();
	UMiniAbilitySystemComponent* ASC = GetPhaseASC();
	const UMiniGamePhaseAbility* AbilityCDO = AbilityClass ? AbilityClass->GetDefaultObject<UMiniGamePhaseAbility>() : nullptr;
	const FGameplayTag Tag = AbilityCDO ? AbilityCDO->GetPhaseTag() : FGameplayTag();
	const bool bFixedFamily = AbilityClass && (AbilityClass->IsChildOf(UMiniGamePhaseAbility_Warmup::StaticClass()) ||
		AbilityClass->IsChildOf(UMiniGamePhaseAbility_Playing::StaticClass()) ||
		AbilityClass->IsChildOf(UMiniGamePhaseAbility_PostMatch::StaticClass()));
	const bool bSupportedTag = Tag == MiniGameplayTags::GamePhase_MiniArena_Warmup ||
		Tag == MiniGameplayTags::GamePhase_MiniArena_Playing || Tag == MiniGameplayTags::GamePhase_MiniArena_PostMatch;
	// Validate the whole new request before touching the currently running ability.
	if (!IsAuthority() || bShuttingDown || bMutatingRequest || !ASC || !Source || Source->GetWorld() != GetWorld() ||
		Source->GetOwner() != GetWorld()->GetGameState() || SnapshotSource.Get() != Source ||
		!Source->IsPhaseContextAvailable() || !Source->IsRegistered() || GetWorld()->bIsTearingDown ||
		!GetWorld()->GetGameState<AMiniGameState>()->GetExperienceManagerComponent()->IsExperienceLoaded() ||
		!AbilityCDO || !bFixedFamily || !bSupportedTag || AbilityClass->HasAnyClassFlags(CLASS_Abstract) ||
		!FMath::IsFinite(DurationSeconds) || DurationSeconds < 0.0f ||
		AbilityCDO->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::InstancedPerActor ||
		AbilityCDO->GetNetExecutionPolicy() != EGameplayAbilityNetExecutionPolicy::ServerOnly ||
		ASC->GetOwnerActor() != Source->GetOwner() || ASC->GetAvatarActor() != Source->GetOwner())
	{
		return false;
	}
	if (Request.IsSet())
	{
		if (Request->Source.Get() != Source) { return false; }
		if (Request->Tag == Tag && Request->SourceGeneration == Source->GetSourceGeneration())
		{
			OutHandle = Request->Handle;
			return true; // No new grant, deadline or start event for an identical request.
		}
	}
	TGuardValue<bool> MutationGuard(bMutatingRequest, true);
	const TWeakObjectPtr<UMiniArenaRulesComponent> WeakSource(Source);
	const uint32 ExpectedSourceGeneration = Source->GetSourceGeneration();
	if (Request.IsSet() && !CancelPhaseForSource(Source)) { return false; }
	// A completion may stop/remove the source while cancelling the previous phase.
	if (WeakSource.Get() != Source || !Source->IsPhaseContextAvailable() ||
		Source->GetSourceGeneration() != ExpectedSourceGeneration || SnapshotSource.Get() != Source ||
		bShuttingDown || GetWorld()->bIsTearingDown) { return false; }
	// A deferred end may still own its spec after the request was invalidated during teardown.
	for (const FGameplayAbilitySpec& Existing : ASC->GetActivatableAbilities())
	{
		if (Existing.Ability && Existing.Ability->IsA<UMiniGamePhaseAbility>() && Existing.IsActive()) { return false; }
	}

	FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, Source);
	FPhaseRequest NewRequest;
	NewRequest.Source = Source;
	NewRequest.Handle = Spec.Handle;
	NewRequest.Tag = Tag;
	NewRequest.Duration = DurationSeconds;
	NewRequest.SourceGeneration = Source->GetSourceGeneration();
	NewRequest.RequestGeneration = ++RequestGeneration;
	NewRequest.Completion = MoveTemp(Completion);
	const uint32 ExpectedGeneration = NewRequest.RequestGeneration;
	Request = MoveTemp(NewRequest); // Activate/End may both occur before the grant returns.
	OutHandle = Spec.Handle;
	const FGameplayAbilitySpecHandle GrantedHandle = ASC->GiveAbilityAndActivateOnce(Spec);
	if (!GrantedHandle.IsValid())
	{
		FinishRequest(Spec.Handle, EMiniGamePhaseEndReason::Failed);
		OutHandle = FGameplayAbilitySpecHandle();
		return false;
	}
	if (Request.IsSet() && Request->RequestGeneration == ExpectedGeneration && !Request->bBegan)
	{
		PendingCheckTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ThisClass::CheckPendingRequest, ExpectedGeneration));
	}
	return true;
}

bool UMiniGamePhaseSubsystem::NotifyPhaseBegan(FGameplayAbilitySpecHandle Handle, UMiniGamePhaseAbility* Ability)
{
	if (!IsAuthority() || bShuttingDown || !Request.IsSet() || Request->Handle != Handle || Request->bBegan || !Ability) { return false; }
	UMiniArenaRulesComponent* Source = Request->Source.Get();
	if (!Source || !Source->IsPhaseContextAvailable() || Source->GetSourceGeneration() != Request->SourceGeneration ||
		Ability->GetPhaseTag() != Request->Tag) { return false; }
	const AMiniGameState* State = GetWorld()->GetGameState<AMiniGameState>();
	const double Now = State->GetServerWorldTimeSeconds();
	const double Deadline = Request->Duration > 0.0f ? Now + Request->Duration : 0.0;
	Request->Ability = Ability;
	Request->bBegan = true;
	Ability->StartPhaseTimer(Request->Duration);
	UE_LOG(LogMiniExperience, Display, TEXT("MiniPhase BEGIN: World=%s Tag=%s Handle=%s Deadline=%.3f"),
		*GetWorld()->GetName(), *Request->Tag.ToString(), *Handle.ToString(), Deadline);
	Source->CommitPhaseState(Request->Tag, Now, Deadline);
	return Request.IsSet() && Request->Handle == Handle && Request->bBegan && Ability->IsActive();
}

void UMiniGamePhaseSubsystem::NotifyPhaseEnded(FGameplayAbilitySpecHandle Handle, bool bWasCancelled)
{
	if (IsAuthority())
	{
		FinishRequest(Handle, bWasCancelled ? EMiniGamePhaseEndReason::Cancelled : EMiniGamePhaseEndReason::Completed);
	}
}

void UMiniGamePhaseSubsystem::FinishRequest(FGameplayAbilitySpecHandle Handle, EMiniGamePhaseEndReason Reason)
{
	if (!Request.IsSet() || Request->Handle != Handle) { return; }
	TGuardValue<bool> MutationGuard(bMutatingRequest, true);
	FPhaseRequest Finished = MoveTemp(Request.GetValue());
	Request.Reset();
	GetWorld()->GetTimerManager().ClearTimer(PendingCheckTimer);
	if (UMiniArenaRulesComponent* Source = Finished.Source.Get())
	{
		if (Finished.bBegan) { Source->CommitPhaseState(FGameplayTag()); }
	}
	UE_LOG(LogMiniExperience, Display, TEXT("MiniPhase END: World=%s Tag=%s Handle=%s Reason=%d"),
		*GetWorld()->GetName(), *Finished.Tag.ToString(), *Handle.ToString(), static_cast<int32>(Reason));
	// Queries already see None and GAS has finished before external completion runs.
	Finished.Completion.ExecuteIfBound(Handle, Reason);
}

bool UMiniGamePhaseSubsystem::CancelPhaseForSource(UMiniArenaRulesComponent* Source)
{
	if (!IsAuthority() || !Source || Source->GetWorld() != GetWorld()) { return false; }
	if (!Request.IsSet()) { return true; }
	if (Request->Source.Get() != Source) { return false; }
	UMiniAbilitySystemComponent* ASC = GetPhaseASC();
	if (!ASC) { return false; }
	const FGameplayAbilitySpecHandle Handle = Request->Handle;
	const bool bSourceInvalidated = bShuttingDown || !Source->IsPhaseContextAvailable() ||
		Source->GetSourceGeneration() != Request->SourceGeneration;
	UMiniGamePhaseAbility* Ability = Request->Ability.Get();
	if (bSourceInvalidated)
	{
		// Source removal must stop its work even if a diagnostic ability disabled normal cancellation.
		if (Ability) { Ability->StopPhaseTimer(); }
		ASC->ClearAbility(Handle); // Also removes a queued grant; never touches player/other-source abilities.
		FinishRequest(Handle, EMiniGamePhaseEndReason::Cancelled);
		return true;
	}
	if (Ability && Ability->IsActive())
	{
		if (!Ability->CanBeCanceled()) { return false; }
		ASC->CancelAbilityHandle(Handle);
		return !Request.IsSet() || Request->Handle != Handle;
	}
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle, EConsiderPending::All);
	if (Spec && Spec->SourceObject.Get() != Source) { return false; }
	ASC->ClearAbility(Handle);
	FinishRequest(Handle, EMiniGamePhaseEndReason::Cancelled);
	return true;
}

void UMiniGamePhaseSubsystem::CheckPendingRequest(uint32 ExpectedGeneration)
{
	if (!Request.IsSet() || Request->RequestGeneration != ExpectedGeneration || Request->bBegan) { return; }
	UMiniAbilitySystemComponent* ASC = GetPhaseASC();
	const FGameplayAbilitySpecHandle Handle = Request->Handle;
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle, EConsiderPending::All) : nullptr;
	const FGameplayAbilitySpec* OwnedSpec = ASC ? ASC->FindAbilitySpecFromHandle(Handle, EConsiderPending::None) : nullptr;
	if (!OwnedSpec && Spec && !Spec->PendingRemove)
	{
		// GiveAbility may have queued this spec behind a GAS list lock.
		PendingCheckTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ThisClass::CheckPendingRequest, ExpectedGeneration));
		return;
	}
	if (ASC) { ASC->ClearAbility(Handle); }
	FinishRequest(Handle, EMiniGamePhaseEndReason::Failed);
}

bool UMiniGamePhaseSubsystem::RegisterPhaseSource(UMiniArenaRulesComponent* Source)
{
	if (bShuttingDown || !Source || Source->GetWorld() != GetWorld() || Source->GetOwner() != GetWorld()->GetGameState() ||
		(SnapshotSource.IsValid() && SnapshotSource.Get() != Source)) { return false; }
	if (SnapshotSource.Get() == Source) { PublishPhaseState(Source, Source->GetPhaseState()); return true; }
	SnapshotSource = Source;
	CurrentSnapshot = Source->GetPhaseState();
	OnPhaseStateChanged.Broadcast(CurrentSnapshot); // Context arrival matters even for the initial None.
	return true;
}

void UMiniGamePhaseSubsystem::PublishPhaseState(UMiniArenaRulesComponent* Source, const FMiniGamePhaseState& State)
{
	if (Source != SnapshotSource.Get() || !Source || Source->GetWorld() != GetWorld() || !Source->IsPhaseContextAvailable()) { return; }
	if (!(CurrentSnapshot == State))
	{
		CurrentSnapshot = State;
		OnPhaseStateChanged.Broadcast(CurrentSnapshot);
	}
}

void UMiniGamePhaseSubsystem::UnregisterPhaseSource(UMiniArenaRulesComponent* Source)
{
	if (SnapshotSource.Get() != Source) { return; }
	SnapshotSource.Reset();
	const uint32 LastRevision = CurrentSnapshot.Revision;
	CurrentSnapshot = FMiniGamePhaseState();
	CurrentSnapshot.Revision = LastRevision + 1;
	OnPhaseStateChanged.Broadcast(CurrentSnapshot);
}

bool UMiniGamePhaseSubsystem::IsPhaseActive(FGameplayTag ExactTag) const
{
	return ExactTag.IsValid() && CurrentSnapshot.PhaseTag == ExactTag;
}

double UMiniGamePhaseSubsystem::GetRemainingSeconds() const
{
	const AMiniGameState* State = GetWorld() ? GetWorld()->GetGameState<AMiniGameState>() : nullptr;
	return State && CurrentSnapshot.PhaseTag.IsValid() && CurrentSnapshot.PhaseEndTimeServer > 0.0
		? FMath::Max(0.0, CurrentSnapshot.PhaseEndTimeServer - State->GetServerWorldTimeSeconds()) : -1.0;
}

UMiniArenaRulesComponent* UMiniGamePhaseSubsystem::GetActiveSource() const
{
	return Request.IsSet() ? Request->Source.Get() : nullptr;
}

UMiniArenaRulesComponent* UMiniGamePhaseSubsystem::GetStateSource() const
{
	return SnapshotSource.Get();
}

FGameplayAbilitySpecHandle UMiniGamePhaseSubsystem::GetActivePhaseHandle() const
{
	return Request.IsSet() && Request->bBegan ? Request->Handle : FGameplayAbilitySpecHandle();
}

bool UMiniGamePhaseSubsystem::HasPendingPhase() const
{
	return Request.IsSet() && !Request->bBegan;
}

void UMiniGamePhaseSubsystem::ShutdownPhases()
{
	bShuttingDown = true;
	if (Request.IsSet())
	{
		if (UMiniArenaRulesComponent* Source = Request->Source.Get()) { CancelPhaseForSource(Source); }
		else
		{
			if (UMiniAbilitySystemComponent* ASC = GetPhaseASC()) { ASC->ClearAbility(Request->Handle); }
			Request.Reset();
		}
	}
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(PendingCheckTimer); }
}

void UMiniGamePhaseSubsystem::Deinitialize()
{
	ShutdownPhases();
	SnapshotSource.Reset();
	CurrentSnapshot = FMiniGamePhaseState();
	OnPhaseStateChanged.Clear();
	Super::Deinitialize();
}
