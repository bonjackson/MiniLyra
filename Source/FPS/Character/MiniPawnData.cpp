#include "MiniPawnData.h"

#include "System/MiniAssetManager.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FPrimaryAssetId UMiniPawnData::GetPrimaryAssetId() const
{
	return HasAnyFlags(RF_ClassDefaultObject)
		? FPrimaryAssetId()
		: FPrimaryAssetId(FMiniPrimaryAssetTypes::PawnData, GetFName());
}

bool UMiniPawnData::ValidatePawnData(FString& OutError) const
{
	OutError.Reset();
	if (!PawnClass)
	{
		OutError = FString::Printf(TEXT("PawnData '%s' has no PawnClass"), *GetPathName());
		return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UMiniPawnData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FString Error;
		if (!ValidatePawnData(Error))
		{
			Context.AddError(FText::FromString(Error));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif
