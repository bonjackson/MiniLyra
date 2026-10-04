#pragma once

#include "GameFeatureStateChangeObserver.h"
#include "Engine/AssetManagerTypes.h"
#include "GameFeatures/MiniRequiredActionResources.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UObject/ObjectKey.h"
#include "MiniTask26ProbeSubsystem.generated.h"

class UMiniExperienceManagerComponent;
class UMiniExperienceDefinition;
class UMiniPrimaryGameLayout;
class UMiniFrontEndWidget;
class UMiniConnectionStatusWidget;
class UMiniGameFeatureAction_AddWidgets;
class UMiniGameFeatureAction_AddActors;
class UButton;
struct FStreamableHandle;

/** Stable identity and cleanup-time measurements never keep an old World alive. */
USTRUCT()
struct FMiniTask26WorldObservation
{
	GENERATED_BODY()
	UPROPERTY(Transient) TWeakObjectPtr<UWorld> World;
	UPROPERTY(Transient) TWeakObjectPtr<UMiniExperienceManagerComponent> Manager;
	UPROPERTY(Transient) TWeakObjectPtr<UMiniPrimaryGameLayout> Root;
	UPROPERTY(Transient) TWeakObjectPtr<UMiniFrontEndWidget> Front;
	UPROPERTY(Transient) TWeakObjectPtr<UMiniConnectionStatusWidget> Modal;
	UPROPERTY(Transient) TArray<TObjectPtr<UGameFeatureAction>> Actions;
	FObjectKey WorldKey;
	bool bResourcesReleasedAtCleanup = false;
	int32 Loaded = 0;
	int32 Failed = 0;
	bool bCleaned = false;
	FString Reason;
};

/** Opt-in Development-only fault acceptance; Shipping can compile this class without hooks. */
UCLASS()
class FPS_API UMiniTask26ProbeSubsystem final : public UGameInstanceSubsystem,
	public FTickableGameObject, public IGameFeatureStateChangeObserver
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual void OnGameFeaturePostMounting(const FString& PluginName,
		const FGameFeaturePluginIdentifier& PluginIdentifier, FGameFeaturePostMountingContext& Context) override;
	virtual void OnGameFeatureActivated(const UGameFeatureData* Data, const FString& PluginURL) override;
	virtual void OnGameFeatureDeactivating(const UGameFeatureData* Data,
		FGameFeatureDeactivatingContext& Context, const FString& PluginURL) override;
private:
	struct FHeldLoad
	{
		TWeakObjectPtr<UObject> Source;
		TWeakObjectPtr<UWorld> World;
		TSharedPtr<FStreamableHandle> Handle;
		double ReleaseAt = 0;
	};
	struct FActionObserver
	{
		TWeakObjectPtr<UGameFeatureAction> Action;
		FDelegateHandle Handle;
	};
	void SelectionPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World, FPrimaryAssetId& SelectedId);
	void PrimaryPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World, bool& bUseStalledRegisteredPath);
	void ResourcesPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World,
		TArray<FMiniRequiredActionClassRequest>& Requests);
	void ActionWorldPrepared(UGameFeatureAction* Action, UWorld* World);
	void HandlePrepared(UObject* Source, UWorld* World, TSharedPtr<FStreamableHandle> Handle, bool& bHold);
	void RecordActionFailure(UWorld* World, UGameFeatureAction* Action, uint64 Generation, const FString& Reason);
	void RecordLoaded(const UMiniExperienceDefinition* Experience, TWeakObjectPtr<UWorld> World);
	void RecordFailed(const FString& Reason, TWeakObjectPtr<UWorld> World);
	void WorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	FMiniTask26WorldObservation* FindObservation(const UWorld* World);
	FMiniTask26WorldObservation* ClaimedObservation();
	bool SnapshotReleasedResources(FMiniTask26WorldObservation& Observation, UWorld* CleaningWorld);
	void Observe(UMiniExperienceManagerComponent* Manager, UWorld* World);
	bool Claim(UMiniExperienceManagerComponent* Manager, UWorld* World);
	bool IsClaimed(const UWorld* World) const;
	bool IsPendingMode() const;
	bool IsFrontFault() const;
	bool IsExpectedFailure() const;
	void CaptureActions(FMiniTask26WorldObservation& Observation);
	void ReleaseHolds(bool bCancel);
	bool VerifyOldWorldReleased();
	bool FrontEndReady(bool bAllowError);
	bool GameplayReady();
	bool FailureReady();
	bool VerifyPending();
	bool VerifyStaleNoticeRejected();
	bool Media(FName Stage);
	bool Click(UButton* Button);
	void Pass();
	void Fail(const TCHAR* Reason);
	FSoftObjectPath MissingClassPath() const;
	FString Mode;
	FString TargetMap;
	FString MediaDirectory;
	FString PendingMediaPath;
	FName PendingMediaStage;
	FString FeatureURL;
	FString ActionFailureReason;
	TWeakObjectPtr<UWorld> ClaimedWorld;
	TWeakObjectPtr<UMiniExperienceManagerComponent> ClaimedManager;
	UPROPERTY(Transient) TObjectPtr<UGameFeatureAction> FailedAction;
	uint64 FailedActionGeneration = 0;
	uint32 ClaimedLoadGeneration = 0;
	UPROPERTY(Transient) TArray<FMiniTask26WorldObservation> Observations;
	TArray<FHeldLoad> HeldLoads;
	TArray<FActionObserver> ActionObservers;
	TSet<FName> MediaDone;
	FSimpleDelegate FeatureMountResume;
	double FeatureResumeAt = 0;
	double StartedAt = 0;
	double StepStartedAt = 0;
	double MediaStartedAt = 0;
	int32 TimeoutSeconds = 150;
	int32 Step = 0;
	int32 ClaimedObservationIndex = INDEX_NONE;
	int32 ClaimedCount = 0;
	int32 Mutations = 0;
	int32 HoldCount = 0;
	int32 RejectedOtherWorldHooks = 0;
	int32 LateFeatureActivations = 0;
	int32 LateFeatureDeactivations = 0;
	bool bInitialized = false;
	bool bDone = false;
	bool bConsumed = false;
	bool bFaultApplied = false;
	bool bPendingObserved = false;
	bool bReturnRequested = false;
	bool bUnloadStarted = false;
	bool bFeatureInstalled = false;
	bool bFeaturePaused = false;
	bool bStaleVerified = false;
	FDelegateHandle SelectionHandle;
	FDelegateHandle PrimaryHandle;
	FDelegateHandle ResourcesHandle;
	FDelegateHandle ActionWorldHandle;
	FDelegateHandle StreamableHandle;
	FDelegateHandle CleanupHandle;
};
