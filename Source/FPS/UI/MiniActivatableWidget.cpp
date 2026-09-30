#include "MiniActivatableWidget.h"

#include "Input/UIActionBindingHandle.h"
#include "UI/MiniPrimaryGameLayout.h"

UMiniActivatableWidget::UMiniActivatableWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The stack controls activation. Children can receive pointer input while the
	// screen itself does not swallow otherwise empty parts of the viewport.
	bAutoActivate = false;
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

TOptional<FUIInputConfig> UMiniActivatableWidget::GetDesiredInputConfig() const
{
	switch (InputMode)
	{
	case EMiniWidgetInputMode::Game:
		return FUIInputConfig(ECommonInputMode::Game, GameMouseCaptureMode);
	case EMiniWidgetInputMode::GameAndMenu:
		return FUIInputConfig(ECommonInputMode::All, GameMouseCaptureMode);
	case EMiniWidgetInputMode::Menu:
		return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false);
	case EMiniWidgetInputMode::Default:
	default:
		return Super::GetDesiredInputConfig();
	}
}

void UMiniActivatableWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	ReleaseGameplayInputGate();
	// Blueprint activation callbacks can deactivate synchronously.
	if (!IsActivated()) { return; }
	if (InputMode == EMiniWidgetInputMode::Menu || bBlocksGameplayInput)
	{
		if (UMiniPrimaryGameLayout* Layout = Cast<UMiniPrimaryGameLayout>(
			UPrimaryGameLayout::GetPrimaryGameLayout(GetOwningLocalPlayer())))
		{
			InputGateLayout = Layout;
			Layout->SetGameplayInputBlockedForSource(this, true);
		}
	}
}

void UMiniActivatableWidget::NativeOnDeactivated()
{
	ReleaseGameplayInputGate();
	Super::NativeOnDeactivated();
}

void UMiniActivatableWidget::NativeDestruct()
{
	// CommonUI pools inactive widgets. Release bindings on deactivation/destruct,
	// without waiting for UObject destruction or garbage collection.
	ReleaseGameplayInputGate();
	Super::NativeDestruct();
}

void UMiniActivatableWidget::ReleaseGameplayInputGate()
{
	if (UMiniPrimaryGameLayout* Layout = InputGateLayout.Get())
	{
		Layout->SetGameplayInputBlockedForSource(this, false);
	}
	InputGateLayout.Reset();
}
