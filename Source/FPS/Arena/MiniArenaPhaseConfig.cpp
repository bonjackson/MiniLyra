#include "MiniArenaPhaseConfig.h"

#include "GameModes/MiniGamePhaseAbility.h"
#include "System/MiniGameplayTags.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UMiniArenaPhaseConfig::UMiniArenaPhaseConfig()
{
	Phases.SetNum(3);
	Phases[0].AbilityClass = UMiniGamePhaseAbility_Warmup::StaticClass();
	Phases[0].DurationSeconds = 10.0f;
	Phases[1].AbilityClass = UMiniGamePhaseAbility_Playing::StaticClass();
	Phases[1].DurationSeconds = 60.0f;
	Phases[2].AbilityClass = UMiniGamePhaseAbility_PostMatch::StaticClass();
	Phases[2].DurationSeconds = 5.0f;
}

bool UMiniArenaPhaseConfig::ValidateConfig(FString& OutError) const
{
	OutError.Reset();
	const FGameplayTag ExpectedTags[] = {MiniGameplayTags::GamePhase_MiniArena_Warmup,
		MiniGameplayTags::GamePhase_MiniArena_Playing, MiniGameplayTags::GamePhase_MiniArena_PostMatch};
	const UClass* ExpectedClasses[] = {UMiniGamePhaseAbility_Warmup::StaticClass(),
		UMiniGamePhaseAbility_Playing::StaticClass(), UMiniGamePhaseAbility_PostMatch::StaticClass()};
	if (Phases.Num() != UE_ARRAY_COUNT(ExpectedTags))
	{
		OutError = TEXT("Arena requires exactly Warmup, Playing, PostMatch.");
		return false;
	}
	for (int32 Index = 0; Index < Phases.Num(); ++Index)
	{
		const FMiniArenaPhaseEntry& Entry = Phases[Index];
		const UMiniGamePhaseAbility* Ability = Entry.AbilityClass ? Entry.AbilityClass->GetDefaultObject<UMiniGamePhaseAbility>() : nullptr;
		if (!Ability || Entry.AbilityClass->HasAnyClassFlags(CLASS_Abstract) ||
			!Entry.AbilityClass->IsChildOf(ExpectedClasses[Index]) || Ability->GetPhaseTag() != ExpectedTags[Index] ||
			!FMath::IsFinite(Entry.DurationSeconds) || Entry.DurationSeconds < 0.0f ||
			Ability->GetNetExecutionPolicy() != EGameplayAbilityNetExecutionPolicy::ServerOnly ||
			Ability->GetInstancingPolicy() != EGameplayAbilityInstancingPolicy::InstancedPerActor)
		{
			OutError = FString::Printf(TEXT("Arena phase entry %d has an invalid class, tag, duration or execution policy."), Index);
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UMiniArenaPhaseConfig::IsDataValid(FDataValidationContext& Context) const
{
	FString Error;
	if (!ValidateConfig(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return EDataValidationResult::Valid;
}
#endif
