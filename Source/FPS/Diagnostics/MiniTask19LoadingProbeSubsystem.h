#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask19LoadingProbeSubsystem.generated.h"

/** Real Experience loaded/failed UI acceptance. Opt-in and never created in shipping. */
UCLASS()
class FPS_API UMiniTask19LoadingProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void Fail(const TCHAR* Reason);
	bool CaptureTerminalUI(const TCHAR* StateName, const TCHAR* NetModeName);

	float WaitSeconds = 0.0f;
	bool bObservedLoadingBlock = false;
	bool bScreenshotRequested = false;
	bool bPassed = false;
	bool bFailed = false;
	FString ScreenshotPath;
};
