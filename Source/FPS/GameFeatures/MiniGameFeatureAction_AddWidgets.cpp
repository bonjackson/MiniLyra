#include "MiniGameFeatureAction_AddWidgets.h"

#if !UE_BUILD_SHIPPING
#include "Diagnostics/MiniTask26ActionProbeHooks.h"
#endif

#include "CommonActivatableWidget.h"
#include "CommonLocalPlayer.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StreamableManager.h"
#include "EngineUtils.h"
#include "GameFeaturesSubsystemSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameUIManagerSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Player/MiniHUD.h"
#include "PrimaryGameLayout.h"
#include "System/MiniLogChannels.h"
#include "UI/MiniGameUIPolicy.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UI/MiniHUDLayout.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

#if WITH_EDITORONLY_DATA
#include "AssetRegistry/AssetBundleData.h"
#endif
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UMiniGameFeatureAction_AddWidgets::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	ResetContext(Context);
	FPerContextData& Data = ContextData.Add(Context);
	// Configuration changes during activation cannot invalidate the pending load.
	Data.LayoutRequests = Layouts;
	Data.ElementRequests = Elements;
	const FGameFeatureStateChangeContext Key(Context);
	Data.GameInstanceStartHandle = FWorldDelegates::OnStartGameInstance.AddUObject(
		this, &ThisClass::HandleGameInstanceStart, Key);
	Data.WorldInitializedHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(
		this, &ThisClass::HandleWorldInitialized, Key);
	Data.WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
		this, &ThisClass::HandleWorldCleanup, Key);
	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			if (Context.ShouldApplyToWorldContext(WorldContext))
			{
				AddToWorld(WorldContext.World(), Key);
			}
		}
	}
}

void UMiniGameFeatureAction_AddWidgets::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	ResetContext(Context);
	Super::OnGameFeatureDeactivating(Context);
}

void UMiniGameFeatureAction_AddWidgets::OnGameFeatureUnregistering()
{
	ResetAllContexts();
	Super::OnGameFeatureUnregistering();
}

void UMiniGameFeatureAction_AddWidgets::BeginDestroy()
{
	ResetAllContexts();
	Super::BeginDestroy();
}

void UMiniGameFeatureAction_AddWidgets::HandleGameInstanceStart(UGameInstance* Instance,
	FGameFeatureStateChangeContext ChangeContext)
{
	AddToWorld(Instance ? Instance->GetWorld() : nullptr, ChangeContext);
}

void UMiniGameFeatureAction_AddWidgets::HandleWorldInitialized(UWorld* World,
	UWorld::InitializationValues Values, FGameFeatureStateChangeContext ChangeContext)
{
	AddToWorld(World, ChangeContext);
}

void UMiniGameFeatureAction_AddWidgets::AddToWorld(UWorld* World,
	const FGameFeatureStateChangeContext& ChangeContext)
{
	FPerContextData* Context = ContextData.Find(ChangeContext);
	const FWorldContext* WorldContext = GEngine && World ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	if (!Context || !World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer ||
		!WorldContext || !ChangeContext.ShouldApplyToWorldContext(*WorldContext))
	{
		return;
	}
	UGameInstance* Instance = World->GetGameInstance();
	UGameFrameworkComponentManager* Manager = Instance
		? Instance->GetSubsystem<UGameFrameworkComponentManager>() : nullptr;
	if (!Manager)
	{
		return;
	}
	const TWeakObjectPtr<UWorld> WorldKey(World);
	if (Context->Worlds.Contains(WorldKey))
	{
		BindWorldPolicy(World, ChangeContext);
		return;
	}
	FPerWorldData& WorldData = Context->Worlds.Add(WorldKey);
	WorldData.Generation = ++NextWorldGeneration;
	WorldData.LayoutRequests = Context->LayoutRequests;
	WorldData.ElementRequests = Context->ElementRequests;
#if !UE_BUILD_SHIPPING
	FMiniTask26ActionProbeHooks::ActionWorldPrepared().Broadcast(this, World);
#endif
	BindWorldPolicy(World, ChangeContext);
	// AddExtensionHandler synchronously calls existing receivers: install our World first.
	TSharedPtr<FComponentRequestHandle> Receiver = Manager->AddExtensionHandler(
		TSoftClassPtr<AActor>(AMiniHUD::StaticClass()),
		UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(
			this, &ThisClass::HandleHUDExtension, ChangeContext, WorldKey));
	Context = ContextData.Find(ChangeContext);
	FPerWorldData* Data = Context ? Context->Worlds.Find(WorldKey) : nullptr;
	if (Data)
	{
		Data->HUDReceiverHandle = MoveTemp(Receiver);
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddWidgets WORLD_ACTIVE: World=%s Action=%s"),
			*GetNameSafe(World), *GetPathName());
	}
}

void UMiniGameFeatureAction_AddWidgets::BindWorldPolicy(UWorld* World,
	const FGameFeatureStateChangeContext& ChangeContext)
{
	FPerContextData* Context = ContextData.Find(ChangeContext);
	FPerWorldData* Data = Context ? Context->Worlds.Find(World) : nullptr;
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	UGameUIManagerSubsystem* UIManager = Instance ? Instance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	UMiniGameUIPolicy* Policy = UIManager ? Cast<UMiniGameUIPolicy>(UIManager->GetCurrentUIPolicy()) : nullptr;
	if (!Data || !Policy || Data->Policy.Get() == Policy)
	{
		return;
	}
	if (UMiniGameUIPolicy* Previous = Data->Policy.Get())
	{
		Previous->OnRootLayoutReady.Remove(Data->LayoutReadyHandle);
		Previous->OnRootLayoutUnavailable.Remove(Data->LayoutUnavailableHandle);
	}
	Data->Policy = Policy;
	Data->LayoutReadyHandle = Policy->OnRootLayoutReady.AddUObject(this,
		&ThisClass::HandleLayoutReady, ChangeContext, TWeakObjectPtr<UWorld>(World));
	Data->LayoutUnavailableHandle = Policy->OnRootLayoutUnavailable.AddUObject(this,
		&ThisClass::HandleLayoutUnavailable, ChangeContext, TWeakObjectPtr<UWorld>(World));
}

void UMiniGameFeatureAction_AddWidgets::HandleWorldCleanup(UWorld* World,
	bool bSessionEnded, bool bCleanupResources, FGameFeatureStateChangeContext ChangeContext)
{
	FPerContextData* Context = ContextData.Find(ChangeContext);
	if (!Context)
	{
		return;
	}
	FPerWorldData Data;
	if (Context->Worlds.RemoveAndCopyValue(World, Data))
	{
		ResetWorld(Data);
	}
}

void UMiniGameFeatureAction_AddWidgets::HandleHUDExtension(AActor* Actor, FName EventName,
	FGameFeatureStateChangeContext ChangeContext, TWeakObjectPtr<UWorld> WorldKey)
{
	AMiniHUD* HUD = Cast<AMiniHUD>(Actor);
	if (!HUD || HUD->GetWorld() != WorldKey.Get())
	{
		return;
	}
	if (EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved ||
		EventName == UGameFrameworkComponentManager::NAME_ExtensionRemoved)
	{
		RemoveHUD(ChangeContext, WorldKey, HUD);
	}
	else if (EventName == UGameFrameworkComponentManager::NAME_ReceiverAdded ||
		EventName == UGameFrameworkComponentManager::NAME_ExtensionAdded ||
		EventName == UGameFrameworkComponentManager::NAME_GameActorReady)
	{
		TryAddHUD(HUD, ChangeContext, WorldKey);
	}
}

void UMiniGameFeatureAction_AddWidgets::HandleLayoutReady(UCommonLocalPlayer* LocalPlayer,
	UMiniPrimaryGameLayout* RootLayout, FGameFeatureStateChangeContext ChangeContext,
	TWeakObjectPtr<UWorld> WorldKey)
{
	if (!WorldKey.IsValid() || !RootLayout || !RootLayout->IsLayoutReady())
	{
		return;
	}
	for (TActorIterator<AMiniHUD> It(WorldKey.Get()); It; ++It)
	{
		APlayerController* Controller = It->GetOwningPlayerController();
		if (Controller && Controller->GetLocalPlayer() == LocalPlayer)
		{
			TryAddHUD(*It, ChangeContext, WorldKey);
		}
	}
}

void UMiniGameFeatureAction_AddWidgets::HandleLayoutUnavailable(UCommonLocalPlayer* LocalPlayer,
	UMiniPrimaryGameLayout* RootLayout, FGameFeatureStateChangeContext ChangeContext,
	TWeakObjectPtr<UWorld> WorldKey)
{
	FPerContextData* Context = ContextData.Find(ChangeContext);
	FPerWorldData* Data = Context ? Context->Worlds.Find(WorldKey) : nullptr;
	if (!Data)
	{
		return;
	}
	TArray<TWeakObjectPtr<AMiniHUD>> ToRemove;
	for (const auto& Pair : Data->HUDs)
	{
		if (Pair.Value.LocalPlayer.Get() == LocalPlayer &&
			(!Pair.Value.RootLayout.IsValid() || Pair.Value.RootLayout.Get() == RootLayout))
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (TWeakObjectPtr<AMiniHUD> HUD : ToRemove)
	{
		RemoveHUD(ChangeContext, WorldKey, HUD);
	}
}

void UMiniGameFeatureAction_AddWidgets::TryAddHUD(AMiniHUD* HUD,
	const FGameFeatureStateChangeContext& ChangeContext, TWeakObjectPtr<UWorld> WorldKey)
{
	APlayerController* Controller = IsValid(HUD) ? HUD->GetOwningPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = Controller && Controller->IsLocalController() ? Controller->GetLocalPlayer() : nullptr;
	if (!LocalPlayer || !WorldKey.IsValid() || HUD->GetWorld() != WorldKey.Get())
	{
		return;
	}
	BindWorldPolicy(WorldKey.Get(), ChangeContext);
	FPerContextData* Context = ContextData.Find(ChangeContext);
	FPerWorldData* World = Context ? Context->Worlds.Find(WorldKey) : nullptr;
	if (!World || World->bFailed || World->bProbeSuspended)
	{
		return;
	}
	// A controller may briefly have its outgoing and replacement HUD in one World.
	TArray<TWeakObjectPtr<AMiniHUD>> PreviousHUDs;
	for (const auto& Pair : World->HUDs)
	{
		if (Pair.Key.Get() != HUD && Pair.Value.LocalPlayer.Get() == LocalPlayer)
		{
			PreviousHUDs.Add(Pair.Key);
		}
	}
	for (TWeakObjectPtr<AMiniHUD> Previous : PreviousHUDs)
	{
		RemoveHUD(ChangeContext, WorldKey, Previous);
	}
	Context = ContextData.Find(ChangeContext);
	World = Context ? Context->Worlds.Find(WorldKey) : nullptr;
	if (!World)
	{
		return;
	}
	if (FPerHUDData* Existing = World->HUDs.Find(HUD))
	{
		TryInjectHUD(ChangeContext, WorldKey, HUD, Existing->Generation);
		return;
	}
	FPerHUDData& Data = World->HUDs.Add(HUD);
	Data.Generation = ++NextHUDGeneration;
	Data.LocalPlayer = LocalPlayer;
	const uint64 Generation = Data.Generation;
	TArray<FSoftObjectPath> Paths;
	for (const FMiniHUDLayoutRequest& Entry : World->LayoutRequests)
	{
		if (!Entry.LayoutClass.IsNull()) { Paths.AddUnique(Entry.LayoutClass.ToSoftObjectPath()); }
	}
	for (const FMiniHUDElementRequest& Entry : World->ElementRequests)
	{
		if (!Entry.WidgetClass.IsNull()) { Paths.AddUnique(Entry.WidgetClass.ToSoftObjectPath()); }
	}
	if (Paths.IsEmpty())
	{
		HandleClassesLoaded(ChangeContext, WorldKey, HUD, Generation);
		return;
	}
	Data.LoadHandle = UAssetManager::Get().GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateUObject(this, &ThisClass::HandleClassesLoaded,
			ChangeContext, WorldKey, TWeakObjectPtr<AMiniHUD>(HUD), Generation),
		FStreamableManager::DefaultAsyncLoadPriority, false, true, TEXT("MiniAddWidgets"));
	if (Data.LoadHandle.IsValid())
	{
		bool bHoldStalled = false;
#if !UE_BUILD_SHIPPING
		FMiniTask26ActionProbeHooks::HandlePrepared().Broadcast(this, WorldKey.Get(), Data.LoadHandle, bHoldStalled);
#endif
		if (!bHoldStalled) { Data.LoadHandle->StartStalledHandle(); }
	}
	else
	{
		FailHUD(ChangeContext, WorldKey, HUD, Generation, TEXT("class load request failed"));
	}
}

UMiniGameFeatureAction_AddWidgets::FPerHUDData* UMiniGameFeatureAction_AddWidgets::FindHUD(
	const FGameFeatureStateChangeContext& ChangeContext, TWeakObjectPtr<UWorld> WorldKey,
	TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation)
{
	FPerContextData* Context = ContextData.Find(ChangeContext);
	FPerWorldData* World = Context ? Context->Worlds.Find(WorldKey) : nullptr;
	FPerHUDData* Data = World ? World->HUDs.Find(HUDKey) : nullptr;
	return WorldKey.IsValid() && !WorldKey->bIsTearingDown && World && !World->bFailed &&
		!World->bProbeSuspended && Data && Data->Generation == Generation ? Data : nullptr;
}

void UMiniGameFeatureAction_AddWidgets::HandleClassesLoaded(FGameFeatureStateChangeContext ChangeContext,
	TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation)
{
	FPerHUDData* Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
	if (!Data || !WorldKey.IsValid() || !HUDKey.IsValid())
	{
		return;
	}
	FPerContextData* Context = ContextData.Find(ChangeContext);
	FPerWorldData* World = Context ? Context->Worlds.Find(WorldKey) : nullptr;
	if (!World) { return; }
	for (const FMiniHUDLayoutRequest& Entry : World->LayoutRequests)
	{
		UClass* Class = Entry.LayoutClass.Get();
		if (!Class || !Class->IsChildOf(UCommonActivatableWidget::StaticClass()) ||
			Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || !Entry.LayerTag.IsValid())
		{
			Data->bLoadFailed = true;
		}
	}
	for (const FMiniHUDElementRequest& Entry : World->ElementRequests)
	{
		UClass* Class = Entry.WidgetClass.Get();
		if (!Class || !Class->IsChildOf(UUserWidget::StaticClass()) ||
			Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || !Entry.SlotTag.IsValid())
		{
			Data->bLoadFailed = true;
		}
	}
	if (Data->bLoadFailed)
	{
		FailHUD(ChangeContext, WorldKey, HUDKey, Generation, TEXT("required widget class or tag is invalid"));
		return;
	}
	Data->bClassesLoaded = true;
	TryInjectHUD(ChangeContext, WorldKey, HUDKey, Generation);
}

void UMiniGameFeatureAction_AddWidgets::TryInjectHUD(const FGameFeatureStateChangeContext& ChangeContext,
	TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation)
{
	FPerHUDData* Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
	AMiniHUD* HUD = HUDKey.Get();
	if (!Data || !HUD || !WorldKey.IsValid() || !Data->bClassesLoaded || Data->bLoadFailed || Data->bInjecting)
	{
		return;
	}
	APlayerController* Controller = HUD->GetOwningPlayerController();
	ULocalPlayer* LocalPlayer = Controller && Controller->IsLocalController() ? Controller->GetLocalPlayer() : nullptr;
	UMiniPrimaryGameLayout* Root = Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(LocalPlayer));
	if (!Root || !Root->IsLayoutReady() || HUD->GetWorld() != WorldKey.Get() || Data->LocalPlayer.Get() != LocalPlayer)
	{
		// Policy Ready or a future HUD extension event retries; there is no timer/delay.
		return;
	}
	if (Data->RootLayout.IsValid() && Data->RootLayout.Get() != Root)
	{
		RemoveWidgets(*Data);
		Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
		if (!Data) { return; }
	}
	Data->RootLayout = Root;
	Data->bInjecting = true;
	ON_SCOPE_EXIT
	{
		if (FPerHUDData* Current = FindHUD(ChangeContext, WorldKey, HUDKey, Generation))
		{
			Current->bInjecting = false;
		}
	};
	const TArray<FMiniHUDLayoutRequest> LayoutRequests = ContextData.FindChecked(ChangeContext).Worlds.FindChecked(WorldKey).LayoutRequests;
	const TArray<FMiniHUDElementRequest> ElementRequests = ContextData.FindChecked(ChangeContext).Worlds.FindChecked(WorldKey).ElementRequests;
	for (int32 Index = 0; Index < LayoutRequests.Num(); ++Index)
	{
		Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
		if (!Data) { return; }
		if (Data->AddedLayouts.Contains(Index)) { continue; }
		const FMiniHUDLayoutRequest& Entry = LayoutRequests[Index];
		UCommonActivatableWidget* Widget = Root->PushWidgetToLayerStack(Entry.LayerTag, Entry.LayoutClass.Get());
		// Activation/Construct may cause feature teardown, so do not keep Map pointers across it.
		Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
		if (!Data)
		{
			if (Widget) { Widget->DeactivateWidget(); Root->FindAndRemoveWidgetFromLayer(Widget); }
			return;
		}
		if (Widget)
		{
			Data->AddedLayouts.Add(Index, Widget);
		}
		else
		{
			FailHUD(ChangeContext, WorldKey, HUDKey, Generation,
				FString::Printf(TEXT("NO_LAYER_OR_WIDGET: layer=%s class=%s"), *Entry.LayerTag.ToString(), *Entry.LayoutClass.ToString()));
			return;
		}
	}
	UUIExtensionSubsystem* Extensions = WorldKey->GetSubsystem<UUIExtensionSubsystem>();
	if (!Extensions && !ElementRequests.IsEmpty())
	{
		FailHUD(ChangeContext, WorldKey, HUDKey, Generation, TEXT("NO_EXTENSION_SUBSYSTEM"));
		return;
	}
	for (int32 Index = 0; Index < ElementRequests.Num(); ++Index)
	{
		Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
		if (!Data) { return; }
		if (Data->ExtensionHandles.Contains(Index)) { continue; }
		const FMiniHUDElementRequest& Entry = ElementRequests[Index];
		FUIExtensionHandle Handle = Extensions->RegisterExtensionAsWidgetForContext(
			Entry.SlotTag, LocalPlayer, Entry.WidgetClass.Get(), Entry.Priority);
		Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
		if (!Data)
		{
			Handle.Unregister();
			return;
		}
		if (!Handle.IsValid())
		{
			FailHUD(ChangeContext, WorldKey, HUDKey, Generation,
				FString::Printf(TEXT("EXTENSION_REGISTRATION_FAILED: slot=%s"), *Entry.SlotTag.ToString()));
			return;
		}
		// The native Mini HUD constructs matched extensions synchronously. A valid
		// registration handle alone does not prove an actual widget was attached.
		bool bAttached = false;
		if (UCommonActivatableWidgetContainerBase* GameLayer = Root->GetLayerWidget(UMiniPrimaryGameLayout::GetGameLayerTag()))
		{
			for (UCommonActivatableWidget* LayoutWidget : GameLayer->GetWidgetList())
			{
				if (const UMiniHUDLayout* Layout = Cast<UMiniHUDLayout>(LayoutWidget))
				{
					bAttached |= Layout->HasAttachedExtensionWidget(Handle);
				}
			}
		}
		if (!bAttached)
		{
			Handle.Unregister();
			FailHUD(ChangeContext, WorldKey, HUDKey, Generation,
				FString::Printf(TEXT("REQUIRED_EXTENSION_NOT_ATTACHED: slot=%s class=%s"), *Entry.SlotTag.ToString(), *Entry.WidgetClass.ToString()));
			return;
		}
		Data->ExtensionHandles.Add(Index, MoveTemp(Handle));
	}
	Data = FindHUD(ChangeContext, WorldKey, HUDKey, Generation);
	if (!Data) { return; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniAddWidgets INJECTED: HUD=%s Layouts=%d Elements=%d"),
		*GetNameSafe(HUD), Data->AddedLayouts.Num(), Data->ExtensionHandles.Num());
}

void UMiniGameFeatureAction_AddWidgets::RemoveHUD(const FGameFeatureStateChangeContext& ChangeContext,
	TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey)
{
	FPerContextData* Context = ContextData.Find(ChangeContext);
	FPerWorldData* World = Context ? Context->Worlds.Find(WorldKey) : nullptr;
	FPerHUDData Data;
	// Remove first: widget/delegate callbacks cannot resurrect a revoked entry.
	if (World && World->HUDs.RemoveAndCopyValue(HUDKey, Data))
	{
		ResetHUD(Data);
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddWidgets REMOVED: HUD=%s"), *GetNameSafe(HUDKey.Get()));
	}
}

void UMiniGameFeatureAction_AddWidgets::RemoveWidgets(FPerHUDData& Data)
{
	TMap<int32, FUIExtensionHandle> Handles = MoveTemp(Data.ExtensionHandles);
	TMap<int32, TWeakObjectPtr<UCommonActivatableWidget>> AddedLayouts = MoveTemp(Data.AddedLayouts);
	const TWeakObjectPtr<UMiniPrimaryGameLayout> Root = Data.RootLayout;
	Data.RootLayout.Reset();
	// Unregister extensions before their host layout is dismantled.
	for (auto& Pair : Handles) { Pair.Value.Unregister(); }
	for (auto& Pair : AddedLayouts)
	{
		if (UCommonActivatableWidget* Widget = Pair.Value.Get())
		{
			Widget->DeactivateWidget();
			if (Root.IsValid()) { Root->FindAndRemoveWidgetFromLayer(Widget); }
			else { Widget->RemoveFromParent(); }
		}
	}
}

void UMiniGameFeatureAction_AddWidgets::ResetHUD(FPerHUDData& Data)
{
	++Data.Generation;
	TSharedPtr<FStreamableHandle> Load = MoveTemp(Data.LoadHandle);
	if (Load.IsValid()) { Load->CancelHandle(); }
	RemoveWidgets(Data);
	if (Load.IsValid()) { Load->ReleaseHandle(); }
}

void UMiniGameFeatureAction_AddWidgets::ResetWorld(FPerWorldData& Data)
{
	if (UMiniGameUIPolicy* Policy = Data.Policy.Get())
	{
		Policy->OnRootLayoutReady.Remove(Data.LayoutReadyHandle);
		Policy->OnRootLayoutUnavailable.Remove(Data.LayoutUnavailableHandle);
	}
	Data.Policy.Reset();
	Data.HUDReceiverHandle.Reset();
	for (auto& Pair : Data.HUDs) { ResetHUD(Pair.Value); }
	Data.HUDs.Empty();
}

void UMiniGameFeatureAction_AddWidgets::ResetContext(const FGameFeatureStateChangeContext& ChangeContext)
{
	FPerContextData Data;
	if (!ContextData.RemoveAndCopyValue(ChangeContext, Data)) { return; }
	FWorldDelegates::OnStartGameInstance.Remove(Data.GameInstanceStartHandle);
	FWorldDelegates::OnPostWorldInitialization.Remove(Data.WorldInitializedHandle);
	FWorldDelegates::OnWorldCleanup.Remove(Data.WorldCleanupHandle);
	for (auto& Pair : Data.Worlds) { ResetWorld(Pair.Value); }
}

void UMiniGameFeatureAction_AddWidgets::ResetAllContexts()
{
	TArray<FGameFeatureStateChangeContext> Keys;
	ContextData.GetKeys(Keys);
	for (const FGameFeatureStateChangeContext& Key : Keys) { ResetContext(Key); }
}

void UMiniGameFeatureAction_AddWidgets::SetProbeSuspended(UWorld* World, bool bSuspend)
{
#if !UE_BUILD_SHIPPING
	if (!World) { return; }
	TArray<FGameFeatureStateChangeContext> Keys;
	ContextData.GetKeys(Keys);
	for (const FGameFeatureStateChangeContext& Key : Keys)
	{
		FPerContextData* Context = ContextData.Find(Key);
		FPerWorldData* Data = Context ? Context->Worlds.Find(World) : nullptr;
		if (!Data || Data->bProbeSuspended == bSuspend) { continue; }
		Data->bProbeSuspended = bSuspend;
		if (bSuspend)
		{
			TArray<TWeakObjectPtr<AMiniHUD>> HUDKeys;
			Data->HUDs.GetKeys(HUDKeys);
			for (TWeakObjectPtr<AMiniHUD> HUD : HUDKeys) { RemoveHUD(Key, World, HUD); }
		}
		else
		{
			for (TActorIterator<AMiniHUD> It(World); It; ++It) { TryAddHUD(*It, Key, World); }
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddWidgets PROBE_%s: World=%s"),
			bSuspend ? TEXT("SUSPENDED") : TEXT("RESUMED"), *GetNameSafe(World));
	}
#endif
}

FMiniWidgetContributionCounts UMiniGameFeatureAction_AddWidgets::GetProbeContributionCounts(const UWorld* World) const
{
	FMiniWidgetContributionCounts Counts;
#if !UE_BUILD_SHIPPING
	for (const auto& Context : ContextData)
	{
		for (const auto& Pair : Context.Value.Worlds)
		{
			if (Pair.Key.Get() != World) { continue; }
			Counts.HUDs += Pair.Value.HUDs.Num();
			for (const auto& HUD : Pair.Value.HUDs)
			{
				Counts.Layouts += HUD.Value.AddedLayouts.Num();
				Counts.Elements += HUD.Value.ExtensionHandles.Num();
				Counts.PendingLoads += HUD.Value.LoadHandle.IsValid() &&
					!HUD.Value.LoadHandle->HasLoadCompleted() && !HUD.Value.LoadHandle->WasCanceled() ? 1 : 0;
			}
		}
	}
#endif
	return Counts;
}

#if WITH_EDITORONLY_DATA
void UMiniGameFeatureAction_AddWidgets::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	Super::AddAdditionalAssetBundleData(AssetBundleData);
	for (const FMiniHUDLayoutRequest& Entry : Layouts)
	{
		if (!Entry.LayoutClass.IsNull())
		{
			AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateClient,
				Entry.LayoutClass.ToSoftObjectPath().GetAssetPath());
		}
	}
	for (const FMiniHUDElementRequest& Entry : Elements)
	{
		if (!Entry.WidgetClass.IsNull())
		{
			AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateClient,
				Entry.WidgetClass.ToSoftObjectPath().GetAssetPath());
		}
	}
}
#endif

#if WITH_EDITOR
EDataValidationResult UMiniGameFeatureAction_AddWidgets::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bInvalid = false;
	TSet<FString> Entries;
	for (const FMiniHUDLayoutRequest& Entry : Layouts)
	{
		const FString Key = Entry.LayerTag.ToString() + TEXT(":") + Entry.LayoutClass.ToString();
		if (Entry.LayoutClass.IsNull() || !Entry.LayerTag.IsValid() || Entries.Contains(Key))
		{
			Context.AddError(FText::FromString(TEXT("Mini HUD layout requires a class, layer and unique class/layer pair.")));
			bInvalid = true;
		}
		Entries.Add(Key);
	}
	Entries.Reset();
	for (const FMiniHUDElementRequest& Entry : Elements)
	{
		const FString Key = Entry.SlotTag.ToString() + TEXT(":") + Entry.WidgetClass.ToString();
		if (Entry.WidgetClass.IsNull() || !Entry.SlotTag.IsValid() || Entries.Contains(Key))
		{
			Context.AddError(FText::FromString(TEXT("Mini HUD element requires a class, slot and unique class/slot pair.")));
			bInvalid = true;
		}
		Entries.Add(Key);
	}
	return bInvalid || Result == EDataValidationResult::Invalid ? EDataValidationResult::Invalid
		: EDataValidationResult::Valid;
}
#endif

bool UMiniGameFeatureAction_AddWidgets::GatherRequiredClasses(UWorld* World, bool bAuthority,
	TArray<FMiniRequiredActionClassRequest>& OutRequests, FString& OutError) const
{
	if (!World || World->GetNetMode() == NM_DedicatedServer) { return true; }
	const TArray<FMiniHUDLayoutRequest>* LayoutRequests = &Layouts;
	const TArray<FMiniHUDElementRequest>* ElementRequests = &Elements;
	for (const auto& Pair : ContextData)
	{
		if (Pair.Value.Worlds.Contains(World))
		{
			LayoutRequests = &Pair.Value.Worlds.FindChecked(World).LayoutRequests;
			ElementRequests = &Pair.Value.Worlds.FindChecked(World).ElementRequests;
			break;
		}
	}
	for (int32 Index = 0; Index < LayoutRequests->Num(); ++Index)
	{
		const FMiniHUDLayoutRequest& Entry = (*LayoutRequests)[Index];
		if (Entry.LayoutClass.IsNull() || !Entry.LayerTag.IsValid())
		{
			OutError = FString::Printf(TEXT("%s Layout[%d] requires a class and layer tag"), *GetPathName(), Index);
			return false;
		}
		FMiniRequiredActionClassRequest& Request = OutRequests.AddDefaulted_GetRef();
		Request.Action = const_cast<UMiniGameFeatureAction_AddWidgets*>(this);
		Request.ClassPath = Entry.LayoutClass.ToSoftObjectPath();
		Request.Entry = FString::Printf(TEXT("Layout[%d] layer=%s"), Index, *Entry.LayerTag.ToString());
		Request.Kind = EMiniRequiredActionClassKind::Layout;
	}
	for (int32 Index = 0; Index < ElementRequests->Num(); ++Index)
	{
		const FMiniHUDElementRequest& Entry = (*ElementRequests)[Index];
		if (Entry.WidgetClass.IsNull() || !Entry.SlotTag.IsValid())
		{
			OutError = FString::Printf(TEXT("%s Element[%d] requires a class and slot tag"), *GetPathName(), Index);
			return false;
		}
		FMiniRequiredActionClassRequest& Request = OutRequests.AddDefaulted_GetRef();
		Request.Action = const_cast<UMiniGameFeatureAction_AddWidgets*>(this);
		Request.ClassPath = Entry.WidgetClass.ToSoftObjectPath();
		Request.Entry = FString::Printf(TEXT("Element[%d] slot=%s"), Index, *Entry.SlotTag.ToString());
		Request.Kind = EMiniRequiredActionClassKind::HUDElement;
	}
	return true;
}

bool UMiniGameFeatureAction_AddWidgets::GetRequiredFailure(const UWorld* World,
	uint64& OutGeneration, FString& OutReason) const
{
	for (const auto& Context : ContextData)
	{
		for (const auto& Pair : Context.Value.Worlds)
		{
			if (Pair.Key.Get() == World && Pair.Value.bFailed)
			{
				OutGeneration = Pair.Value.Generation;
				OutReason = Pair.Value.FailureReason;
				return true;
			}
		}
	}
	return false;
}

void UMiniGameFeatureAction_AddWidgets::FailHUD(const FGameFeatureStateChangeContext& ChangeContext,
	TWeakObjectPtr<UWorld> WorldKey, TWeakObjectPtr<AMiniHUD> HUDKey, uint64 Generation, const FString& Reason)
{
	if (!FindHUD(ChangeContext, WorldKey, HUDKey, Generation)) { return; }
	FPerWorldData* World = ContextData.FindChecked(ChangeContext).Worlds.Find(WorldKey);
	World->bFailed = true;
	World->FailureReason = Reason;
	const uint64 WorldGeneration = World->Generation;
	const FString WorldName = GetNameSafe(WorldKey.Get());
	TMap<TWeakObjectPtr<AMiniHUD>, FPerHUDData> HUDs = MoveTemp(World->HUDs);
	// Detach ownership before callbacks from cancel/destruct can reenter.
	for (auto& Pair : HUDs) { ResetHUD(Pair.Value); }
	UE_LOG(LogMiniInit, Error, TEXT("MiniAddWidgets REQUIRED_FAILED: World=%s Action=%s Generation=%llu Reason=%s"),
		*WorldName, *GetPathName(), WorldGeneration, *Reason);
	OnRequiredActionFailed.Broadcast(WorldKey.Get(), this, WorldGeneration, Reason);
	// Failure may synchronously deactivate this context; never reuse World here.
}
