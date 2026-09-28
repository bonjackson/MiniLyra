#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"
#include "MiniHeroComponent.generated.h"

class UGameFrameworkComponentManager;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
class UMiniInputComponent;
class UMiniAbilitySystemComponent;
class UMiniInputConfig;
struct FInputActionValue;
struct FActorInitStateChangedParams;

/** Coordinates local input and the data/ASC prerequisites for gameplay. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniHeroComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()

public:
	UMiniHeroComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static const FName NAME_ActorFeatureName;
	static const FName NAME_BindInputsNow;
	static const FName NAME_InputUnavailable;

	static UMiniHeroComponent* FindHeroComponent(const AActor* Actor);

	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	virtual void CheckDefaultInitialization() override;
	void NotifyInputDependenciesChanged();
	void NotifyPawnUnpossessed();
	bool ActivateInput(UInputMappingContext* MappingContext);
	void RemoveInputFeature();
	void SetInputSuppressed(bool bSuppressed);
	bool IsInputActive() const { return bInputActive; }
	/** Task 11's local aim preview is owned by this pawn until the aim ability takes over. */
	bool OwnsTemporaryAimTag(const UMiniAbilitySystemComponent* ASC) const;
	int32 GetInputBindingCount() const { return BindingHandles.Num(); }
	bool OwnsInputMapping() const;

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void DeactivateInput();
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_JumpPressed(const FInputActionValue& Value);
	void Input_JumpReleased(const FInputActionValue& Value);
	void Input_AbilityPressed(FGameplayTag InputTag);
	void Input_AbilityReleased(FGameplayTag InputTag);
	void SetAimInputHeld(bool bHeld);

	TWeakObjectPtr<UInputMappingContext> RequestedMappingContext;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSubsystem;
	TWeakObjectPtr<UMiniInputComponent> BoundInputComponent;
	TWeakObjectPtr<UMiniAbilitySystemComponent> BoundAbilitySystem;
	TWeakObjectPtr<UMiniAbilitySystemComponent> AimTagAbilitySystem;
	TArray<uint32> BindingHandles;
	bool bInputActive = false;
	bool bInputSuppressed = false;
};
