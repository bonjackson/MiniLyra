#include "MiniEquipmentDefinition.h"

#include "AbilitySystem/MiniAbilitySet.h"
#include "AbilitySystem/MiniGameplayAbility_FromEquipment.h"
#include "Engine/SkeletalMesh.h"

USkeletalMesh* UMiniEquipmentDefinition::GetWeaponMesh() const
{
	return WeaponMesh.LoadSynchronous();
}

UMiniRifleEquipmentDefinition::UMiniRifleEquipmentDefinition()
{
	WeaponMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Rifle/Mesh/SK_Rifle.SK_Rifle")));
	AbilitySet = CreateDefaultSubobject<UMiniAbilitySet>(TEXT("RifleEquipmentAbilities"));
	FMiniAbilitySetAbility& Ability = AbilitySet->Abilities.AddDefaulted_GetRef();
	Ability.Ability = UMiniGameplayAbility_EquipmentProbe::StaticClass();
}

UMiniPistolEquipmentDefinition::UMiniPistolEquipmentDefinition()
{
	WeaponMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Pistol/Mesh/SK_Pistol.SK_Pistol")));
	AbilitySet = CreateDefaultSubobject<UMiniAbilitySet>(TEXT("PistolEquipmentAbilities"));
	FMiniAbilitySetAbility& Ability = AbilitySet->Abilities.AddDefaulted_GetRef();
	Ability.Ability = UMiniGameplayAbility_EquipmentProbe::StaticClass();
}
