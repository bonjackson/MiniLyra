#include "MiniDamageGameplayEffect.h"

#include "AbilitySystem/MiniHealthSet.h"
#include "System/MiniGameplayTags.h"

UMiniDamageGameplayEffect::UMiniDamageGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FSetByCallerFloat DamageMagnitude;
	DamageMagnitude.DataTag = MiniGameplayTags::Data_Damage;

	FGameplayModifierInfo& DamageModifier = Modifiers.AddDefaulted_GetRef();
	DamageModifier.Attribute = UMiniHealthSet::GetIncomingDamageAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::Additive;
	DamageModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(DamageMagnitude);
}
