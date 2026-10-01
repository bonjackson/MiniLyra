#include "MiniArenaRulesComponent.h"

#include "Arena/MiniArenaPhaseConfig.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGamePhaseAbility.h"
#include "GameModes/MiniGameState.h"
#include "System/MiniLogChannels.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UMiniArenaRulesComponent::UMiniArenaRulesComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UMiniArenaRulesComponent::BeginPlay()
{
	Super::BeginPlay();
	bPhaseContextAvailable = true;
	const uint32 ExpectedGeneration = ++SourceGeneration;
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	if (!Phases || !Phases->RegisterPhaseSource(this))
	{
		bPhaseContextAvailable = false;
		UE_LOG(LogMiniExperience, Error, TEXT("MiniPhase SOURCE_REJECTED: %s"), *GetPathName());
		return;
	}
	AMiniGameState* State = Cast<AMiniGameState>(GetOwner());
	if (!State || !State->HasAuthority()) { return; }
	UMiniExperienceManagerComponent* Experience = State->GetExperienceManagerComponent();
	if (!Experience) { StopArenaPhases(); return; }
	Experience->CallOrRegister_OnExperienceFailed(FOnMiniExperienceFailed::FDelegate::CreateWeakLambda(this,
		[this, ExpectedGeneration](const FString&)
		{
			if (SourceGeneration == ExpectedGeneration && bPhaseContextAvailable) { StopArenaPhases(); }
		}));
	Experience->CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate::CreateWeakLambda(this,
		[this, ExpectedGeneration](const UMiniExperienceDefinition*)
		{
			if (SourceGeneration == ExpectedGeneration && bPhaseContextAvailable) { StartArenaPhases(); }
		}));
}

void UMiniArenaRulesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Consumers must see that the arena context is gone before the None notification.
	bPhaseContextAvailable = false;
	if (UMiniGamePhaseSubsystem* Phases = GetWorld() ? GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>() : nullptr)
	{
		Phases->UnregisterPhaseSource(this);
	}
	StopArenaPhases();
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(AdvanceTimer); }
	OnPhaseCompleted.Clear();
	Super::EndPlay(EndPlayReason);
}

void UMiniArenaRulesComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMiniArenaRulesComponent, PhaseState);
}

bool UMiniArenaRulesComponent::StartArenaPhases()
{
	AMiniGameState* State = Cast<AMiniGameState>(GetOwner());
	if (!State || !State->HasAuthority() || !bPhaseContextAvailable || !GetWorld() || GetWorld()->bIsTearingDown ||
		!State->GetExperienceManagerComponent()->IsExperienceLoaded()) { return false; }
	if (bArenaRunning) { return true; }
	FString Error;
	if (!PhaseConfig || !PhaseConfig->ValidateConfig(Error))
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniPhase CONFIG_REJECTED: Source=%s Reason=%s"), *GetPathName(),
			PhaseConfig ? *Error : TEXT("Missing PhaseConfig"));
		return false;
	}
	++SourceGeneration;
	bArenaRunning = true;
	PhaseIndex = 0;
	return StartConfiguredPhase();
}

void UMiniArenaRulesComponent::StopArenaPhases()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) { return; }
	bArenaRunning = false;
	++SourceGeneration;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(AdvanceTimer);
		if (UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>())
		{
			Phases->CancelPhaseForSource(this);
		}
	}
	LastRequestedHandle = FGameplayAbilitySpecHandle();
	PhaseIndex = INDEX_NONE;
	if (PhaseState.PhaseTag.IsValid()) { CommitPhaseState(FGameplayTag()); }
}

bool UMiniArenaRulesComponent::JumpToPhase(FGameplayTag ExactPhaseTag)
{
	AMiniGameState* State = Cast<AMiniGameState>(GetOwner());
	UMiniGamePhaseSubsystem* Phases = GetWorld() ? GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>() : nullptr;
	FString Error;
	if (!State || !State->HasAuthority() || !bPhaseContextAvailable || bManualTransitionInProgress || !Phases ||
		GetWorld()->bIsTearingDown || !State->GetExperienceManagerComponent()->IsExperienceLoaded() ||
		!PhaseConfig || !PhaseConfig->ValidateConfig(Error)) { return false; }
	int32 TargetIndex = INDEX_NONE;
	for (int32 Index = 0; Index < PhaseConfig->Phases.Num(); ++Index)
	{
		if (PhaseConfig->Phases[Index].AbilityClass->GetDefaultObject<UMiniGamePhaseAbility>()->GetPhaseTag() == ExactPhaseTag)
		{
			TargetIndex = Index;
			break;
		}
	}
	if (TargetIndex == INDEX_NONE) { return false; }
	if (PhaseState.PhaseTag == ExactPhaseTag && Phases->GetActiveSource() == this && Phases->GetActivePhaseHandle().IsValid()) { return true; }
	const uint32 PreviousGeneration = SourceGeneration;
	TGuardValue<bool> TransitionGuard(bManualTransitionInProgress, true);
	// Keep the generation unchanged until normal cancellation succeeds. This must not force an uncancellable phase to end.
	if (!Phases->CancelPhaseForSource(this)) { return false; }
	if (!bPhaseContextAvailable || SourceGeneration != PreviousGeneration || GetWorld()->bIsTearingDown) { return false; }
	GetWorld()->GetTimerManager().ClearTimer(AdvanceTimer);
	++SourceGeneration;
	PhaseIndex = TargetIndex;
	bArenaRunning = true;
	return StartConfiguredPhase();
}

bool UMiniArenaRulesComponent::StartConfiguredPhase()
{
	UMiniGamePhaseSubsystem* Phases = GetWorld() ? GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>() : nullptr;
	if (!bArenaRunning || !Phases || !PhaseConfig || !PhaseConfig->Phases.IsValidIndex(PhaseIndex)) { return false; }
	const FMiniArenaPhaseEntry& Entry = PhaseConfig->Phases[PhaseIndex];
	const uint32 ExpectedGeneration = SourceGeneration;
	const int32 ExpectedIndex = PhaseIndex;
	const bool bAccepted = Phases->StartPhase(this, Entry.AbilityClass, Entry.DurationSeconds,
		FOnMiniGamePhaseComplete::CreateWeakLambda(this,
			[this, ExpectedGeneration, ExpectedIndex](FGameplayAbilitySpecHandle Handle, EMiniGamePhaseEndReason Reason)
			{
				HandlePhaseCompleted(ExpectedGeneration, ExpectedIndex, Handle, Reason);
			}), LastRequestedHandle);
	if (!bAccepted) { bArenaRunning = false; }
	return bAccepted;
}

void UMiniArenaRulesComponent::HandlePhaseCompleted(uint32 ExpectedGeneration, int32 ExpectedIndex,
	FGameplayAbilitySpecHandle Handle, EMiniGamePhaseEndReason Reason)
{
	if (!bArenaRunning || !bPhaseContextAvailable || SourceGeneration != ExpectedGeneration || PhaseIndex != ExpectedIndex) { return; }
	const FGameplayTag CompletedTag = PhaseConfig && PhaseConfig->Phases.IsValidIndex(ExpectedIndex)
		? PhaseConfig->Phases[ExpectedIndex].AbilityClass->GetDefaultObject<UMiniGamePhaseAbility>()->GetPhaseTag() : FGameplayTag();
	if (bManualTransitionInProgress)
	{
		OnPhaseCompleted.Broadcast(CompletedTag, Reason);
		return;
	}
	if (Reason != EMiniGamePhaseEndReason::Completed)
	{
		bArenaRunning = false;
		GetWorld()->GetTimerManager().ClearTimer(AdvanceTimer);
		LastRequestedHandle = FGameplayAbilitySpecHandle();
		PhaseIndex = INDEX_NONE;
		OnPhaseCompleted.Broadcast(CompletedTag, Reason);
		return;
	}
	OnPhaseCompleted.Broadcast(CompletedTag, Reason);
	if (!bArenaRunning || !bPhaseContextAvailable || SourceGeneration != ExpectedGeneration || PhaseIndex != ExpectedIndex) { return; }
	const uint32 CompletedRevision = PhaseState.Revision;
	// Never grant from inside GAS' activation/end scope. Recheck source, handle and snapshot when consuming the work.
	AdvanceTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, ExpectedGeneration, ExpectedIndex, Handle, CompletedRevision]()
		{
			if (!bArenaRunning || !bPhaseContextAvailable || SourceGeneration != ExpectedGeneration ||
				PhaseIndex != ExpectedIndex || LastRequestedHandle != Handle || PhaseState.Revision != CompletedRevision ||
				!GetOwner()->HasAuthority() || GetWorld()->bIsTearingDown) { return; }
			++PhaseIndex;
			if (PhaseConfig && PhaseConfig->Phases.IsValidIndex(PhaseIndex)) { StartConfiguredPhase(); }
			else { bArenaRunning = false; }
		}));
}

void UMiniArenaRulesComponent::CommitPhaseState(FGameplayTag Tag, double StartTime, double EndTime)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) { return; }
	PhaseState.PhaseTag = Tag;
	PhaseState.PhaseStartTimeServer = StartTime;
	PhaseState.PhaseEndTimeServer = EndTime;
	++PhaseState.Revision;
	GetOwner()->ForceNetUpdate();
	OnRep_PhaseState(); // A listen host does not receive its own RepNotify.
}

void UMiniArenaRulesComponent::OnRep_PhaseState()
{
	if (bPhaseContextAvailable && GetWorld())
	{
		if (UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>())
		{
			Phases->PublishPhaseState(this, PhaseState);
		}
	}
}
