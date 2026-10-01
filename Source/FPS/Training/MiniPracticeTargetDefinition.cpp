#include "MiniPracticeTargetDefinition.h"

#include "Materials/MaterialInterface.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "MiniPracticeTarget"

namespace
{
bool Task20FiniteTargetColor(const FLinearColor& Color)
{
	return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) &&
		FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
}
}

UMiniPracticeTargetDefinition::UMiniPracticeTargetDefinition()
{
	DisplayName = LOCTEXT("TargetName", "训练靶");
	BoardMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/Mini/Targets/M_MiniPracticeTarget.M_MiniPracticeTarget")));
}

FPrimaryAssetId UMiniPracticeTargetDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("MiniPracticeTargetDefinition"), GetFName());
}

bool UMiniPracticeTargetDefinition::ValidateDefinition(FString& OutError) const
{
	OutError.Reset();
	if (!FMath::IsFinite(MaxHealth) || MaxHealth < 1.0f ||
		!FMath::IsFinite(ResetDelay) || ResetDelay < 0.1f || DisplayName.IsEmpty() ||
		!Task20FiniteTargetColor(ActiveColor) || !Task20FiniteTargetColor(DisabledColor))
	{
		OutError = TEXT("Target requires a label, finite health >= 1, finite reset delay >= 0.1 and finite colors.");
		return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UMiniPracticeTargetDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FString Error;
	if (!ValidateDefinition(Error))
	{
		Context.AddError(FText::FromString(Error));
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif

#undef LOCTEXT_NAMESPACE
