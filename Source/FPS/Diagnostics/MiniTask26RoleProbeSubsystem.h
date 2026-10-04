#pragma once

#include "Engine/AssetManagerTypes.h"
#include "GameFeatures/MiniRequiredActionResources.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "MiniTask26RoleProbeSubsystem.generated.h"

class UMiniExperienceManagerComponent;
class UMiniExperienceDefinition;
class UMiniGameFeatureAction_AddActors;
class UMiniGameFeatureAction_AddWidgets;

/** Independent opt-in role/replication acceptance. Does not fabricate Manager outcomes. */
UCLASS()
class FPS_API UMiniTask26RoleProbeSubsystem final : public UGameInstanceSubsystem, public FTickableGameObject
{

	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
private:
	bool Claim(UMiniExperienceManagerComponent* Manager, UWorld* World);
	void SelectionPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World, FPrimaryAssetId& Id);
	void ResourcesPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World,
		TArray<FMiniRequiredActionClassRequest>& Requests);
	void Loaded(const UMiniExperienceDefinition* Experience);
	void Failed(const FString& Reason);
	bool NormalReady();
	bool FailureReady();
	bool InjectLateUIFailure();
	bool Media(FName Stage);
	void Pass();
	void Fail(const TCHAR* Reason);
	FString Role;
	FString Scenario;
	FString Peer;
	FString TargetMap;
	FString FinishFile;
	FString InjectFile;
	FString MediaDirectory;
	FString MediaPath;
	FString FailureReason;
	FName MediaStage;
	TSet<FName> MediaDone;
	TWeakObjectPtr<UWorld> ClaimedWorld;
	TWeakObjectPtr<UMiniExperienceManagerComponent> ClaimedManager;
	UPROPERTY(Transient) TArray<TObjectPtr<UMiniGameFeatureAction_AddActors>> ActorActions;
	UPROPERTY(Transient) TObjectPtr<UMiniGameFeatureAction_AddWidgets> SelectedHUDAction;
	FDelegateHandle SelectionHandle;
	FDelegateHandle ResourceHandle;
	double StartedAt = 0;
	double MediaStartedAt = 0;
	int32 TimeoutSeconds = 240;
	int32 Claims = 0;
	int32 LoadedCount = 0;
	int32 FailedCount = 0;
	int32 MutationCount = 0;
	int32 LayoutRequests = 0;
	int32 ElementRequests = 0;
	int32 ActorRequests = 0;
	int32 SelectedPluginUIRequests = 0;
	bool bInitialized = false;
	bool bDone = false;
	bool bResourcesObserved = false;
	bool bReadyEvidence = false;
	bool bFailureEvidence = false;
	bool bLateInjected = false;
};
