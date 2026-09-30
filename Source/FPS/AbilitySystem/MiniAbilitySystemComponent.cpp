#include "MiniAbilitySystemComponent.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/MiniAbilityTagRelationshipMapping.h"
#include "AbilitySystem/MiniGameplayAbility.h"
#include "System/MiniGameplayTags.h"

bool UMiniAbilitySystemComponent::FindActiveGameplayCueParameters(
	FGameplayTag CueTag, FGameplayCueParameters& OutParameters) const
{
	for (const FActiveGameplayCue& Cue : ActiveGameplayCues.GameplayCues)
	{
		if (Cue.GameplayCueTag == CueTag && !Cue.bPredictivelyRemoved)
		{
			OutParameters = Cue.Parameters;
			return true;
		}
	}
	return false;
}

UMiniAbilitySystemComponent::UMiniAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

bool UMiniAbilitySystemComponent::IsAbilityInputBlocked() const
{
	return HasMatchingGameplayTag(MiniGameplayTags::Gameplay_AbilityInputBlocked);
}

void UMiniAbilitySystemComponent::SetTagRelationshipMapping(const UMiniAbilityTagRelationshipMapping* Mapping)
{
	TagRelationshipMapping = Mapping;
}

bool UMiniAbilitySystemComponent::AreAbilityTagRequirementsMet(
	const FGameplayTagContainer& AbilityTags, FGameplayTagContainer* OutFailureTags) const
{
	if (!TagRelationshipMapping)
	{
		return true;
	}
	FGameplayTagContainer Required;
	FGameplayTagContainer Blocked;
	TagRelationshipMapping->GetActivationTagRequirements(AbilityTags, Required, Blocked);
	const bool bHasRequired = HasAllMatchingGameplayTags(Required);
	const bool bHasBlocked = HasAnyMatchingGameplayTags(Blocked);
	if (OutFailureTags)
	{
		if (!bHasRequired)
		{
			OutFailureTags->AppendTags(Required);
		}
		if (bHasBlocked)
		{
			OutFailureTags->AppendTags(Blocked);
		}
	}
	return bHasRequired && !bHasBlocked;
}

void UMiniAbilitySystemComponent::OnTagUpdated(const FGameplayTag& Tag, bool TagExists)
{
	Super::OnTagUpdated(Tag, TagExists);
	if (!TagExists)
	{
		return;
	}
	if (TagRelationshipMapping)
	{
		FGameplayTagContainer CancelTags;
		TagRelationshipMapping->GetCancelAbilityTags(Tag, CancelTags);
		if (!CancelTags.IsEmpty())
		{
			CancelAbilities(&CancelTags);
		}
	}
	if (Tag == MiniGameplayTags::Gameplay_AbilityInputBlocked)
	{
		ClearAbilityInput();
	}
}

void UMiniAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid() || IsAbilityInputBlocked())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag) &&
			!InputHeldSpecHandles.Contains(Spec.Handle))
		{
			InputPressedSpecHandles.AddUnique(Spec.Handle);
			InputHeldSpecHandles.Add(Spec.Handle);
		}
	}
}

void UMiniAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	for (const FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag) &&
			InputHeldSpecHandles.Remove(Spec.Handle) > 0)
		{
			InputReleasedSpecHandles.AddUnique(Spec.Handle);
		}
	}
}

void UMiniAbilitySystemComponent::ProcessAbilityInput(float /*DeltaTime*/, bool bGamePaused)
{
	if (bGamePaused || IsAbilityInputBlocked())
	{
		ClearAbilityInput();
		return;
	}

	TArray<FGameplayAbilitySpecHandle> ToActivate;
	for (const FGameplayAbilitySpecHandle& Handle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = true;
			if (Spec->IsActive())
			{
				AbilitySpecInputPressed(*Spec);
			}
			else if (!Spec->Ability || !Spec->Ability->IsA<UMiniGameplayAbility>() ||
				CastChecked<UMiniGameplayAbility>(Spec->Ability)->GetActivationPolicy() ==
					EMiniAbilityActivationPolicy::OnInputTriggered)
			{
				ToActivate.AddUnique(Handle);
			}
		}
	}

	// This baseline activates once on press; held state remains until release.
	for (const FGameplayAbilitySpecHandle& Handle : InputHeldSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = true;
			if (!Spec->IsActive() && Spec->Ability && Spec->Ability->IsA<UMiniGameplayAbility>() &&
				CastChecked<UMiniGameplayAbility>(Spec->Ability)->GetActivationPolicy() ==
					EMiniAbilityActivationPolicy::WhileInputActive)
			{
				ToActivate.AddUnique(Handle);
			}
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : ToActivate)
	{
		if (TryActivateAbility(Handle))
		{
			InputActivatedSpecHandles.AddUnique(Handle);
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : InputReleasedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = false;
			if (Spec->IsActive())
			{
				AbilitySpecInputReleased(*Spec);
			}
		}
	}

	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.RemoveAll([this](const FGameplayAbilitySpecHandle& Handle)
	{
		return FindAbilitySpecFromHandle(Handle) == nullptr;
	});
	InputActivatedSpecHandles.RemoveAll([this](const FGameplayAbilitySpecHandle& Handle)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		return !Spec || !Spec->IsActive();
	});
}

void UMiniAbilitySystemComponent::ClearAbilityInput()
{
	TArray<FGameplayAbilitySpecHandle> HandlesToClear = InputHeldSpecHandles;
	for (const FGameplayAbilitySpecHandle& Handle : InputReleasedSpecHandles)
	{
		HandlesToClear.AddUnique(Handle);
	}
	for (const FGameplayAbilitySpecHandle& Handle : InputPressedSpecHandles)
	{
		HandlesToClear.AddUnique(Handle);
	}
	for (const FGameplayAbilitySpecHandle& Handle : InputActivatedSpecHandles)
	{
		HandlesToClear.AddUnique(Handle);
	}
	for (const FGameplayAbilitySpecHandle& Handle : HandlesToClear)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			const bool bPendingInput = InputHeldSpecHandles.Contains(Handle) ||
				InputReleasedSpecHandles.Contains(Handle) || InputPressedSpecHandles.Contains(Handle);
			Spec->InputPressed = false;
			if (Spec->IsActive() && bPendingInput)
			{
				AbilitySpecInputReleased(*Spec);
			}
			if (Spec->IsActive() && InputActivatedSpecHandles.Contains(Handle))
			{
				// A tag can also target an ability activated by an event; do not cancel that instance.
				CancelAbilityHandle(Handle);
			}
		}
	}
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
	InputActivatedSpecHandles.Reset();
}

void UMiniAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputPressed(Spec);
	if (Spec.IsActive())
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		const FPredictionKey PredictionKey = Instance
			? Instance->GetCurrentActivationInfo().GetActivationPredictionKey()
			: Spec.ActivationInfo.GetActivationPredictionKey();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, PredictionKey);
	}
}

void UMiniAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	Super::AbilitySpecInputReleased(Spec);
	if (Spec.IsActive())
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
		const FPredictionKey PredictionKey = Instance
			? Instance->GetCurrentActivationInfo().GetActivationPredictionKey()
			: Spec.ActivationInfo.GetActivationPredictionKey();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Spec.Handle, PredictionKey);
	}
}
