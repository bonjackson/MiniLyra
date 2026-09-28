#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask12AssetSetupLibrary.generated.h"

class UMiniAbilitySet;
class UMiniAbilityTagRelationshipMapping;
class UMiniPawnData;

/** Narrow editor bridge for repeatable Task 12 ability asset authoring. */
UCLASS()
class FPS_API UMiniTask12AssetSetupLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool ConfigurePracticeAbilities(UMiniAbilitySet* PawnSet,
		UMiniAbilityTagRelationshipMapping* Relationships, UMiniPawnData* PawnData);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyPracticeAbilities(const UMiniAbilitySet* PawnSet,
		const UMiniAbilityTagRelationshipMapping* Relationships, const UMiniPawnData* PawnData);
};
