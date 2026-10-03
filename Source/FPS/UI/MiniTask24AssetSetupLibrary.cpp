#include "MiniTask24AssetSetupLibrary.h"

#if WITH_EDITOR
#include "GameFeatures/MiniGameFeatureAction_AddWidgets.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "UI/MiniFrontEndWidget.h"
#include "UI/MiniPrimaryGameLayout.h"

namespace
{
const TCHAR* FrontEndPath = TEXT("/Game/Mini/System/Experiences/DA_MiniFrontEndExperience.DA_MiniFrontEndExperience");
const FName FrontEndActionName(TEXT("MiniFrontEnd_AddWidgets"));
bool IsOwnedExperience(const UMiniExperienceDefinition* Experience)
{
	return Experience && Experience->GetPathName() == FrontEndPath;
}
}
#endif

bool UMiniTask24AssetSetupLibrary::EnsureFrontEndExperience(UMiniExperienceDefinition* Experience)
{
#if WITH_EDITOR
	if (!IsOwnedExperience(Experience)) { return false; }
	if (VerifyFrontEndExperience(Experience)) { return true; }
	// Refuse unrelated contributions instead of silently replacing gameplay data.
	if (Experience->DefaultPawnData || !Experience->GameFeaturesToEnable.IsEmpty() ||
		!Experience->ActionSets.IsEmpty() || Experience->Actions.Num() > 1) { return false; }
	UMiniGameFeatureAction_AddWidgets* Action = nullptr;
	if (!Experience->Actions.IsEmpty())
	{
		Action = Cast<UMiniGameFeatureAction_AddWidgets>(Experience->Actions[0]);
		if (!Action || Action->GetOuter() != Experience || Action->GetFName() != FrontEndActionName) { return false; }
	}
	else
	{
		UObject* Existing = FindObject<UObject>(Experience, *FrontEndActionName.ToString());
		if (Existing && !Existing->IsA<UMiniGameFeatureAction_AddWidgets>()) { return false; }
		Action = Cast<UMiniGameFeatureAction_AddWidgets>(Existing);
	}
	Experience->Modify();
	if (!Action) { Action = NewObject<UMiniGameFeatureAction_AddWidgets>(Experience, FrontEndActionName, RF_Transactional); }
	Action->Modify();
	Action->Layouts.Reset();
	Action->Elements.Reset();
	FMiniHUDLayoutRequest Request;
	Request.LayoutClass = UMiniFrontEndWidget::StaticClass();
	Request.LayerTag = UMiniPrimaryGameLayout::GetMenuLayerTag();
	Action->Layouts.Add(Request);
	Experience->bIsFrontEnd = true;
	Experience->Actions = {Action};
	Experience->MarkPackageDirty();
	return VerifyFrontEndExperience(Experience);
#else
	return false;
#endif
}

bool UMiniTask24AssetSetupLibrary::VerifyFrontEndExperience(const UMiniExperienceDefinition* Experience)
{
#if WITH_EDITOR
	if (!IsOwnedExperience(Experience) || !Experience->bIsFrontEnd || Experience->DefaultPawnData ||
		!Experience->GameFeaturesToEnable.IsEmpty() || !Experience->ActionSets.IsEmpty() || Experience->Actions.Num() != 1) { return false; }
	const UMiniGameFeatureAction_AddWidgets* Action = Cast<UMiniGameFeatureAction_AddWidgets>(Experience->Actions[0]);
	if (!Action || Action->GetOuter() != Experience || Action->GetFName() != FrontEndActionName ||
		Action->Layouts.Num() != 1 || !Action->Elements.IsEmpty() ||
		Action->Layouts[0].LayoutClass.ToSoftObjectPath() != FSoftObjectPath(UMiniFrontEndWidget::StaticClass()) ||
		Action->Layouts[0].LayerTag != UMiniPrimaryGameLayout::GetMenuLayerTag()) { return false; }
	FString Error;
	return Experience->ValidateDefinition(Error);
#else
	return false;
#endif
}
