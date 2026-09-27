#pragma once

#include "Engine/AssetManager.h"
#include "MiniAssetManager.generated.h"

class UMiniExperienceDefinition;

// These names are part of the saved asset IDs and must stay in sync with DefaultGame.ini.
struct FPS_API FMiniPrimaryAssetTypes
{
	static const FPrimaryAssetType Experience;
	static const FPrimaryAssetType ActionSet;
	static const FPrimaryAssetType PawnData;
};

UCLASS(Config = Game)
class FPS_API UMiniAssetManager : public UAssetManager
{
	GENERATED_BODY()

public:
	// Returns null and logs an error if AssetManagerClassName was not configured.
	static UMiniAssetManager* GetMiniAssetManager();

	// A configured ID must be correctly typed and registered by the asset scan.
	bool TryGetDefaultExperienceId(FPrimaryAssetId& OutId, FString& OutError) const;
	bool TryResolveExperienceId(const FSoftObjectPath& AssetPath, FPrimaryAssetId& OutId, FString& OutError) const;
	bool TryValidateExperienceId(const FPrimaryAssetId& ExperienceId, FString& OutError) const;

	// A blocking helper for setup and probes. Task 05 will use LoadPrimaryAsset asynchronously.
	UMiniExperienceDefinition* LoadExperienceSynchronously(const FPrimaryAssetId& ExperienceId, FString& OutError) const;

private:
	UPROPERTY(Config, EditDefaultsOnly, Category = "Mini|Experience")
	FString DefaultExperienceId;
};
