#include "MiniSpawnProtectionGameplayEffect.h"

#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "System/MiniGameplayTags.h"

UMiniSpawnProtectionGameplayEffect::UMiniSpawnProtectionGameplayEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(2.0f));
	FInheritedTagContainer GrantedTags;
	GrantedTags.Added.AddTag(MiniGameplayTags::State_SpawnProtected);
	UTargetTagsGameplayEffectComponent* TargetTags =
		CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("TargetTags"));
	GEComponents.Add(TargetTags);
	TargetTags->SetAndApplyTargetTagChanges(GrantedTags);
}
