#pragma once

#include "Components/GameStateComponent.h"
#include "GameModes/MiniGamePhaseTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "TimerManager.h"
#include "MiniArenaRulesComponent.generated.h"

class UMiniArenaPhaseConfig;
class UMiniExperienceDefinition;
class UMiniGamePhaseSubsystem;

/** Experience-injected, replicated phase sequencing only. FFA rules belong to task 22. */
UCLASS(Blueprintable, ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniArenaRulesComponent : public UGameStateComponent
{
	GENERATED_BODY()
public:
	UMiniArenaRulesComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Arena")
	TObjectPtr<UMiniArenaPhaseConfig> PhaseConfig;
	const FMiniGamePhaseState& GetPhaseState() const { return PhaseState; }
	uint32 GetSourceGeneration() const { return SourceGeneration; }
	bool IsPhaseContextAvailable() const { return bPhaseContextAvailable; }
	bool IsArenaPhasesEnabled() const { return bArenaRunning && bPhaseContextAvailable; }
	bool StartArenaPhases();
	void StopArenaPhases();

private:
	friend class UMiniGamePhaseSubsystem;
	void CommitPhaseState(FGameplayTag Tag, double StartTime = 0.0, double EndTime = 0.0);
	UFUNCTION()
	void OnRep_PhaseState();
	bool StartConfiguredPhase();
	void HandlePhaseCompleted(uint32 ExpectedGeneration, int32 ExpectedIndex,
		FGameplayAbilitySpecHandle Handle, EMiniGamePhaseEndReason Reason);

	UPROPERTY(ReplicatedUsing = OnRep_PhaseState)
	FMiniGamePhaseState PhaseState;
	FTimerHandle AdvanceTimer;
	FGameplayAbilitySpecHandle LastRequestedHandle;
	uint32 SourceGeneration = 0;
	int32 PhaseIndex = INDEX_NONE;
	bool bPhaseContextAvailable = false;
	bool bArenaRunning = false;
};
