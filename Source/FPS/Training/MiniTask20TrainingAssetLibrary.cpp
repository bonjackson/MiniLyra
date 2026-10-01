#include "MiniTask20TrainingAssetLibrary.h"

#if WITH_EDITOR
#include "Engine/AssetManagerSettings.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Training/MiniPracticeTarget.h"
#include "Training/MiniPracticeTargetDefinition.h"
#include "UObject/UnrealType.h"

namespace MiniTask20TrainingAssets
{
const FString BlueprintPath(TEXT("/Game/Mini/Targets/BP_MiniPracticeTarget.BP_MiniPracticeTarget"));
const FString DefinitionPath(TEXT("/Game/Mini/Targets/DA_MiniPracticeTarget.DA_MiniPracticeTarget"));
const FSoftObjectPath MaterialPath(TEXT("/Game/Mini/Targets/M_MiniPracticeTarget.M_MiniPracticeTarget"));
const FName ColorParameterName(TEXT("TargetColor"));
const FLinearColor ActiveColor(0.02f, 0.65f, 1.0f, 1.0f);
const FLinearColor DisabledColor(1.0f, 0.25f, 0.05f, 1.0f);

bool IsTargetBlueprint(const UBlueprint* Blueprint)
{
	return Blueprint && Blueprint->GetClass() == UBlueprint::StaticClass() &&
		Blueprint->GetPathName() == BlueprintPath && Blueprint->BlueprintType == BPTYPE_Normal &&
		Blueprint->ParentClass == AMiniPracticeTarget::StaticClass();
}
}
#endif

bool UMiniTask20TrainingAssetLibrary::EnsureTargetBlueprint(UBlueprint* TargetBlueprint,
	UMiniPracticeTargetDefinition* Definition)
{
#if WITH_EDITOR
	if (!MiniTask20TrainingAssets::IsTargetBlueprint(TargetBlueprint) || !VerifyTargetDefinition(Definition))
	{
		return false;
	}
	if (VerifyTargetBlueprint(TargetBlueprint))
	{
		return true;
	}
	FKismetEditorUtilities::CompileBlueprint(TargetBlueprint);
	AMiniPracticeTarget* CDO = TargetBlueprint->GeneratedClass ?
		Cast<AMiniPracticeTarget>(TargetBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!CDO)
	{
		return false;
	}
	TargetBlueprint->Modify();
	CDO->Modify();
	CDO->TargetDefinition = Definition;
	FBlueprintEditorUtils::MarkBlueprintAsModified(TargetBlueprint);
	FKismetEditorUtilities::CompileBlueprint(TargetBlueprint);
	TargetBlueprint->MarkPackageDirty();
	return VerifyTargetBlueprint(TargetBlueprint);
#else
	return false;
#endif
}

bool UMiniTask20TrainingAssetLibrary::VerifyTargetBlueprint(const UBlueprint* TargetBlueprint)
{
#if WITH_EDITOR
	if (!MiniTask20TrainingAssets::IsTargetBlueprint(TargetBlueprint) || TargetBlueprint->Status != BS_UpToDate ||
		!TargetBlueprint->GeneratedClass ||
		TargetBlueprint->GeneratedClass->GetSuperClass() != AMiniPracticeTarget::StaticClass())
	{
		return false;
	}
	const AMiniPracticeTarget* CDO = Cast<AMiniPracticeTarget>(TargetBlueprint->GeneratedClass->GetDefaultObject());
	return CDO && VerifyTargetDefinition(CDO->TargetDefinition.Get());
#else
	return false;
#endif
}

bool UMiniTask20TrainingAssetLibrary::VerifyTargetDefinition(const UMiniPracticeTargetDefinition* Definition)
{
#if WITH_EDITOR
	using namespace MiniTask20TrainingAssets;
	FString Error;
	return Definition && Definition->GetClass() == UMiniPracticeTargetDefinition::StaticClass() &&
		Definition->GetPathName() == DefinitionPath && Definition->ValidateDefinition(Error) &&
		Definition->GetPrimaryAssetId() == FPrimaryAssetId(TEXT("MiniPracticeTargetDefinition"), TEXT("DA_MiniPracticeTarget")) &&
		Definition->DisplayName.ToString() == TEXT("训练靶") &&
		FMath::IsNearlyEqual(Definition->MaxHealth, 100.0f) &&
		FMath::IsNearlyEqual(Definition->ResetDelay, 2.0f) &&
		Definition->BoardMaterial.ToSoftObjectPath() == MaterialPath &&
		Definition->ActiveColor.Equals(ActiveColor) && Definition->DisabledColor.Equals(DisabledColor);
#else
	return false;
#endif
}

bool UMiniTask20TrainingAssetLibrary::VerifyTargetMaterial(const UMaterial* Material)
{
#if WITH_EDITOR
	using namespace MiniTask20TrainingAssets;
	if (!Material || Material->GetClass() != UMaterial::StaticClass() ||
		FSoftObjectPath(Material) != MaterialPath || Material->MaterialDomain != MD_Surface ||
		Material->GetBlendMode() != BLEND_Opaque || Material->bUseMaterialAttributes ||
		!Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit))
	{
		return false;
	}
	const UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	const UMaterialExpressionVectorParameter* ColorParameter = EditorData ?
		Cast<UMaterialExpressionVectorParameter>(EditorData->BaseColor.Expression) : nullptr;
	// Check the saved graph instead of compiling or repairing it during verification.
	return ColorParameter && Material->GetExpressions().Num() == 1 &&
		Material->GetExpressions()[0] == ColorParameter && ColorParameter->GetOuter() == Material &&
		ColorParameter->ParameterName == ColorParameterName && !ColorParameter->bUseCustomPrimitiveData &&
		ColorParameter->DefaultValue.Equals(ActiveColor) && EditorData->BaseColor.OutputIndex == 0 &&
		(!EditorData->BaseColor.Mask || (EditorData->BaseColor.MaskR && EditorData->BaseColor.MaskG &&
			EditorData->BaseColor.MaskB && !EditorData->BaseColor.MaskA));
#else
	return false;
#endif
}

bool UMiniTask20TrainingAssetLibrary::VerifyCookScan()
{
#if WITH_EDITOR
	const UAssetManagerSettings* Settings = GetDefault<UAssetManagerSettings>();
	if (!Settings)
	{
		return false;
	}
	const FPrimaryAssetTypeInfo* LabelScan = nullptr;
	for (const FPrimaryAssetTypeInfo& TypeInfo : Settings->PrimaryAssetTypesToScan)
	{
		if (TypeInfo.PrimaryAssetType == FName(TEXT("PrimaryAssetLabel")))
		{
			if (LabelScan)
			{
				return false;
			}
			LabelScan = &TypeInfo;
		}
	}
	return LabelScan && !LabelScan->bIsEditorOnly && !LabelScan->bHasBlueprintClasses &&
		LabelScan->Rules.bApplyRecursively &&
		LabelScan->GetAssetBaseClass().ToSoftObjectPath() == FSoftObjectPath(TEXT("/Script/Engine.PrimaryAssetLabel")) &&
		LabelScan->GetDirectories().Num() == 1 &&
		LabelScan->GetDirectories()[0].Path == TEXT("/Game/Mini/System/Packaging") &&
		LabelScan->GetSpecificAssets().IsEmpty();
#else
	return false;
#endif
}

bool UMiniTask20TrainingAssetLibrary::VerifySoftDefault(const UObject* CDO, FName PropertyName,
	const FString& ExpectedObjectPath)
{
#if WITH_EDITOR
	if (!CDO || !CDO->HasAnyFlags(RF_ClassDefaultObject) || PropertyName.IsNone() || ExpectedObjectPath.IsEmpty())
	{
		return false;
	}
	const FSoftObjectProperty* Property = FindFProperty<FSoftObjectProperty>(CDO->GetClass(), PropertyName);
	return Property && Property->GetPropertyValue_InContainer(CDO).ToSoftObjectPath() == FSoftObjectPath(ExpectedObjectPath);
#else
	return false;
#endif
}
