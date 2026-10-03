#include "MiniConnectionStatusWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "System/MiniTravelSubsystem.h"

#define LOCTEXT_NAMESPACE "MiniConnectionStatus"

UMiniConnectionStatusWidget::UMiniConnectionStatusWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputMode = EMiniWidgetInputMode::Menu;
	bIsModal = true;
	bIsBackHandler = false;
	bAutoRestoreFocus = true;
}

void UMiniConnectionStatusWidget::NativeOnInitialized()
{
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConnectionBackground"));
	Background->SetBrushColor(FLinearColor(0.005f, 0.01f, 0.018f, 0.92f));
	Background->SetPadding(FMargin(32.0f));
	Background->SetHorizontalAlignment(HAlign_Center);
	Background->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Background;
	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ConnectionWidth"));
	Width->SetWidthOverride(580.0f);
	Background->SetContent(Width);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConnectionPanel"));
	Panel->SetBrushColor(FLinearColor(0.035f, 0.05f, 0.08f, 1.0f));
	Panel->SetPadding(FMargin(36.0f));
	Width->SetContent(Panel);
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConnectionContent"));
	Panel->SetContent(Content);
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ConnectionTitle"));
	TitleText->SetText(LOCTEXT("Connecting", "正在连接"));
	TitleText->SetFontSize(28.0f);
	TitleText->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	DetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ConnectionDetail"));
	DetailText->SetFontSize(17.0f);
	DetailText->SetAutoWrapText(true);
	DetailText->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(DetailText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
	AddressText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ConnectionAddress"));
	AddressText->SetFontSize(16.0f);
	AddressText->SetAutoWrapText(true);
	AddressText->SetJustification(ETextJustify::Center);
	AddressText->SetColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.75f, 0.86f)));
	Content->AddChildToVerticalBox(AddressText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 20.0f));
	const auto AddButton = [this, Content](FName Name, const FText& Label)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Label"))));
		Text->SetText(Label);
		Text->SetFontSize(21.0f);
		Text->SetJustification(ETextJustify::Center);
		Button->SetContent(Text);
		// Padding belongs to the Button so collapsing it leaves no empty row.
		Content->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 6.0f));
		return Button;
	};
	CancelButton = AddButton(TEXT("CancelButton"), LOCTEXT("Cancel", "取消并返回主菜单"));
	CloseButton = AddButton(TEXT("CloseButton"), LOCTEXT("Close", "关闭提示"));
	ReturnButton = AddButton(TEXT("ReturnButton"), LOCTEXT("Return", "返回主菜单"));
	Super::NativeOnInitialized();
}

void UMiniConnectionStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCancelClicked);
	CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseClicked);
	ReturnButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReturnClicked);
	StartListening();
}

void UMiniConnectionStatusWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (IsActivated()) { StartListening(); }
}

void UMiniConnectionStatusWidget::NativeOnDeactivated()
{
	StopListening();
	Super::NativeOnDeactivated();
}

void UMiniConnectionStatusWidget::NativeDestruct()
{
	StopListening();
	CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCancelClicked);
	CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	ReturnButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleReturnClicked);
	Super::NativeDestruct();
}

void UMiniConnectionStatusWidget::StartListening()
{
	UMiniTravelSubsystem* Travel = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>() : nullptr;
	if (TravelSubsystem.Get() != Travel) { StopListening(); }
	if (!Travel) { return; }
	TravelSubsystem = Travel;
	HandleTravelState(Travel->GetState());
	if (!TravelStateHandle.IsValid())
	{
		TravelStateHandle = Travel->OnStateChanged.AddUObject(this, &ThisClass::HandleTravelState);
	}
}

void UMiniConnectionStatusWidget::StopListening()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get()) { Travel->OnStateChanged.Remove(TravelStateHandle); }
	TravelStateHandle.Reset();
	TravelSubsystem.Reset();
}

void UMiniConnectionStatusWidget::HandleTravelState(const FMiniTravelState& State)
{
	UWidget* PreviousFocusTarget = NativeGetDesiredFocusTarget();
	if (TitleText)
	{
		TitleText->SetText(State.StatusText.IsEmpty() ?
			(State.bHasError ? LOCTEXT("Failed", "连接未完成") : LOCTEXT("Connecting", "正在连接")) : State.StatusText);
		TitleText->SetColorAndOpacity(FSlateColor(State.bHasError ? FLinearColor(1.0f, 0.55f, 0.45f) : FLinearColor::White));
	}
	if (DetailText)
	{
		DetailText->SetText(State.DetailText.IsEmpty() ?
			(State.bHasError ? LOCTEXT("ErrorHelp", "请检查主机地址与网络连接后重试。") :
			 LOCTEXT("ProgressHelp", "正在准备场景，完成后将自动进入。")) : State.DetailText);
	}
	if (AddressText)
	{
		AddressText->SetText(FText::Format(LOCTEXT("Target", "主机地址：{0}"), FText::FromString(State.TargetAddress)));
		AddressText->SetVisibility(State.TargetAddress.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	CancelButton->SetVisibility(State.bBusy ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	CancelButton->SetIsEnabled(State.bBusy && State.Operation != EMiniTravelOperation::ReturnToFrontEnd &&
		State.Operation != EMiniTravelOperation::Quit);
	CloseButton->SetVisibility(!State.bBusy && State.bHasError ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ReturnButton->SetVisibility(!State.bBusy && State.bHasError ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	UWidget* CurrentFocusTarget = NativeGetDesiredFocusTarget();
	if (PreviousFocusTarget != CurrentFocusTarget)
	{
		// A pooled/reused modal can replace Cancel with the error actions without
		// reactivating. Drop the old restoration target before CommonUI refocuses.
		ClearFocusRestorationTarget();
		if (IsActivated() && CurrentFocusTarget) { RequestRefreshFocus(); }
	}
}

UButton* UMiniConnectionStatusWidget::GetButton(FName Name) const
{
	if (Name == TEXT("CancelButton")) { return CancelButton; }
	if (Name == TEXT("CloseButton")) { return CloseButton; }
	if (Name == TEXT("ReturnButton")) { return ReturnButton; }
	return nullptr;
}
FText UMiniConnectionStatusWidget::GetTitleText() const { return TitleText ? TitleText->GetText() : FText::GetEmpty(); }
FText UMiniConnectionStatusWidget::GetDisplayText() const { return DetailText ? DetailText->GetText() : FText::GetEmpty(); }
UWidget* UMiniConnectionStatusWidget::NativeGetDesiredFocusTarget() const
{
	for (UButton* Button : { CancelButton.Get(), CloseButton.Get(), ReturnButton.Get() })
	{
		if (Button && Button->GetVisibility() == ESlateVisibility::Visible && Button->GetIsEnabled()) { return Button; }
	}
	return nullptr;
}

void UMiniConnectionStatusWidget::HandleCancelClicked()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get())
	{
		const FMiniTravelState& State = Travel->GetState();
		if (State.bBusy && State.Operation != EMiniTravelOperation::ReturnToFrontEnd &&
			State.Operation != EMiniTravelOperation::Quit) { Travel->ReturnToFrontEnd(); }
	}
}
void UMiniConnectionStatusWidget::HandleCloseClicked()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get())
	{
		if (!Travel->GetState().bBusy && Travel->GetState().bHasError) { Travel->DismissError(); }
	}
}
void UMiniConnectionStatusWidget::HandleReturnClicked()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get())
	{
		if (!Travel->GetState().bBusy && Travel->GetState().bHasError) { Travel->ReturnToFrontEnd(); }
	}
}

FReply UMiniConnectionStatusWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get())
		{
			if (Travel->GetState().bBusy) { HandleCancelClicked(); }
			else { HandleCloseClicked(); }
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
