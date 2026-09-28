#include "MiniGameFeatureAction_AddInput.h"

#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputMappingContext.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"

void UMiniGameFeatureAction_AddInput::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	FPerContextData& Data = ContextData.FindOrAdd(Context);
	RemoveAll(Data);
	Data.ExtensionRequest.Reset();
	Data.bProbeSuspended = false;
	if (!MappingContext || !GEngine)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniAddInput missing MappingContext: Action=%s"), *GetPathName());
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
			TSoftClassPtr<AActor>(AMiniCharacter::StaticClass()),
			UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(
				this, &ThisClass::HandlePawnExtension, FGameFeatureStateChangeContext(Context)));
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddInput ACTION_ACTIVE: World=%s Action=%s Mapping=%s"),
			*GetNameSafe(World), *GetPathName(), *GetPathNameSafe(MappingContext));
		break;
	}
}

void UMiniGameFeatureAction_AddInput::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	if (FPerContextData* Data = ContextData.Find(Context))
	{
		RemoveAll(*Data);
		Data->ExtensionRequest.Reset();
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddInput ACTION_INACTIVE: World=%s Action=%s"),
			*GetNameSafe(Data->World.Get()), *GetPathName());
		ContextData.Remove(Context);
	}
	Super::OnGameFeatureDeactivating(Context);
}

void UMiniGameFeatureAction_AddInput::HandlePawnExtension(
	AActor* Actor, FName EventName, FGameFeatureStateChangeContext ChangeContext)
{
	FPerContextData* Data = ContextData.Find(ChangeContext);
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(Actor);
	if (!Data || !Pawn || Pawn->GetWorld() != Data->World.Get())
	{
		return;
	}
	if (EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved ||
		EventName == UGameFrameworkComponentManager::NAME_ExtensionRemoved ||
		EventName == UMiniHeroComponent::NAME_InputUnavailable)
	{
		RemoveFromPawn(Pawn, *Data);
	}
	else if (EventName == UGameFrameworkComponentManager::NAME_ReceiverAdded ||
		EventName == UGameFrameworkComponentManager::NAME_ExtensionAdded ||
		EventName == UGameFrameworkComponentManager::NAME_GameActorReady ||
		EventName == UMiniHeroComponent::NAME_BindInputsNow)
	{
		BindToPawn(Pawn, *Data);
	}
}

void UMiniGameFeatureAction_AddInput::BindToPawn(AMiniCharacter* Pawn, FPerContextData& Data)
{
	if (!Pawn || !MappingContext || Data.bProbeSuspended)
	{
		return;
	}
	UMiniHeroComponent* Hero = Pawn->GetHeroComponent();
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(Pawn->GetController());
	ULocalPlayer* LocalPlayer = Controller && Controller->IsLocalController()
		? Controller->GetLocalPlayer() : nullptr;
	if (!Hero || !LocalPlayer)
	{
		return;
	}
	if (TWeakObjectPtr<UMiniHeroComponent>* Previous = Data.Owners.Find(LocalPlayer))
	{
		if (Previous->Get() != Hero)
		{
			if (UMiniHeroComponent* OldHero = Previous->Get())
			{
				OldHero->RemoveInputFeature();
			}
			Data.Owners.Remove(LocalPlayer);
		}
	}
	if (Hero->ActivateInput(MappingContext))
	{
		Data.Owners.Add(LocalPlayer, Hero);
	}
}

void UMiniGameFeatureAction_AddInput::RemoveFromPawn(AMiniCharacter* Pawn, FPerContextData& Data)
{
	UMiniHeroComponent* Hero = Pawn ? Pawn->GetHeroComponent() : nullptr;
	for (auto It = Data.Owners.CreateIterator(); It; ++It)
	{
		if (It->Value.Get() == Hero)
		{
			if (Hero)
			{
				Hero->RemoveInputFeature();
			}
			It.RemoveCurrent();
		}
	}
}

void UMiniGameFeatureAction_AddInput::RemoveAll(FPerContextData& Data)
{
	for (const TPair<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<UMiniHeroComponent>>& Pair : Data.Owners)
	{
		if (UMiniHeroComponent* Hero = Pair.Value.Get())
		{
			Hero->RemoveInputFeature();
		}
	}
	Data.Owners.Reset();
}

void UMiniGameFeatureAction_AddInput::SetProbeSuspended(UWorld* World, bool bSuspend)
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
			RemoveAll(Data);
		}
		else
		{
			for (TActorIterator<AMiniCharacter> It(World); It; ++It)
			{
				BindToPawn(*It, Data);
			}
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniAddInput PROBE_%s: World=%s Action=%s"),
			bSuspend ? TEXT("SUSPENDED") : TEXT("RESUMED"), *GetNameSafe(World), *GetPathName());
	}
#endif
}
