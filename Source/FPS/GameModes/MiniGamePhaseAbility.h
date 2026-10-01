#pragma once

#include "Abilities/GameplayAbility.h"
#include "TimerManager.h"
#include "MiniGamePhaseAbility.generated.h"

class UMiniGamePhaseSubsystem;

/** Server work on the GameState ASC, independent of player ability/input rules. */
UCLASS(Abstract, NotBlueprintable)
class FPS_API UMiniGamePhaseAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UMiniGamePhaseAbility();
	FGameplayTag GetPhaseTag() const { return PhaseTag; }
	bool HasPhaseTimer() const;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;
	FGameplayTag PhaseTag;

private:
	friend class UMiniGamePhaseSubsystem;
	void StartPhaseTimer(float DurationSeconds);
	void StopPhaseTimer();
	void HandlePhaseDeadline();
	FTimerHandle PhaseTimer;
};

UCLASS()
class FPS_API UMiniGamePhaseAbility_Warmup : public UMiniGamePhaseAbility
{
	GENERATED_BODY()
public:
	UMiniGamePhaseAbility_Warmup();
};

UCLASS()
class FPS_API UMiniGamePhaseAbility_Playing : public UMiniGamePhaseAbility
{
	GENERATED_BODY()
public:
	UMiniGamePhaseAbility_Playing();
};

UCLASS()
class FPS_API UMiniGamePhaseAbility_PostMatch : public UMiniGamePhaseAbility
{
	GENERATED_BODY()
public:
	UMiniGamePhaseAbility_PostMatch();
};
