#pragma once

#include "Engine/World.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameplayTagContainer.h"
#include "GameFeatures/MiniRequiredActionResources.h"
#include "UIExtensionSystem.h"
#include "MiniGameFeatureAction_AddWidgets.generated.h"

class AMiniHUD;
struct FStreamableHandle;
class UCommonActivatableWidget;
class UCommonLocalPlayer;
class UGameInstance;
class ULocalPlayer;
class UMiniGameUIPolicy;
class UMiniPrimaryGameLayout;
class UUserWidget;
struct FComponentRequestHandle;

struct FMiniWidgetContributionCounts
{
	int32 HUDs = 0;
	int32 Layouts = 0;
	int32 Elements = 0;
	int32 PendingLoads = 0;
};

USTRUCT(BlueprintType)
struct FMiniHUDLayoutRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Mini|UI", meta = (AssetBundles = "Client"))
	TSoftClassPtr<UCommonActivatableWidget> LayoutClass;

	UPROPERTY(EditAnywhere, Category = "Mini|UI", meta = (Categories = "UI.Layer"))
	FGameplayTag LayerTag;
};

USTRUCT(BlueprintType)
struct FMiniHUDElementRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Mini|UI", meta = (AssetBundles = "Client"))
	TSoftClassPtr<UUserWidget> WidgetClass;

	UPROPERTY(EditAnywhere, Category = "Mini|UI", meta = (Categories = "UI.HUD"))
	FGameplayTag SlotTag;

	UPROPERTY(EditAnywhere, Category = "Mini|UI")
	int32 Priority = 0;
};

/** Per-context, per-world and per-HUD asynchronous UI injection with complete teardown. */
UCLASS(meta = (DisplayName = "Mini Add Widgets"))
class FPS_API UMiniGameFeatureAction_AddWidgets final : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Mini|UI")
	TArray<FMiniHUDLayoutRequest> Layouts;

	UPROPERTY(EditAnywhere, Category = "Mini|UI")
	TArray<FMiniHUDElementRequest> Elements;

	/** All configured UI is required; Dedicated Server has no local UI requirement. */
	bool GatherRequiredClasses(UWorld* World, bool bAuthority,
		TArray<FMiniRequiredActionClassRequest>& OutRequests, FString& OutError) const;
	bool GetRequiredFailure(const UWorld* World, uint64& OutGeneration, FString& OutReason) const;
	FMiniRequiredActionFailed OnRequiredActionFailed;

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
	virtual void OnGameFeatureUnregistering() override;
	virtual void BeginDestroy() override;
	/** Diagnostic suspension reuses contribution teardown, leaving receiver/policy registrations active. */
	void SetProbeSuspended(UWorld* World, bool bSuspend);
	FMiniWidgetContributionCounts GetProbeContributionCounts(const UWorld* World) const;

#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) override;
#endif
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
#if !UE_BUILD_SHIPPING
	friend class UMiniTask26ProbeSubsystem;
	friend class UMiniTask26RoleProbeSubsystem;
#endif
	struct FPerHUDData
	{
		TWeakObjectPtr<ULocalPlayer> LocalPlayer;
		TWeakObjectPtr<UMiniPrimaryGameLayout> RootLayout;
		TSharedPtr<FStreamableHandle> LoadHandle;
		TMap<int32, TWeakObjectPtr<UCommonActivatableWidget>> AddedLayouts;
		TMap<int32, FUIExtensionHandle> ExtensionHandles;
		uint64 Generation = 0;
		bool bClassesLoaded = false;
		bool bLoadFailed = false;
		bool bInjecting = false;
	};

	struct FPerWorldData
	{
		TArray<FMiniHUDLayoutRequest> LayoutRequests;
		TArray<FMiniHUDElementRequest> ElementRequests;
		TSharedPtr<FComponentRequestHandle> HUDReceiverHandle;
		TWeakObjectPtr<UMiniGameUIPolicy> Policy;
		FDelegateHandle LayoutReadyHandle;
		FDelegateHandle LayoutUnavailableHandle;
		TMap<TWeakObjectPtr<AMiniHUD>, FPerHUDData> HUDs;
		uint64 Generation = 0;
		FString FailureReason;
		bool bFailed = false;
		bool bProbeSuspended = false;
	};

	struct FPerContextData
	{
		FDelegateHandle GameInstanceStartHandle;
		FDelegateHandle WorldInitializedHandle;
		FDelegateHandle WorldCleanupHandle;
		TArray<FMiniHUDLayoutRequest> LayoutRequests;
		TArray<FMiniHUDElementRequest> ElementRequests;
		TMap<TWeakObjectPtr<UWorld>, FPerWorldData> Worlds;
	};

	TMap<FGameFeatureStateChangeContext, FPerContextData> ContextData;
	uint64 NextHUDGeneration = 0;
	uint64 NextWorldGeneration = 0;

	void AddToWorld(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext);
	void BindWorldPolicy(UWorld* World, const FGameFeatureStateChangeContext& ChangeContext);
	void HandleGameInstanceStart(UGameInstance* Instance, FGameFeatureStateChangeContext ChangeContext);
	void HandleWorldInitialized(UWorld* World, UWorld::InitializationValues Values,
		FGameFeatureStateChangeContext ChangeContext);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources,
		FGameFeatureStateChangeContext ChangeContext);
	void HandleHUDExtension(AActor* Actor, FName EventName, FGameFeatureStateChangeContext ChangeContext,
		TWeakObjectPtr<UWorld> WorldKey);
	void HandleLayoutReady(UCommonLocalPlayer* LocalPlayer, UMiniPrimaryGameLayout* RootLayout,
		FGameFeatureStateChangeContext ChangeContext, TWeakObjectPtr<UWorld> WorldKey);
	void HandleLayoutUnavailable(UCommonLocalPlayer* LocalPlayer, UMiniPrimaryGameLayout* RootLayout,
		FGameFeatureStateChangeContext ChangeContext, TWeakObjectPtr<UWorld> WorldKey);
	void TryAddHUD(AMiniHUD* HUD, const FGameFeatureStateChangeContext& ChangeContext,
		TWeakObjectPtr<UWorld> WorldKey);
	void HandleClassesLoaded(FGameFeatureStateChangeContext ChangeContext,
		TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation);
	void TryInjectHUD(const FGameFeatureStateChangeContext& ChangeContext,
		TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation);
	FPerHUDData* FindHUD(const FGameFeatureStateChangeContext& ChangeContext,
		TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation);
	void FailHUD(const FGameFeatureStateChangeContext& ChangeContext,
		TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation, const FString& Reason);
	void RemoveHUD(const FGameFeatureStateChangeContext& ChangeContext,
		TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey);
	static void RemoveWidgets(FPerHUDData& Data);
	static void ResetHUD(FPerHUDData& Data);
	static void ResetWorld(FPerWorldData& Data);
	void ResetContext(const FGameFeatureStateChangeContext& ChangeContext);
	void ResetAllContexts();
};
