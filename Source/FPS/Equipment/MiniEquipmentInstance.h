#pragma once

#include "AbilitySystem/MiniAbilitySet.h"
#include "MiniEquipmentInstance.generated.h"

class UMiniAbilitySystemComponent;
class UMiniEquipmentDefinition;
class UMiniInventoryItemInstance;
class AMiniCharacter;

/** Public representation of a weapon; its private inventory source stays on the owning controller. */
UCLASS(BlueprintType)
class FPS_API UMiniEquipmentInstance : public UObject
{
	GENERATED_BODY()

public:
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority only; the Outer must be the owning MiniCharacter. */
	bool InitializeEquipment(UMiniInventoryItemInstance* InItem,
		TSubclassOf<UMiniEquipmentDefinition> InDefinition, UMiniAbilitySystemComponent* ASC);
	void RevokeAbilities();

	UFUNCTION(BlueprintPure, Category = "Mini|Equipment")
	TSubclassOf<UMiniEquipmentDefinition> GetEquipmentDefinition() const { return EquipmentDefinition; }

	UFUNCTION(BlueprintPure, Category = "Mini|Equipment")
	FGuid GetSourceItemId() const { return SourceItemId; }

	UFUNCTION(BlueprintPure, Category = "Mini|Equipment")
	AMiniCharacter* GetOwningCharacter() const;

	/** Server cache or owning-client inventory lookup; simulated proxies cannot see private items. */
	UMiniInventoryItemInstance* GetSourceItem() const;
	const FMiniAbilitySetGrantedHandles& GetGrantedHandles() const { return GrantedHandles; }

private:
	UFUNCTION()
	void OnRep_EquipmentDefinition();

	UPROPERTY(ReplicatedUsing = OnRep_EquipmentDefinition)
	TSubclassOf<UMiniEquipmentDefinition> EquipmentDefinition;

	UPROPERTY(Replicated)
	FGuid SourceItemId;

	UPROPERTY(Transient)
	TWeakObjectPtr<UMiniInventoryItemInstance> SourceItem;

	/** Pawn possession may change before revocation, so remember the granting ASC. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UMiniAbilitySystemComponent> GrantedAbilitySystem;

	UPROPERTY(Transient)
	FMiniAbilitySetGrantedHandles GrantedHandles;
};
