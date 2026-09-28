#include "MiniPawnData.h"

#include "AbilitySystem/MiniAbilitySet.h"
#include "Character/MiniCharacter.h"
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
	if (!PawnClass->IsChildOf(AMiniCharacter::StaticClass()))
	{
		OutError = FString::Printf(TEXT("PawnData '%s' PawnClass '%s' must derive from MiniCharacter"),
			*GetPathName(), *PawnClass->GetPathName());
		return false;
	}
	TSet<const UMiniAbilitySet*> SeenSets;
	for (const UMiniAbilitySet* Set : AbilitySets)
	{
		if (!Set || SeenSets.Contains(Set))
		{
			OutError = FString::Printf(TEXT("PawnData '%s' has a missing or duplicate AbilitySet"), *GetPathName());
			return false;
		}
		SeenSets.Add(Set);
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
