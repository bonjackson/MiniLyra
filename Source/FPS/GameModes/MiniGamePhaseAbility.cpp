#include "MiniGamePhaseAbility.h"

#include "GameModes/MiniGamePhaseSubsystem.h"
#include "System/MiniGameplayTags.h"
#include "Engine/World.h"

UMiniGamePhaseAbility::UMiniGamePhaseAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
}

void UMiniGamePhaseAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UMiniGamePhaseSubsystem* Phases = GetWorld() ? GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>() : nullptr;
	if (!ActorInfo || !ActorInfo->IsNetAuthority() || !Phases || !Phases->NotifyPhaseBegan(Handle, this))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, false, true);
		return;
	}
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UMiniGamePhaseAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) { return; }
	// Defer our cleanup too: Super alone would defer GAS while reporting an ended phase early.
	if (ScopeLockCount > 0)
	{
		WaitingToExecute.Add(FPostLockDelegate::CreateUObject(this, &ThisClass::EndAbility,
			Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled));
		return;
	}
	StopPhaseTimer();
	TWeakObjectPtr<UMiniGamePhaseSubsystem> Phases = GetWorld() ? GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>() : nullptr;
	const bool bAuthority = ActorInfo && ActorInfo->IsNetAuthority();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (bAuthority && Phases.IsValid()) { Phases->NotifyPhaseEnded(Handle, bWasCancelled); }
}

void UMiniGamePhaseAbility::StartPhaseTimer(float DurationSeconds)
{
	StopPhaseTimer();
	if (DurationSeconds > 0.0f && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(PhaseTimer, this, &ThisClass::HandlePhaseDeadline, DurationSeconds, false);
	}
}

void UMiniGamePhaseAbility::StopPhaseTimer()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(PhaseTimer); }
	PhaseTimer.Invalidate();
}

bool UMiniGamePhaseAbility::HasPhaseTimer() const
{
	return GetWorld() && GetWorld()->GetTimerManager().TimerExists(PhaseTimer);
}

void UMiniGamePhaseAbility::HandlePhaseDeadline()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}

UMiniGamePhaseAbility_Warmup::UMiniGamePhaseAbility_Warmup()
{
	PhaseTag = MiniGameplayTags::GamePhase_MiniArena_Warmup;
}
UMiniGamePhaseAbility_Playing::UMiniGamePhaseAbility_Playing()
{
	PhaseTag = MiniGameplayTags::GamePhase_MiniArena_Playing;
}
UMiniGamePhaseAbility_PostMatch::UMiniGamePhaseAbility_PostMatch()
{
	PhaseTag = MiniGameplayTags::GamePhase_MiniArena_PostMatch;
}
