#include "MiniPrimaryGameLayout.h"

#include "Blueprint/WidgetTree.h"
#include "CommonLocalPlayer.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "GameUIManagerSubsystem.h"
#include "NativeGameplayTags.h"
#include "UI/MiniActivatableWidget.h"
#include "UI/MiniGameUIPolicy.h"
#include "UI/MiniLoadingStatusWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Mini_UI_Layer_Game, "UI.Layer.Game");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Mini_UI_Layer_Menu, "UI.Layer.Menu");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Mini_UI_Layer_Modal, "UI.Layer.Modal");

namespace
{
void AddFullScreenChild(UCanvasPanel* Canvas, UWidget* Child, int32 ZOrder)
{
	UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
	Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	Slot->SetOffsets(FMargin(0.0f));
	Slot->SetZOrder(ZOrder);
}
}

FGameplayTag UMiniPrimaryGameLayout::GetGameLayerTag() { return TAG_Mini_UI_Layer_Game; }
FGameplayTag UMiniPrimaryGameLayout::GetMenuLayerTag() { return TAG_Mini_UI_Layer_Menu; }
FGameplayTag UMiniPrimaryGameLayout::GetModalLayerTag() { return TAG_Mini_UI_Layer_Modal; }

void UMiniPrimaryGameLayout::NativeOnInitialized()
{
	BuildNativeLayout();
	Super::NativeOnInitialized();
}

void UMiniPrimaryGameLayout::BuildNativeLayout()
{
	if (!WidgetTree || GameLayer)
	{
		return;
	}
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Canvas;
	GameLayer = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
		UCommonActivatableWidgetStack::StaticClass(), TEXT("GameLayer"));
	MenuLayer = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
		UCommonActivatableWidgetStack::StaticClass(), TEXT("MenuLayer"));
	ModalLayer = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(
		UCommonActivatableWidgetStack::StaticClass(), TEXT("ModalLayer"));
	LoadingStatus = WidgetTree->ConstructWidget<UMiniLoadingStatusWidget>(
		UMiniLoadingStatusWidget::StaticClass(), TEXT("LoadingStatus"));
	AddFullScreenChild(Canvas, GameLayer, 0);
	AddFullScreenChild(Canvas, MenuLayer, 100);
	AddFullScreenChild(Canvas, ModalLayer, 200);
	AddFullScreenChild(Canvas, LoadingStatus, 50);
	RegisterLayer(GetGameLayerTag(), GameLayer);
	RegisterLayer(GetMenuLayerTag(), MenuLayer);
	RegisterLayer(GetModalLayerTag(), ModalLayer);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UMiniPrimaryGameLayout::HandleViewportAdded()
{
	if (bLayoutReady || !GameLayer || !MenuLayer || !ModalLayer || !GetCachedWidget().IsValid())
	{
		return;
	}
	bLayoutReady = true;
	// Preserve a Game input configuration even after the gameplay HUD is revoked.
	// This is foundation input routing, not a gameplay plugin contribution.
	GameInputFallback = GameLayer->AddWidget<UMiniActivatableWidget>(UMiniActivatableWidget::StaticClass());
	if (LoadingStatus)
	{
		LoadingStatus->StartMonitoring(GetWorld());
	}
	RefreshInputBlock();
}

void UMiniPrimaryGameLayout::ClearLayer(UCommonActivatableWidgetStack* Layer)
{
	if (!Layer)
	{
		return;
	}
	TArray<TWeakObjectPtr<UCommonActivatableWidget>> WidgetsToRemove;
	for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
	{
		WidgetsToRemove.Add(Widget);
	}
	for (const TWeakObjectPtr<UCommonActivatableWidget>& Entry : WidgetsToRemove)
	{
		if (UCommonActivatableWidget* Widget = Entry.Get())
		{
			Widget->DeactivateWidget();
			Layer->RemoveWidget(*Widget);
		}
	}
	Layer->ClearWidgets();
}

void UMiniPrimaryGameLayout::HandleViewportRemoved()
{
	if (bRemovingFromViewport)
	{
		return;
	}
	TGuardValue<bool> RemovalGuard(bRemovingFromViewport, true);
	bLayoutReady = false;
	if (LoadingStatus)
	{
		LoadingStatus->StopMonitoring();
	}
	ClearLayer(ModalLayer);
	ClearLayer(MenuLayer);
	ClearLayer(GameLayer);
	GameInputFallback = nullptr;
	InputBlockSources.Empty();
	RefreshInputBlock();
}

void UMiniPrimaryGameLayout::NativeDestruct()
{
	// CommonGame's removed-from-viewport callback happens after RemoveFromParent.
	// Notify actions here before child stacks are cleared; the policy deduplicates.
	UCommonLocalPlayer* LocalPlayer = GetOwningLocalPlayer<UCommonLocalPlayer>();
	UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	UGameUIManagerSubsystem* UIManager = GameInstance ? GameInstance->GetSubsystem<UGameUIManagerSubsystem>() : nullptr;
	if (UMiniGameUIPolicy* Policy = UIManager ? Cast<UMiniGameUIPolicy>(UIManager->GetCurrentUIPolicy()) : nullptr)
	{
		Policy->NotifyRootLayoutUnavailable(LocalPlayer, this);
	}
	HandleViewportRemoved();
	Super::NativeDestruct();
}

void UMiniPrimaryGameLayout::SetGameplayInputBlockedForSource(UObject* Source, bool bBlocked)
{
	if (!Source)
	{
		return;
	}
	if (bBlocked && bLayoutReady && !bRemovingFromViewport)
	{
		InputBlockSources.Add(Source);
	}
	else
	{
		InputBlockSources.Remove(Source);
	}
	RefreshInputBlock();
}

void UMiniPrimaryGameLayout::RefreshInputBlock()
{
	for (auto It = InputBlockSources.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}
	const bool bBlocked = bLayoutReady && !InputBlockSources.IsEmpty();
	if (bGameplayInputBlockedByUI != bBlocked)
	{
		bGameplayInputBlockedByUI = bBlocked;
		OnGameplayInputBlockChanged.Broadcast(this, bBlocked);
	}
}
