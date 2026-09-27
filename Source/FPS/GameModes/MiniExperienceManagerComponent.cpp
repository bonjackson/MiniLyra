#include "MiniExperienceManagerComponent.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "Net/UnrealNetwork.h"
#include "System/MiniAssetManager.h"
#include "System/MiniLogChannels.h"

namespace
{
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
	if (!CurrentExperience->GameFeaturesToEnable.IsEmpty())
	{
		FailExperience(TEXT("Experience requests GameFeatures; activation is not implemented until task 06"));
		return;
	}
	for (const UMiniExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (!ActionSet->GameFeaturesToEnable.IsEmpty())
		{
			FailExperience(FString::Printf(TEXT("ActionSet '%s' requests GameFeatures; activation is not implemented until task 06"),
				*ActionSet->GetPathName()));
			return;
		}
	}

	SetLoadState(EMiniExperienceLoadState::ExecutingActions);
	if (!CurrentExperience->Actions.IsEmpty())
	{
		FailExperience(TEXT("Experience contains Actions; execution is not implemented until task 06"));
		return;
	}
	for (const UMiniExperienceActionSet* ActionSet : CurrentExperience->ActionSets)
	{
		if (!ActionSet->Actions.IsEmpty())
		{
			FailExperience(FString::Printf(TEXT("ActionSet '%s' contains Actions; execution is not implemented until task 06"),
				*ActionSet->GetPathName()));
			return;
		}
	}

	SetLoadState(EMiniExperienceLoadState::Loaded);
	OnExperienceLoaded.Broadcast(CurrentExperience);
	OnExperienceLoaded.Clear();
	OnExperienceFailed.Clear();
}

void UMiniExperienceManagerComponent::FailExperience(const FString& Reason)
{
	if (bEndingPlay || LoadState == EMiniExperienceLoadState::Failed || LoadState == EMiniExperienceLoadState::Deactivating)
	{
		return;
	}
	++LoadGeneration;
	CancelPendingLoad();
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
