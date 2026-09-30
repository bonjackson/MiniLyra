#pragma once

#include "CommonActivatableWidget.h"
#include "MiniActivatableWidget.generated.h"

class UMiniPrimaryGameLayout;

UENUM(BlueprintType)
enum class EMiniWidgetInputMode : uint8
{
	Default,
	Game,
	GameAndMenu,
	Menu
};

/** CommonUI input configuration and a source-scoped gameplay input gate. */
UCLASS(Blueprintable)
class FPS_API UMiniActivatableWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UMiniActivatableWidget(const FObjectInitializer& ObjectInitializer);
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Input")
	EMiniWidgetInputMode InputMode = EMiniWidgetInputMode::Game;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Input")
	EMouseCaptureMode GameMouseCaptureMode = EMouseCaptureMode::CapturePermanently;

	/** Menu mode always blocks gameplay; other modes can opt in. */
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Input")
	bool bBlocksGameplayInput = false;

private:
	void ReleaseGameplayInputGate();
	TWeakObjectPtr<UMiniPrimaryGameLayout> InputGateLayout;
};
