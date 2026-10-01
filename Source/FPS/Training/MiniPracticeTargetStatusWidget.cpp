#include "MiniPracticeTargetStatusWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Training/MiniPracticeTarget.h"

#define LOCTEXT_NAMESPACE "MiniPracticeTargetStatus"

void UMiniPracticeTargetStatusWidget::NativeOnInitialized()
{
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.01f, 0.02f, 0.03f, 0.85f));
	Backdrop->SetPadding(FMargin(12.0f));
	Backdrop->SetHorizontalAlignment(HAlign_Center);
	Backdrop->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Backdrop;
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetFontSize(28.0f);
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetText(LOCTEXT("SyncTarget", "训练靶同步中…"));
	Backdrop->SetContent(StatusText);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Super::NativeOnInitialized();
}

void UMiniPracticeTargetStatusWidget::SetTargetState(const FText& Label, const FMiniPracticeTargetState& State,
	float ResetDelay, const FLinearColor& Color)
{
	DisplayColor = Color;
	if (!StatusText) { return; }
	StatusText->SetColorAndOpacity(FSlateColor(Color));
	if (State.Revision == 0)
	{
		StatusText->SetText(LOCTEXT("SyncTarget", "训练靶同步中…"));
		return;
	}
	StatusText->SetText(FText::Format(LOCTEXT("TargetStatus", "{0}\n{1} / {2}{3}"),
		Label, FText::AsNumber(FMath::RoundToInt(State.Health)), FText::AsNumber(FMath::RoundToInt(State.MaxHealth)),
		State.bEnabled ? FText::GetEmpty() : FText::Format(LOCTEXT("ResetSuffix", "  复位中（{0} 秒）"), FText::AsNumber(ResetDelay))));
}

FText UMiniPracticeTargetStatusWidget::GetDisplayText() const
{
	return StatusText ? StatusText->GetText() : FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
