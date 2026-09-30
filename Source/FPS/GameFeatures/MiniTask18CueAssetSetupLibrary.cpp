#include "MiniTask18CueAssetSetupLibrary.h"

#if WITH_EDITOR
#include "GameplayCueNotify_Static.h"
#include "GameFeatureData.h"
#include "GameFeatures/MiniGameFeatureAction_AddGameplayCuePath.h"
#include "GameplayTagsManager.h"

namespace
{
const FName CueActionName(TEXT("MiniTask18_AddGameplayCuePath"));
const FString CuePath(TEXT("/MiniShooterCore/GameplayCues"));
}
#endif

bool UMiniTask18CueAssetSetupLibrary::EnsurePluginCueAction(UGameFeatureData* FeatureData)
{
#if WITH_EDITOR
	if (!FeatureData)
	{
		return false;
	}
	auto& Actions = FeatureData->GetMutableActionsInEditor();
	UMiniGameFeatureAction_AddGameplayCuePath* CueAction = nullptr;
	for (UGameFeatureAction* Action : Actions)
	{
		if (Action && Action->GetFName() == CueActionName)
		{
			if (CueAction || !Action->IsA<UMiniGameFeatureAction_AddGameplayCuePath>())
			{
				return false;
			}
			CueAction = CastChecked<UMiniGameFeatureAction_AddGameplayCuePath>(Action);
		}
	}
	FeatureData->Modify();
	if (!CueAction)
	{
		CueAction = NewObject<UMiniGameFeatureAction_AddGameplayCuePath>(
			FeatureData, CueActionName, RF_Transactional);
		Actions.Add(CueAction);
	}
	CueAction->Modify();
	CueAction->CuePath = CuePath;
	FeatureData->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UMiniTask18CueAssetSetupLibrary::VerifyPluginCueAction(const UGameFeatureData* FeatureData)
{
#if WITH_EDITOR
	if (!FeatureData)
	{
		return false;
	}
	int32 Matches = 0;
	for (const UGameFeatureAction* Action : FeatureData->GetActions())
	{
		if (Action && Action->GetFName() == CueActionName)
		{
			const UMiniGameFeatureAction_AddGameplayCuePath* CueAction =
				Cast<UMiniGameFeatureAction_AddGameplayCuePath>(Action);
			if (!CueAction || CueAction->CuePath != CuePath)
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

bool UMiniTask18CueAssetSetupLibrary::VerifyProbeCueClass(UClass* CueClass)
{
#if WITH_EDITOR
	if (!CueClass || !CueClass->IsChildOf(UGameplayCueNotify_Static::StaticClass()) ||
		!CueClass->GetPathName().StartsWith(CuePath + TEXT("/")))
	{
		return false;
	}
	const FGameplayTag ExpectedTag = UGameplayTagsManager::Get().RequestGameplayTag(
		TEXT("GameplayCue.Mini.AssetProbe"), false);
	const UGameplayCueNotify_Static* Cue = CueClass->GetDefaultObject<UGameplayCueNotify_Static>();
	return Cue && ExpectedTag.IsValid() && Cue->GameplayCueTag == ExpectedTag &&
		Cue->GameplayCueName == ExpectedTag.GetTagName();
#else
	return false;
#endif
}
