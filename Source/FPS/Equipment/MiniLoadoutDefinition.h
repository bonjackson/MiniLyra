#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "MiniLoadoutDefinition.generated.h"

class UMiniInventoryItemDefinition;

/** One item instance per configured quick-bar slot, created by the server. */
USTRUCT(BlueprintType)
struct FPS_API FMiniLoadoutEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Loadout", meta = (ClampMin = "0", ClampMax = "1"))
	int32 SlotIndex = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Loadout")
	TSubclassOf<UMiniInventoryItemDefinition> ItemDefinition;
};

/** Shared immutable starting equipment; referenced by PawnData, not selected by map name. */
UCLASS(BlueprintType, NotBlueprintable, Const)
class FPS_API UMiniLoadoutDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// Two slots are a deliberate first-version product constraint.
	static constexpr int32 NumQuickBarSlots = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Loadout")
	TArray<FMiniLoadoutEntry> Items;

	/** Empty Items + INDEX_NONE explicitly creates an unarmed Pawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Loadout", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 InitiallySelectedSlot = INDEX_NONE;

	bool ValidateLoadout(FString& OutError) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
