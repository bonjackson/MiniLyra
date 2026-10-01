#include "MiniMatchRulesConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UMiniMatchRulesConfig::ValidateConfig(FString& OutError) const
{
	OutError.Reset();
	if (MinPlayers < 2 || ScoreLimit < 1 || !FMath::IsFinite(SpawnProtectionSeconds) ||
		SpawnProtectionSeconds < 0.0f || !FMath::IsFinite(RespawnDelaySeconds) || RespawnDelaySeconds < 0.0f)
	{
		OutError = TEXT("FFA requires MinPlayers >= 2, ScoreLimit >= 1, and finite nonnegative protection/respawn durations.");
		return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UMiniMatchRulesConfig::IsDataValid(FDataValidationContext& Context) const
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
