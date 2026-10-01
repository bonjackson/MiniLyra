#pragma once

#include "GameModes/MiniGamePhaseTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "MiniGamePhaseSubsystem.generated.h"

class UMiniArenaRulesComponent;
class UMiniGamePhaseAbility;
class UMiniAbilitySystemComponent;

DECLARE_DELEGATE_TwoParams(FOnMiniGamePhaseComplete, FGameplayAbilitySpecHandle, EMiniGamePhaseEndReason);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMiniGamePhaseStateChanged, const FMiniGamePhaseState&);

/** Exactly one server phase request per world; clients consume Rules' replicated snapshot. */
UCLASS()
class FPS_API UMiniGamePhaseSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	bool StartPhase(UMiniArenaRulesComponent* Source, TSubclassOf<UMiniGamePhaseAbility> AbilityClass,
		float DurationSeconds, FOnMiniGamePhaseComplete Completion, FGameplayAbilitySpecHandle& OutHandle);
	bool CancelPhaseForSource(UMiniArenaRulesComponent* Source);
	void ShutdownPhases();

	FMiniGamePhaseState GetCurrentPhaseState() const { return CurrentSnapshot; }
	bool IsPhaseActive(FGameplayTag ExactTag) const;
	double GetRemainingSeconds() const;
	bool HasArenaContext() const { return SnapshotSource.IsValid(); }
	UMiniArenaRulesComponent* GetStateSource() const;
	UMiniArenaRulesComponent* GetActiveSource() const;
	FGameplayAbilitySpecHandle GetActivePhaseHandle() const;
	bool HasPendingPhase() const;
	FOnMiniGamePhaseStateChanged OnPhaseStateChanged;

	// Rules' server commit, client OnRep, and lifecycle all publish through this world.
	bool RegisterPhaseSource(UMiniArenaRulesComponent* Source);
	void UnregisterPhaseSource(UMiniArenaRulesComponent* Source);
	void PublishPhaseState(UMiniArenaRulesComponent* Source, const FMiniGamePhaseState& State);
	bool NotifyPhaseBegan(FGameplayAbilitySpecHandle Handle, UMiniGamePhaseAbility* Ability);
	void NotifyPhaseEnded(FGameplayAbilitySpecHandle Handle, bool bWasCancelled);

private:
	struct FPhaseRequest
	{
		TWeakObjectPtr<UMiniArenaRulesComponent> Source;
		TWeakObjectPtr<UMiniGamePhaseAbility> Ability;
		FGameplayAbilitySpecHandle Handle;
		FGameplayTag Tag;
		FOnMiniGamePhaseComplete Completion;
		float Duration = 0.0f;
		uint32 SourceGeneration = 0;
		uint32 RequestGeneration = 0;
		bool bBegan = false;
	};
	UMiniAbilitySystemComponent* GetPhaseASC() const;
	bool IsAuthority() const;
	void FinishRequest(FGameplayAbilitySpecHandle Handle, EMiniGamePhaseEndReason Reason);
	void CheckPendingRequest(uint32 ExpectedGeneration);
	TOptional<FPhaseRequest> Request;
	TWeakObjectPtr<UMiniArenaRulesComponent> SnapshotSource;
	FMiniGamePhaseState CurrentSnapshot;
	FTimerHandle PendingCheckTimer;
	uint32 RequestGeneration = 0;
	bool bMutatingRequest = false;
	bool bShuttingDown = false;
};
