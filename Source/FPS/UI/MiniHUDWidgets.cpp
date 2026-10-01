#include "MiniHUDWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "UI/MiniHUDViewModel.h"
#include "System/MiniGameplayTags.h"

#define LOCTEXT_NAMESPACE "MiniHUDWidgets"

void UMiniHUDDataWidget::NativeOnInitialized()
{
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetFontSize(22.0f);
	StatusText->SetShadowColorAndOpacity(FLinearColor::Black);
	StatusText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	WidgetTree->RootWidget = StatusText;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Super::NativeOnInitialized();
}

void UMiniHUDDataWidget::SetViewModel(UMiniHUDViewModel* Model)
{
	if (ViewModel != Model)
	{
		StopListening();
		ViewModel = Model;
	}
	if (ViewModel)
	{
		DisplayedSnapshot = ViewModel->GetSnapshot();
		RenderSnapshot(DisplayedSnapshot);
	}
}

void UMiniHUDDataWidget::NativeConstruct()
{
	Super::NativeConstruct();
	StartListening();
}

void UMiniHUDDataWidget::NativeDestruct()
{
	StopListening();
	Super::NativeDestruct();
}

void UMiniHUDDataWidget::StartListening()
{
	if (bListening || !ViewModel || !ViewModel->IsRunning() || !UGameplayMessageSubsystem::HasInstance(this))
	{
		return;
	}
	// Late-created widgets need actual state even if no future change ever occurs.
	DisplayedSnapshot = ViewModel->GetSnapshot();
	if (!MatchesContext(DisplayedSnapshot.LocalPlayer, DisplayedSnapshot.World, ViewModel))
	{
		DisplayedSnapshot = FMiniHUDSnapshot();
		return;
	}
	RenderSnapshot(DisplayedSnapshot);
	UGameplayMessageSubsystem& Router = UGameplayMessageSubsystem::Get(this);
	StateMessageHandle = Router.RegisterListener<FMiniHUDStateMessage>(MiniHUDTags::StateChanged, this, &ThisClass::HandleStateMessage);
	if (WantsHitMessages())
	{
		HitMessageHandle = Router.RegisterListener<FMiniHUDHitMessage>(MiniHUDTags::HitConfirmed, this, &ThisClass::HandleHitMessage);
	}
	if (WantsEmptyMessages())
	{
		EmptyMessageHandle = Router.RegisterListener<FMiniHUDEmptyMessage>(MiniHUDTags::EmptyMagazine, this, &ThisClass::HandleEmptyMessage);
	}
	bListening = true;
}

void UMiniHUDDataWidget::StopListening()
{
	bListening = false;
	StateMessageHandle.Unregister();
	HitMessageHandle.Unregister();
	EmptyMessageHandle.Unregister();
	// Handles whose subsystem has already disappeared must also be reset.
	StateMessageHandle = FGameplayMessageListenerHandle();
	HitMessageHandle = FGameplayMessageListenerHandle();
	EmptyMessageHandle = FGameplayMessageListenerHandle();
	OnStoppedListening();
	DisplayedSnapshot = FMiniHUDSnapshot();
}

int32 UMiniHUDDataWidget::GetMessageListenerCount() const
{
	return (StateMessageHandle.IsValid() ? 1 : 0) + (HitMessageHandle.IsValid() ? 1 : 0) +
		(EmptyMessageHandle.IsValid() ? 1 : 0);
}

FText UMiniHUDDataWidget::GetDisplayText() const { return StatusText ? StatusText->GetText() : FText::GetEmpty(); }

bool UMiniHUDDataWidget::MatchesContext(ULocalPlayer* LocalPlayer, UWorld* World, UMiniHUDViewModel* Source) const
{
	return Source == ViewModel && LocalPlayer == GetOwningLocalPlayer() && World == GetWorld() &&
		ViewModel && ViewModel->GetWorld() == World;
}

void UMiniHUDDataWidget::HandleStateMessage(FGameplayTag Channel, const FMiniHUDStateMessage& Message)
{
	if (bListening && MatchesContext(Message.Snapshot.LocalPlayer, Message.Snapshot.World, Message.Source))
	{
		DisplayedSnapshot = Message.Snapshot;
		RenderSnapshot(DisplayedSnapshot);
	}
}

void UMiniHUDDataWidget::HandleHitMessage(FGameplayTag Channel, const FMiniHUDHitMessage& Message)
{
	if (bListening && MatchesContext(Message.LocalPlayer, Message.World, Message.Source) &&
		Message.Pawn == DisplayedSnapshot.Pawn)
	{
		ReceiveHit(Message);
	}
}

void UMiniHUDDataWidget::HandleEmptyMessage(FGameplayTag Channel, const FMiniHUDEmptyMessage& Message)
{
	if (bListening && MatchesContext(Message.LocalPlayer, Message.World, Message.Source) &&
		Message.ItemId == DisplayedSnapshot.ItemId)
	{
		ReceiveEmpty(Message);
	}
}

void UMiniHUDHealthWidget::RenderSnapshot(const FMiniHUDSnapshot& State)
{
	if (!GetTextBlock()) { return; }
	GetTextBlock()->SetText(!State.bHealthReady ? LOCTEXT("HealthSync", "血量同步中…") :
		FText::Format(LOCTEXT("HealthFormat", "生命  {0} / {1}{2}"),
			FText::AsNumber(FMath::RoundToInt(State.Health)), FText::AsNumber(FMath::RoundToInt(State.MaxHealth)),
			State.bDead ? LOCTEXT("DeadSuffix", "  已阵亡") : FText::GetEmpty()));
	GetTextBlock()->SetColorAndOpacity(FSlateColor(State.bDead ? FLinearColor(1.0f, 0.3f, 0.25f) : FLinearColor::White));
}

void UMiniHUDAmmoWidget::RenderSnapshot(const FMiniHUDSnapshot& State)
{
	if (!GetTextBlock()) { return; }
	if (State.bReloading || State.bDead)
	{
		PendingEmptyItemId.Invalidate();
		EmptyItemId.Invalidate();
	}
	if (PendingEmptyItemId.IsValid() && State.ItemId != PendingEmptyItemId)
	{
		PendingEmptyItemId.Invalidate();
	}
	if (EmptyItemId.IsValid() && State.ItemId != EmptyItemId)
	{
		EmptyItemId.Invalidate();
	}
	if (PendingEmptyItemId.IsValid() && State.bAmmoReady)
	{
		if (State.MagazineAmmo == 0)
		{
			EmptyItemId = PendingEmptyItemId;
			PendingEmptyItemId.Invalidate();
		}
		else if (State.MagazineAmmo > MagazineAtEmptyNotice || State.ReserveAmmo < ReserveAtEmptyNotice)
		{
			// A refill can coalesce the zero and reload tag on this connection.
			// Falling positive ammo is still older firing replication, so it must
			// not consume the pending notice before the authoritative zero arrives.
			PendingEmptyItemId.Invalidate();
		}
	}
	if (EmptyItemId.IsValid() && State.bAmmoReady && State.MagazineAmmo > 0)
	{
		EmptyItemId.Invalidate();
	}
	if (!State.bAmmoReady)
	{
		GetTextBlock()->SetText(LOCTEXT("AmmoSync", "装备同步中…"));
		return;
	}
	const FText Extra = State.bReloading ? LOCTEXT("Reloading", "  装填中") :
		(EmptyItemId.IsValid() ? LOCTEXT("EmptyMagazine", "  弹匣为空") : FText::GetEmpty());
	GetTextBlock()->SetText(FText::Format(LOCTEXT("AmmoFormat", "{0}  {1} / {2}{3}"),
		State.WeaponName, FText::AsNumber(State.MagazineAmmo), FText::AsNumber(State.ReserveAmmo), Extra));
}

void UMiniHUDAmmoWidget::ReceiveEmpty(const FMiniHUDEmptyMessage& Message)
{
	const FMiniHUDSnapshot& State = GetDisplayedSnapshot();
	if (!Message.ItemId.IsValid() || State.bReloading || State.bDead) { return; }
	PendingEmptyItemId = Message.ItemId;
	EmptyItemId.Invalidate();
	MagazineAtEmptyNotice = State.MagazineAmmo;
	ReserveAtEmptyNotice = State.ReserveAmmo;
	// The Pawn owner RPC may precede private Controller ammo replication. Keep
	// this notice pending through older positive snapshots until zero is observed.
	RenderSnapshot(State);
}
void UMiniHUDAmmoWidget::OnStoppedListening()
{
	PendingEmptyItemId.Invalidate();
	EmptyItemId.Invalidate();
	MagazineAtEmptyNotice = 0;
	ReserveAtEmptyNotice = 0;
}

void UMiniHUDCrosshairWidget::RenderSnapshot(const FMiniHUDSnapshot& State)
{
	if (!GetTextBlock()) { return; }
	SetVisibility(State.bDead || !State.bHealthReady ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	GetTextBlock()->SetFontSize(36.0f);
	GetTextBlock()->SetJustification(ETextJustify::Center);
	GetTextBlock()->SetText(bHitMarkerVisible ? LOCTEXT("HitCrosshair", "×") : LOCTEXT("Crosshair", "+"));
	GetTextBlock()->SetColorAndOpacity(FSlateColor(!bHitMarkerVisible ? FLinearColor::White :
		(bKillMarker ? FLinearColor(1.0f, 0.3f, 0.25f) : FLinearColor(1.0f, 0.86f, 0.25f))));
}

void UMiniHUDCrosshairWidget::ReceiveHit(const FMiniHUDHitMessage& Message)
{
	++DisplayedHitCount;
	bHitMarkerVisible = true;
	bKillMarker = Message.bKilled;
	RenderSnapshot(GetDisplayedSnapshot());
	if (UWorld* World = GetWorld())
	{
		HitMarkerWorld = World;
		// A visual duration, not a readiness delay or client-side hit inference.
		World->GetTimerManager().SetTimer(HitMarkerTimer, this, &ThisClass::ClearHitMarker, 0.18f, false);
	}
}

void UMiniHUDCrosshairWidget::ClearHitMarker()
{
	bHitMarkerVisible = false;
	bKillMarker = false;
	RenderSnapshot(GetDisplayedSnapshot());
}

void UMiniHUDCrosshairWidget::OnStoppedListening()
{
	if (UWorld* World = HitMarkerWorld.Get()) { World->GetTimerManager().ClearTimer(HitMarkerTimer); }
	HitMarkerTimer.Invalidate();
	HitMarkerWorld.Reset();
	bHitMarkerVisible = false;
	bKillMarker = false;
}

void UMiniHUDMatchWidget::RenderSnapshot(const FMiniHUDSnapshot& State)
{
	if (!GetTextBlock()) { return; }
	GetTextBlock()->SetFontSize(17.0f);
	GetTextBlock()->SetJustification(ETextJustify::Center);
	if (State.bHasMatchData)
	{
		const FMiniMatchState& Match = State.MatchState;
		FText Status;
		const bool bWaiting = State.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Warmup;
		if (bWaiting)
		{
			Status = FText::Format(LOCTEXT("WaitingForPlayers", "等待玩家  {0} / {1}"),
				FText::AsNumber(Match.ConnectedPlayerCount), FText::AsNumber(Match.MinPlayers));
		}
		else if (Match.bHasResult)
		{
			if (Match.EndReason == EMiniMatchEndReason::InsufficientPlayers)
			{
				Status = LOCTEXT("InsufficientPlayers", "玩家不足，本局结束");
			}
			else if (Match.bIsDraw) { Status = LOCTEXT("MatchDraw", "本局并列"); }
			else
			{
				FString Winners;
				for (const FMiniMatchPlayerRow& Row : Match.ResultRows)
				{
					if (Match.WinnerPlayerIds.Contains(Row.PlayerId))
					{
						if (!Winners.IsEmpty()) { Winners += TEXT("、"); }
						Winners += Row.DisplayName;
					}
				}
				Status = FText::Format(LOCTEXT("MatchWinner", "胜者：{0}"), FText::FromString(Winners));
			}
		}
		else if (!Match.bAcceptingScores)
		{
			Status = FText::Format(LOCTEXT("WaitingForPlayers", "等待玩家  {0} / {1}"),
				FText::AsNumber(Match.ConnectedPlayerCount), FText::AsNumber(Match.MinPlayers));
		}
		else
		{
			Status = FText::Format(LOCTEXT("FFARound", "第 {0} 局   目标 {1} 击杀   剩余 {2} 秒"),
				FText::AsNumber(Match.RoundId), FText::AsNumber(Match.ScoreLimit),
				FText::AsNumber(FMath::Max(0, State.PhaseRemainingSeconds)));
		}
		FString Board = Status.ToString();
		const TArray<FMiniMatchPlayerRow>& Rows = Match.bHasResult && !bWaiting ? Match.ResultRows : Match.Rows;
		for (const FMiniMatchPlayerRow& Row : Rows)
		{
			Board += TEXT("\n") + FText::Format(LOCTEXT("ScoreboardRow", "{0}   击杀 {1}   死亡 {2}{3}"),
				FText::FromString(Row.DisplayName), FText::AsNumber(Row.Kills), FText::AsNumber(Row.Deaths),
				Row.bConnected ? FText::GetEmpty() : LOCTEXT("PlayerLeft", "  已离开")).ToString();
		}
		GetTextBlock()->SetText(FText::FromString(Board));
		return;
	}
	if (State.bHasPhaseData && !State.bHasMatchData)
	{
		FText PhaseName = LOCTEXT("PhaseEnded", "阶段结束");
		if (State.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Warmup) { PhaseName = LOCTEXT("PhaseWarmup", "准备"); }
		else if (State.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Playing) { PhaseName = LOCTEXT("PhasePlaying", "进行中"); }
		else if (State.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_PostMatch) { PhaseName = LOCTEXT("PhasePostMatch", "结束"); }
		const FText Time = State.PhaseRemainingSeconds < 0 ? LOCTEXT("NoPhaseDeadline", "—") : FText::AsNumber(State.PhaseRemainingSeconds);
		GetTextBlock()->SetText(FText::Format(LOCTEXT("PhaseFormat", "竞技场   {0}   时间 {1}"), PhaseName, Time));
		return;
	}
	GetTextBlock()->SetText(LOCTEXT("PracticeMatch", "训练模式   比分 —   时间 —"));
}

UMiniDebugMenuWidget::UMiniDebugMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InputMode = EMiniWidgetInputMode::Menu;
	// Escape is handled below until the front-end supplies a CommonUI Back action.
	bIsBackHandler = false;
	bAutoRestoreFocus = true;
}

void UMiniDebugMenuWidget::NativeOnInitialized()
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuPanel"));
	Panel->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.035f, 0.94f));
	Panel->SetPadding(FMargin(48.0f));
	Panel->SetHorizontalAlignment(HAlign_Center);
	Panel->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Panel;
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuContent"));
	Panel->SetContent(Content);
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MenuTitle"));
	Title->SetText(LOCTEXT("MenuTitle", "菜单"));
	Title->SetFontSize(28.0f);
	Title->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 24.0f));
	ContinueButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ContinueButton"));
	UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ContinueLabel"));
	ButtonText->SetText(LOCTEXT("Continue", "继续游戏"));
	ButtonText->SetFontSize(22.0f);
	ButtonText->SetJustification(ETextJustify::Center);
	ContinueButton->SetContent(ButtonText);
	Content->AddChildToVerticalBox(ContinueButton);
	Super::NativeOnInitialized();
}

void UMiniDebugMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ContinueButton) { ContinueButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCloseClicked); }
}

void UMiniDebugMenuWidget::NativeDestruct()
{
	if (ContinueButton) { ContinueButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked); }
	Super::NativeDestruct();
}

UWidget* UMiniDebugMenuWidget::NativeGetDesiredFocusTarget() const { return ContinueButton; }
void UMiniDebugMenuWidget::HandleCloseClicked() { DeactivateWidget(); }

FReply UMiniDebugMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		DeactivateWidget();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
