#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask20LoadoutAssetLibrary.generated.h"

class UMiniLoadoutDefinition;
class UMiniPawnData;

/** Narrow editor bridge for task 20 loadout defaults; never changes other PawnData fields. */
UCLASS()
class FPS_API UMiniTask20LoadoutAssetLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigureSharedLoadout(UMiniLoadoutDefinition* Loadout);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigureRifleOnlyLoadout(UMiniLoadoutDefinition* Loadout);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigureUnarmedLoadout(UMiniLoadoutDefinition* Loadout);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifySharedLoadout(const UMiniLoadoutDefinition* Loadout);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyRifleOnlyLoadout(const UMiniLoadoutDefinition* Loadout);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyUnarmedLoadout(const UMiniLoadoutDefinition* Loadout);

	/** Only the Practice/Diagnostics PawnData assets and three owned loadouts are supported. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigurePawnDataLoadout(UMiniPawnData* PawnData, UMiniLoadoutDefinition* Loadout);
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyPawnDataLoadout(const UMiniPawnData* PawnData, const UMiniLoadoutDefinition* Loadout);

	/** Uses transient objects only: 0/1/2 items and malformed slot/class/selection boundaries. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyValidationCases();
};
