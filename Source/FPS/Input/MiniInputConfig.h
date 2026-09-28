#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MiniInputConfig.generated.h"

class UInputAction;
class FDataValidationContext;

/** Associates one Enhanced Input action with a gameplay input tag. */
USTRUCT(BlueprintType)
struct FMiniInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

/** Native movement/look bindings and tag-driven gameplay ability bindings. */
UCLASS(BlueprintType, Const)
class FPS_API UMiniInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (TitleProperty = "InputAction"))
	TArray<FMiniInputAction> NativeInputActions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (TitleProperty = "InputAction"))
	TArray<FMiniInputAction> AbilityInputActions;

	const UInputAction* FindNativeInputActionForTag(FGameplayTag InputTag, bool bLogNotFound = false) const;
	const UInputAction* FindAbilityInputActionForTag(FGameplayTag InputTag, bool bLogNotFound = false) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
