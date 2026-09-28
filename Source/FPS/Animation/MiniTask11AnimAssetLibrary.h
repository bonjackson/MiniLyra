#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask11AnimAssetLibrary.generated.h"

class UAnimBlueprint;
class UAnimSequence;

/** Editor bridge for a reproducible five-pose Manny AnimGraph. */
UCLASS()
class FPS_API UMiniTask11AnimAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Task11")
	static bool ConfigurePracticeAnim(UAnimBlueprint* AnimBlueprint,
		UAnimSequence* RifleIdle, UAnimSequence* RifleJog,
		UAnimSequence* Jump, UAnimSequence* Fall, UAnimSequence* Land);

	UFUNCTION(BlueprintCallable, Category = "Mini|Task11")
	static bool VerifyPracticeAnim(const UAnimBlueprint* AnimBlueprint,
		const UAnimSequence* RifleIdle, const UAnimSequence* RifleJog,
		const UAnimSequence* Jump, const UAnimSequence* Fall, const UAnimSequence* Land);
};
