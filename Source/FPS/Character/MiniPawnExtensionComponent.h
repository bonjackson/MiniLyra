#pragma once

#include "Components/GameFrameworkInitStateInterface.h"
#include "Components/PawnComponent.h"
#include "MiniPawnExtensionComponent.generated.h"

class UGameFrameworkComponentManager;
class UMiniAbilitySystemComponent;
struct FActorInitStateChangedParams;

/** Coordinates the PawnData and other init-state features on a Mini pawn. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniPawnExtensionComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
	GENERATED_BODY()

public:
	UMiniPawnExtensionComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static const FName NAME_ActorFeatureName;

	static UMiniPawnExtensionComponent* FindPawnExtensionComponent(const AActor* Actor);

	virtual FName GetFeatureName() const override { return NAME_ActorFeatureName; }
	virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) const override;
	virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag CurrentState, FGameplayTag DesiredState) override;
	virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
	virtual void CheckDefaultInitialization() override;
	void RefreshAbilitySystem();
	void NotifyPawnPossessed();
	void UninitializeAbilitySystem(bool bSuperseded = false);
	UMiniAbilitySystemComponent* GetMiniAbilitySystemComponent() const { return AbilitySystemComponent; }

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void InitializeAbilitySystem(UMiniAbilitySystemComponent* ASC);

	UPROPERTY(Transient)
	TObjectPtr<UMiniAbilitySystemComponent> AbilitySystemComponent;
	bool bWasBoundWithController = false;
	bool bAvatarSuperseded = false;
};
