#pragma once

#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "Engine/World.h"
#include "GameFeatures/MiniRequiredActionResources.h"
#include "MiniGameFeatureAction_AddActors.generated.h"

class AActor;
class UGameInstance;
struct FStreamableHandle;

/** One replicated actor contributed by a world-scoped Experience Action. */
USTRUCT(BlueprintType)
struct FMiniFeatureActorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Mini|Actors", meta = (AssetBundles = "Server"))
	TSoftClassPtr<AActor> ActorClass;

	UPROPERTY(EditAnywhere, Category = "Mini|Actors")
	FTransform Transform = FTransform::Identity;
};

/** Spawns on authority; clients observe replication. Deactivation removes only this Action's actors. */
UCLASS(meta = (DisplayName = "Mini Add Actors"))
class FPS_API UMiniGameFeatureAction_AddActors final : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Mini|Actors", meta = (TitleProperty = "ActorClass"))
	TArray<FMiniFeatureActorEntry> Actors;

	bool GatherRequiredClasses(UWorld* World, bool bAuthority,
		TArray<FMiniRequiredActionClassRequest>& OutRequests, FString& OutError) const;
	bool GetRequiredFailure(const UWorld* World, uint64& OutGeneration, FString& OutReason) const;
	FMiniRequiredActionFailed OnRequiredActionFailed;

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
	virtual void OnGameFeatureUnregistering() override;
	virtual void BeginDestroy() override;

#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) override;
#endif
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** Diagnostic counters reflect live ownership, not historical requests. Loaded != async spawn ready. */
	void GetWorldStats(const UWorld* World, int32& OutWorlds, int32& OutAliveActors,
		int32& OutPendingLoads, int32& OutBeginPlayBindings, bool& bOutReady, bool& bOutFailed) const;

private:
#if !UE_BUILD_SHIPPING
	friend class UMiniTask26ProbeSubsystem;
#endif
	struct FWorldData
	{
		uint64 Generation = 0;
		TArray<FMiniFeatureActorEntry> Entries;
		TSharedPtr<FStreamableHandle> LoadHandle;
		FDelegateHandle BeginPlayHandle;
		TArray<TWeakObjectPtr<AActor>> OwnedActors;
		bool bSpawning = false;
		bool bReady = false;
		bool bFailed = false;
		FString FailureReason;
	};
	struct FContextData
	{
		TArray<FMiniFeatureActorEntry> Entries;
		TMap<TWeakObjectPtr<UWorld>, FWorldData> Worlds;
	};
	TMap<FGameFeatureStateChangeContext, FContextData> ContextData;
	uint64 NextGeneration = 0;
	FDelegateHandle GameInstanceStartHandle;
	FDelegateHandle WorldInitializedHandle;
	FDelegateHandle WorldCleanupHandle;

	void HandleGameInstanceStart(UGameInstance* GameInstance);
	void HandleWorldInitialized(UWorld* World, const UWorld::InitializationValues InitializationValues);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void ConsiderWorld(UWorld* World);
	void BeginWorld(FGameFeatureStateChangeContext Context, TWeakObjectPtr<UWorld> World, uint64 Generation);
	void FinishLoad(FGameFeatureStateChangeContext Context, TWeakObjectPtr<UWorld> World, uint64 Generation);
	FWorldData* FindWorld(FGameFeatureStateChangeContext Context, TWeakObjectPtr<UWorld> World, uint64 Generation);
	void FailWorld(FGameFeatureStateChangeContext Context, TWeakObjectPtr<UWorld> World, uint64 Generation, const TCHAR* Reason);
	void RemoveWorld(FGameFeatureStateChangeContext Context, TWeakObjectPtr<UWorld> World);
	void RemoveContext(FGameFeatureStateChangeContext Context);
	void RemoveAllContexts();
	void RemoveDelegatesIfUnused();
	static void ReleaseWorld(TWeakObjectPtr<UWorld> World, FWorldData& Data);
};
