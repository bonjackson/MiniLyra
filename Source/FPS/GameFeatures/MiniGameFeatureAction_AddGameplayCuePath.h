#pragma once

#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "MiniGameFeatureAction_AddGameplayCuePath.generated.h"

/** Registers a feature's cue assets only while the feature is active. */
UCLASS(meta = (DisplayName = "Mini Add Gameplay Cue Path"))
class FPS_API UMiniGameFeatureAction_AddGameplayCuePath final : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	/** Package path within the mounted GameFeature plugin, not a filesystem path. */
	UPROPERTY(EditAnywhere, Category = "Mini|Gameplay Cues")
	FString CuePath = TEXT("/MiniShooterCore/GameplayCues");

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
	virtual void OnGameFeatureUnregistering() override;

private:
	/** Save the path per activation so an edited asset cannot leak its old path. */
	TMap<FGameFeatureStateChangeContext, FString> ActiveContextPaths;
	static void ReleasePath(const FString& Path);
};
