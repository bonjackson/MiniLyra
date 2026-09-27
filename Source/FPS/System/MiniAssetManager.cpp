#include "MiniAssetManager.h"

#include "Engine/Engine.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "MiniLogChannels.h"

const FPrimaryAssetType FMiniPrimaryAssetTypes::Experience(TEXT("MiniExperienceDefinition"));
const FPrimaryAssetType FMiniPrimaryAssetTypes::ActionSet(TEXT("MiniExperienceActionSet"));
const FPrimaryAssetType FMiniPrimaryAssetTypes::PawnData(TEXT("MiniPawnData"));

UMiniAssetManager* UMiniAssetManager::GetMiniAssetManager()
{
	UMiniAssetManager* Manager = Cast<UMiniAssetManager>(&UAssetManager::Get());
	if (!Manager)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("AssetManagerClassName must be /Script/FPS.MiniAssetManager"));
	}
	return Manager;
}

bool UMiniAssetManager::TryGetDefaultExperienceId(FPrimaryAssetId& OutId, FString& OutError) const
{
	OutId = FPrimaryAssetId();
	OutError.Reset();
	const FString ConfiguredId = DefaultExperienceId.TrimStartAndEnd();
	if (ConfiguredId.IsEmpty())
	{
		OutError = TEXT("DefaultExperienceId is empty in [/Script/FPS.MiniAssetManager]");
		return false;
	}

	OutId = FPrimaryAssetId::FromString(ConfiguredId);
	if (!TryValidateExperienceId(OutId, OutError))
	{
		OutId = FPrimaryAssetId();
		return false;
	}
	return true;
}

bool UMiniAssetManager::TryResolveExperienceId(const FSoftObjectPath& AssetPath, FPrimaryAssetId& OutId, FString& OutError) const
{
	OutId = FPrimaryAssetId();
	OutError.Reset();
	if (!AssetPath.IsValid())
	{
		OutError = TEXT("Experience asset path is empty or invalid");
		return false;
	}

	const FPrimaryAssetId FoundId = GetPrimaryAssetIdForPath(AssetPath);
	if (!FoundId.IsValid())
	{
		OutError = FString::Printf(TEXT("Experience asset %s was not found by the Primary Asset scan"), *AssetPath.ToString());
		return false;
	}
	if (!TryValidateExperienceId(FoundId, OutError))
	{
		return false;
	}
	const FSoftObjectPath RegisteredPath = GetPrimaryAssetPath(FoundId);
	if (RegisteredPath != AssetPath)
	{
		OutError = FString::Printf(TEXT("Experience path %s resolves to ID '%s', but the registered asset path is %s"),
			*AssetPath.ToString(), *FoundId.ToString(), *RegisteredPath.ToString());
		return false;
	}

	OutId = FoundId;
	return true;
}

bool UMiniAssetManager::TryValidateExperienceId(const FPrimaryAssetId& ExperienceId, FString& OutError) const
{
	OutError.Reset();
	if (!ExperienceId.IsValid() || ExperienceId.PrimaryAssetType != FMiniPrimaryAssetTypes::Experience)
	{
		OutError = FString::Printf(TEXT("Expected a MiniExperienceDefinition ID, got '%s'"), *ExperienceId.ToString());
		return false;
	}
	if (!GetPrimaryAssetPath(ExperienceId).IsValid())
	{
		OutError = FString::Printf(TEXT("Unknown Experience ID '%s': no scanned asset has this ID"), *ExperienceId.ToString());
		return false;
	}
	return true;
}

UMiniExperienceDefinition* UMiniAssetManager::LoadExperienceSynchronously(const FPrimaryAssetId& ExperienceId, FString& OutError) const
{
	if (!TryValidateExperienceId(ExperienceId, OutError))
	{
		UE_LOG(LogMiniExperience, Error, TEXT("%s"), *OutError);
		return nullptr;
	}

	const FSoftObjectPath AssetPath = GetPrimaryAssetPath(ExperienceId);
	UMiniExperienceDefinition* Experience = Cast<UMiniExperienceDefinition>(AssetPath.TryLoad());
	if (!Experience)
	{
		OutError = FString::Printf(TEXT("Experience ID '%s' resolved to %s, but it did not load as a MiniExperienceDefinition instance"),
			*ExperienceId.ToString(), *AssetPath.ToString());
		UE_LOG(LogMiniExperience, Error, TEXT("%s"), *OutError);
		return nullptr;
	}
	if (Experience->GetPrimaryAssetId() != ExperienceId)
	{
		OutError = FString::Printf(TEXT("Experience %s returned mismatched ID '%s' (expected '%s')"),
			*AssetPath.ToString(), *Experience->GetPrimaryAssetId().ToString(), *ExperienceId.ToString());
		UE_LOG(LogMiniExperience, Error, TEXT("%s"), *OutError);
		return nullptr;
	}
	if (!Experience->ValidateDefinition(OutError))
	{
		UE_LOG(LogMiniExperience, Error, TEXT("Experience '%s' is invalid: %s"), *ExperienceId.ToString(), *OutError);
		return nullptr;
	}
	OutError.Reset();
	return Experience;
}
