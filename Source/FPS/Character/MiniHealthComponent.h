#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "Arena/MiniMatchTypes.h"
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
	/** Copies only this synchronous GE conversion's source; never retains callback pointers. */
	void BeginDamageContext(const FGameplayEffectContextHandle& Context);
	void EndDamageContext();
	void TryApplySpawnProtectionForCurrentLife();
	void RemoveSpawnProtectionForCurrentLife() { RemoveSpawnProtectionEffect(); }

private:
	UFUNCTION()
	void OnRep_DeathStarted();
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	FMiniPlayerDeathInfo ResolveDamageContext(const FGameplayEffectContextHandle& Context) const;
	void StartDeath(const FMiniPlayerDeathInfo* DeathInfo = nullptr);
	void ApplyDeathPresentation();
	void RemoveSpawnProtectionEffect();

	UPROPERTY(ReplicatedUsing = OnRep_DeathStarted, VisibleInstanceOnly, Category = "Mini|Health")
	bool bDeathStarted = false;

	TWeakObjectPtr<UMiniAbilitySystemComponent> BoundAbilitySystem;
	FDelegateHandle HealthChangedHandle;
	FActiveGameplayEffectHandle DeathEffectHandle;
	FActiveGameplayEffectHandle SpawnProtectionEffectHandle;
	uint32 SpawnProtectionGrantedLifeId = 0;
	TArray<FMiniPlayerDeathInfo> SynchronousDamageContexts;
};
