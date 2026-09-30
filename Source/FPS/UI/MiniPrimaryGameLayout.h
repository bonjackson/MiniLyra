#pragma once

#include "PrimaryGameLayout.h"
#include "MiniPrimaryGameLayout.generated.h"

class UCommonActivatableWidgetStack;
class UMiniActivatableWidget;
class UMiniLoadingStatusWidget;
class UMiniPrimaryGameLayout;

DECLARE_MULTICAST_DELEGATE_TwoParams(FMiniGameplayUIInputBlockChanged,
	UMiniPrimaryGameLayout* /*Layout*/, bool /*bBlocked*/);

/** Native root with permanent Game/Menu/Modal layers and a local loading status. */
UCLASS()
class FPS_API UMiniPrimaryGameLayout : public UPrimaryGameLayout
{
	GENERATED_BODY()

public:
	static FGameplayTag GetGameLayerTag();
	static FGameplayTag GetMenuLayerTag();
	static FGameplayTag GetModalLayerTag();

	bool IsLayoutReady() const { return bLayoutReady; }
	void HandleViewportAdded();
	void HandleViewportRemoved();

	/** Each live menu/loading source owns exactly one block. */
	void SetGameplayInputBlockedForSource(UObject* Source, bool bBlocked);
	bool IsGameplayInputBlockedByUI() const { return bGameplayInputBlockedByUI; }
	int32 GetGameplayInputBlockCount() const { return InputBlockSources.Num(); }
	FMiniGameplayUIInputBlockChanged OnGameplayInputBlockChanged;

	UMiniLoadingStatusWidget* GetLoadingStatusWidget() const { return LoadingStatus; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	void BuildNativeLayout();
	void ClearLayer(UCommonActivatableWidgetStack* Layer);
	void RefreshInputBlock();

	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> GameLayer;
	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> MenuLayer;
	UPROPERTY(Transient)
	TObjectPtr<UCommonActivatableWidgetStack> ModalLayer;
	UPROPERTY(Transient)
	TObjectPtr<UMiniActivatableWidget> GameInputFallback;
	UPROPERTY(Transient)
	TObjectPtr<UMiniLoadingStatusWidget> LoadingStatus;

	TSet<TWeakObjectPtr<UObject>> InputBlockSources;
	bool bLayoutReady = false;
	bool bGameplayInputBlockedByUI = false;
	bool bRemovingFromViewport = false;
};
