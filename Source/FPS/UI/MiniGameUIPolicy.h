#pragma once

#include "GameUIPolicy.h"
#include "MiniGameUIPolicy.generated.h"

class UMiniPrimaryGameLayout;

DECLARE_MULTICAST_DELEGATE_TwoParams(FMiniRootLayoutReady,
	UCommonLocalPlayer* /*LocalPlayer*/, UMiniPrimaryGameLayout* /*Layout*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FMiniRootLayoutUnavailable,
	UCommonLocalPlayer* /*LocalPlayer*/, UMiniPrimaryGameLayout* /*Layout*/);

/** The root remains owned by CommonGame; gameplay actions own their contributions. */
UCLASS(Blueprintable)
class FPS_API UMiniGameUIPolicy : public UGameUIPolicy
{
	GENERATED_BODY()

public:
	FMiniRootLayoutReady OnRootLayoutReady;
	FMiniRootLayoutUnavailable OnRootLayoutUnavailable;

	/** NativeDestruct calls this before clearing child stacks. Idempotent. */
	void NotifyRootLayoutUnavailable(UCommonLocalPlayer* LocalPlayer, UMiniPrimaryGameLayout* Layout);
	/** Call before UIManagerSubsystem::Deinitialize switches away from this policy. */
	void ShutdownLayouts();

protected:
	virtual void OnRootLayoutAddedToViewport(UCommonLocalPlayer* LocalPlayer, UPrimaryGameLayout* Layout) override;
	virtual void OnRootLayoutRemovedFromViewport(UCommonLocalPlayer* LocalPlayer, UPrimaryGameLayout* Layout) override;
	virtual void OnRootLayoutReleased(UCommonLocalPlayer* LocalPlayer, UPrimaryGameLayout* Layout) override;

private:
	TMap<TWeakObjectPtr<UCommonLocalPlayer>, TWeakObjectPtr<UMiniPrimaryGameLayout>> AvailableLayouts;
};
