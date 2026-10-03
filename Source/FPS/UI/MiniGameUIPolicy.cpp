#include "MiniGameUIPolicy.h"

#include "CommonLocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "Widgets/SViewport.h"

void UMiniGameUIPolicy::OnRootLayoutAddedToViewport(UCommonLocalPlayer* LocalPlayer, UPrimaryGameLayout* Layout)
{
	Super::OnRootLayoutAddedToViewport(LocalPlayer, Layout);
	UMiniPrimaryGameLayout* MiniLayout = Cast<UMiniPrimaryGameLayout>(Layout);
	if (!LocalPlayer || !MiniLayout)
	{
		return;
	}
	// Ordinary travel removes the old focused widget. Establish a focus path
	// for this player's viewport when none survives; CommonUI still owns the
	// active screen's input configuration and desired focus target.
	if (LocalPlayer->IsPrimaryPlayer() && FSlateApplication::IsInitialized() && LocalPlayer->ViewportClient)
	{
		const int32 UserIndex = LocalPlayer->GetGameInstance()->GetLocalPlayers().Find(LocalPlayer);
		const TSharedPtr<SViewport> Viewport = LocalPlayer->ViewportClient->GetGameViewportWidget();
		FSlateApplication& Slate = FSlateApplication::Get();
		if (UserIndex != INDEX_NONE && Viewport.IsValid() && !Slate.GetUserFocusedWidget(UserIndex).IsValid())
		{
			Slate.SetUserFocus(UserIndex, Viewport);
		}
	}
	MiniLayout->HandleViewportAdded();
	if (MiniLayout->IsLayoutReady())
	{
		const TWeakObjectPtr<UMiniPrimaryGameLayout>* Previous = AvailableLayouts.Find(LocalPlayer);
		if (!Previous || Previous->Get() != MiniLayout)
		{
			AvailableLayouts.Add(LocalPlayer, MiniLayout);
			OnRootLayoutReady.Broadcast(LocalPlayer, MiniLayout);
		}
	}
}

void UMiniGameUIPolicy::NotifyRootLayoutUnavailable(UCommonLocalPlayer* LocalPlayer,
	UMiniPrimaryGameLayout* Layout)
{
	const TWeakObjectPtr<UMiniPrimaryGameLayout>* Current = AvailableLayouts.Find(LocalPlayer);
	if (Current && Current->Get() == Layout)
	{
		// Remove before broadcasting: an action may reenter root cleanup.
		AvailableLayouts.Remove(LocalPlayer);
		OnRootLayoutUnavailable.Broadcast(LocalPlayer, Layout);
	}
}

void UMiniGameUIPolicy::OnRootLayoutRemovedFromViewport(UCommonLocalPlayer* LocalPlayer,
	UPrimaryGameLayout* Layout)
{
	if (UMiniPrimaryGameLayout* MiniLayout = Cast<UMiniPrimaryGameLayout>(Layout))
	{
		NotifyRootLayoutUnavailable(LocalPlayer, MiniLayout);
		MiniLayout->HandleViewportRemoved();
	}
	Super::OnRootLayoutRemovedFromViewport(LocalPlayer, Layout);
}

void UMiniGameUIPolicy::OnRootLayoutReleased(UCommonLocalPlayer* LocalPlayer, UPrimaryGameLayout* Layout)
{
	if (UMiniPrimaryGameLayout* MiniLayout = Cast<UMiniPrimaryGameLayout>(Layout))
	{
		NotifyRootLayoutUnavailable(LocalPlayer, MiniLayout);
		MiniLayout->HandleViewportRemoved();
	}
	Super::OnRootLayoutReleased(LocalPlayer, Layout);
}

void UMiniGameUIPolicy::ShutdownLayouts()
{
	const auto LayoutsToRemove = AvailableLayouts;
	for (const auto& Pair : LayoutsToRemove)
	{
		UCommonLocalPlayer* LocalPlayer = Pair.Key.Get();
		UMiniPrimaryGameLayout* Layout = Pair.Value.Get();
		if (Layout)
		{
			NotifyRootLayoutUnavailable(LocalPlayer, Layout);
			Layout->HandleViewportRemoved();
			// Clearing child stacks does not remove the root's Slate widget.
			// Use CommonGame's viewport path while the LocalPlayer still exists;
			// its subsequent unavailable callbacks are deliberately idempotent.
			if (LocalPlayer) { RemoveLayoutFromViewport(LocalPlayer, Layout); }
			else { Layout->RemoveFromParent(); }
		}
	}
	AvailableLayouts.Empty();
}
