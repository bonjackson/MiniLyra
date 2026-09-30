#pragma once

#include "UI/MiniActivatableWidget.h"
#include "UIExtensionSystem.h"
#include "MiniHUDLayout.generated.h"

class UVerticalBox;
class UMiniDebugMenuWidget;
class UMiniHUDDataWidget;
class UMiniHUDViewModel;
class UMiniPrimaryGameLayout;

/** Game-layer layout. Gameplay feature widgets are real LocalPlayer UI extensions. */
UCLASS()
class FPS_API UMiniHUDLayout : public UMiniActivatableWidget
{
	GENERATED_BODY()

public:
	UMiniHUDViewModel* GetViewModel() const { return ViewModel; }
	int32 GetExtensionWidgetCount() const { return ExtensionWidgets.Num(); }
	int32 GetExtensionPointCount() const { return ExtensionPointHandles.Num(); }
	void GetExtensionWidgets(TArray<UMiniHUDDataWidget*>& OutWidgets) const;
	UMiniDebugMenuWidget* OpenDebugMenu();
	void CloseDebugMenu();
	UMiniDebugMenuWidget* GetDebugMenu() const { return DebugMenu.Get(); }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

private:
	void BuildHUDLayout();
	void RegisterExtensionPoints();
	void ClearGameplayUI();
	void HandleExtension(EUIExtensionAction Action, const FUIExtensionRequest& Request,
		FGameplayTag SlotTag, uint32 RegistrationGeneration);

	UPROPERTY(Transient)
	TObjectPtr<UMiniHUDViewModel> ViewModel;
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UVerticalBox>> SlotPanels;
	UPROPERTY(Transient)
	TMap<FUIExtensionHandle, TObjectPtr<UMiniHUDDataWidget>> ExtensionWidgets;
	TArray<FUIExtensionPointHandle> ExtensionPointHandles;
	TWeakObjectPtr<UMiniDebugMenuWidget> DebugMenu;
	TWeakObjectPtr<UMiniPrimaryGameLayout> DebugMenuRoot;
	uint32 GameplayUIGeneration = 0;
	bool bHUDActive = false;
	bool bClearingUI = false;
};
