#include "MiniGameFeatureAction_AddActors.h"

#if !UE_BUILD_SHIPPING
#include "Diagnostics/MiniTask26ActionProbeHooks.h"
#endif

#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFeaturesSubsystemSettings.h"
#include "System/MiniLogChannels.h"

#if WITH_EDITORONLY_DATA
#include "AssetRegistry/AssetBundleData.h"
#endif
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UMiniGameFeatureAction_AddActors::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	// A global plugin activation cannot own map-specific geometry. Experience supplies an explicit world handle.
	if (FGameFeatureStateChangeContext(Context) == FGameFeatureStateChangeContext())
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniAddActors INVALID_SCOPE: Action=%s"), *GetPathName());
		return;
	}
	if (ContextData.Contains(Context))
	{
		return;
	}
	FContextData Data;
	Data.Entries = Actors;
	ContextData.Add(Context, MoveTemp(Data));
	if (!GameInstanceStartHandle.IsValid())
	{
		GameInstanceStartHandle = FWorldDelegates::OnStartGameInstance.AddUObject(this, &ThisClass::HandleGameInstanceStart);
		WorldInitializedHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::HandleWorldInitialized);
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleWorldCleanup);
	}
	TArray<TWeakObjectPtr<UWorld>> Worlds;
	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			Worlds.Add(WorldContext.World());
		}
	}
	for (const TWeakObjectPtr<UWorld>& World : Worlds)
	{
		ConsiderWorld(World.Get());
	}
}

void UMiniGameFeatureAction_AddActors::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	RemoveContext(Context);
	Super::OnGameFeatureDeactivating(Context);
}

void UMiniGameFeatureAction_AddActors::OnGameFeatureUnregistering()
{
	RemoveAllContexts();
	Super::OnGameFeatureUnregistering();
}

void UMiniGameFeatureAction_AddActors::BeginDestroy()
{
	RemoveAllContexts();
	Super::BeginDestroy();
}

#if WITH_EDITORONLY_DATA
void UMiniGameFeatureAction_AddActors::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	for (const FMiniFeatureActorEntry& Entry : Actors)
	{
		if (!Entry.ActorClass.IsNull())
		{
			AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateServer,
				Entry.ActorClass.ToSoftObjectPath().GetAssetPath());
		}
	}
}
#endif

#if WITH_EDITOR
EDataValidationResult UMiniGameFeatureAction_AddActors::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const FMiniFeatureActorEntry& Entry : Actors)
	{
		UClass* Class = Entry.ActorClass.LoadSynchronous();
		const AActor* Default = Class && Class->IsChildOf(AActor::StaticClass()) ? Cast<AActor>(Class->GetDefaultObject()) : nullptr;
		if (!Default || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
			!Default->GetIsReplicated() || Entry.Transform.ContainsNaN() || !Entry.Transform.IsValid())
		{
			Context.AddError(FText::FromString(TEXT("Mini Add Actors requires concrete replicated classes and finite transforms.")));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif

void UMiniGameFeatureAction_AddActors::HandleGameInstanceStart(UGameInstance* GameInstance)
{
	ConsiderWorld(GameInstance ? GameInstance->GetWorld() : nullptr);
}

void UMiniGameFeatureAction_AddActors::HandleWorldInitialized(UWorld* World, const UWorld::InitializationValues InitializationValues)
{
	ConsiderWorld(World);
}

void UMiniGameFeatureAction_AddActors::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	TArray<FGameFeatureStateChangeContext> Contexts;
	ContextData.GetKeys(Contexts);
	for (const FGameFeatureStateChangeContext& Context : Contexts)
	{
		RemoveWorld(Context, World);
	}
}

void UMiniGameFeatureAction_AddActors::ConsiderWorld(UWorld* World)
{
	if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown || !GEngine)
	{
		return;
	}
	const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World);
	if (!WorldContext)
	{
		return;
	}
	TArray<FGameFeatureStateChangeContext> Contexts;
	ContextData.GetKeys(Contexts);
	for (const FGameFeatureStateChangeContext& Context : Contexts)
	{
		FContextData* Data = ContextData.Find(Context);
		if (!Data || !Context.ShouldApplyToWorldContext(*WorldContext) || Data->Worlds.Contains(World))
		{
			continue;
		}
		const uint64 Generation = ++NextGeneration;
		FWorldData& Added = Data->Worlds.Add(World);
		Added.Generation = Generation;
		Added.Entries = Data->Entries;
#if !UE_BUILD_SHIPPING
		FMiniTask26ActionProbeHooks::ActionWorldPrepared().Broadcast(this, World);
#endif
		if (!World->HasBegunPlay())
		{
			const FDelegateHandle Handle = World->OnWorldBeginPlay.AddUObject(this, &ThisClass::BeginWorld,
				Context, TWeakObjectPtr<UWorld>(World), Generation);
			if (FWorldData* Fresh = FindWorld(Context, World, Generation))
			{
				Fresh->BeginPlayHandle = Handle;
			}
			else
			{
				World->OnWorldBeginPlay.Remove(Handle);
			}
		}
		else
		{
			BeginWorld(Context, World, Generation);
		}
	}
}

UMiniGameFeatureAction_AddActors::FWorldData* UMiniGameFeatureAction_AddActors::FindWorld(
	FGameFeatureStateChangeContext Context, TWeakObjectPtr<UWorld> World, uint64 Generation)
{
	FContextData* Data = ContextData.Find(Context);
	FWorldData* WorldData = Data ? Data->Worlds.Find(World) : nullptr;
	return WorldData && WorldData->Generation == Generation ? WorldData : nullptr;
}

void UMiniGameFeatureAction_AddActors::BeginWorld(FGameFeatureStateChangeContext Context,
	TWeakObjectPtr<UWorld> WorldKey, uint64 Generation)
{
	UWorld* World = WorldKey.Get();
	FWorldData* Data = FindWorld(Context, WorldKey, Generation);
	if (!IsValid(World) || World->bIsTearingDown || !Data || Data->bReady || Data->bFailed || Data->LoadHandle.IsValid())
	{
		return;
	}
	World->OnWorldBeginPlay.Remove(Data->BeginPlayHandle);
	Data->BeginPlayHandle.Reset();
	// NetMode before BeginPlay is not sufficient to identify a connecting client.
	if (World->GetNetMode() == NM_Client || !World->GetAuthGameMode())
	{
		Data->bReady = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddActors SKIP_CLIENT: World=%s Action=%s"), *World->GetName(), *GetPathName());
		return;
	}
	const TArray<FMiniFeatureActorEntry> Entries = Data->Entries;
	TArray<FSoftObjectPath> Paths;
	for (const FMiniFeatureActorEntry& Entry : Entries)
	{
		if (Entry.ActorClass.IsNull() || Entry.Transform.ContainsNaN() || !Entry.Transform.IsValid())
		{
			FailWorld(Context, WorldKey, Generation, TEXT("invalid class or transform"));
			return;
		}
		Paths.AddUnique(Entry.ActorClass.ToSoftObjectPath());
	}
	if (Paths.IsEmpty())
	{
		Data->bReady = true;
		return;
	}
	const TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Paths, FStreamableDelegate::CreateUObject(this, &ThisClass::FinishLoad, Context, WorldKey, Generation),
		FStreamableManager::DefaultAsyncLoadPriority, false, true, TEXT("MiniAddActors"));
	if (FWorldData* Fresh = FindWorld(Context, WorldKey, Generation))
	{
		Fresh->LoadHandle = Handle;
		if (!Handle.IsValid())
		{
			FailWorld(Context, WorldKey, Generation, TEXT("class load request failed"));
			return;
		}
		bool bHoldStalled = false;
#if !UE_BUILD_SHIPPING
		FMiniTask26ActionProbeHooks::HandlePrepared().Broadcast(this, WorldKey.Get(), Handle, bHoldStalled);
#endif
		if (!bHoldStalled) { Handle->StartStalledHandle(); }
	}
	else if (Handle.IsValid())
	{
		Handle->CancelHandle();
	}
}

void UMiniGameFeatureAction_AddActors::FinishLoad(FGameFeatureStateChangeContext Context,
	TWeakObjectPtr<UWorld> WorldKey, uint64 Generation)
{
	UWorld* World = WorldKey.Get();
	FWorldData* Data = FindWorld(Context, WorldKey, Generation);
	if (!IsValid(World) || World->bIsTearingDown || !Data || Data->bSpawning || Data->bReady || Data->bFailed)
	{
		return;
	}
	const TArray<FMiniFeatureActorEntry> Entries = Data->Entries;
	for (const FMiniFeatureActorEntry& Entry : Entries)
	{
		UClass* Class = Entry.ActorClass.Get();
		const AActor* Default = Class && Class->IsChildOf(AActor::StaticClass()) ? Cast<AActor>(Class->GetDefaultObject()) : nullptr;
		if (!Default || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || !Default->GetIsReplicated())
		{
			FailWorld(Context, WorldKey, Generation, TEXT("class must be concrete and replicated"));
			return;
		}
	}
	Data->bSpawning = true;
	for (const FMiniFeatureActorEntry& Entry : Entries)
	{
		AActor* Actor = World->SpawnActorDeferred<AActor>(Entry.ActorClass.Get(), Entry.Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		FWorldData* Fresh = FindWorld(Context, WorldKey, Generation);
		if (!IsValid(Actor) || !Fresh || World->bIsTearingDown)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
			FailWorld(Context, WorldKey, Generation, TEXT("spawn failed or activation ended"));
			return;
		}
		Fresh->OwnedActors.Add(Actor);
		Actor->FinishSpawning(Entry.Transform);
		Fresh = FindWorld(Context, WorldKey, Generation);
		if (!Fresh || !IsValid(Actor) || Actor->IsActorBeingDestroyed() || World->bIsTearingDown)
		{
			if (IsValid(Actor) && !Actor->IsActorBeingDestroyed())
			{
				Actor->Destroy();
			}
			FailWorld(Context, WorldKey, Generation, TEXT("construction ended activation or destroyed actor"));
			return;
		}
	}
	if (FWorldData* Fresh = FindWorld(Context, WorldKey, Generation))
	{
		Fresh->bSpawning = false;
		Fresh->bReady = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddActors READY: World=%s Action=%s Actors=%d Generation=%llu"),
			*World->GetName(), *GetPathName(), Fresh->OwnedActors.Num(), Generation);
	}
}

void UMiniGameFeatureAction_AddActors::FailWorld(FGameFeatureStateChangeContext Context,
	TWeakObjectPtr<UWorld> World, uint64 Generation, const TCHAR* Reason)
{
	if (FWorldData* Data = FindWorld(Context, World, Generation))
	{
		Data->bFailed = true;
		Data->FailureReason = Reason;
		Data->bSpawning = false;
		// Detach all resources before cancellation/destruction can reenter the
		// Action or tear down its World. Failed worlds retain only failure state.
		const FString WorldName = GetNameSafe(World.Get());
		const FString ActionName = GetPathName();
		if (World.IsValid())
		{
			World->OnWorldBeginPlay.Remove(Data->BeginPlayHandle);
		}
		Data->BeginPlayHandle.Reset();
		TSharedPtr<FStreamableHandle> Load = MoveTemp(Data->LoadHandle);
		TArray<TWeakObjectPtr<AActor>> Owned = MoveTemp(Data->OwnedActors);
		if (Load.IsValid())
		{
			Load->CancelHandle();
			Load->ReleaseHandle();
			Load.Reset();
		}
		for (const TWeakObjectPtr<AActor>& Actor : Owned)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}
		UE_LOG(LogMiniInit, Error, TEXT("MiniAddActors FAILED: World=%s Action=%s Reason=%s Generation=%llu"),
			*WorldName, *ActionName, Reason, Generation);
		OnRequiredActionFailed.Broadcast(World.Get(), this, Generation, FString(Reason));
		// Failure may synchronously deactivate this context; do not use Data again.
	}
}

void UMiniGameFeatureAction_AddActors::ReleaseWorld(TWeakObjectPtr<UWorld> World, FWorldData& Data)
{
	if (World.IsValid())
	{
		World->OnWorldBeginPlay.Remove(Data.BeginPlayHandle);
	}
	Data.BeginPlayHandle.Reset();
	if (Data.LoadHandle.IsValid())
	{
		Data.LoadHandle->CancelHandle();
		Data.LoadHandle->ReleaseHandle();
		Data.LoadHandle.Reset();
	}
	for (int32 Index = Data.OwnedActors.Num() - 1; Index >= 0; --Index)
	{
		if (AActor* Actor = Data.OwnedActors[Index].Get())
		{
			Actor->Destroy();
		}
	}
	Data.OwnedActors.Reset();
}

void UMiniGameFeatureAction_AddActors::RemoveWorld(FGameFeatureStateChangeContext Context, TWeakObjectPtr<UWorld> World)
{
	FContextData* Data = ContextData.Find(Context);
	FWorldData Removed;
	if (Data && Data->Worlds.RemoveAndCopyValue(World, Removed))
	{
		const int32 ActorCount = Removed.OwnedActors.Num();
		ReleaseWorld(World, Removed);
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddActors WORLD_RELEASED: World=%s Action=%s Actors=%d"),
			*GetNameSafe(World.Get()), *GetPathName(), ActorCount);
	}
}

void UMiniGameFeatureAction_AddActors::RemoveContext(FGameFeatureStateChangeContext Context)
{
	FContextData Removed;
	if (ContextData.RemoveAndCopyValue(Context, Removed))
	{
		RemoveDelegatesIfUnused();
		for (TPair<TWeakObjectPtr<UWorld>, FWorldData>& Pair : Removed.Worlds)
		{
			const int32 ActorCount = Pair.Value.OwnedActors.Num();
			ReleaseWorld(Pair.Key, Pair.Value);
			UE_LOG(LogMiniInit, Display, TEXT("MiniAddActors WORLD_RELEASED: World=%s Action=%s Actors=%d"),
				*GetNameSafe(Pair.Key.Get()), *GetPathName(), ActorCount);
		}
	}
}

void UMiniGameFeatureAction_AddActors::RemoveAllContexts()
{
	TArray<FGameFeatureStateChangeContext> Contexts;
	ContextData.GetKeys(Contexts);
	for (const FGameFeatureStateChangeContext& Context : Contexts)
	{
		RemoveContext(Context);
	}
	RemoveDelegatesIfUnused();
}

void UMiniGameFeatureAction_AddActors::RemoveDelegatesIfUnused()
{
	if (ContextData.IsEmpty())
	{
		FWorldDelegates::OnStartGameInstance.Remove(GameInstanceStartHandle);
		FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitializedHandle);
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
		GameInstanceStartHandle.Reset();
		WorldInitializedHandle.Reset();
		WorldCleanupHandle.Reset();
	}
}

void UMiniGameFeatureAction_AddActors::GetWorldStats(const UWorld* World, int32& OutWorlds,
	int32& OutAliveActors, int32& OutPendingLoads, int32& OutBeginPlayBindings, bool& bOutReady, bool& bOutFailed) const
{
	OutWorlds = OutAliveActors = OutPendingLoads = OutBeginPlayBindings = 0;
	bOutReady = true;
	bOutFailed = false;
	for (const TPair<FGameFeatureStateChangeContext, FContextData>& Context : ContextData)
	{
		for (const TPair<TWeakObjectPtr<UWorld>, FWorldData>& Pair : Context.Value.Worlds)
		{
			if (World && Pair.Key.Get() != World)
			{
				continue;
			}
			++OutWorlds;
			bOutReady &= Pair.Value.bReady;
			bOutFailed |= Pair.Value.bFailed;
			OutBeginPlayBindings += Pair.Value.BeginPlayHandle.IsValid() ? 1 : 0;
			if (Pair.Value.LoadHandle.IsValid() && !Pair.Value.LoadHandle->HasLoadCompleted() && !Pair.Value.LoadHandle->WasCanceled())
			{
				++OutPendingLoads;
			}
			for (const TWeakObjectPtr<AActor>& Actor : Pair.Value.OwnedActors)
			{
				OutAliveActors += Actor.IsValid() && !Actor->IsActorBeingDestroyed() ? 1 : 0;
			}
		}
	}
	bOutReady &= OutWorlds > 0;
}

bool UMiniGameFeatureAction_AddActors::GatherRequiredClasses(UWorld* World, bool bAuthority,
	TArray<FMiniRequiredActionClassRequest>& OutRequests, FString& OutError) const
{
	if (!bAuthority) { return true; }
	const TArray<FMiniFeatureActorEntry>* Entries = &Actors;
	for (const auto& Pair : ContextData)
	{
		if (Pair.Value.Worlds.Contains(World))
		{
			Entries = &Pair.Value.Worlds.FindChecked(World).Entries;
			break;
		}
	}
	for (int32 Index = 0; Index < Entries->Num(); ++Index)
	{
		const FMiniFeatureActorEntry& Entry = (*Entries)[Index];
		if (Entry.ActorClass.IsNull() || Entry.Transform.ContainsNaN() || !Entry.Transform.IsValid())
		{
			OutError = FString::Printf(TEXT("%s Actor[%d] requires a class and finite transform"), *GetPathName(), Index);
			return false;
		}
		FMiniRequiredActionClassRequest& Request = OutRequests.AddDefaulted_GetRef();
		Request.Action = const_cast<UMiniGameFeatureAction_AddActors*>(this);
		Request.ClassPath = Entry.ActorClass.ToSoftObjectPath();
		Request.Entry = FString::Printf(TEXT("Actor[%d]"), Index);
		Request.Kind = EMiniRequiredActionClassKind::ReplicatedActor;
	}
	return true;
}

bool UMiniGameFeatureAction_AddActors::GetRequiredFailure(const UWorld* World,
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
