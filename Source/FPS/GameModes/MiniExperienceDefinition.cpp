#include "MiniExperienceDefinition.h"

#include "Character/MiniPawnData.h"
#include "GameFeatureAction.h"
#include "MiniExperienceActionSet.h"
#include "System/MiniAssetManager.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FPrimaryAssetId UMiniExperienceDefinition::GetPrimaryAssetId() const
{
	return HasAnyFlags(RF_ClassDefaultObject)
		? FPrimaryAssetId()
		: FPrimaryAssetId(FMiniPrimaryAssetTypes::Experience, GetFName());
}

bool UMiniExperienceDefinition::ValidateDefinition(FString& OutError) const
{
	OutError.Reset();
	if (!DefaultPawnData)
	{
		OutError = FString::Printf(TEXT("Experience '%s' has no DefaultPawnData"), *GetPathName());
		return false;
	}
	if (!DefaultPawnData->ValidatePawnData(OutError))
	{
		OutError = FString::Printf(TEXT("Experience '%s': %s"), *GetPathName(), *OutError);
		return false;
	}
	for (int32 Index = 0; Index < GameFeaturesToEnable.Num(); ++Index)
	{
		if (GameFeaturesToEnable[Index].TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("Experience '%s' has an empty GameFeaturesToEnable entry at index %d"), *GetPathName(), Index);
			return false;
		}
	}
	for (int32 Index = 0; Index < Actions.Num(); ++Index)
	{
		if (!Actions[Index])
		{
			OutError = FString::Printf(TEXT("Experience '%s' has a null Action at index %d"), *GetPathName(), Index);
			return false;
		}
	}
	for (int32 Index = 0; Index < ActionSets.Num(); ++Index)
	{
		if (!ActionSets[Index])
		{
			OutError = FString::Printf(TEXT("Experience '%s' has a null ActionSet at index %d"), *GetPathName(), Index);
			return false;
		}
		if (!ActionSets[Index]->ValidateActionSet(OutError))
		{
			OutError = FString::Printf(TEXT("Experience '%s': %s"), *GetPathName(), *OutError);
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UMiniExperienceDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FString Error;
		if (!ValidateDefinition(Error))
		{
			Context.AddError(FText::FromString(Error));
			Result = EDataValidationResult::Invalid;
		}
	}
	for (const UGameFeatureAction* Action : Actions)
	{
		if (Action)
		{
			Result = CombineDataValidationResults(Result, Action->IsDataValid(Context));
		}
	}
	return Result;
}
#endif

#if WITH_EDITORONLY_DATA
void UMiniExperienceDefinition::UpdateAssetBundleData()
{
	Super::UpdateAssetBundleData();
	for (UGameFeatureAction* Action : Actions)
	{
		if (Action)
		{
			Action->AddAdditionalAssetBundleData(AssetBundleData);
		}
	}
}
#endif
