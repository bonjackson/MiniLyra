#include "MiniLoadingStatusWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameState.h"
#include "UI/MiniPrimaryGameLayout.h"

#define LOCTEXT_NAMESPACE "MiniLoadingStatus"

void UMiniLoadingStatusWidget::NativeOnInitialized()
{
	StatusPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StatusPanel"));
	StatusPanel->SetPadding(FMargin(40.0f));
	StatusPanel->SetHorizontalAlignment(HAlign_Center);
	StatusPanel->SetVerticalAlignment(VAlign_Center);
	StatusPanel->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.035f, 0.94f));
	WidgetTree->RootWidget = StatusPanel;
	UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StatusTexts"));
	StatusPanel->SetContent(Texts);
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	DetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DetailText"));
	TitleText->SetFontSize(26.0f);
	TitleText->SetJustification(ETextJustify::Center);
	DetailText->SetFontSize(15.0f);
	DetailText->SetAutoWrapText(true);
	DetailText->SetJustification(ETextJustify::Center);
	Texts->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	Texts->AddChildToVerticalBox(DetailText);
	SetStatus(LOCTEXT("StartingTitle", "正在准备对局"), FText::GetEmpty(), false, false);
	Super::NativeOnInitialized();
}

void UMiniLoadingStatusWidget::StartMonitoring(UWorld* World)
{
	if (!World || !World->IsGameWorld())
	{
		StopMonitoring();
		return;
	}
	if (!bMonitoring || BoundWorld.Get() != World)
	{
		StopMonitoring();
		bMonitoring = true;
		BoundWorld = World;
		InputGateLayout = GetTypedOuter<UMiniPrimaryGameLayout>();
		GameStateSetHandle = World->GameStateSetEvent.AddUObject(this, &ThisClass::HandleGameStateSet);
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleWorldCleanup);
	}
	BindCurrentGameState();
	if (RefreshStatus())
	{
		EnsureStatusTicker();
	}
}

void UMiniLoadingStatusWidget::StopMonitoring()
{
	bMonitoring = false;
	if (UWorld* World = BoundWorld.Get())
	{
		World->GameStateSetEvent.Remove(GameStateSetHandle);
	}
	GameStateSetHandle.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	WorldCleanupHandle.Reset();
	FTSTicker::GetCoreTicker().RemoveTicker(StatusTickerHandle);
	StatusTickerHandle.Reset();
	if (UMiniPrimaryGameLayout* Layout = InputGateLayout.Get())
	{
		Layout->SetGameplayInputBlockedForSource(this, false);
	}
	InputGateLayout.Reset();
	ExperienceManager.Reset();
	BoundWorld.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UMiniLoadingStatusWidget::NativeDestruct()
{
	StopMonitoring();
	Super::NativeDestruct();
}

void UMiniLoadingStatusWidget::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World == BoundWorld.Get())
	{
		StopMonitoring();
	}
}

void UMiniLoadingStatusWidget::BindCurrentGameState()
{
	const UWorld* World = BoundWorld.Get();
	AMiniGameState* GameState = World ? World->GetGameState<AMiniGameState>() : nullptr;
	ExperienceManager = GameState ? GameState->GetExperienceManagerComponent() : nullptr;
}

void UMiniLoadingStatusWidget::HandleGameStateSet(AGameStateBase* GameState)
{
	BindCurrentGameState();
	if (bMonitoring && RefreshStatus())
	{
		EnsureStatusTicker();
	}
}

void UMiniLoadingStatusWidget::EnsureStatusTicker()
{
	if (!StatusTickerHandle.IsValid())
	{
		// Real per-frame status snapshots, not a delay that assumes readiness.
		// The current Experience API exposes no removable load-state delegate.
		// Stop on Loaded/Failed; all World subscriptions still have explicit handles.
		StatusTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::TickStatus), 0.0f);
	}
}

bool UMiniLoadingStatusWidget::TickStatus(float DeltaTime)
{
	if (!bMonitoring || !BoundWorld.IsValid() || !RefreshStatus())
	{
		StatusTickerHandle.Reset();
		return false;
	}
	return true;
}

bool UMiniLoadingStatusWidget::RefreshStatus()
{
	if (!ExperienceManager.IsValid())
	{
		BindCurrentGameState();
	}
	UMiniExperienceManagerComponent* Manager = ExperienceManager.Get();
	if (!Manager)
	{
		SetStatus(LOCTEXT("WaitingGameState", "正在准备对局"),
			LOCTEXT("WaitingGameStateDetail", "等待服务器同步对局状态…"), false, false);
		return true;
	}
	const EMiniExperienceLoadState State = Manager->GetLoadState();
	if (State == EMiniExperienceLoadState::Loaded)
	{
		SetStatus(FText::GetEmpty(), FText::GetEmpty(), false, true);
		return false;
	}
	if (State == EMiniExperienceLoadState::Failed)
	{
		SetStatus(LOCTEXT("FailedTitle", "玩法加载失败"), FText::FromString(Manager->GetFailureReason()), true, false);
		return false;
	}
	FText Detail;
	switch (State)
	{
	case EMiniExperienceLoadState::Unloaded:
		Detail = LOCTEXT("WaitingSelection", "等待服务器选择玩法…");
		break;
	case EMiniExperienceLoadState::LoadingAssets:
		Detail = LOCTEXT("LoadingAssets", "正在载入玩法资源…");
		break;
	case EMiniExperienceLoadState::LoadingFeatures:
		Detail = LOCTEXT("LoadingFeatures", "正在准备玩法功能…");
		break;
	case EMiniExperienceLoadState::LoadingActionResources:
		Detail = LOCTEXT("LoadingActionResources", "正在准备界面与地图资源…");
		break;
	case EMiniExperienceLoadState::ExecutingActions:
		Detail = LOCTEXT("ExecutingActions", "正在初始化玩法…");
		break;
	case EMiniExperienceLoadState::Deactivating:
		Detail = LOCTEXT("Deactivating", "正在退出当前玩法…");
		break;
	default:
		break;
	}
	SetStatus(LOCTEXT("LoadingTitle", "正在加载玩法"), Detail, false, false);
	return true;
}

void UMiniLoadingStatusWidget::SetStatus(const FText& Title, const FText& Detail, bool bFailure, bool bHidden)
{
	if (!StatusTitle.EqualTo(Title))
	{
		StatusTitle = Title;
		if (TitleText)
		{
			TitleText->SetText(Title);
		}
	}
	if (!StatusDetail.EqualTo(Detail))
	{
		StatusDetail = Detail;
		if (DetailText)
		{
			DetailText->SetText(Detail);
		}
	}
	bHasFailure = bFailure;
	if (TitleText)
	{
		TitleText->SetColorAndOpacity(FSlateColor(bFailure ? FLinearColor(1.0f, 0.3f, 0.25f) : FLinearColor::White));
	}
	SetVisibility(bHidden ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (UMiniPrimaryGameLayout* Layout = InputGateLayout.Get())
	{
		Layout->SetGameplayInputBlockedForSource(this, !bHidden && bMonitoring);
	}
}

#undef LOCTEXT_NAMESPACE
