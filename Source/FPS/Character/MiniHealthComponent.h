#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "MiniHealthComponent.generated.h"

class UMiniAbilitySystemComponent;

/** Pawn-scoped death lifecycle over the PlayerState-owned, persistent HealthSet. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMiniHealthComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeWithAbilitySystem(UMiniAbilitySystemComponent* ASC);
	void UninitializeAbilitySystem();
	bool IsDead() const { return bDeathStarted; }
	/** Authority-only: remove only this life's death effect before a replacement Pawn is bound. */
	void RemoveDeathEffect();

private:
	UFUNCTION()
	void OnRep_DeathStarted();
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void StartDeath();
	void ApplyDeathPresentation();

	UPROPERTY(ReplicatedUsing = OnRep_DeathStarted, VisibleInstanceOnly, Category = "Mini|Health")
	bool bDeathStarted = false;

	TWeakObjectPtr<UMiniAbilitySystemComponent> BoundAbilitySystem;
	FDelegateHandle HealthChangedHandle;
	FActiveGameplayEffectHandle DeathEffectHandle;
};
