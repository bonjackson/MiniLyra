#include "MiniHeroComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniPawnData.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "Components/GameFrameworkComponentDelegates.h"
#include "Components/GameFrameworkComponentManager.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Input/MiniInputComponent.h"
#include "Input/MiniInputConfig.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Math/RotationMatrix.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

const FName UMiniHeroComponent::NAME_ActorFeatureName(TEXT("Hero"));
const FName UMiniHeroComponent::NAME_BindInputsNow(TEXT("MiniBindInputsNow"));
const FName UMiniHeroComponent::NAME_InputUnavailable(TEXT("MiniInputUnavailable"));

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
	RemoveInputFeature();
	UnregisterInitStateFeature();
	Super::EndPlay(EndPlayReason);
}

bool UMiniHeroComponent::OwnsInputMapping() const
{
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = MappingSubsystem.Get();
	return bInputActive && Subsystem && RequestedMappingContext.IsValid() &&
		Subsystem->HasMappingContext(RequestedMappingContext.Get());
}

void UMiniHeroComponent::NotifyInputDependenciesChanged()
{
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	const UMiniPawnExtensionComponent* Extension = Pawn ? Pawn->GetPawnExtensionComponent() : nullptr;
	const UMiniAbilitySystemComponent* ASC = Extension ? Extension->GetMiniAbilitySystemComponent() : nullptr;
	if (!Pawn || !Pawn->IsLocallyControlled() || !Pawn->GetPlayerInputComponent() ||
		!HasReachedInitState(MiniGameplayTags::InitState_DataInitialized) ||
		!Pawn->GetPawnData() || !Pawn->GetPawnData()->InputConfig || !ASC ||
		ASC->GetAvatarActor() != Pawn)
	{
		return;
	}
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(Pawn, NAME_BindInputsNow);
}

void UMiniHeroComponent::NotifyPawnUnpossessed()
{
	DeactivateInput();
	if (AMiniCharacter* Pawn = GetPawn<AMiniCharacter>())
	{
		UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(Pawn, NAME_InputUnavailable);
	}
}

void UMiniHeroComponent::SetInputSuppressed(bool bSuppressed)
{
	if (bInputSuppressed == bSuppressed)
	{
		// A new Pawn may have been denied its first binding by a blocked Controller.
		if (!bSuppressed)
		{
			NotifyInputDependenciesChanged();
		}
		return;
	}
	bInputSuppressed = bSuppressed;
	if (bSuppressed)
	{
		DeactivateInput();
	}
	else
	{
		NotifyInputDependenciesChanged();
	}
}

bool UMiniHeroComponent::ActivateInput(UInputMappingContext* MappingContext)
{
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	AMiniPlayerController* Controller = Pawn ? Cast<AMiniPlayerController>(Pawn->GetController()) : nullptr;
	UMiniPawnExtensionComponent* Extension = Pawn ? Pawn->GetPawnExtensionComponent() : nullptr;
	UMiniAbilitySystemComponent* ASC = Extension ? Extension->GetMiniAbilitySystemComponent() : nullptr;
	UMiniInputComponent* InputComponent = Pawn ? Cast<UMiniInputComponent>(Pawn->GetPlayerInputComponent()) : nullptr;
	const UMiniInputConfig* Config = Pawn && Pawn->GetPawnData() ? Pawn->GetPawnData()->InputConfig : nullptr;
	ULocalPlayer* LocalPlayer = Controller && Controller->IsLocalController() ? Controller->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!MappingContext || !Pawn || !Controller || !InputComponent || !Config || !ASC || !Subsystem ||
		!HasReachedInitState(MiniGameplayTags::InitState_DataInitialized) ||
		ASC->GetAvatarActor() != Pawn || bInputSuppressed || Controller->IsMiniInputBlocked())
	{
		return false;
	}
	if (bInputActive && RequestedMappingContext.Get() == MappingContext &&
		BoundInputComponent.Get() == InputComponent && MappingSubsystem.Get() == Subsystem &&
		BoundAbilitySystem.Get() == ASC)
	{
		return true;
	}
	DeactivateInput();
	RequestedMappingContext = MappingContext;
	if (!InputComponent->BindNativeAction(Config, MiniGameplayTags::InputTag_Move,
		ETriggerEvent::Triggered, this, &ThisClass::Input_Move, BindingHandles, true) ||
		!InputComponent->BindNativeAction(Config, MiniGameplayTags::InputTag_Look,
		ETriggerEvent::Triggered, this, &ThisClass::Input_Look, BindingHandles, true) ||
		!InputComponent->BindNativeAction(Config, MiniGameplayTags::InputTag_Jump,
		ETriggerEvent::Started, this, &ThisClass::Input_JumpPressed, BindingHandles, true) ||
		!InputComponent->BindNativeAction(Config, MiniGameplayTags::InputTag_Jump,
		ETriggerEvent::Completed, this, &ThisClass::Input_JumpReleased, BindingHandles, true) ||
		InputComponent->BindAbilityActions(Config, this, &ThisClass::Input_AbilityPressed,
			&ThisClass::Input_AbilityReleased, BindingHandles) != Config->AbilityInputActions.Num())
	{
		InputComponent->RemoveBinds(BindingHandles);
		return false;
	}
	Subsystem->AddMappingContext(MappingContext, 0);
	MappingSubsystem = Subsystem;
	BoundInputComponent = InputComponent;
	BoundAbilitySystem = ASC;
	bInputActive = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniInput BOUND: Pawn=%s Controller=%s Bindings=%d Mapping=%s"),
		*Pawn->GetPathName(), *Controller->GetPathName(), BindingHandles.Num(), *MappingContext->GetPathName());
	CheckDefaultInitialization();
	return true;
}

void UMiniHeroComponent::DeactivateInput()
{
	if (AMiniCharacter* Pawn = GetPawn<AMiniCharacter>())
	{
		Pawn->StopJumping();
	}
	if (UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
	{
		ASC->ClearAbilityInput();
	}
	if (UMiniInputComponent* InputComponent = BoundInputComponent.Get())
	{
		InputComponent->RemoveBinds(BindingHandles);
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = MappingSubsystem.Get())
	{
		if (UInputMappingContext* MappingContext = RequestedMappingContext.Get())
		{
			Subsystem->RemoveMappingContext(MappingContext);
		}
	}
	if (bInputActive)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniInput UNBOUND: Pawn=%s"), *GetPathNameSafe(GetPawn<AMiniCharacter>()));
	}
	BindingHandles.Reset();
	MappingSubsystem.Reset();
	BoundInputComponent.Reset();
	BoundAbilitySystem.Reset();
	bInputActive = false;
}

void UMiniHeroComponent::RemoveInputFeature()
{
	DeactivateInput();
	RequestedMappingContext.Reset();
}

void UMiniHeroComponent::Input_Move(const FInputActionValue& Value)
{
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!bInputActive || !Pawn || !Controller)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawOnly(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	const FRotationMatrix Rotation(YawOnly);
	Pawn->AddMovementInput(Rotation.GetUnitAxis(EAxis::X), Axis.Y);
	Pawn->AddMovementInput(Rotation.GetUnitAxis(EAxis::Y), Axis.X);
}

void UMiniHeroComponent::Input_Look(const FInputActionValue& Value)
{
	AMiniCharacter* Pawn = GetPawn<AMiniCharacter>();
	if (bInputActive && Pawn)
	{
		const FVector2D Axis = Value.Get<FVector2D>();
		Pawn->AddControllerYawInput(Axis.X);
		Pawn->AddControllerPitchInput(Axis.Y);
	}
}

void UMiniHeroComponent::Input_JumpPressed(const FInputActionValue& /*Value*/)
{
	if (bInputActive)
	{
		if (UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
		{
			ASC->AbilityInputTagPressed(MiniGameplayTags::InputTag_Jump);
		}
	}
}

void UMiniHeroComponent::Input_JumpReleased(const FInputActionValue& /*Value*/)
{
	if (UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
	{
		ASC->AbilityInputTagReleased(MiniGameplayTags::InputTag_Jump);
	}
}

void UMiniHeroComponent::Input_AbilityPressed(FGameplayTag InputTag)
{
	if (bInputActive)
	{
		if (UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
		{
			ASC->AbilityInputTagPressed(InputTag);
			UE_LOG(LogMiniInit, Display, TEXT("MiniInput TAG_PRESSED: Pawn=%s Tag=%s"),
				*GetPathNameSafe(GetPawn<AMiniCharacter>()), *InputTag.ToString());
		}
	}
}

void UMiniHeroComponent::Input_AbilityReleased(FGameplayTag InputTag)
{
	if (UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
	{
		ASC->AbilityInputTagReleased(InputTag);
		UE_LOG(LogMiniInit, Display, TEXT("MiniInput TAG_RELEASED: Pawn=%s Tag=%s"),
			*GetPathNameSafe(GetPawn<AMiniCharacter>()), *InputTag.ToString());
	}
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
	if (CurrentState == MiniGameplayTags::InitState_DataInitialized &&
		DesiredState == MiniGameplayTags::InitState_GameplayReady)
	{
		const UMiniPawnExtensionComponent* Extension = Pawn->GetPawnExtensionComponent();
		const UMiniAbilitySystemComponent* ASC = Extension ? Extension->GetMiniAbilitySystemComponent() : nullptr;
		if (!ASC || ASC->GetAvatarActor() != Pawn ||
			!Manager->HasFeatureReachedInitState(Pawn, UMiniPawnExtensionComponent::NAME_ActorFeatureName,
				MiniGameplayTags::InitState_GameplayReady))
		{
			return false;
		}
		return !Pawn->IsLocallyControlled() || (bInputActive && OwnsInputMapping());
	}
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
		UE_LOG(LogMiniInit, Display, TEXT("MiniInitState DEFERRED: Feature=Hero Pawn=%s Target=InitState.GameplayReady WaitingFor=Input"),
			*GetPathNameSafe(GetOwner()));
	}
}

void UMiniHeroComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{
	if (Params.FeatureName == UMiniPawnExtensionComponent::NAME_ActorFeatureName &&
		(Params.FeatureState == MiniGameplayTags::InitState_DataInitialized ||
		 Params.FeatureState == MiniGameplayTags::InitState_GameplayReady))
	{
		CheckDefaultInitialization();
		NotifyInputDependenciesChanged();
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
