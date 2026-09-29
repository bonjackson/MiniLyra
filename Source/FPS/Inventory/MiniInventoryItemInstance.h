#pragma once

#include "Inventory/MiniInventoryItemDefinition.h"

#include "MiniInventoryItemInstance.generated.h"

/** Server-created item object owned and replicated by the inventory's actor. */
UCLASS(BlueprintType)
class FPS_API UMiniInventoryItemInstance : public UObject
{
	GENERATED_BODY()

public:
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Create once, with an authoritative Actor as Outer, before adding to the FastArray. */
	bool InitializeItem(TSubclassOf<UMiniInventoryItemDefinition> InDefinition);

	UFUNCTION(BlueprintPure, Category = "Mini|Inventory")
	TSubclassOf<UMiniInventoryItemDefinition> GetItemDefinition() const { return ItemDefinition; }

	UFUNCTION(BlueprintPure, Category = "Mini|Inventory")
	FGuid GetInstanceId() const { return InstanceId; }

	UFUNCTION(BlueprintPure, Category = "Mini|Inventory")
	int32 GetStat(FGameplayTag Tag) const;

	UFUNCTION(BlueprintPure, Category = "Mini|Inventory")
	TArray<FMiniInventoryStat> GetStats() const { return Stats; }

	/** Only the server may mutate an item instance. Zero is a valid value. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Mini|Inventory")
	bool SetStat(FGameplayTag Tag, int32 Count);

	const UMiniInventoryItemFragment* FindFragmentByClass(TSubclassOf<UMiniInventoryItemFragment> FragmentClass) const;

	template <typename TFragment>
	const TFragment* FindFragment() const
	{
		return Cast<TFragment>(FindFragmentByClass(TFragment::StaticClass()));
	}

private:
	UPROPERTY(Replicated)
	TSubclassOf<UMiniInventoryItemDefinition> ItemDefinition;

	UPROPERTY(Replicated)
	FGuid InstanceId;

	UPROPERTY(Replicated)
	TArray<FMiniInventoryStat> Stats;
};
