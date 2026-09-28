#include "MiniGameFeatureAction_AddAbilities.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniLogChannels.h"

void UMiniGameFeatureAction_AddAbilities::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	FPerContextData& Data = ContextData.FindOrAdd(Context);
	if (Data.ExtensionRequest.IsValid() || !Data.Grants.IsEmpty())
	{
		RevokeAll(Data);
		Data.ExtensionRequest.Reset();
	}
	if (!AbilitySet || !GEngine)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniAddAbilities missing AbilitySet: Action=%s"), *GetPathName());
		return;
	}
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (!Context.ShouldApplyToWorldContext(WorldContext) || !World || !World->IsGameWorld())
		{
			continue;
		}
		UGameInstance* GameInstance = World->GetGameInstance();
		UGameFrameworkComponentManager* Manager = GameInstance
			? GameInstance->GetSubsystem<UGameFrameworkComponentManager>() : nullptr;
		if (!Manager)
		{
			continue;
		}
		Data.World = World;
		Data.ExtensionRequest = Manager->AddExtensionHandler(
			TSoftClassPtr<AActor>(AMiniPlayerState::StaticClass()),
			UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(
				this, &ThisClass::HandlePlayerStateExtension, FGameFeatureStateChangeContext(Context)));
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddAbilities ACTION_ACTIVE: World=%s Action=%s Set=%s"),
			*GetNameSafe(World), *GetPathName(), *GetPathNameSafe(AbilitySet));
		break; // Experience provides exactly one required World context.
	}
}

void UMiniGameFeatureAction_AddAbilities::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	if (FPerContextData* Data = ContextData.Find(Context))
	{
		RevokeAll(*Data);
		Data->ExtensionRequest.Reset();
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddAbilities ACTION_INACTIVE: World=%s Action=%s"),
			*GetNameSafe(Data->World.Get()), *GetPathName());
		ContextData.Remove(Context);
	}
	Super::OnGameFeatureDeactivating(Context);
}

void UMiniGameFeatureAction_AddAbilities::HandlePlayerStateExtension(
	AActor* Actor, FName EventName, FGameFeatureStateChangeContext ChangeContext)
{
	FPerContextData* Data = ContextData.Find(ChangeContext);
	AMiniPlayerState* PlayerState = Cast<AMiniPlayerState>(Actor);
	if (!Data || !PlayerState || PlayerState->GetWorld() != Data->World.Get())
	{
		return;
	}
	if (EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved ||
		EventName == UGameFrameworkComponentManager::NAME_ExtensionRemoved)
	{
		RevokeFromPlayerState(PlayerState, *Data);
	}
	else if (EventName == UGameFrameworkComponentManager::NAME_ReceiverAdded ||
		EventName == UGameFrameworkComponentManager::NAME_ExtensionAdded ||
		EventName == UGameFrameworkComponentManager::NAME_GameActorReady ||
		EventName == AMiniPlayerState::NAME_AbilityActorReady)
	{
		GrantToPlayerState(PlayerState, *Data);
	}
}

void UMiniGameFeatureAction_AddAbilities::GrantToPlayerState(AMiniPlayerState* PlayerState, FPerContextData& Data)
{
	if (!PlayerState || !PlayerState->HasAuthority() || !PlayerState->HasActorBegunPlay() ||
		!AbilitySet || Data.bProbeSuspended || Data.Grants.Contains(PlayerState))
	{
		return;
	}
	UMiniAbilitySystemComponent* ASC = PlayerState->GetMiniAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}
	FMiniAbilitySetGrantedHandles Handles;
	if (AbilitySet->GiveToAbilitySystem(ASC, Handles, this))
	{
		const int32 AbilityCount = Handles.GetAbilityCount();
		const int32 EffectCount = Handles.GetEffectCount();
		const int32 AttributeCount = Handles.GetAttributeCount();
		Data.Grants.Add(PlayerState, MoveTemp(Handles));
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniAddAbilities GRANTED: PlayerState=%s Set=%s Abilities=%d Effects=%d Attributes=%d"),
			*PlayerState->GetPathName(), *AbilitySet->GetPathName(), AbilityCount, EffectCount, AttributeCount);
	}
}

void UMiniGameFeatureAction_AddAbilities::RevokeFromPlayerState(AMiniPlayerState* PlayerState, FPerContextData& Data)
{
	if (FMiniAbilitySetGrantedHandles* Handles = Data.Grants.Find(PlayerState))
	{
		const int32 AbilityCount = Handles->GetAbilityCount();
		const int32 EffectCount = Handles->GetEffectCount();
		const int32 AttributeCount = Handles->GetAttributeCount();
		if (IsValid(PlayerState))
		{
			Handles->TakeFromAbilitySystem(PlayerState->GetMiniAbilitySystemComponent());
		}
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniAddAbilities REVOKED: PlayerState=%s Abilities=%d Effects=%d Attributes=%d"),
			*GetPathNameSafe(PlayerState), AbilityCount, EffectCount, AttributeCount);
		Data.Grants.Remove(PlayerState);
	}
}

void UMiniGameFeatureAction_AddAbilities::RevokeAll(FPerContextData& Data)
{
	for (auto It = Data.Grants.CreateIterator(); It; ++It)
	{
		AMiniPlayerState* PlayerState = It->Key.Get();
		FMiniAbilitySetGrantedHandles& Handles = It->Value;
		const int32 AbilityCount = Handles.GetAbilityCount();
		const int32 EffectCount = Handles.GetEffectCount();
		const int32 AttributeCount = Handles.GetAttributeCount();
		if (IsValid(PlayerState))
		{
			Handles.TakeFromAbilitySystem(PlayerState->GetMiniAbilitySystemComponent());
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddAbilities REVOKED: PlayerState=%s Abilities=%d Effects=%d Attributes=%d"),
			*GetPathNameSafe(PlayerState), AbilityCount, EffectCount, AttributeCount);
	}
	Data.Grants.Reset();
}

void UMiniGameFeatureAction_AddAbilities::SetProbeSuspended(UWorld* World, bool bSuspend)
{
#if !UE_BUILD_SHIPPING
	if (!World)
	{
		return;
	}
	for (TPair<FGameFeatureStateChangeContext, FPerContextData>& Pair : ContextData)
	{
		FPerContextData& Data = Pair.Value;
		if (Data.World.Get() != World || Data.bProbeSuspended == bSuspend)
		{
			continue;
		}
		Data.bProbeSuspended = bSuspend;
		if (bSuspend)
		{
			RevokeAll(Data);
		}
		else
		{
			for (TActorIterator<AMiniPlayerState> It(World); It; ++It)
			{
				GrantToPlayerState(*It, Data);
			}
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddAbilities PROBE_%s: World=%s Action=%s"),
			bSuspend ? TEXT("SUSPENDED") : TEXT("RESUMED"), *GetNameSafe(World), *GetPathName());
	}
#endif
}
