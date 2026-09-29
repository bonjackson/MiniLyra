#pragma once

#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPtr.h"

#include "MiniInventoryItemDefinition.generated.h"

class UMiniInventoryItemInstance;
class UTexture2D;

namespace MiniInventoryTags
{
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AmmoInMagazine);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ReserveAmmo);
}

/** A small, replicated unit of mutable item state. */
USTRUCT(BlueprintType)
struct FPS_API FMiniInventoryStat
{
	GENERATED_BODY()

	FMiniInventoryStat() = default;
	FMiniInventoryStat(FGameplayTag InTag, int32 InCount)
		: Tag(InTag), Count(InCount) {}

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Inventory")
	FGameplayTag Tag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Inventory", meta = (ClampMin = "0"))
	int32 Count = 0;
};

/** Immutable definition data lives on the definition class default object. */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew, Abstract)
class FPS_API UMiniInventoryItemFragment : public UObject
{
	GENERATED_BODY()

public:
	virtual void OnInstanceCreated(UMiniInventoryItemInstance* Instance) const {}
};

UCLASS(BlueprintType, Blueprintable, Abstract, Const)
class FPS_API UMiniInventoryItemDefinition : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Inventory")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Instanced, Category = "Mini|Inventory")
	TArray<TObjectPtr<UMiniInventoryItemFragment>> Fragments;

	UFUNCTION(BlueprintCallable, Category = "Mini|Inventory", meta = (DeterminesOutputType = "FragmentClass"))
	const UMiniInventoryItemFragment* FindFragmentByClass(TSubclassOf<UMiniInventoryItemFragment> FragmentClass) const;

	template <typename TFragment>
	const TFragment* FindFragment() const
	{
		return Cast<TFragment>(FindFragmentByClass(TFragment::StaticClass()));
	}
};

/** Applied once on the server when an instance is created. */
UCLASS(BlueprintType)
class FPS_API UMiniInventoryFragment_InitialStats : public UMiniInventoryItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Inventory")
	TArray<FMiniInventoryStat> InitialStats;

	virtual void OnInstanceCreated(UMiniInventoryItemInstance* Instance) const override;
};

/** The equipment class is supplied in task 15 without changing item ownership. */
UCLASS(BlueprintType)
class FPS_API UMiniInventoryFragment_Equippable : public UMiniInventoryItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Inventory")
	TSoftClassPtr<UObject> EquipmentDefinition;
};

UCLASS(BlueprintType)
class FPS_API UMiniInventoryFragment_Icon : public UMiniInventoryItemFragment
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Inventory")
	TSoftObjectPtr<UTexture2D> Icon;
};

/** Native defaults are sufficient for the first two inventory items. */
UCLASS(BlueprintType, Blueprintable)
class FPS_API UMiniRifleItemDefinition : public UMiniInventoryItemDefinition
{
	GENERATED_BODY()

public:
	UMiniRifleItemDefinition();
};

UCLASS(BlueprintType, Blueprintable)
class FPS_API UMiniPistolItemDefinition : public UMiniInventoryItemDefinition
{
	GENERATED_BODY()

public:
	UMiniPistolItemDefinition();
};
