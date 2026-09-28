#include "MiniAbilityTagRelationshipMapping.h"

void UMiniAbilityTagRelationshipMapping::GetActivationTagRequirements(
	const FGameplayTagContainer& AbilityTags, FGameplayTagContainer& OutRequired,
	FGameplayTagContainer& OutBlocked) const
{
	for (const FMiniAbilityTagRelationship& Entry : Relationships)
	{
		if (Entry.Tag.IsValid() && AbilityTags.HasTagExact(Entry.Tag))
		{
			OutRequired.AppendTags(Entry.ActivationRequiredTags);
			OutBlocked.AppendTags(Entry.ActivationBlockedTags);
		}
	}
}

void UMiniAbilityTagRelationshipMapping::GetCancelAbilityTags(
	FGameplayTag AddedStatusTag, FGameplayTagContainer& OutCancelTags) const
{
	for (const FMiniAbilityTagRelationship& Entry : Relationships)
	{
		if (Entry.Tag == AddedStatusTag)
		{
			OutCancelTags.AppendTags(Entry.CancelAbilitiesWithTags);
		}
	}
}
