#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "MiniEquipmentDefinition.generated.h"

class UMiniAbilitySet;
class USkeletalMesh;

/** Immutable equipment data; an item fragment selects one of these classes. */
UCLASS(BlueprintType, Blueprintable, Abstract, Const)
class FPS_API UMiniEquipmentDefinition : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Equipment")
	USkeletalMesh* GetWeaponMesh() const;

	const UMiniAbilitySet* GetAbilitySet() const { return AbilitySet; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Equipment")
	TSoftObjectPtr<USkeletalMesh> WeaponMesh;

	/** Granted only while an instance of this equipment is active. */
	UPROPERTY(VisibleDefaultsOnly, Category = "Mini|Equipment")
	TObjectPtr<UMiniAbilitySet> AbilitySet;
};

UCLASS(BlueprintType, Blueprintable)
class FPS_API UMiniRifleEquipmentDefinition : public UMiniEquipmentDefinition
{
	GENERATED_BODY()

public:
	UMiniRifleEquipmentDefinition();
	float GetFireDamage() const { return FireDamage; }
	float GetFireRange() const { return FireRange; }
	float GetFireInterval() const { return FireInterval; }

private:
	/** Server-owned combat tuning; clients never choose these values per request. */
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Fire", meta = (ClampMin = "0.0"))
	float FireDamage = 25.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Fire", meta = (ClampMin = "0.0"))
	float FireRange = 10000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Fire", meta = (ClampMin = "0.0"))
	float FireInterval = 0.12f;
};

UCLASS(BlueprintType, Blueprintable)
class FPS_API UMiniPistolEquipmentDefinition : public UMiniEquipmentDefinition
{
	GENERATED_BODY()

public:
	UMiniPistolEquipmentDefinition();
};
