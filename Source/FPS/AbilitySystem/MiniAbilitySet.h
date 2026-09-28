#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "AttributeSet.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "MiniAbilitySet.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct FMiniAbilitySetAbility
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> Ability;

	UPROPERTY(EditDefaultsOnly, Category = "Ability", meta = (ClampMin = "1"))
	int32 Level = 1;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	FGameplayTag InputTag;
};

USTRUCT(BlueprintType)
struct FMiniAbilitySetEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Effect")
	TSubclassOf<UGameplayEffect> Effect;

	UPROPERTY(EditDefaultsOnly, Category = "Effect", meta = (ClampMin = "0.01"))
	float Level = 1.0f;
};

USTRUCT(BlueprintType)
struct FMiniAbilitySetAttribute
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Attribute")
	TSubclassOf<UAttributeSet> AttributeSet;
};

/** Exact grants from one source; revocation never touches another source's handles. */
USTRUCT()
struct FPS_API FMiniAbilitySetGrantedHandles
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<FGameplayAbilitySpecHandle> AbilityHandles;

	UPROPERTY(Transient)
	TArray<FActiveGameplayEffectHandle> EffectHandles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAttributeSet>> AttributeSets;

	int32 GetAbilityCount() const { return AbilityHandles.Num(); }
	int32 GetEffectCount() const { return EffectHandles.Num(); }
	int32 GetAttributeCount() const { return AttributeSets.Num(); }
	bool IsEmpty() const { return AbilityHandles.IsEmpty() && EffectHandles.IsEmpty() && AttributeSets.IsEmpty(); }
	void TakeFromAbilitySystem(UAbilitySystemComponent* ASC);
};

/** Data-driven abilities, active effects and attribute sets granted as one unit. */
UCLASS(BlueprintType, Const)
class FPS_API UMiniAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Abilities")
	TArray<FMiniAbilitySetAbility> Abilities;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Effects")
	TArray<FMiniAbilitySetEffect> Effects;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Attributes")
	TArray<FMiniAbilitySetAttribute> Attributes;

	bool GiveToAbilitySystem(UAbilitySystemComponent* ASC, FMiniAbilitySetGrantedHandles& OutHandles,
		UObject* SourceObject = nullptr) const;
};
