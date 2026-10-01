#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask20TrainingAssetLibrary.generated.h"

class UBlueprint;
class UMaterial;
class UMiniPracticeTargetDefinition;

/** Editor authoring bridge and read-only checks for the saved training target assets. */
UCLASS()
class FPS_API UMiniTask20TrainingAssetLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool EnsureTargetBlueprint(UBlueprint* TargetBlueprint, UMiniPracticeTargetDefinition* Definition);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyTargetBlueprint(const UBlueprint* TargetBlueprint);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyTargetDefinition(const UMiniPracticeTargetDefinition* Definition);

	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyTargetMaterial(const UMaterial* Material);

	/** Read the merged DeveloperSettings directly; its scan array is not exposed to Python. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifyCookScan();

	/** Read a native CDO soft property by its native PascalCase name without loading or modifying it. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Editor", meta = (DevelopmentOnly))
	static bool VerifySoftDefault(const UObject* CDO, FName PropertyName, const FString& ExpectedObjectPath);
};
