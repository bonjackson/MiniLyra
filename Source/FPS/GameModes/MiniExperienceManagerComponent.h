#pragma once

#include "Components/GameStateComponent.h"
#include "Engine/AssetManagerTypes.h"
#include "MiniExperienceManagerComponent.generated.h"

struct FStreamableHandle;
struct FMiniFeatureActivationLease;
class UGameFeatureAction;
class UMiniExperienceDefinition;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMiniExperienceLoaded, const UMiniExperienceDefinition* /*Experience*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMiniExperienceFailed, const FString& /*Reason*/);

UENUM(BlueprintType)
enum class EMiniExperienceLoadState : uint8
{
	Unloaded,
	LoadingAssets,
	LoadingFeatures,
	ExecutingActions,
	Loaded,
	Failed,
	Deactivating
};

/**
 * The server selects and replicates an Experience ID. Each world then loads the
 * asset locally, so a replicated ID alone never means gameplay is ready.
 */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniExperienceManagerComponent final : public UGameStateComponent
{
	GENERATED_BODY()

public:
	UMiniExperienceManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server only. One Experience is selected per world; later calls with the same ID are harmless. */
	bool SetCurrentExperience(const FPrimaryAssetId& ExperienceId);

	/** Server only. Used when even selecting an ID fails (bad map override/default config). */
	void FailExperienceSelection(const FString& Reason);

	bool HasExperienceSelection() const { return CurrentExperienceId.IsValid(); }
	bool IsExperienceLoaded() const { return LoadState == EMiniExperienceLoadState::Loaded && CurrentExperience != nullptr; }
	FPrimaryAssetId GetCurrentExperienceId() const { return CurrentExperienceId; }
	EMiniExperienceLoadState GetLoadState() const { return LoadState; }
	FString GetFailureReason() const { return FailureReason; }
	const UMiniExperienceDefinition* GetCurrentExperience() const { return IsExperienceLoaded() ? CurrentExperience.Get() : nullptr; }
	FString GetLoadingDebugString() const;

	/** Called once on this world's local load, or immediately if already loaded. */
	void CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate&& Delegate);
	/** Called once on failure, or immediately if this world has already failed. */
	void CallOrRegister_OnExperienceFailed(FOnMiniExperienceFailed::FDelegate&& Delegate);

private:
	UFUNCTION()
	void OnRep_CurrentExperienceId();

	UFUNCTION()
	void OnRep_SelectionFailureReason();

	void StartExperienceLoad();
	void HandleAssetsLoaded(uint32 ExpectedGeneration);
	void HandleAssetsCanceled(uint32 ExpectedGeneration);
	void CompleteExperienceLoad();
	void ActivateNextGameFeature(uint32 ExpectedGeneration);
	void HandleGameFeatureActivated(const TSharedRef<FMiniFeatureActivationLease>& Lease, uint32 ExpectedGeneration, bool bSucceeded, const FString& Error);
	void ExecuteExperienceActions();
	void CleanupExperienceActionsAndFeatures();
	void FailExperience(const FString& Reason);
	void SetLoadState(EMiniExperienceLoadState NewState);
	void CancelPendingLoad();

	UPROPERTY(ReplicatedUsing = OnRep_CurrentExperienceId)
	FPrimaryAssetId CurrentExperienceId;

	// A pre-ID selection error must reach clients too; an invalid/default ID cannot trigger OnRep.
	UPROPERTY(ReplicatedUsing = OnRep_SelectionFailureReason)
	FString SelectionFailureReason;

	UPROPERTY(Transient)
	TObjectPtr<UMiniExperienceDefinition> CurrentExperience;

	EMiniExperienceLoadState LoadState = EMiniExperienceLoadState::Unloaded;
	FString FailureReason;
	TSharedPtr<FStreamableHandle> AssetLoadHandle;
	TArray<FString> GameFeaturePluginURLs;
	TArray<TSharedPtr<FMiniFeatureActivationLease>> GameFeatureLeases;
	int32 NextGameFeatureIndex = 0;

	// Keep activated action objects alive until their world-scoped deactivation begins.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UGameFeatureAction>> ActivatedActions;
	FName ActionWorldContextHandle;
	uint32 LoadGeneration = 0;
	bool bEndingPlay = false;

	FOnMiniExperienceLoaded OnExperienceLoaded;
	FOnMiniExperienceFailed OnExperienceFailed;
};
