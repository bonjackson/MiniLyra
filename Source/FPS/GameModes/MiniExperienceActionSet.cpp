#include "MiniExperienceActionSet.h"

#include "GameFeatureAction.h"
#include "System/MiniAssetManager.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FPrimaryAssetId UMiniExperienceActionSet::GetPrimaryAssetId() const
{
	return HasAnyFlags(RF_ClassDefaultObject)
		? FPrimaryAssetId()
		: FPrimaryAssetId(FMiniPrimaryAssetTypes::ActionSet, GetFName());
}

bool UMiniExperienceActionSet::ValidateActionSet(FString& OutError) const
{
	OutError.Reset();
	for (int32 Index = 0; Index < GameFeaturesToEnable.Num(); ++Index)
	{
		if (GameFeaturesToEnable[Index].TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("ActionSet '%s' has an empty GameFeaturesToEnable entry at index %d"), *GetPathName(), Index);
			return false;
		}
	}
	for (int32 Index = 0; Index < Actions.Num(); ++Index)
	{
		if (!Actions[Index])
		{
			OutError = FString::Printf(TEXT("ActionSet '%s' has a null Action at index %d"), *GetPathName(), Index);
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UMiniExperienceActionSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FString Error;
		if (!ValidateActionSet(Error))
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
void UMiniExperienceActionSet::UpdateAssetBundleData()
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
