#include "MiniTask19AssetSetupLibrary.h"

#if WITH_EDITOR
#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "Engine/Blueprint.h"
#include "GameFeatureData.h"
#include "GameFeatures/MiniGameFeatureAction_AddWidgets.h"
#include "GameplayTagsManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UI/MiniGameUIPolicy.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UObject/UnrealType.h"

namespace
{
const FString Task19AssetPolicyAssetPath(TEXT("/Game/Mini/UI/B_MiniUIPolicy.B_MiniUIPolicy"));
const FString Task19AssetFeatureAssetPath(TEXT("/MiniShooterCore/GameFeatureData.GameFeatureData"));
const FName Task19AssetWidgetActionName(TEXT("MiniTask19_AddWidgets"));
const FSoftObjectPath Task19AssetRootClassPath(TEXT("/Script/FPS.MiniPrimaryGameLayout"));
const FSoftObjectPath Task19AssetHUDClassPath(TEXT("/Script/FPS.MiniHUDLayout"));

struct Task19AssetFElementDefinition
{
	const TCHAR* Tag;
	const TCHAR* ClassPath;
};

const Task19AssetFElementDefinition Task19AssetElements[] =
{
	{ TEXT("UI.HUD.Health"), TEXT("/Script/FPS.MiniHUDHealthWidget") },
	{ TEXT("UI.HUD.Ammo"), TEXT("/Script/FPS.MiniHUDAmmoWidget") },
	{ TEXT("UI.HUD.Crosshair"), TEXT("/Script/FPS.MiniHUDCrosshairWidget") },
	{ TEXT("UI.HUD.Match"), TEXT("/Script/FPS.MiniHUDMatchWidget") }
};

FGameplayTag Task19AssetFindTag(const TCHAR* Name)
{
	return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), false);
}

bool Task19AssetIsPolicyAsset(const UBlueprint* Blueprint)
{
	return Blueprint && Blueprint->GetPathName() == Task19AssetPolicyAssetPath &&
		Blueprint->ParentClass == UMiniGameUIPolicy::StaticClass();
}

bool Task19AssetResolveWidgetClasses()
{
	const UClass* Layout = LoadClass<UCommonActivatableWidget>(nullptr, *Task19AssetHUDClassPath.ToString());
	if (!Layout || Layout->HasAnyClassFlags(CLASS_Abstract) || !Task19AssetFindTag(TEXT("UI.Layer.Game")).IsValid())
	{
		return false;
	}
	for (const Task19AssetFElementDefinition& Entry : Task19AssetElements)
	{
		const UClass* Widget = LoadClass<UUserWidget>(nullptr, Entry.ClassPath);
		if (!Widget || Widget->HasAnyClassFlags(CLASS_Abstract) || !Task19AssetFindTag(Entry.Tag).IsValid())
		{
			return false;
		}
	}
	return true;
}
}
#endif

bool UMiniTask19AssetSetupLibrary::EnsurePolicyBlueprint(UBlueprint* PolicyBlueprint)
{
#if WITH_EDITOR
	if (!Task19AssetIsPolicyAsset(PolicyBlueprint))
	{
		return false;
	}
	if (VerifyPolicyBlueprint(PolicyBlueprint))
	{
		return true;
	}
	FKismetEditorUtilities::CompileBlueprint(PolicyBlueprint);
	UObject* CDO = PolicyBlueprint->GeneratedClass ? PolicyBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
	FSoftClassProperty* LayoutProperty = FindFProperty<FSoftClassProperty>(
		UGameUIPolicy::StaticClass(), TEXT("LayoutClass"));
	if (!CDO || !LayoutProperty ||
		!UMiniPrimaryGameLayout::StaticClass()->IsChildOf(LayoutProperty->MetaClass))
	{
		return false;
	}
	PolicyBlueprint->Modify();
	CDO->Modify();
	// The CommonGame property is private in C++; author its editable Blueprint
	// default only here. Runtime policy initialization never mutates it.
	LayoutProperty->SetPropertyValue_InContainer(CDO, FSoftObjectPtr(Task19AssetRootClassPath));
	FBlueprintEditorUtils::MarkBlueprintAsModified(PolicyBlueprint);
	FKismetEditorUtilities::CompileBlueprint(PolicyBlueprint);
	PolicyBlueprint->MarkPackageDirty();
	return VerifyPolicyBlueprint(PolicyBlueprint);
#else
	return false;
#endif
}

bool UMiniTask19AssetSetupLibrary::VerifyPolicyBlueprint(const UBlueprint* PolicyBlueprint)
{
#if WITH_EDITOR
	if (!Task19AssetIsPolicyAsset(PolicyBlueprint) || PolicyBlueprint->Status != BS_UpToDate ||
		!PolicyBlueprint->GeneratedClass ||
		PolicyBlueprint->GeneratedClass->GetSuperClass() != UMiniGameUIPolicy::StaticClass())
	{
		return false;
	}
	const FSoftClassProperty* LayoutProperty = FindFProperty<FSoftClassProperty>(
		UGameUIPolicy::StaticClass(), TEXT("LayoutClass"));
	const UObject* CDO = PolicyBlueprint->GeneratedClass->GetDefaultObject();
	return LayoutProperty && CDO &&
		LayoutProperty->GetPropertyValue_InContainer(CDO).ToSoftObjectPath() == Task19AssetRootClassPath;
#else
	return false;
#endif
}

bool UMiniTask19AssetSetupLibrary::EnsurePluginWidgetAction(UGameFeatureData* FeatureData)
{
#if WITH_EDITOR
	if (!FeatureData || FeatureData->GetPathName() != Task19AssetFeatureAssetPath || !Task19AssetResolveWidgetClasses())
	{
		return false;
	}
	UMiniGameFeatureAction_AddWidgets* WidgetAction = nullptr;
	auto& Actions = FeatureData->GetMutableActionsInEditor();
	for (UGameFeatureAction* Action : Actions)
	{
		if (!Action)
		{
			continue;
		}
		if (Action->GetFName() == Task19AssetWidgetActionName)
		{
			if (WidgetAction || !Action->IsA<UMiniGameFeatureAction_AddWidgets>())
			{
				return false;
			}
			WidgetAction = CastChecked<UMiniGameFeatureAction_AddWidgets>(Action);
		}
		else if (Action->IsA<UMiniGameFeatureAction_AddWidgets>())
		{
			// A second UI owner in the same feature would duplicate practice HUD.
			return false;
		}
	}
	if (VerifyPluginWidgetAction(FeatureData))
	{
		return true;
	}
	FeatureData->Modify();
	if (!WidgetAction)
	{
		WidgetAction = NewObject<UMiniGameFeatureAction_AddWidgets>(FeatureData,
			Task19AssetWidgetActionName, RF_Transactional);
		Actions.Add(WidgetAction);
	}
	WidgetAction->Modify();
	WidgetAction->Layouts.Reset();
	FMiniHUDLayoutRequest& Layout = WidgetAction->Layouts.AddDefaulted_GetRef();
	Layout.LayoutClass = TSoftClassPtr<UCommonActivatableWidget>(Task19AssetHUDClassPath);
	Layout.LayerTag = Task19AssetFindTag(TEXT("UI.Layer.Game"));
	WidgetAction->Elements.Reset();
	for (const Task19AssetFElementDefinition& Entry : Task19AssetElements)
	{
		FMiniHUDElementRequest& Element = WidgetAction->Elements.AddDefaulted_GetRef();
		Element.WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(Entry.ClassPath));
		Element.SlotTag = Task19AssetFindTag(Entry.Tag);
		Element.Priority = 0;
	}
	FeatureData->MarkPackageDirty();
	return VerifyPluginWidgetAction(FeatureData);
#else
	return false;
#endif
}

bool UMiniTask19AssetSetupLibrary::VerifyPluginWidgetAction(const UGameFeatureData* FeatureData)
{
#if WITH_EDITOR
	if (!FeatureData || FeatureData->GetPathName() != Task19AssetFeatureAssetPath || !Task19AssetResolveWidgetClasses())
	{
		return false;
	}
	const UMiniGameFeatureAction_AddWidgets* WidgetAction = nullptr;
	for (const UGameFeatureAction* Action : FeatureData->GetActions())
	{
		if (Action && (Action->GetFName() == Task19AssetWidgetActionName || Action->IsA<UMiniGameFeatureAction_AddWidgets>()))
		{
			if (WidgetAction || Action->GetFName() != Task19AssetWidgetActionName)
			{
				return false;
			}
			WidgetAction = Cast<UMiniGameFeatureAction_AddWidgets>(Action);
			if (!WidgetAction || WidgetAction->GetOuter() != FeatureData)
			{
				return false;
			}
		}
	}
	if (!WidgetAction || WidgetAction->Layouts.Num() != 1 ||
		WidgetAction->Elements.Num() != UE_ARRAY_COUNT(Task19AssetElements))
	{
		return false;
	}
	const FMiniHUDLayoutRequest& Layout = WidgetAction->Layouts[0];
	if (Layout.LayoutClass.ToSoftObjectPath() != Task19AssetHUDClassPath ||
		Layout.LayerTag != Task19AssetFindTag(TEXT("UI.Layer.Game")))
	{
		return false;
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Task19AssetElements); ++Index)
	{
		const FMiniHUDElementRequest& Element = WidgetAction->Elements[Index];
		if (Element.WidgetClass.ToSoftObjectPath() != FSoftObjectPath(Task19AssetElements[Index].ClassPath) ||
			Element.SlotTag != Task19AssetFindTag(Task19AssetElements[Index].Tag) || Element.Priority != 0)
		{
			return false;
		}
	}
	return true;
#else
	return false;
#endif
}
