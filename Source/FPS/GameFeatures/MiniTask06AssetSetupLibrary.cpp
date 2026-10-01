#include "MiniTask06AssetSetupLibrary.h"

#if WITH_EDITOR
#include "GameFeatureAction_AddComponents.h"
#include "GameFeatureData.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "MiniLegacyAssetAuthoringGuard.h"
#endif

bool UMiniTask06AssetSetupLibrary::EnsureAddComponentsAction(UObject* Owner, FName ActionName,
	TSubclassOf<AActor> ActorClass, TSubclassOf<UActorComponent> ComponentClass)
{
#if WITH_EDITOR
	if (!MiniIsDiagnosticsAuthoringAsset(Owner) || ActionName.IsNone() || !ActorClass || !ComponentClass)
	{
		return false;
	}

	TArray<TObjectPtr<UGameFeatureAction>>* Actions = nullptr;
	if (UGameFeatureData* FeatureData = Cast<UGameFeatureData>(Owner))
	{
		Actions = &FeatureData->GetMutableActionsInEditor();
	}
	else if (UMiniExperienceDefinition* Experience = Cast<UMiniExperienceDefinition>(Owner))
	{
		Actions = &Experience->Actions;
	}
	if (!Actions)
	{
		return false;
	}

	UGameFeatureAction_AddComponents* Target = nullptr;
	for (UGameFeatureAction* Action : *Actions)
	{
		if (Action && Action->GetFName() == ActionName)
		{
			Target = Cast<UGameFeatureAction_AddComponents>(Action);
			if (!Target)
			{
				return false;
			}
		}
	}

	Owner->Modify();
	if (!Target)
	{
		Target = NewObject<UGameFeatureAction_AddComponents>(Owner, ActionName, RF_Transactional);
		Actions->Add(Target);
	}
	Target->Modify();

	FGameFeatureComponentEntry Entry;
	Entry.ActorClass = TSoftClassPtr<AActor>(ActorClass.Get());
	Entry.ComponentClass = TSoftClassPtr<UActorComponent>(ComponentClass.Get());
	Entry.bClientComponent = true;
	Entry.bServerComponent = true;
	Target->ComponentList.Reset();
	Target->ComponentList.Add(Entry);
	Owner->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
