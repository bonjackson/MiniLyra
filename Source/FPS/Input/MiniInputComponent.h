#pragma once

#include "EnhancedInputComponent.h"
#include "Input/MiniInputConfig.h"
#include "MiniInputComponent.generated.h"

/** Binds data-defined actions and keeps their Enhanced Input handles removable. */
UCLASS(Config = Input)
class FPS_API UMiniInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:
	template<class UserClass, typename FuncType>
	bool BindNativeAction(const UMiniInputConfig* Config, FGameplayTag InputTag,
		ETriggerEvent TriggerEvent, UserClass* Object, FuncType Function,
		TArray<uint32>& OutHandles, bool bLogIfNotFound = false);

	template<class UserClass, typename PressedFuncType, typename ReleasedFuncType>
	int32 BindAbilityActions(const UMiniInputConfig* Config, UserClass* Object,
		PressedFuncType PressedFunction, ReleasedFuncType ReleasedFunction,
		TArray<uint32>& OutHandles);

	void RemoveBinds(TArray<uint32>& Handles);
};

template<class UserClass, typename FuncType>
bool UMiniInputComponent::BindNativeAction(const UMiniInputConfig* Config, FGameplayTag InputTag,
	ETriggerEvent TriggerEvent, UserClass* Object, FuncType Function,
	TArray<uint32>& OutHandles, bool bLogIfNotFound)
{
	if (!Config || !Object)
	{
		return false;
	}
	if (const UInputAction* Action = Config->FindNativeInputActionForTag(InputTag, bLogIfNotFound))
	{
		OutHandles.Add(BindAction(Action, TriggerEvent, Object, Function).GetHandle());
		return true;
	}
	return false;
}

template<class UserClass, typename PressedFuncType, typename ReleasedFuncType>
int32 UMiniInputComponent::BindAbilityActions(const UMiniInputConfig* Config, UserClass* Object,
	PressedFuncType PressedFunction, ReleasedFuncType ReleasedFunction,
	TArray<uint32>& OutHandles)
{
	if (!Config || !Object)
	{
		return 0;
	}
	int32 BoundActions = 0;
	for (const FMiniInputAction& Entry : Config->AbilityInputActions)
	{
		if (!Entry.InputAction || !Entry.InputTag.IsValid())
		{
			continue;
		}
		if (PressedFunction)
		{
			// Started fires once; Triggered may repeat each frame for a held key.
			OutHandles.Add(BindAction(Entry.InputAction, ETriggerEvent::Started,
				Object, PressedFunction, Entry.InputTag).GetHandle());
		}
		if (ReleasedFunction)
		{
			OutHandles.Add(BindAction(Entry.InputAction, ETriggerEvent::Completed,
				Object, ReleasedFunction, Entry.InputTag).GetHandle());
			OutHandles.Add(BindAction(Entry.InputAction, ETriggerEvent::Canceled,
				Object, ReleasedFunction, Entry.InputTag).GetHandle());
		}
		++BoundActions;
	}
	return BoundActions;
}
