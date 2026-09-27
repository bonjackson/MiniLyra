#pragma once

#include "CommonGameInstance.h"

#include "MiniGameInstance.generated.h"

class AActor;
class UWorld;
struct FComponentRequestHandle;

UCLASS(Config = Game)
class FPS_API UMiniGameInstance : public UCommonGameInstance
{
	GENERATED_BODY()

public:
	UMiniGameInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Init() override;
	virtual void OnStart() override;
	virtual void Shutdown() override;

private:
	void HandleProbeWorldBeginPlay();
	void RunRequestedProbes();
	bool RunReceiverProbe();
	bool RunExperienceProbe();
	void HandleReceiverProbeEvent(AActor* Actor, FName EventName);

	TWeakObjectPtr<UWorld> ProbeWorld;
	FDelegateHandle ProbeWorldBeginPlayHandle;
	TSharedPtr<FComponentRequestHandle> ReceiverProbeHandle;
	bool bProbeReceiversRequested = false;
	bool bProbeExperienceRequested = false;
	bool bProbeReceiverAdded = false;
	bool bProbeGameActorReady = false;
	bool bProbeReceiverRemoved = false;
};
