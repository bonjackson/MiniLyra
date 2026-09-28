#include "MiniInputConfig.h"

#include "InputAction.h"
#include "System/MiniLogChannels.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
const UInputAction* FindActionForTag(const TArray<FMiniInputAction>& Actions, FGameplayTag Tag)
{
	for (const FMiniInputAction& Entry : Actions)
	{
		if (Entry.InputTag == Tag && Entry.InputAction)
		{
			return Entry.InputAction.Get();
		}
	}
	return nullptr;
}

#if WITH_EDITOR
bool ValidateActions(const TArray<FMiniInputAction>& Actions, const TCHAR* Group, FDataValidationContext& Context)
{
	bool bValid = true;
	TSet<FGameplayTag> Tags;
	TSet<const UInputAction*> InputActions;
	for (int32 Index = 0; Index < Actions.Num(); ++Index)
	{
		const FMiniInputAction& Entry = Actions[Index];
		if (!Entry.InputAction || !Entry.InputTag.IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("%s[%d] requires a valid InputAction and InputTag."), Group, Index)));
			bValid = false;
			continue;
		}
		if (Tags.Contains(Entry.InputTag))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("%s has duplicate InputTag %s."), Group, *Entry.InputTag.ToString())));
			bValid = false;
		}
		if (InputActions.Contains(Entry.InputAction.Get()))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("%s has duplicate InputAction %s."), Group, *GetNameSafe(Entry.InputAction.Get()))));
			bValid = false;
		}
		Tags.Add(Entry.InputTag);
		InputActions.Add(Entry.InputAction.Get());
	}
	return bValid;
}
#endif
}

const UInputAction* UMiniInputConfig::FindNativeInputActionForTag(FGameplayTag InputTag, bool bLogNotFound) const
{
	const UInputAction* Action = FindActionForTag(NativeInputActions, InputTag);
	if (!Action && bLogNotFound)
	{
		UE_LOG(LogMiniInit, Warning, TEXT("Native input tag %s is missing from %s"),
			*InputTag.ToString(), *GetPathName());
	}
	return Action;
}

const UInputAction* UMiniInputConfig::FindAbilityInputActionForTag(FGameplayTag InputTag, bool bLogNotFound) const
{
	const UInputAction* Action = FindActionForTag(AbilityInputActions, InputTag);
	if (!Action && bLogNotFound)
	{
		UE_LOG(LogMiniInit, Warning, TEXT("Ability input tag %s is missing from %s"),
			*InputTag.ToString(), *GetPathName());
	}
	return Action;
}

#if WITH_EDITOR
EDataValidationResult UMiniInputConfig::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	const bool bNativeValid = ValidateActions(NativeInputActions, TEXT("NativeInputActions"), Context);
	const bool bAbilityValid = ValidateActions(AbilityInputActions, TEXT("AbilityInputActions"), Context);
	return bNativeValid && bAbilityValid && ParentResult != EDataValidationResult::Invalid
		? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
