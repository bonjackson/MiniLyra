#include "MiniPawnExtensionComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFramework/Controller.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

const FName UMiniPawnExtensionComponent::NAME_ActorFeatureName(TEXT("PawnExtension"));

UMiniPawnExtensionComponent::UMiniPawnExtensionComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMiniPawnExtensionComponent* UMiniPawnExtensionComponent::FindPawnExtensionComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UMiniPawnExtensionComponent>() : nullptr;
}

void UMiniPawnExtensionComponent::OnRegister()
{
	Super::OnRegister();

	const AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	if (!ensureMsgf(Pawn, TEXT("MiniPawnExtensionComponent must belong to a MiniCharacter: %s"), *GetNameSafe(GetOwner())))
	{
		return;
	}

	TArray<UActorComponent*> Extensions;
	Pawn->GetComponents(StaticClass(), Extensions);
	if (!ensureMsgf(Extensions.Num() == 1, TEXT("MiniCharacter %s must have exactly one PawnExtension component"), *GetNameSafe(Pawn)))
	{
		return;
	}
	RegisterInitStateFeature();
}

void UMiniPawnExtensionComponent::BeginPlay()
{
	Super::BeginPlay();
	BindOnActorInitStateChanged(NAME_None, FGameplayTag(), false);
	ensure(TryToChangeInitState(MiniGameplayTags::InitState_Spawned));
	CheckDefaultInitialization();
}

void UMiniPawnExtensionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeAbilitySystem(true);
	UnregisterInitStateFeature();
	Super::EndPlay(EndPlayReason);
}

void UMiniPawnExtensionComponent::InitializeAbilitySystem(UMiniAbilitySystemComponent* ASC)
{
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	AMiniPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	if (!Pawn || !PlayerState || !ASC || bAvatarSuperseded)
	{
		return;
	}
	if (AbilitySystemComponent == ASC && ASC->GetAvatarActor() == Pawn)
	{
		return;
	}
	if (AbilitySystemComponent && AbilitySystemComponent != ASC)
	{
		UninitializeAbilitySystem();
	}
	if (AActor* PreviousAvatar = ASC->GetAvatarActor())
	{
		if (PreviousAvatar != Pawn)
		{
			if (UMiniPawnExtensionComponent* OldExtension = FindPawnExtensionComponent(PreviousAvatar))
			{
				OldExtension->UninitializeAbilitySystem(true);
			}
		}
	}
	ASC->InitAbilityActorInfo(PlayerState, Pawn);
	AbilitySystemComponent = ASC;
	bWasBoundWithController = Pawn->GetController() != nullptr;
	UE_LOG(LogMiniInit, Display, TEXT("MiniASC AVATAR_BOUND: PlayerState=%s ASC=%s Avatar=%s Role=%d"),
		*GetPathNameSafe(PlayerState), *GetPathNameSafe(ASC), *Pawn->GetPathName(),
		static_cast<int32>(Pawn->GetLocalRole()));
}

void UMiniPawnExtensionComponent::UninitializeAbilitySystem(bool bSuperseded)
{
	if (bSuperseded)
	{
		bAvatarSuperseded = true;
	}
	UMiniAbilitySystemComponent* ASC = AbilitySystemComponent;
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	AbilitySystemComponent = nullptr;
	if (ASC && Pawn && ASC->GetAvatarActor() == Pawn)
	{
		ASC->CancelAbilities();
		ASC->SetAvatarActor(nullptr);
		UE_LOG(LogMiniInit, Display, TEXT("MiniASC AVATAR_UNBOUND: ASC=%s OldAvatar=%s Superseded=%d"),
			*GetPathNameSafe(ASC), *Pawn->GetPathName(), bSuperseded);
	}
}

void UMiniPawnExtensionComponent::NotifyPawnPossessed()
{
	const AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (Controller && Controller->GetPawn() == Pawn)
	{
		// A previously unpossessed Pawn may be reused by the same PlayerState.
		// Only a new, confirmed possession makes it eligible to own the Avatar again.
		bAvatarSuperseded = false;
		bWasBoundWithController = false;
	}
}

void UMiniPawnExtensionComponent::RefreshAbilitySystem()
{
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	if (!Pawn || bAvatarSuperseded || !HasReachedInitState(MiniGameplayTags::InitState_DataInitialized))
	{
		return;
	}
	AMiniPlayerState* PlayerState = Pawn->GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetMiniAbilitySystemComponent() : nullptr;
	const bool bDataMatches = PlayerState && Pawn->GetPawnData() &&
		PlayerState->GetPawnData() == Pawn->GetPawnData();
	const AController* Controller = Pawn->GetController();
	const bool bPossessionMatches = Pawn->GetLocalRole() == ROLE_SimulatedProxy ||
		(Controller && Controller->GetPlayerState<AMiniPlayerState>() == PlayerState);
	if (!ASC || !bDataMatches || !bPossessionMatches)
	{
		UninitializeAbilitySystem(bWasBoundWithController && !Controller);
		return;
	}
	InitializeAbilitySystem(ASC);
}

bool UMiniPawnExtensionComponent::CanChangeInitState(
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
		// A simulated proxy has no local controller or input setup to wait for.
		if (Pawn->GetLocalRole() == ROLE_SimulatedProxy)
		{
			return true;
		}
		const AController* Controller = Pawn->GetController();
		return Controller && Controller->GetPlayerState<AMiniPlayerState>() == PlayerState;
	}
	if (CurrentState == MiniGameplayTags::InitState_DataAvailable && DesiredState == MiniGameplayTags::InitState_DataInitialized)
	{
		return Manager->HasFeatureReachedInitState(Pawn, UMiniHeroComponent::NAME_ActorFeatureName,
			MiniGameplayTags::InitState_DataAvailable) &&
			Manager->HaveAllFeaturesReachedInitState(Pawn, MiniGameplayTags::InitState_DataAvailable, NAME_ActorFeatureName);
	}
	if (CurrentState == MiniGameplayTags::InitState_DataInitialized &&
		DesiredState == MiniGameplayTags::InitState_GameplayReady)
	{
		return AbilitySystemComponent && AbilitySystemComponent->GetAvatarActor() == Pawn &&
			Manager->HasFeatureReachedInitState(Pawn, UMiniHeroComponent::NAME_ActorFeatureName,
				MiniGameplayTags::InitState_DataInitialized);
	}
	return false;
}

void UMiniPawnExtensionComponent::HandleChangeInitState(
	UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniInitState TRANSITION: Feature=PawnExtension Role=%d Pawn=%s From=%s To=%s"),
		GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1, *GetPathNameSafe(GetOwner()),
		*CurrentState.ToString(), *DesiredState.ToString());
	if (DesiredState == MiniGameplayTags::InitState_DataInitialized)
	{
		if (AMiniCharacter* Pawn = GetPawn<AMiniCharacter>())
		{
			if (AMiniPlayerState* PlayerState = Pawn->GetPlayerState<AMiniPlayerState>())
			{
				InitializeAbilitySystem(PlayerState->GetMiniAbilitySystemComponent());
			}
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniInitState DEFERRED: Feature=PawnExtension Pawn=%s Target=InitState.GameplayReady WaitingFor=Input"),
			*GetPathNameSafe(GetOwner()));
	}
}

void UMiniPawnExtensionComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	if (Params.FeatureName != NAME_ActorFeatureName &&
		(Params.FeatureState == MiniGameplayTags::InitState_DataAvailable ||
		 Params.FeatureState == MiniGameplayTags::InitState_DataInitialized))
	{
		CheckDefaultInitialization();
	}
}

void UMiniPawnExtensionComponent::CheckDefaultInitialization()
{
	// Another feature may have become ready since the last PawnData or possession event.
	CheckDefaultInitializationForImplementers();
	static const TArray<FGameplayTag> StateChain = {
		MiniGameplayTags::InitState_Spawned,
		MiniGameplayTags::InitState_DataAvailable,
		MiniGameplayTags::InitState_DataInitialized,
		MiniGameplayTags::InitState_GameplayReady
	};
	ContinueInitStateChain(StateChain);
}
