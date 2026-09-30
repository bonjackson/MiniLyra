#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "MiniEquipmentDefinition.generated.h"

class UMiniAbilitySet;
class USkeletalMesh;
class UAnimInstance;

/** Immutable equipment data; an item fragment selects one of these classes. */
UCLASS(BlueprintType, Blueprintable, Abstract, Const)
class FPS_API UMiniEquipmentDefinition : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Mini|Equipment")
	USkeletalMesh* GetWeaponMesh() const;
	UClass* GetWeaponAnimClass() const;

	const UMiniAbilitySet* GetAbilitySet() const { return AbilitySet; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Equipment")
	TSoftObjectPtr<USkeletalMesh> WeaponMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Equipment")
	TSoftClassPtr<UAnimInstance> WeaponAnimClass;

	/** Granted only while an instance of this equipment is active. */
	UPROPERTY(VisibleDefaultsOnly, Category = "Mini|Equipment")
	TObjectPtr<UMiniAbilitySet> AbilitySet;
};

/** Shared authoritative tuning for a hitscan weapon. The inventory item owns mutable ammo. */
UCLASS(BlueprintType, Blueprintable, Abstract)
class FPS_API UMiniRangedWeaponEquipmentDefinition : public UMiniEquipmentDefinition
{
	GENERATED_BODY()

public:
	float GetFireDamage() const { return FireDamage; }
	float GetFireRange() const { return FireRange; }
	float GetFireInterval() const { return FireInterval; }
	int32 GetMagazineCapacity() const { return MagazineCapacity; }
	float GetReloadDuration() const { return ReloadDuration; }

protected:
	/** Server-owned combat tuning; clients never choose these values per request. */
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Fire", meta = (ClampMin = "0.0"))
	float FireDamage = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Fire", meta = (ClampMin = "0.0"))
	float FireRange = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Fire", meta = (ClampMin = "0.0"))
	float FireInterval = 0.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Ammo", meta = (ClampMin = "1"))
	int32 MagazineCapacity = 1;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Equipment|Ammo", meta = (ClampMin = "0.01"))
	float ReloadDuration = 1.0f;
};

UCLASS(BlueprintType, Blueprintable)
class FPS_API UMiniRifleEquipmentDefinition : public UMiniRangedWeaponEquipmentDefinition
{
	GENERATED_BODY()

public:
	UMiniRifleEquipmentDefinition();
};

UCLASS(BlueprintType, Blueprintable)
class FPS_API UMiniPistolEquipmentDefinition : public UMiniRangedWeaponEquipmentDefinition
{
	GENERATED_BODY()

public:
	UMiniPistolEquipmentDefinition();
};
