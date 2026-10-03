#include "MiniFrontEndWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "System/MiniTravelSubsystem.h"

#define LOCTEXT_NAMESPACE "MiniFrontEnd"

UMiniFrontEndWidget::UMiniFrontEndWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputMode = EMiniWidgetInputMode::Menu;
	bIsBackHandler = false;
	bAutoRestoreFocus = true;
}

void UMiniFrontEndWidget::NativeOnInitialized()
{
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FrontEndBackground"));
	Background->SetBrushColor(FLinearColor(0.012f, 0.018f, 0.03f, 1.0f));
	Background->SetPadding(FMargin(36.0f));
	Background->SetHorizontalAlignment(HAlign_Center);
	Background->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Background;
	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FrontEndWidth"));
	Width->SetWidthOverride(500.0f);
	Background->SetContent(Width);
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FrontEndContent"));
	Width->SetContent(Content);
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FrontEndTitle"));
	TitleText->SetText(LOCTEXT("Title", "MINI 竞技场"));
	TitleText->SetFontSize(40.0f);
	TitleText->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	UTextBlock* Subtitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FrontEndSubtitle"));
	Subtitle->SetText(LOCTEXT("Subtitle", "本地训练 · 2–4 人竞技场"));
	Subtitle->SetFontSize(17.0f);
	Subtitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.72f, 0.84f)));
	Subtitle->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(Subtitle)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 30.0f));
	const auto AddButton = [this, Content](FName Name, const FText& Label)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*(Name.ToString() + TEXT("Label"))));
		Text->SetText(Label);
		Text->SetFontSize(22.0f);
		Text->SetJustification(ETextJustify::Center);
		Button->SetContent(Text);
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
			FName(*(Name.ToString() + TEXT("Size"))));
		Size->SetHeightOverride(52.0f);
		Size->SetContent(Button);
		Content->AddChildToVerticalBox(Size)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		return Button;
	};
	PracticeButton = AddButton(TEXT("PracticeButton"), LOCTEXT("Practice", "进入训练"));
	HostArenaButton = AddButton(TEXT("HostArenaButton"), LOCTEXT("Host", "创建竞技场"));
	UTextBlock* AddressLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AddressLabel"));
	AddressLabel->SetText(LOCTEXT("AddressLabel", "主机地址"));
	AddressLabel->SetFontSize(17.0f);
	Content->AddChildToVerticalBox(AddressLabel)->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 8.0f));
	AddressEntry = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("AddressEntry"));
	// CommonUI transfers focus to the connection modal after the commit callback.
	// Clearing it here would remove the viewport focus path before that transfer.
	AddressEntry->SetClearKeyboardFocusOnCommit(false);
	AddressEntry->SetText(FText::FromString(TEXT("127.0.0.1:7777")));
	AddressEntry->SetHintText(LOCTEXT("AddressHint", "IPv4 地址，可选端口，如 192.168.1.20:7777"));
	AddressEntry->SetSelectAllTextWhenFocused(true);
	Content->AddChildToVerticalBox(AddressEntry)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	JoinButton = AddButton(TEXT("JoinButton"), LOCTEXT("Join", "加入竞技场"));
	UTextBlock* Help = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AddressHelp"));
	Help->SetText(LOCTEXT("AddressHelp", "与朋友游玩时，请输入主机的 IPv4 地址。"));
	Help->SetFontSize(15.0f);
	Help->SetAutoWrapText(true);
	Help->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.72f, 0.84f)));
	Content->AddChildToVerticalBox(Help)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FrontEndStatus"));
	StatusText->SetText(LOCTEXT("Ready", "选择模式开始游戏"));
	StatusText->SetFontSize(16.0f);
	StatusText->SetAutoWrapText(true);
	StatusText->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	QuitButton = AddButton(TEXT("QuitButton"), LOCTEXT("Quit", "退出游戏"));
	Super::NativeOnInitialized();
}

void UMiniFrontEndWidget::NativeConstruct()
{
	Super::NativeConstruct();
	PracticeButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandlePracticeClicked);
	HostArenaButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleHostClicked);
	JoinButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleJoinClicked);
	QuitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleQuitClicked);
	AddressEntry->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::HandleAddressCommitted);
	StartListening();
}

void UMiniFrontEndWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (IsActivated()) { StartListening(); }
}

void UMiniFrontEndWidget::NativeOnDeactivated()
{
	StopListening();
	Super::NativeOnDeactivated();
}

void UMiniFrontEndWidget::NativeDestruct()
{
	StopListening();
	PracticeButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePracticeClicked);
	HostArenaButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleHostClicked);
	JoinButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleJoinClicked);
	QuitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleQuitClicked);
	AddressEntry->OnTextCommitted.RemoveDynamic(this, &ThisClass::HandleAddressCommitted);
	Super::NativeDestruct();
}

void UMiniFrontEndWidget::StartListening()
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

void UMiniFrontEndWidget::StopListening()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get()) { Travel->OnStateChanged.Remove(TravelStateHandle); }
	TravelStateHandle.Reset();
	TravelSubsystem.Reset();
}

void UMiniFrontEndWidget::HandleTravelState(const FMiniTravelState& State)
{
	for (UButton* Button : { PracticeButton.Get(), HostArenaButton.Get(), JoinButton.Get(), QuitButton.Get() })
	{
		if (Button) { Button->SetIsEnabled(!State.bBusy); }
	}
	if (AddressEntry) { AddressEntry->SetIsEnabled(!State.bBusy); }
	if (StatusText)
	{
		StatusText->SetText(State.StatusText.IsEmpty() ? LOCTEXT("Ready", "选择模式开始游戏") : State.StatusText);
		StatusText->SetColorAndOpacity(FSlateColor(State.bHasError ?
			FLinearColor(1.0f, 0.55f, 0.45f) : FLinearColor(0.72f, 0.82f, 0.92f)));
	}
}

UButton* UMiniFrontEndWidget::GetButton(FName Name) const
{
	if (Name == TEXT("PracticeButton")) { return PracticeButton; }
	if (Name == TEXT("HostArenaButton")) { return HostArenaButton; }
	if (Name == TEXT("JoinButton")) { return JoinButton; }
	if (Name == TEXT("QuitButton")) { return QuitButton; }
	return nullptr;
}

FText UMiniFrontEndWidget::GetTitleText() const { return TitleText ? TitleText->GetText() : FText::GetEmpty(); }
FText UMiniFrontEndWidget::GetDisplayText() const { return StatusText ? StatusText->GetText() : FText::GetEmpty(); }
UWidget* UMiniFrontEndWidget::NativeGetDesiredFocusTarget() const { return PracticeButton; }

void UMiniFrontEndWidget::HandlePracticeClicked()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get()) { if (!Travel->GetState().bBusy) { Travel->StartPractice(); } }
}
void UMiniFrontEndWidget::HandleHostClicked()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get()) { if (!Travel->GetState().bBusy) { Travel->HostArena(); } }
}
void UMiniFrontEndWidget::HandleJoinClicked()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get())
	{
		if (!Travel->GetState().bBusy && AddressEntry) { Travel->JoinAddress(AddressEntry->GetText().ToString()); }
	}
}
void UMiniFrontEndWidget::HandleQuitClicked()
{
	if (UMiniTravelSubsystem* Travel = TravelSubsystem.Get()) { if (!Travel->GetState().bBusy) { Travel->QuitGame(); } }
}
void UMiniFrontEndWidget::HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter) { HandleJoinClicked(); }
}

#undef LOCTEXT_NAMESPACE
