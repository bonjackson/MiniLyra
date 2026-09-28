#pragma once

#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "MiniGameFeatureAction_AddInput.generated.h"

class AMiniCharacter;
class UInputMappingContext;
class ULocalPlayer;
class UMiniHeroComponent;
struct FComponentRequestHandle;

/** Adds one World-scoped input mapping and its Pawn bindings to local players. */
UCLASS(meta = (DisplayName = "Mini Add Input"))
class FPS_API UMiniGameFeatureAction_AddInput final : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Mini|Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

	/** Probe runs the same remove/rebind path as deactivation for one World. */
	void SetProbeSuspended(UWorld* World, bool bSuspend);

private:
	struct FPerContextData
	{
		TWeakObjectPtr<UWorld> World;
		TSharedPtr<FComponentRequestHandle> ExtensionRequest;
		TMap<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<UMiniHeroComponent>> Owners;
		bool bProbeSuspended = false;
	};

	TMap<FGameFeatureStateChangeContext, FPerContextData> ContextData;
	void HandlePawnExtension(AActor* Actor, FName EventName, FGameFeatureStateChangeContext ChangeContext);
	void BindToPawn(AMiniCharacter* Pawn, FPerContextData& Data);
	void RemoveFromPawn(AMiniCharacter* Pawn, FPerContextData& Data);
	void RemoveAll(FPerContextData& Data);
};
