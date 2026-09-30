#pragma once

#include "Diagnostics/MiniTask19ProbeActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniTask19ProbeSubsystem.generated.h"

class AMiniCharacter;
class AMiniPlayerController;
class UMiniDebugMenuWidget;
class UMiniHUDDataWidget;
class UMiniHUDLayout;
class UMiniHUDViewModel;
class UMiniPrimaryGameLayout;

/** Task 19 three-process UI/data/feature-lifecycle acceptance; opt-in, never shipping. */
UCLASS()
class FPS_API UMiniTask19ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	bool StartServer();
	bool ApplyHostAuthorityChange();
	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	void Advance(EMiniTask19Phase Value);
	bool AllAcknowledged() const;
	void Acknowledge(AMiniTask19ProbeActor* Actor, const TCHAR* Marker);
	void Fail(const TCHAR* Reason);
	void RetainUI(UMiniHUDLayout* Layout);
	bool RetainedUIStopped() const;
	bool RetainedWidgetsStopped() const;
	void BeginFeatureDeactivation();
	bool RecreateRootAfterFeatureOff(AMiniPlayerController* Controller);
	bool CaptureCheckpoint(int32 Owner, const TCHAR* View);

	EMiniTask19Phase Phase = EMiniTask19Phase::Initial;
	EMiniTask19Phase ClientPhase = EMiniTask19Phase::Complete;
	float StageSeconds = 0.0f;
	bool bStarted = false;
	bool bStageAction = false;
	bool bFailed = false;
	bool bClientAcknowledged = false;
	bool bKeyReleased = false;
	bool bFeatureRequested = false;
	bool bFeatureDeactivated = false;
	bool bFeatureOffRootReleased = false;
	bool bFeatureOffRootRecreated = false;
	float FeatureOffRootRecreatedAt = 0.0f;
	bool bScreenshotRequested = false;
	float ScreenshotRequestedAt = 0.0f;
	TWeakObjectPtr<AMiniPlayerController> Controllers[2];
	TWeakObjectPtr<AMiniCharacter> Pawns[2];
	TWeakObjectPtr<AMiniTask19ProbeActor> Probes[2];
	TWeakObjectPtr<AMiniPlayerController> HostController;
	TWeakObjectPtr<AMiniCharacter> PreviousPawn;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDLayout> RetainedLayout;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDViewModel> RetainedViewModel;
	UPROPERTY(Transient) TArray<TObjectPtr<UMiniHUDDataWidget>> RetainedWidgets;
	UPROPERTY(Transient) TObjectPtr<UMiniPrimaryGameLayout> RetainedRoot;
	UPROPERTY(Transient) TObjectPtr<UMiniDebugMenuWidget> RetainedMenu;
	int32 RetainedRefreshCount = 0;
};
