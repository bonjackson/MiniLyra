#pragma once

#include "AbilitySystem/MiniAbilitySet.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "MiniGameFeatureAction_AddAbilities.generated.h"

class AMiniPlayerState;
struct FComponentRequestHandle;

/** World-scoped GameFeature Action granting one AbilitySet to each MiniPlayerState. */
UCLASS(meta = (DisplayName = "Mini Add Abilities"))
class FPS_API UMiniGameFeatureAction_AddAbilities final : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Mini|Abilities")
	TObjectPtr<UMiniAbilitySet> AbilitySet;

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

	/** Probe uses the same per-source revoke/grant path while retaining the current World. */
	void SetProbeSuspended(UWorld* World, bool bSuspend);

private:
	struct FPerContextData
	{
		TWeakObjectPtr<UWorld> World;
		TSharedPtr<FComponentRequestHandle> ExtensionRequest;
		TMap<TWeakObjectPtr<AMiniPlayerState>, FMiniAbilitySetGrantedHandles> Grants;
		bool bProbeSuspended = false;
	};

	TMap<FGameFeatureStateChangeContext, FPerContextData> ContextData;
	void HandlePlayerStateExtension(AActor* Actor, FName EventName, FGameFeatureStateChangeContext ChangeContext);
	void GrantToPlayerState(AMiniPlayerState* PlayerState, FPerContextData& Data);
	void RevokeFromPlayerState(AMiniPlayerState* PlayerState, FPerContextData& Data);
	void RevokeAll(FPerContextData& Data);
};
