#include "MiniTask10AssetSetupLibrary.h"

#if WITH_EDITOR
#include "AbilitySystem/MiniAbilitySet.h"
#include "AbilitySystem/MiniProbeAbility.h"
#include "Character/MiniPawnData.h"
#include "EnhancedActionKeyMapping.h"
#include "GameFeatures/MiniGameFeatureAction_AddInput.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "Input/MiniInputConfig.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "System/MiniGameplayTags.h"
#include "UObject/UnrealType.h"
#endif

#if WITH_EDITOR
namespace
{
const FName InputActionName(TEXT("MiniTask10_AddInput"));

enum class EMappingModifiers : uint8
{
	None,
	Negate,
	Swizzle,
	SwizzleAndNegate
};

void AddMapping(UInputMappingContext* Context, const UInputAction* Action, FKey Key,
	EMappingModifiers ModifierPattern = EMappingModifiers::None)
{
	FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
	if (ModifierPattern == EMappingModifiers::Swizzle || ModifierPattern == EMappingModifiers::SwizzleAndNegate)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Context, NAME_None, RF_Transactional);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
	}
	if (ModifierPattern == EMappingModifiers::Negate || ModifierPattern == EMappingModifiers::SwizzleAndNegate)
	{
		Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context, NAME_None, RF_Transactional));
	}
}

bool IsMapping(const FEnhancedActionKeyMapping& Mapping, const UInputAction* Action, FKey Key,
	EMappingModifiers ModifierPattern)
{
	if (Mapping.Action != Action || Mapping.Key != Key)
	{
		return false;
	}
	const bool bSwizzle = ModifierPattern == EMappingModifiers::Swizzle ||
		ModifierPattern == EMappingModifiers::SwizzleAndNegate;
	const bool bNegate = ModifierPattern == EMappingModifiers::Negate ||
		ModifierPattern == EMappingModifiers::SwizzleAndNegate;
	if (Mapping.Modifiers.Num() != static_cast<int32>(bSwizzle) + static_cast<int32>(bNegate))
	{
		return false;
	}
	int32 Index = 0;
	if (bSwizzle)
	{
		const UInputModifierSwizzleAxis* Swizzle = Cast<UInputModifierSwizzleAxis>(Mapping.Modifiers[Index++].Get());
		if (!Swizzle || Swizzle->Order != EInputAxisSwizzle::YXZ)
		{
			return false;
		}
	}
	if (bNegate)
	{
		const UInputModifierNegate* Negate = Cast<UInputModifierNegate>(Mapping.Modifiers[Index].Get());
		if (!Negate || !Negate->bX || !Negate->bY || !Negate->bZ)
		{
			return false;
		}
	}
	return true;
}

template<typename EntryType>
bool IsInputEntry(const EntryType& Entry, const UInputAction* Action, FGameplayTag Tag)
{
	return Entry.InputAction == Action && Entry.InputTag == Tag;
}
}
#endif

bool UMiniTask10AssetSetupLibrary::ConfigurePracticeInput(
	UMiniInputConfig* InputConfig, UInputMappingContext* MappingContext,
	UMiniAbilitySet* PawnAbilitySet,
	UInputAction* Move, UInputAction* Look, UInputAction* Jump, UInputAction* Fire,
	UInputAction* Reload, UInputAction* SwitchWeapon, UInputAction* Aim,
	UMiniPawnData* PawnData, UMiniExperienceDefinition* Experience)
{
#if WITH_EDITOR
	if (!InputConfig || !MappingContext || !PawnAbilitySet || !Move || !Look || !Jump || !Fire || !Reload || !SwitchWeapon || !Aim ||
		!PawnData || !Experience || Experience->DefaultPawnData.Get() != PawnData ||
		PawnData->AbilitySets.Num() != 1 || PawnData->AbilitySets[0] != PawnAbilitySet ||
		PawnAbilitySet->Abilities.Num() != 1 ||
		PawnAbilitySet->Abilities[0].Ability != UMiniPawnProbeAbility::StaticClass())
	{
		return false;
	}

	UMiniGameFeatureAction_AddInput* Action = nullptr;
	for (UGameFeatureAction* Candidate : Experience->Actions)
	{
		if (Candidate && Candidate->GetFName() == InputActionName)
		{
			if (Action || !Candidate->IsA<UMiniGameFeatureAction_AddInput>())
			{
				return false;
			}
			Action = CastChecked<UMiniGameFeatureAction_AddInput>(Candidate);
		}
	}

	InputConfig->Modify();
	MappingContext->Modify();
	PawnAbilitySet->Modify();
	Move->Modify();
	Look->Modify();
	Jump->Modify();
	Fire->Modify();
	Reload->Modify();
	SwitchWeapon->Modify();
	Aim->Modify();
	PawnData->Modify();
	Experience->Modify();
	if (!Action)
	{
		Action = NewObject<UMiniGameFeatureAction_AddInput>(Experience, InputActionName, RF_Transactional);
		Experience->Actions.Add(Action);
	}
	Action->Modify();

	Move->ValueType = EInputActionValueType::Axis2D;
	Move->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
	Look->ValueType = EInputActionValueType::Axis2D;
	for (UInputAction* Button : {Jump, Fire, Reload, SwitchWeapon, Aim})
	{
		Button->ValueType = EInputActionValueType::Boolean;
	}

	InputConfig->NativeInputActions.Reset();
	InputConfig->AbilityInputActions.Reset();
	auto AddNative = [InputConfig](const UInputAction* InAction, FGameplayTag Tag)
	{
		auto& Entry = InputConfig->NativeInputActions.AddDefaulted_GetRef();
		Entry.InputAction = InAction;
		Entry.InputTag = Tag;
	};
	auto AddAbility = [InputConfig](const UInputAction* InAction, FGameplayTag Tag)
	{
		auto& Entry = InputConfig->AbilityInputActions.AddDefaulted_GetRef();
		Entry.InputAction = InAction;
		Entry.InputTag = Tag;
	};
	AddNative(Move, MiniGameplayTags::InputTag_Move);
	AddNative(Look, MiniGameplayTags::InputTag_Look);
	AddNative(Jump, MiniGameplayTags::InputTag_Jump);
	AddAbility(Fire, MiniGameplayTags::InputTag_Fire);
	AddAbility(Reload, MiniGameplayTags::InputTag_Reload);
	AddAbility(SwitchWeapon, MiniGameplayTags::InputTag_SwitchWeapon);
	AddAbility(Aim, MiniGameplayTags::InputTag_Aim);
	PawnAbilitySet->Abilities[0].InputTag = MiniGameplayTags::InputTag_Fire;

	// UE 5.8 stores these in DefaultKeyMappings; MapKey avoids the deprecated Mappings property.
	MappingContext->UnmapAll();
	// Track exact Add/Remove ownership so repeated possession can be verified.
	if (FProperty* Tracking = MappingContext->GetClass()->FindPropertyByName(TEXT("RegistrationTrackingMode")))
	{
		*Tracking->ContainerPtrToValuePtr<EMappingContextRegistrationTrackingMode>(MappingContext) =
			EMappingContextRegistrationTrackingMode::CountRegistrations;
	}
	AddMapping(MappingContext, Move, EKeys::W, EMappingModifiers::Swizzle);
	AddMapping(MappingContext, Move, EKeys::A, EMappingModifiers::Negate);
	AddMapping(MappingContext, Move, EKeys::S, EMappingModifiers::SwizzleAndNegate);
	AddMapping(MappingContext, Move, EKeys::D);
	AddMapping(MappingContext, Look, EKeys::Mouse2D);
	AddMapping(MappingContext, Jump, EKeys::SpaceBar);
	AddMapping(MappingContext, Fire, EKeys::LeftMouseButton);
	AddMapping(MappingContext, Reload, EKeys::R);
	AddMapping(MappingContext, SwitchWeapon, EKeys::Q);
	AddMapping(MappingContext, Aim, EKeys::RightMouseButton);

	PawnData->InputConfig = InputConfig;
	Action->MappingContext = MappingContext;
	const TArray<UObject*> Assets = {InputConfig, MappingContext, PawnAbilitySet, Move, Look, Jump, Fire,
		Reload, SwitchWeapon, Aim, PawnData, Experience};
	for (UObject* Asset : Assets)
	{
		Asset->MarkPackageDirty();
	}
	return true;
#else
	return false;
#endif
}

bool UMiniTask10AssetSetupLibrary::VerifyPracticeInput(
	const UMiniInputConfig* InputConfig, const UInputMappingContext* MappingContext,
	const UMiniAbilitySet* PawnAbilitySet,
	const UInputAction* Move, const UInputAction* Look, const UInputAction* Jump, const UInputAction* Fire,
	const UInputAction* Reload, const UInputAction* SwitchWeapon, const UInputAction* Aim,
	const UMiniPawnData* PawnData, const UMiniExperienceDefinition* Experience)
{
#if WITH_EDITOR
	if (!InputConfig || !MappingContext || !PawnAbilitySet || !Move || !Look || !Jump || !Fire || !Reload || !SwitchWeapon || !Aim ||
		!PawnData || !Experience || Experience->DefaultPawnData.Get() != PawnData ||
		PawnData->InputConfig != InputConfig ||
		PawnData->AbilitySets.Num() != 1 || PawnData->AbilitySets[0] != PawnAbilitySet ||
		PawnAbilitySet->Abilities.Num() != 1 ||
		PawnAbilitySet->Abilities[0].Ability != UMiniPawnProbeAbility::StaticClass() ||
		PawnAbilitySet->Abilities[0].InputTag != MiniGameplayTags::InputTag_Fire ||
		Move->ValueType != EInputActionValueType::Axis2D ||
		Move->AccumulationBehavior != EInputActionAccumulationBehavior::Cumulative ||
		Look->ValueType != EInputActionValueType::Axis2D ||
		Jump->ValueType != EInputActionValueType::Boolean ||
		Fire->ValueType != EInputActionValueType::Boolean ||
		Reload->ValueType != EInputActionValueType::Boolean ||
		SwitchWeapon->ValueType != EInputActionValueType::Boolean ||
		Aim->ValueType != EInputActionValueType::Boolean ||
		InputConfig->NativeInputActions.Num() != 3 || InputConfig->AbilityInputActions.Num() != 4)
	{
		return false;
	}
	if (!IsInputEntry(InputConfig->NativeInputActions[0], Move, MiniGameplayTags::InputTag_Move) ||
		!IsInputEntry(InputConfig->NativeInputActions[1], Look, MiniGameplayTags::InputTag_Look) ||
		!IsInputEntry(InputConfig->NativeInputActions[2], Jump, MiniGameplayTags::InputTag_Jump) ||
		!IsInputEntry(InputConfig->AbilityInputActions[0], Fire, MiniGameplayTags::InputTag_Fire) ||
		!IsInputEntry(InputConfig->AbilityInputActions[1], Reload, MiniGameplayTags::InputTag_Reload) ||
		!IsInputEntry(InputConfig->AbilityInputActions[2], SwitchWeapon, MiniGameplayTags::InputTag_SwitchWeapon) ||
		!IsInputEntry(InputConfig->AbilityInputActions[3], Aim, MiniGameplayTags::InputTag_Aim))
	{
		return false;
	}
	const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();
	if (MappingContext->GetRegistrationTrackingMode() !=
			EMappingContextRegistrationTrackingMode::CountRegistrations || Mappings.Num() != 10 ||
		!IsMapping(Mappings[0], Move, EKeys::W, EMappingModifiers::Swizzle) ||
		!IsMapping(Mappings[1], Move, EKeys::A, EMappingModifiers::Negate) ||
		!IsMapping(Mappings[2], Move, EKeys::S, EMappingModifiers::SwizzleAndNegate) ||
		!IsMapping(Mappings[3], Move, EKeys::D, EMappingModifiers::None) ||
		!IsMapping(Mappings[4], Look, EKeys::Mouse2D, EMappingModifiers::None) ||
		!IsMapping(Mappings[5], Jump, EKeys::SpaceBar, EMappingModifiers::None) ||
		!IsMapping(Mappings[6], Fire, EKeys::LeftMouseButton, EMappingModifiers::None) ||
		!IsMapping(Mappings[7], Reload, EKeys::R, EMappingModifiers::None) ||
		!IsMapping(Mappings[8], SwitchWeapon, EKeys::Q, EMappingModifiers::None) ||
		!IsMapping(Mappings[9], Aim, EKeys::RightMouseButton, EMappingModifiers::None))
	{
		return false;
	}
	int32 Matches = 0;
	for (const UGameFeatureAction* Candidate : Experience->Actions)
	{
		if (Candidate && Candidate->GetFName() == InputActionName)
		{
			const UMiniGameFeatureAction_AddInput* Action = Cast<UMiniGameFeatureAction_AddInput>(Candidate);
			if (!Action || Action->MappingContext != MappingContext)
			{
				return false;
			}
			++Matches;
		}
	}
	return Matches == 1;
#else
	return false;
#endif
}
