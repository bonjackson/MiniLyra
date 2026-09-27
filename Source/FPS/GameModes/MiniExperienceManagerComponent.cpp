#include "MiniExperienceManagerComponent.h"

#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "GameFeatureAction.h"
#include "GameFeaturePluginOperationResult.h"
#include "GameFeaturesSubsystem.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "Net/UnrealNetwork.h"
#include "System/MiniAssetManager.h"
#include "System/MiniLogChannels.h"
#include "UObject/StrongObjectPtr.h"

// A request stays counted until its completion callback, even if its world has
// already ended. This keeps another PIE world's shared plugin active and also
// lets a late successful activation undo itself safely.
struct FMiniFeatureActivationLease
{
	FString PluginURL;
	FString NetMode;
	FName WorldContextHandle;
	bool bCompleted = false;
	bool bSucceeded = false;
	bool bReleaseRequested = false;
};

namespace
{
struct FMiniFeatureUsage
{
	int32 RequestCount = 0;
	bool bActivated = false;
};

TMap<FString, FMiniFeatureUsage> GFeatureUsage;
TMap<TWeakObjectPtr<UGameFeatureAction>, int32> GActionUsage;

void FinishFeatureLease(const TSharedRef<FMiniFeatureActivationLease>& Lease)
{
	check(IsInGameThread());
	FMiniFeatureUsage* Usage = GFeatureUsage.Find(Lease->PluginURL);
	check(Usage && Usage->RequestCount > 0);
	--Usage->RequestCount;
	if (Usage->RequestCount == 0)
	{
		const bool bDeactivate = Usage->bActivated;
		GFeatureUsage.Remove(Lease->PluginURL);
		UE_LOG(LogMiniExperience, Display, TEXT("MiniFeature Release NetMode=%s WorldContext=%s URL=%s FinalUser=1 Activated=%d"),
			*Lease->NetMode, *Lease->WorldContextHandle.ToString(), *Lease->PluginURL, bDeactivate ? 1 : 0);
		if (bDeactivate)
		{
			UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(Lease->PluginURL);
		}
	}
	else
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniFeature Release NetMode=%s WorldContext=%s URL=%s RemainingUsers=%d"),
			*Lease->NetMode, *Lease->WorldContextHandle.ToString(), *Lease->PluginURL, Usage->RequestCount);
	}
}

void ReleaseFeatureLease(const TSharedPtr<FMiniFeatureActivationLease>& Lease)
{
	if (!Lease.IsValid() || Lease->bReleaseRequested)
	{
		return;
	}
	Lease->bReleaseRequested = true;
	if (Lease->bCompleted)
	{
		FinishFeatureLease(Lease.ToSharedRef());
	}
}

void CompleteFeatureLease(const TSharedRef<FMiniFeatureActivationLease>& Lease, bool bSucceeded)
{
	check(IsInGameThread());
	if (Lease->bCompleted)
	{
		return;
	}
	Lease->bCompleted = true;
	Lease->bSucceeded = bSucceeded;
	FMiniFeatureUsage* Usage = GFeatureUsage.Find(Lease->PluginURL);
	check(Usage && Usage->RequestCount > 0);
	Usage->bActivated |= bSucceeded;
	if (Lease->bReleaseRequested)
	{
		FinishFeatureLease(Lease);
	}
}

struct FMiniActionCleanupState
{
	TArray<TStrongObjectPtr<UGameFeatureAction>> Actions;
	TArray<TSharedPtr<FMiniFeatureActivationLease>> Leases;
	int32 ExpectedPausers = INDEX_NONE;
	int32 ObservedPausers = 0;
	bool bFinished = false;

	void Finish()
	{
		if (bFinished || ExpectedPausers == INDEX_NONE || ObservedPausers != ExpectedPausers)
		{
			return;
		}
		bFinished = true;
		for (int32 Index = Actions.Num() - 1; Index >= 0; --Index)
		{
			UGameFeatureAction* Action = Actions[Index].Get();
			const TWeakObjectPtr<UGameFeatureAction> Key(Action);
			int32* Count = GActionUsage.Find(Key);
			check(Count && *Count > 0);
			if (--*Count == 0)
			{
				GActionUsage.Remove(Key);
				Action->OnGameFeatureUnloading();
				Action->OnGameFeatureUnregistering();
			}
		}
		Actions.Reset();
		for (int32 Index = Leases.Num() - 1; Index >= 0; --Index)
		{
			ReleaseFeatureLease(Leases[Index]);
		}
		Leases.Reset();
	}

	void OnPauserCompleted()
	{
		check(IsInGameThread());
		++ObservedPausers;
		Finish();
	}
};

const TCHAR* GetStateName(EMiniExperienceLoadState State)
{
	switch (State)
	{
	case EMiniExperienceLoadState::Unloaded: return TEXT("Unloaded");
	case EMiniExperienceLoadState::LoadingAssets: return TEXT("LoadingAssets");
	case EMiniExperienceLoadState::LoadingFeatures: return TEXT("LoadingFeatures");
	case EMiniExperienceLoadState::ExecutingActions: return TEXT("ExecutingActions");
	case EMiniExperienceLoadState::Loaded: return TEXT("Loaded");
	case EMiniExperienceLoadState::Failed: return TEXT("Failed");
	case EMiniExperienceLoadState::Deactivating: return TEXT("Deactivating");
	default: return TEXT("Unknown");
	}
}

const TCHAR* GetNetModeName(const AActor* Owner)
{
	switch (Owner ? Owner->GetNetMode() : NM_Standalone)
	{
	case NM_Standalone: return TEXT("Standalone");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	case NM_ListenServer: return TEXT("ListenServer");
	case NM_Client: return TEXT("Client");
	default: return TEXT("Unknown");
	}
}
}

UMiniExperienceManagerComponent::UMiniExperienceManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UMiniExperienceManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	// OnRep normally starts the client load. This also covers an ID applied before BeginPlay.
	if (LoadState == EMiniExperienceLoadState::Unloaded)
	{
		if (!SelectionFailureReason.IsEmpty())
		{
			FailExperience(SelectionFailureReason);
		}
		else if (CurrentExperienceId.IsValid())
		{
			StartExperienceLoad();
		}
	}
}

void UMiniExperienceManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	++LoadGeneration; // Invalidate both completion and cancellation callbacks before CancelHandle invokes one.
	if (LoadState != EMiniExperienceLoadState::Unloaded)
	{
		SetLoadState(EMiniExperienceLoadState::Deactivating);
	}
	CancelPendingLoad();
	CleanupExperienceActionsAndFeatures();
	CurrentExperience = nullptr;
	OnExperienceLoaded.Clear();
	OnExperienceFailed.Clear();
	Super::EndPlay(EndPlayReason);
}

void UMiniExperienceManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMiniExperienceManagerComponent, CurrentExperienceId);
	DOREPLIFETIME(UMiniExperienceManagerComponent, SelectionFailureReason);
}

bool UMiniExperienceManagerComponent::SetCurrentExperience(const FPrimaryAssetId& ExperienceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("Only an active server GameState may select an Experience"));
		return false;
	}
	if (CurrentExperienceId == ExperienceId && LoadState != EMiniExperienceLoadState::Unloaded)
	{
		return LoadState != EMiniExperienceLoadState::Failed;
	}
	if (LoadState != EMiniExperienceLoadState::Unloaded || CurrentExperienceId.IsValid())
	{
		UE_LOG(LogMiniExperience, Error, TEXT("Experience selection already started: current=%s requested=%s state=%s"),
			*CurrentExperienceId.ToString(), *ExperienceId.ToString(), GetStateName(LoadState));
		return false;
	}
	if (!ExperienceId.IsValid())
	{
		FailExperienceSelection(TEXT("Server selected an invalid Experience ID"));
		return false;
	}

	SelectionFailureReason.Reset();
	CurrentExperienceId = ExperienceId;
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogMiniExperience, Display, TEXT("Experience selected NetMode=%s ID=%s"),
		GetNetModeName(GetOwner()), *CurrentExperienceId.ToString());
	StartExperienceLoad();
	return LoadState != EMiniExperienceLoadState::Failed;
}

void UMiniExperienceManagerComponent::FailExperienceSelection(const FString& Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("Only an active server GameState may fail Experience selection"));
		return;
	}
	if (LoadState != EMiniExperienceLoadState::Unloaded || CurrentExperienceId.IsValid())
	{
		UE_LOG(LogMiniExperience, Error, TEXT("Cannot fail Experience selection after loading began: state=%s ID=%s"),
			GetStateName(LoadState), *CurrentExperienceId.ToString());
		return;
	}
	SelectionFailureReason = Reason.IsEmpty() ? TEXT("Experience selection failed without a reason") : Reason;
	GetOwner()->ForceNetUpdate();
	FailExperience(SelectionFailureReason);
}

FString UMiniExperienceManagerComponent::GetLoadingDebugString() const
{
	if (LoadState == EMiniExperienceLoadState::Failed)
	{
		return FString::Printf(TEXT("Experience Failed: %s"), *FailureReason);
	}
	return FString::Printf(TEXT("Experience %s: %s"), GetStateName(LoadState), *CurrentExperienceId.ToString());
}

void UMiniExperienceManagerComponent::CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate&& Delegate)
{
	if (!Delegate.IsBound() || bEndingPlay || LoadState == EMiniExperienceLoadState::Failed)
	{
		return;
	}
	if (IsExperienceLoaded())
	{
		Delegate.Execute(CurrentExperience);
	}
	else
	{
		OnExperienceLoaded.Add(MoveTemp(Delegate));
	}
}

void UMiniExperienceManagerComponent::CallOrRegister_OnExperienceFailed(FOnMiniExperienceFailed::FDelegate&& Delegate)
{
	if (!Delegate.IsBound() || bEndingPlay || LoadState == EMiniExperienceLoadState::Loaded)
	{
		return;
	}
	if (LoadState == EMiniExperienceLoadState::Failed)
	{
		Delegate.Execute(FailureReason);
	}
	else
	{
		OnExperienceFailed.Add(MoveTemp(Delegate));
	}
}

void UMiniExperienceManagerComponent::OnRep_CurrentExperienceId()
{
	UE_LOG(LogMiniExperience, Display, TEXT("Experience ID replicated NetMode=%s ID=%s"),
		GetNetModeName(GetOwner()), *CurrentExperienceId.ToString());
	if (CurrentExperienceId.IsValid() && LoadState == EMiniExperienceLoadState::Unloaded && !bEndingPlay)
	{
		StartExperienceLoad();
	}
}

void UMiniExperienceManagerComponent::OnRep_SelectionFailureReason()
{
	if (!SelectionFailureReason.IsEmpty() && LoadState == EMiniExperienceLoadState::Unloaded && !bEndingPlay)
	{
		FailExperience(SelectionFailureReason);
	}
}

void UMiniExperienceManagerComponent::StartExperienceLoad()
{
	if (bEndingPlay || LoadState != EMiniExperienceLoadState::Unloaded)
	{
		return;
	}
	SetLoadState(EMiniExperienceLoadState::LoadingAssets);
	UMiniAssetManager* AssetManager = UMiniAssetManager::GetMiniAssetManager();
	if (!AssetManager)
	{
		FailExperience(TEXT("MiniAssetManager is not configured"));
		return;
	}
	FString Error;
	if (!AssetManager->TryValidateExperienceId(CurrentExperienceId, Error))
	{
		FailExperience(Error);
		return;
	}

	const uint32 ExpectedGeneration = ++LoadGeneration;
	const TWeakObjectPtr<UMiniExperienceManagerComponent> WeakThis(this);
	FStreamableDelegate CompleteDelegate = FStreamableDelegate::CreateLambda([WeakThis, ExpectedGeneration]()
	{
		if (UMiniExperienceManagerComponent* Component = WeakThis.Get())
		{
			Component->HandleAssetsLoaded(ExpectedGeneration);
		}
	});

	AssetLoadHandle = AssetManager->LoadPrimaryAsset(CurrentExperienceId, {}, MoveTemp(CompleteDelegate));
	if (LoadState != EMiniExperienceLoadState::LoadingAssets)
	{
		AssetLoadHandle.Reset(); // The delegate may have executed synchronously.
		return;
	}
	if (AssetLoadHandle.IsValid() && AssetLoadHandle->WasCanceled())
	{
		HandleAssetsCanceled(ExpectedGeneration);
	}
	else if (!AssetLoadHandle.IsValid() || AssetLoadHandle->HasLoadCompleted())
	{
		// AssetManager may return no handle for an already loaded asset, or queue a completed callback.
		HandleAssetsLoaded(ExpectedGeneration);
	}
	else
	{
		AssetLoadHandle->BindCancelDelegate(FStreamableDelegate::CreateLambda([WeakThis, ExpectedGeneration]()
		{
			if (UMiniExperienceManagerComponent* Component = WeakThis.Get())
			{
				Component->HandleAssetsCanceled(ExpectedGeneration);
			}
		}));
	}
}

void UMiniExperienceManagerComponent::HandleAssetsLoaded(uint32 ExpectedGeneration)
{
	if (bEndingPlay || ExpectedGeneration != LoadGeneration || LoadState != EMiniExperienceLoadState::LoadingAssets)
	{
		return;
	}
	AssetLoadHandle.Reset();
	UMiniAssetManager* AssetManager = UMiniAssetManager::GetMiniAssetManager();
	if (!AssetManager)
	{
		FailExperience(TEXT("MiniAssetManager became unavailable during Experience load"));
		return;
	}
	UMiniExperienceDefinition* Experience = AssetManager->GetPrimaryAssetObject<UMiniExperienceDefinition>(CurrentExperienceId);
	if (!Experience)
	{
		FailExperience(FString::Printf(TEXT("Experience '%s' finished loading without a MiniExperienceDefinition object"),
			*CurrentExperienceId.ToString()));
		return;
	}
	if (Experience->GetPrimaryAssetId() != CurrentExperienceId)
	{
		FailExperience(FString::Printf(TEXT("Experience '%s' loaded an object with mismatched ID '%s'"),
			*CurrentExperienceId.ToString(), *Experience->GetPrimaryAssetId().ToString()));
		return;
	}
	FString Error;
	if (!Experience->ValidateDefinition(Error))
	{
		FailExperience(Error);
		return;
	}
	CurrentExperience = Experience;
	CompleteExperienceLoad();
}

void UMiniExperienceManagerComponent::HandleAssetsCanceled(uint32 ExpectedGeneration)
{
	if (bEndingPlay || ExpectedGeneration != LoadGeneration || LoadState != EMiniExperienceLoadState::LoadingAssets)
	{
		return;
	}
	FailExperience(FString::Printf(TEXT("Asset load for Experience '%s' was canceled"), *CurrentExperienceId.ToString()));
}

void UMiniExperienceManagerComponent::CompleteExperienceLoad()
{
	check(LoadState == EMiniExperienceLoadState::LoadingAssets && CurrentExperience);
	SetLoadState(EMiniExperienceLoadState::LoadingFeatures);
	const FWorldContext* WorldContext = GEngine ? GEngine->GetWorldContextFromWorld(GetWorld()) : nullptr;
	if (!WorldContext)
	{
		FailExperience(TEXT("No world context is available for world-scoped Experience Actions"));
		return;
	}
	ActionWorldContextHandle = WorldContext->ContextHandle;

	auto CollectPluginURLs = [this](const UObject* Source, const TArray<FString>& Names) -> bool
	{
		for (const FString& Name : Names)
		{
			const FString PluginName = Name.TrimStartAndEnd();
			FString URL;
			if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(PluginName, URL) || URL.IsEmpty())
			{
				FailExperience(FString::Printf(TEXT("GameFeature plugin '%s' from '%s' was not found"),
					*PluginName, *GetNameSafe(Source)));
				return false;
			}
			if (!GameFeaturePluginURLs.Contains(URL))
			{
				GameFeaturePluginURLs.Add(URL);
				UE_LOG(LogMiniExperience, Display, TEXT("MiniFeature Resolved NetMode=%s WorldContext=%s Name=%s URL=%s"),
					GetNetModeName(GetOwner()), *ActionWorldContextHandle.ToString(), *PluginName, *URL);
			}
		}
		return true;
	};

	if (!CollectPluginURLs(CurrentExperience, CurrentExperience->GameFeaturesToEnable))
	{
		return;
	}
	for (const UMiniExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (!CollectPluginURLs(ActionSet, ActionSet->GameFeaturesToEnable))
		{
			return;
		}
	}
	ActivateNextGameFeature(LoadGeneration);
}

void UMiniExperienceManagerComponent::ActivateNextGameFeature(uint32 ExpectedGeneration)
{
	if (bEndingPlay || ExpectedGeneration != LoadGeneration || LoadState != EMiniExperienceLoadState::LoadingFeatures)
	{
		return;
	}
	if (NextGameFeatureIndex == GameFeaturePluginURLs.Num())
	{
		ExecuteExperienceActions();
		return;
	}

	const FString& URL = GameFeaturePluginURLs[NextGameFeatureIndex++];
	TSharedRef<FMiniFeatureActivationLease> Lease = MakeShared<FMiniFeatureActivationLease>();
	Lease->PluginURL = URL;
	Lease->NetMode = GetNetModeName(GetOwner());
	Lease->WorldContextHandle = ActionWorldContextHandle;
	GameFeatureLeases.Add(Lease);
	++GFeatureUsage.FindOrAdd(URL).RequestCount;
	UE_LOG(LogMiniExperience, Display, TEXT("MiniFeature ActivationRequest NetMode=%s WorldContext=%s URL=%s"),
		*Lease->NetMode, *ActionWorldContextHandle.ToString(), *URL);

	// UE activates GameFeatureData Actions for the process. The lease prevents
	// one World from deactivating the plugin while another World still uses it;
	// only the Experience-owned Actions below receive a World-scoped context.
	const TWeakObjectPtr<UMiniExperienceManagerComponent> WeakThis(this);
	UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(URL,
		FGameFeaturePluginLoadComplete::CreateLambda([WeakThis, Lease, ExpectedGeneration](const UE::GameFeatures::FResult& Result)
		{
			const bool bSucceeded = Result.HasValue();
			const FString Error = bSucceeded ? FString() : UE::GameFeatures::ToString(Result);
			CompleteFeatureLease(Lease, bSucceeded);
			if (UMiniExperienceManagerComponent* Component = WeakThis.Get())
			{
				Component->HandleGameFeatureActivated(Lease, ExpectedGeneration, bSucceeded, Error);
			}
			else
			{
				ReleaseFeatureLease(Lease);
			}
		}));
}

void UMiniExperienceManagerComponent::HandleGameFeatureActivated(
	const TSharedRef<FMiniFeatureActivationLease>& Lease, uint32 ExpectedGeneration, bool bSucceeded, const FString& Error)
{
	if (bEndingPlay || ExpectedGeneration != LoadGeneration || LoadState != EMiniExperienceLoadState::LoadingFeatures || Lease->bReleaseRequested)
	{
		ReleaseFeatureLease(Lease);
		return;
	}
	if (!bSucceeded)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFeature ActivationFailed NetMode=%s WorldContext=%s URL=%s Error=%s"),
			*Lease->NetMode, *Lease->WorldContextHandle.ToString(), *Lease->PluginURL, *Error);
		FailExperience(FString::Printf(TEXT("GameFeature plugin '%s' failed to activate: %s"), *Lease->PluginURL, *Error));
		return;
	}
	UE_LOG(LogMiniExperience, Display, TEXT("MiniFeature ActivationSucceeded NetMode=%s WorldContext=%s URL=%s"),
		*Lease->NetMode, *Lease->WorldContextHandle.ToString(), *Lease->PluginURL);
	ActivateNextGameFeature(ExpectedGeneration);
}

void UMiniExperienceManagerComponent::ExecuteExperienceActions()
{
	check(LoadState == EMiniExperienceLoadState::LoadingFeatures && CurrentExperience);
	SetLoadState(EMiniExperienceLoadState::ExecutingActions);
	FGameFeatureActivatingContext Context;
	Context.SetRequiredWorldContextHandle(ActionWorldContextHandle);

	auto ActivateActions = [this, &Context](const TArray<TObjectPtr<UGameFeatureAction>>& Actions) -> bool
	{
		for (UGameFeatureAction* Action : Actions)
		{
			if (bEndingPlay || LoadState != EMiniExperienceLoadState::ExecutingActions)
			{
				return false;
			}
			if (!Action || ActivatedActions.Contains(Action))
			{
				continue;
			}
			ActivatedActions.Add(Action);
			const TWeakObjectPtr<UGameFeatureAction> Key(Action);
			int32& Count = GActionUsage.FindOrAdd(Key);
			if (Count++ == 0)
			{
				Action->OnGameFeatureRegistering();
				Action->OnGameFeatureLoading();
			}
			Action->OnGameFeatureActivating(Context);
			UE_LOG(LogMiniExperience, Display, TEXT("MiniAction Activated NetMode=%s WorldContext=%s Action=%s"),
				GetNetModeName(GetOwner()), *ActionWorldContextHandle.ToString(), *Action->GetPathName());
		}
		return true;
	};
	if (!ActivateActions(CurrentExperience->Actions))
	{
		return;
	}
	for (const UMiniExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (!ActivateActions(ActionSet->Actions))
		{
			return;
		}
	}
	for (int32 Index = 0; Index < ActivatedActions.Num(); ++Index)
	{
		if (bEndingPlay || LoadState != EMiniExperienceLoadState::ExecutingActions)
		{
			return;
		}
		UGameFeatureAction* Action = ActivatedActions[Index];
		Action->OnGameFeatureActivated();
	}
	if (bEndingPlay || LoadState != EMiniExperienceLoadState::ExecutingActions)
	{
		return;
	}
	SetLoadState(EMiniExperienceLoadState::Loaded);
	OnExperienceLoaded.Broadcast(CurrentExperience);
	OnExperienceLoaded.Clear();
	OnExperienceFailed.Clear();
}

void UMiniExperienceManagerComponent::CleanupExperienceActionsAndFeatures()
{
	if (ActivatedActions.IsEmpty())
	{
		for (int32 Index = GameFeatureLeases.Num() - 1; Index >= 0; --Index)
		{
			ReleaseFeatureLease(GameFeatureLeases[Index]);
		}
		GameFeatureLeases.Reset();
		return;
	}

	TSharedRef<FMiniActionCleanupState> Cleanup = MakeShared<FMiniActionCleanupState>();
	Cleanup->Leases = MoveTemp(GameFeatureLeases);
	for (UGameFeatureAction* Action : ActivatedActions)
	{
		Cleanup->Actions.Emplace(Action);
	}
	ActivatedActions.Reset();

	FGameFeatureDeactivatingContext Context(TEXT("MiniExperience"), [Cleanup](FStringView)
	{
		Cleanup->OnPauserCompleted();
	});
	Context.SetRequiredWorldContextHandle(ActionWorldContextHandle);
	for (int32 Index = Cleanup->Actions.Num() - 1; Index >= 0; --Index)
	{
		UGameFeatureAction* Action = Cleanup->Actions[Index].Get();
		Action->OnGameFeatureDeactivating(Context);
		UE_LOG(LogMiniExperience, Display, TEXT("MiniAction Deactivated WorldContext=%s Action=%s"),
			*ActionWorldContextHandle.ToString(), *Action->GetPathName());
	}
	Cleanup->ExpectedPausers = Context.GetNumPausers();
	Cleanup->Finish();
}

void UMiniExperienceManagerComponent::FailExperience(const FString& Reason)
{
	if (bEndingPlay || LoadState == EMiniExperienceLoadState::Failed || LoadState == EMiniExperienceLoadState::Deactivating)
	{
		return;
	}
	++LoadGeneration;
	CancelPendingLoad();
	CleanupExperienceActionsAndFeatures();
	CurrentExperience = nullptr;
	FailureReason = Reason.IsEmpty() ? TEXT("Experience load failed without a reason") : Reason;
	SetLoadState(EMiniExperienceLoadState::Failed);
	OnExperienceFailed.Broadcast(FailureReason);
	OnExperienceFailed.Clear();
	OnExperienceLoaded.Clear();
}

void UMiniExperienceManagerComponent::SetLoadState(EMiniExperienceLoadState NewState)
{
	const EMiniExperienceLoadState OldState = LoadState;
	LoadState = NewState;
	if (NewState == EMiniExperienceLoadState::Failed)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("Experience state NetMode=%s ID=%s %s -> Failed Reason=%s"),
			GetNetModeName(GetOwner()), *CurrentExperienceId.ToString(), GetStateName(OldState), *FailureReason);
	}
	else
	{
		UE_LOG(LogMiniExperience, Display, TEXT("Experience state NetMode=%s ID=%s %s -> %s"),
			GetNetModeName(GetOwner()), *CurrentExperienceId.ToString(), GetStateName(OldState), GetStateName(NewState));
	}
}

void UMiniExperienceManagerComponent::CancelPendingLoad()
{
	if (AssetLoadHandle.IsValid())
	{
		if (!AssetLoadHandle->HasLoadCompleted())
		{
			AssetLoadHandle->CancelHandle();
		}
		AssetLoadHandle.Reset();
	}
}
