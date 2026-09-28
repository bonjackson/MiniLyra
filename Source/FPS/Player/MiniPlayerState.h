#pragma once

#include "AbilitySystemInterface.h"
#include "AbilitySystem/MiniAbilitySet.h"
#include "ModularPlayerState.h"
#include "MiniPlayerState.generated.h"

class UMiniPawnData;
class UMiniAbilitySystemComponent;
class UMiniHealthSet;

/** Replicated ability owner; its ASC survives replacement of the Pawn avatar. */
UCLASS()
class FPS_API AMiniPlayerState : public AModularPlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMiniPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	static const FName NAME_AbilityActorReady;

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UMiniAbilitySystemComponent* GetMiniAbilitySystemComponent() const { return AbilitySystemComponent; }
	UMiniHealthSet* GetHealthSet() const { return HealthSet; }

	/** Authority only. Repeating the same assignment is safe; replacing it is not. */
	bool SetPawnData(const UMiniPawnData* InPawnData);
	const UMiniPawnData* GetPawnData() const { return PawnData; }

private:
	UFUNCTION()
	void OnRep_PawnData();
	void NotifyPawnDataChanged();

	UPROPERTY(ReplicatedUsing = OnRep_PawnData, VisibleInstanceOnly, Category = "Mini|Pawn")
	TObjectPtr<const UMiniPawnData> PawnData;

	UPROPERTY(VisibleAnywhere, Category = "Mini|Abilities")
	TObjectPtr<UMiniAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, Category = "Mini|Abilities")
	TObjectPtr<UMiniHealthSet> HealthSet;

	UPROPERTY(Transient)
	TArray<FMiniAbilitySetGrantedHandles> PawnDataGrantedHandles;
};
