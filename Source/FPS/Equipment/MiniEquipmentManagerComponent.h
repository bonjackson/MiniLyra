#pragma once

#include "Components/ActorComponent.h"
#include "Templates/SubclassOf.h"
#include "MiniEquipmentManagerComponent.generated.h"

class UMiniEquipmentDefinition;
class UMiniEquipmentInstance;
class UMiniInventoryItemInstance;
class UActorChannel;
class FOutBunch;
struct FReplicationFlags;

/** One active, server-authoritative equipment instance on a MiniCharacter. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniEquipmentManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMiniEquipmentManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch,
		FReplicationFlags* RepFlags) override;

	/** Authority only. Accepts only an item in this pawn controller's inventory. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mini|Equipment")
	bool EquipItem(UMiniInventoryItemInstance* Item);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mini|Equipment")
	bool UnequipItem();

	UFUNCTION(BlueprintPure, Category = "Mini|Equipment")
	UMiniEquipmentInstance* GetCurrentEquipment() const { return CurrentEquipment; }

	UFUNCTION(BlueprintPure, Category = "Mini|Equipment")
	TSubclassOf<UMiniEquipmentDefinition> GetCurrentDefinitionClass() const;

	UFUNCTION(BlueprintPure, Category = "Mini|Equipment")
	FGuid GetCurrentItemId() const;

private:
	UFUNCTION()
	void OnRep_CurrentEquipment();

	UPROPERTY(ReplicatedUsing = OnRep_CurrentEquipment)
	TObjectPtr<UMiniEquipmentInstance> CurrentEquipment;
};
