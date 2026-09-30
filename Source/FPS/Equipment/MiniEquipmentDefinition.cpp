#include "MiniEquipmentDefinition.h"

#include "AbilitySystem/MiniAbilitySet.h"
#include "AbilitySystem/MiniGameplayAbility_RangedFire.h"
#include "AbilitySystem/MiniGameplayAbility_Reload.h"
#include "AbilitySystem/MiniGameplayAbility_RifleFire.h"
#include "Engine/SkeletalMesh.h"
#include "System/MiniGameplayTags.h"

USkeletalMesh* UMiniEquipmentDefinition::GetWeaponMesh() const
{
	return WeaponMesh.LoadSynchronous();
}

UMiniRifleEquipmentDefinition::UMiniRifleEquipmentDefinition()
{
	FireDamage = 25.0f;
	FireRange = 10000.0f;
	FireInterval = 0.12f;
	MagazineCapacity = 30;
	ReloadDuration = 1.8f;
	WeaponMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Rifle/Mesh/SK_Rifle.SK_Rifle")));
	AbilitySet = CreateDefaultSubobject<UMiniAbilitySet>(TEXT("RifleEquipmentAbilities"));
	FMiniAbilitySetAbility& Fire = AbilitySet->Abilities.AddDefaulted_GetRef();
	Fire.Ability = UMiniGameplayAbility_RifleFire::StaticClass();
	Fire.InputTag = MiniGameplayTags::InputTag_Fire;
	FMiniAbilitySetAbility& Reload = AbilitySet->Abilities.AddDefaulted_GetRef();
	Reload.Ability = UMiniGameplayAbility_Reload::StaticClass();
	Reload.InputTag = MiniGameplayTags::InputTag_Reload;
}

UMiniPistolEquipmentDefinition::UMiniPistolEquipmentDefinition()
{
	FireDamage = 20.0f;
	FireRange = 7500.0f;
	FireInterval = 0.25f;
	MagazineCapacity = 12;
	ReloadDuration = 1.45f;
	WeaponMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Pistol/Mesh/SK_Pistol.SK_Pistol")));
	AbilitySet = CreateDefaultSubobject<UMiniAbilitySet>(TEXT("PistolEquipmentAbilities"));
	FMiniAbilitySetAbility& Fire = AbilitySet->Abilities.AddDefaulted_GetRef();
	Fire.Ability = UMiniGameplayAbility_RangedFire::StaticClass();
	Fire.InputTag = MiniGameplayTags::InputTag_Fire;
	FMiniAbilitySetAbility& Reload = AbilitySet->Abilities.AddDefaulted_GetRef();
	Reload.Ability = UMiniGameplayAbility_Reload::StaticClass();
	Reload.InputTag = MiniGameplayTags::InputTag_Reload;
}
