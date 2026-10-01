#include "MiniHUDLayout.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "UI/MiniHUDMessages.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

void UMiniHUDLayout::NativeOnInitialized()
{
	ViewModel = NewObject<UMiniHUDViewModel>(this);
	BuildHUDLayout();
	Super::NativeOnInitialized();
}

void UMiniHUDLayout::BuildHUDLayout()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("HUDCanvas"));
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Canvas;
	const auto AddSlot = [this, Canvas](FGameplayTag Tag, FName Name,
		FVector2D Anchor, FVector2D Alignment, FVector2D Position, FVector2D Size)
	{
		UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), Name);
		Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Panel);
		Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		Slot->SetAlignment(Alignment);
		Slot->SetPosition(Position);
		Slot->SetSize(Size);
		if (Tag == MiniHUDTags::CrosshairSlot)
		{
			// Center the actual marker bounds on the same screen point as the
			// camera ray, rather than the top of a fixed-height panel.
			Slot->SetAutoSize(true);
		}
		SlotPanels.Add(Tag, Panel);
	};
	AddSlot(MiniHUDTags::HealthSlot, TEXT("HealthSlot"), FVector2D(0.0f, 1.0f),
		FVector2D(0.0f, 1.0f), FVector2D(24.0f, -24.0f), FVector2D(430.0f, 60.0f));
	AddSlot(MiniHUDTags::AmmoSlot, TEXT("AmmoSlot"), FVector2D(1.0f, 1.0f),
		FVector2D(1.0f, 1.0f), FVector2D(-24.0f, -24.0f), FVector2D(430.0f, 60.0f));
	AddSlot(MiniHUDTags::CrosshairSlot, TEXT("CrosshairSlot"), FVector2D(0.5f, 0.5f),
		FVector2D(0.5f, 0.5f), FVector2D::ZeroVector, FVector2D(120.0f, 60.0f));
	AddSlot(MiniHUDTags::MatchSlot, TEXT("MatchSlot"), FVector2D(0.5f, 0.0f),
		FVector2D(0.5f, 0.0f), FVector2D(0.0f, 24.0f), FVector2D(640.0f, 150.0f));
}

void UMiniHUDLayout::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (!IsActivated() || bHUDActive || bClearingUI || IsDesignTime() || !ViewModel)
	{
		return;
	}
	bHUDActive = true;
	++GameplayUIGeneration;
	ViewModel->Start(GetOwningLocalPlayer(), GetWorld());
	RegisterExtensionPoints();
}

void UMiniHUDLayout::RegisterExtensionPoints()
{
	UWorld* World = GetWorld();
	UUIExtensionSubsystem* Subsystem = World ? World->GetSubsystem<UUIExtensionSubsystem>() : nullptr;
	if (!bHUDActive || bClearingUI || !Subsystem || !GetOwningLocalPlayer() || !ExtensionPointHandles.IsEmpty())
	{
		return;
	}
	TArray<UClass*> AllowedClasses;
	AllowedClasses.Add(UMiniHUDDataWidget::StaticClass());
	const uint32 RegistrationGeneration = GameplayUIGeneration;
	for (const auto& Pair : SlotPanels)
	{
		// Registration synchronously constructs already registered extensions.
		// Their callbacks may deactivate this HUD before a handle is returned.
		FUIExtensionPointHandle Handle = Subsystem->RegisterExtensionPointForContext(Pair.Key,
			GetOwningLocalPlayer(), EUIExtensionPointMatch::ExactMatch, AllowedClasses,
			FExtendExtensionPointDelegate::CreateUObject(this, &ThisClass::HandleExtension, Pair.Key, RegistrationGeneration));
		if (!bHUDActive || bClearingUI || RegistrationGeneration != GameplayUIGeneration)
		{
			if (Handle.IsValid()) { Handle.Unregister(); }
			return;
		}
		if (Handle.IsValid()) { ExtensionPointHandles.Add(Handle); }
	}
}

void UMiniHUDLayout::HandleExtension(EUIExtensionAction Action, const FUIExtensionRequest& Request,
	FGameplayTag SlotTag, uint32 RegistrationGeneration)
{
	if (!bHUDActive || bClearingUI || RegistrationGeneration != GameplayUIGeneration ||
		Request.ContextObject != GetOwningLocalPlayer())
	{
		return;
	}
	if (Action == EUIExtensionAction::Removed)
	{
		if (UMiniHUDDataWidget* Widget = ExtensionWidgets.FindRef(Request.ExtensionHandle))
		{
			// Detach ownership before destruct/Blueprint callbacks can reenter.
			ExtensionWidgets.Remove(Request.ExtensionHandle);
			Widget->StopListening();
			Widget->SetViewModel(nullptr);
			Widget->RemoveFromParent();
		}
		return;
	}
	if (ExtensionWidgets.Contains(Request.ExtensionHandle))
	{
		return;
	}
	UVerticalBox* Panel = SlotPanels.FindRef(SlotTag);
	UClass* WidgetClass = Cast<UClass>(Request.Data);
	if (!Panel || !WidgetClass || !WidgetClass->IsChildOf<UMiniHUDDataWidget>() || WidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return;
	}
	if (UMiniHUDDataWidget* Widget = CreateWidget<UMiniHUDDataWidget>(GetOwningPlayer(), WidgetClass))
	{
		const auto IsCurrentRegistration = [this, RegistrationGeneration]()
		{
			return bHUDActive && !bClearingUI && RegistrationGeneration == GameplayUIGeneration;
		};
		const auto DiscardWidget = [this, Widget, &Request]()
		{
			// A newer activation may own a different widget for this extension.
			if (ExtensionWidgets.FindRef(Request.ExtensionHandle) == Widget)
			{
				ExtensionWidgets.Remove(Request.ExtensionHandle);
			}
			Widget->StopListening();
			Widget->SetViewModel(nullptr);
			Widget->RemoveFromParent();
		};
		if (!IsCurrentRegistration())
		{
			DiscardWidget();
			return;
		}
		// Configure before adding: NativeConstruct sees the correct view model.
		Widget->SetViewModel(ViewModel);
		if (!IsCurrentRegistration())
		{
			DiscardWidget();
			return;
		}
		ExtensionWidgets.Add(Request.ExtensionHandle, Widget);
		Panel->AddChildToVerticalBox(Widget);
		if (!IsCurrentRegistration())
		{
			DiscardWidget();
			return;
		}
		Widget->StartListening();
		if (!IsCurrentRegistration()) { DiscardWidget(); }
	}
}

void UMiniHUDLayout::GetExtensionWidgets(TArray<UMiniHUDDataWidget*>& OutWidgets) const
{
	OutWidgets.Reset();
	for (const auto& Pair : ExtensionWidgets)
	{
		if (Pair.Value) { OutWidgets.Add(Pair.Value); }
	}
}

UMiniDebugMenuWidget* UMiniHUDLayout::OpenDebugMenu()
{
	if (!bHUDActive || !IsActivated()) { return nullptr; }
	if (UMiniDebugMenuWidget* Existing = DebugMenu.Get())
	{
		if (Existing->IsActivated()) { return Existing; }
	}
	UMiniPrimaryGameLayout* Root = Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(GetOwningLocalPlayer()));
	if (!Root || !Root->IsLayoutReady()) { return nullptr; }
	const uint32 MenuGeneration = GameplayUIGeneration;
	UMiniDebugMenuWidget* Menu = Root->PushWidgetToLayerStack<UMiniDebugMenuWidget>(
		UMiniPrimaryGameLayout::GetMenuLayerTag(), UMiniDebugMenuWidget::StaticClass());
	if (!bHUDActive || bClearingUI || !IsActivated() || MenuGeneration != GameplayUIGeneration || !Root->IsLayoutReady())
	{
		if (Menu) { Menu->DeactivateWidget(); Root->FindAndRemoveWidgetFromLayer(Menu); }
		return nullptr;
	}
	DebugMenu = Menu;
	DebugMenuRoot = Root;
	return Menu;
}

void UMiniHUDLayout::CloseDebugMenu()
{
	UMiniDebugMenuWidget* Menu = DebugMenu.Get();
	UMiniPrimaryGameLayout* Root = DebugMenuRoot.Get();
	// Release ownership before deactivation/destruct callbacks can reenter.
	DebugMenu.Reset();
	DebugMenuRoot.Reset();
	if (Menu)
	{
		Menu->DeactivateWidget();
		if (Root)
		{
			if (UCommonActivatableWidgetContainerBase* Layer = Root->GetLayerWidget(UMiniPrimaryGameLayout::GetMenuLayerTag()))
			{
				Layer->RemoveWidget(*Menu);
			}
		}
	}
}

void UMiniHUDLayout::ClearGameplayUI()
{
	if (bClearingUI) { return; }
	TGuardValue<bool> Guard(bClearingUI, true);
	bHUDActive = false;
	++GameplayUIGeneration;
	CloseDebugMenu();
	for (FUIExtensionPointHandle& Handle : ExtensionPointHandles) { Handle.Unregister(); }
	ExtensionPointHandles.Empty();
	// Explicit listener cleanup before panel removal/pooling. Tests may keep the
	// widget objects alive and must still observe zero active message listeners.
	for (const auto& Pair : ExtensionWidgets)
	{
		if (UMiniHUDDataWidget* Widget = Pair.Value)
		{
			Widget->StopListening();
			Widget->SetViewModel(nullptr);
			Widget->RemoveFromParent();
		}
	}
	ExtensionWidgets.Empty();
	for (const auto& Pair : SlotPanels) { if (Pair.Value) { Pair.Value->ClearChildren(); } }
	if (ViewModel) { ViewModel->Stop(); }
}

void UMiniHUDLayout::NativeOnDeactivated()
{
	ClearGameplayUI();
	Super::NativeOnDeactivated();
}

void UMiniHUDLayout::NativeDestruct()
{
	ClearGameplayUI();
	Super::NativeDestruct();
}
