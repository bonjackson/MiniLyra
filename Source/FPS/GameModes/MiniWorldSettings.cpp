#include "MiniWorldSettings.h"

#include "System/MiniAssetManager.h"
#include "System/MiniLogChannels.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FPrimaryAssetId AMiniWorldSettings::GetDefaultGameplayExperience(FString* OutError) const
{
	if (OutError)
	{
		OutError->Reset();
	}
	if (DefaultGameplayExperience.IsNull())
	{
		return FPrimaryAssetId();
	}

	UMiniAssetManager* Manager = UMiniAssetManager::GetMiniAssetManager();
	FPrimaryAssetId ExperienceId;
	FString Error;
	if (!Manager || !Manager->TryResolveExperienceId(DefaultGameplayExperience.ToSoftObjectPath(), ExperienceId, Error))
	{
		if (!Manager)
		{
			Error = TEXT("MiniAssetManager is not configured");
		}
		Error = FString::Printf(TEXT("WorldSettings '%s' DefaultGameplayExperience %s: %s"),
			*GetPathName(), *DefaultGameplayExperience.ToString(), *Error);
		UE_LOG(LogMiniExperience, Error, TEXT("%s"), *Error);
		if (OutError)
		{
			*OutError = MoveTemp(Error);
		}
		return FPrimaryAssetId();
	}
	return ExperienceId;
}

#if WITH_EDITOR
EDataValidationResult AMiniWorldSettings::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!DefaultGameplayExperience.IsNull())
	{
		FString Error;
		if (!GetDefaultGameplayExperience(&Error).IsValid())
		{
			Context.AddError(FText::FromString(Error));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif
