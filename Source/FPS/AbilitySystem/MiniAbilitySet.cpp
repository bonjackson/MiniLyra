#include "MiniAbilitySet.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "System/MiniLogChannels.h"

void FMiniAbilitySetGrantedHandles::TakeFromAbilitySystem(UAbilitySystemComponent* ASC)
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		return;
	}
	for (const FGameplayAbilitySpecHandle& Handle : AbilityHandles)
	{
		if (Handle.IsValid())
		{
			ASC->ClearAbility(Handle);
		}
	}
	for (const FActiveGameplayEffectHandle& Handle : EffectHandles)
	{
		if (Handle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(Handle);
		}
	}
	for (UAttributeSet* Set : AttributeSets)
	{
		if (Set)
		{
			ASC->RemoveSpawnedAttribute(Set);
		}
	}
	AbilityHandles.Reset();
	EffectHandles.Reset();
	AttributeSets.Reset();
}

bool UMiniAbilitySet::GiveToAbilitySystem(UAbilitySystemComponent* ASC,
	FMiniAbilitySetGrantedHandles& OutHandles, UObject* SourceObject) const
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative() || !OutHandles.IsEmpty())
	{
		return false;
	}
	for (const FMiniAbilitySetAttribute& Entry : Attributes)
	{
		if (!Entry.AttributeSet)
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniAbilitySet invalid AttributeSet in %s"), *GetPathName());
			continue;
		}
		UAttributeSet* Set = NewObject<UAttributeSet>(ASC->GetOwner(), Entry.AttributeSet);
		ASC->AddAttributeSetSubobject(Set);
		OutHandles.AttributeSets.Add(Set);
	}
	for (const FMiniAbilitySetAbility& Entry : Abilities)
	{
		if (!Entry.Ability || Entry.Level < 1)
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniAbilitySet invalid Ability in %s"), *GetPathName());
			continue;
		}
		FGameplayAbilitySpec Spec(Entry.Ability->GetDefaultObject<UGameplayAbility>(), Entry.Level);
		Spec.SourceObject = SourceObject;
		if (Entry.InputTag.IsValid())
		{
			Spec.GetDynamicSpecSourceTags().AddTag(Entry.InputTag);
		}
		const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
		if (Handle.IsValid())
		{
			OutHandles.AbilityHandles.Add(Handle);
		}
	}
	for (const FMiniAbilitySetEffect& Entry : Effects)
	{
		if (!Entry.Effect || Entry.Level <= 0.0f)
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniAbilitySet invalid Effect in %s"), *GetPathName());
			continue;
		}
		const UGameplayEffect* Effect = Entry.Effect->GetDefaultObject<UGameplayEffect>();
		const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectToSelf(
			Effect, Entry.Level, ASC->MakeEffectContext());
		if (Handle.IsValid())
		{
			OutHandles.EffectHandles.Add(Handle);
		}
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniAbilitySet GRANTED: Source=%s Set=%s Abilities=%d Effects=%d Attributes=%d"),
		*GetNameSafe(SourceObject), *GetPathName(), OutHandles.GetAbilityCount(), OutHandles.GetEffectCount(),
		OutHandles.GetAttributeCount());
	return true;
}
