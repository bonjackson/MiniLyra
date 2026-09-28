#include "MiniHeroComponent.h"

#include "Character/MiniCharacter.h"
#include "Character/MiniPawnData.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFramework/Controller.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

const FName UMiniHeroComponent::NAME_ActorFeatureName(TEXT("Hero"));

UMiniHeroComponent::UMiniHeroComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMiniHeroComponent* UMiniHeroComponent::FindHeroComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMiniHeroComponent>() : nullptr;
}

void UMiniHeroComponent::OnRegister()
{
	Super::OnRegister();

	const AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	if (!ensureMsgf(Pawn, TEXT("MiniHeroComponent must belong to a MiniCharacter: %s"), *GetNameSafe(GetOwner())))
	{
		return;
	}

	TArray<UActorComponent*> Heroes;
	Pawn->GetComponents(StaticClass(), Heroes);
	if (!ensureMsgf(Heroes.Num() == 1, TEXT("MiniCharacter %s must have exactly one Hero component"), *GetNameSafe(Pawn)))
	{
		return;
	}
	RegisterInitStateFeature();
}

void UMiniHeroComponent::BeginPlay()
{
	Super::BeginPlay();
	BindOnActorInitStateChanged(UMiniPawnExtensionComponent::NAME_ActorFeatureName, FGameplayTag(), false);
	ensure(TryToChangeInitState(MiniGameplayTags::InitState_Spawned));
	CheckDefaultInitialization();
}

void UMiniHeroComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterInitStateFeature();
	Super::EndPlay(EndPlayReason);
}

bool UMiniHeroComponent::CanChangeInitState(
	UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const
{
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	if (!Pawn || !Manager)
	{
		return false;
	}

	if (!CurrentState.IsValid() && DesiredState == MiniGameplayTags::InitState_Spawned)
	{
		return true;
	}
	if (CurrentState == MiniGameplayTags::InitState_Spawned && DesiredState == MiniGameplayTags::InitState_DataAvailable)
	{
		const AMiniPlayerState* PlayerState = Pawn->GetPlayerStateForInitialization();
		const UMiniPawnData* PawnData = Pawn->GetPawnDataForInitialization();
		if (!PawnData || !PlayerState || PlayerState->GetPawnData() != PawnData)
		{
			return false;
		}
		// Only authority and autonomous pawns require a paired controller.
		// Simulated proxies do not have a LocalPlayer or InputComponent.
		if (Pawn->GetLocalRole() == ROLE_SimulatedProxy)
		{
			return true;
		}
		const AController* Controller = Pawn->GetController();
		return Controller && Controller->GetPlayerState<AMiniPlayerState>() == PlayerState;
	}
	if (CurrentState == MiniGameplayTags::InitState_DataAvailable && DesiredState == MiniGameplayTags::InitState_DataInitialized)
	{
		return Manager->HasFeatureReachedInitState(Pawn, UMiniPawnExtensionComponent::NAME_ActorFeatureName,
			MiniGameplayTags::InitState_DataInitialized);
	}
	// Do not call this fully gameplay-ready until ASC and input setup exist.
	return false;
}

void UMiniHeroComponent::HandleChangeInitState(
	UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniInitState TRANSITION: Feature=Hero Role=%d Pawn=%s From=%s To=%s"),
		GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1, *GetPathNameSafe(GetOwner()),
		*CurrentState.ToString(), *DesiredState.ToString());
	if (DesiredState == MiniGameplayTags::InitState_DataInitialized)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniInitState DEFERRED: Feature=Hero Pawn=%s Target=InitState.GameplayReady WaitingFor=InputAndCamera"),
			*GetPathNameSafe(GetOwner()));
	}
}

void UMiniHeroComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	if (Params.FeatureName == UMiniPawnExtensionComponent::NAME_ActorFeatureName &&
		Params.FeatureState == MiniGameplayTags::InitState_DataInitialized)
	{
		CheckDefaultInitialization();
	}
}

void UMiniHeroComponent::CheckDefaultInitialization()
{
	static const TArray<FGameplayTag> StateChain = {
		MiniGameplayTags::InitState_Spawned,
		MiniGameplayTags::InitState_DataAvailable,
		MiniGameplayTags::InitState_DataInitialized,
		MiniGameplayTags::InitState_GameplayReady
	};
	ContinueInitStateChain(StateChain);
}
