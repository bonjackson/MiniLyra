#include "MiniTask19LoadingProbeSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"
#include "System/MiniTravelSubsystem.h"
#include "UI/MiniConnectionStatusWidget.h"
#include "Components/Button.h"
#include "UI/MiniLoadingStatusWidget.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"

bool UMiniTask19LoadingProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask19Loading"));
#else
	return false;
#endif
}

TStatId UMiniTask19LoadingProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask19LoadingProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask19LoadingProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask19Loading FAIL: Reason=%s"), Reason);
	}
}

bool UMiniTask19LoadingProbeSubsystem::CaptureTerminalUI(const TCHAR* StateName, const TCHAR* NetModeName)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask19LoadingMedia"))) { return true; }
	if (!bScreenshotRequested)
	{
		ScreenshotPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("Screenshots"), FString::Printf(TEXT("Task19-Loading-%s-%s.png"), StateName, NetModeName)));
		// The script also clears these exact files before launch. A direct probe
		// run must not accept a previous screenshot as evidence of this request.
		IFileManager::Get().Delete(*ScreenshotPath, false, true);
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
		bScreenshotRequested = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Loading SCREENSHOT_REQUESTED: NetMode=%s State=%s ShowUI=1 Path=%s"),
			NetModeName, StateName, *ScreenshotPath);
		return false;
	}
	// Observe completion of the actual render; do not substitute a fixed delay.
	return IFileManager::Get().FileSize(*ScreenshotPath) > 1024;
}

void UMiniTask19LoadingProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (bPassed || bFailed || !World || !World->HasBegunPlay()) { return; }
	const ENetMode NetMode = World->GetNetMode();
	if (NetMode != NM_ListenServer && NetMode != NM_Client) { return; }
	const TCHAR* NetModeName = NetMode == NM_ListenServer ? TEXT("ListenServer") : TEXT("Client");
	WaitSeconds += DeltaTime;
	if (WaitSeconds > 120.0f)
	{
		Fail(TEXT("actual local loading UI did not reach its expected terminal state within 120 game seconds"));
		return;
	}
	AMiniGameState* GameState = World->GetGameState<AMiniGameState>();
	UMiniExperienceManagerComponent* Manager = GameState ? GameState->GetExperienceManagerComponent() : nullptr;
	AMiniPlayerController* Controller = nullptr;
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
	{
		if (It->IsLocalController() && It->GetLocalPlayer())
		{
			if (Controller) { Fail(TEXT("probe requires one local player in each process")); return; }
			Controller = *It;
		}
	}
	UMiniPrimaryGameLayout* Root = Controller ? Cast<UMiniPrimaryGameLayout>(
		UPrimaryGameLayout::GetPrimaryGameLayout(Controller->GetLocalPlayer())) : nullptr;
	UMiniLoadingStatusWidget* Loading = Root ? Root->GetLoadingStatusWidget() : nullptr;
	if (!Manager || !Controller || !Root || !Root->IsLayoutReady() || !Root->IsInViewport() ||
		Root->GetWorld() != World || !Loading || Loading->GetWorld() != World || !Loading->GetCachedWidget().IsValid())
	{
		return;
	}
	const EMiniExperienceLoadState State = Manager->GetLoadState();
	const bool bExpectedFailure = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeInvalidExperience"));
	if (State != EMiniExperienceLoadState::Loaded && State != EMiniExperienceLoadState::Failed)
	{
		if (!bObservedLoadingBlock && Loading->GetVisibility() == ESlateVisibility::Visible &&
			!Loading->HasFailure() && !Loading->GetStatusTitle().IsEmpty() && Loading->HasActiveStatusTicker() &&
			Root->IsGameplayInputBlockedByUI() && Root->GetGameplayInputBlockCount() == 1 && Controller->IsMiniInputBlocked())
		{
			bObservedLoadingBlock = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Loading PENDING: NetMode=%s Visible=1 Failure=0 UIBlocks=1 ControllerBlocked=1 TickerActive=1"),
				NetModeName);
		}
		return;
	}
	if ((State == EMiniExperienceLoadState::Failed) != bExpectedFailure)
	{
		Fail(TEXT("manager reached the wrong terminal state for this scenario"));
		return;
	}
	const FString ExperienceId = Manager->GetCurrentExperienceId().ToString();
	if (!bExpectedFailure)
	{
		if (ExperienceId != TEXT("MiniExperienceDefinition:DA_MiniPracticeExperience"))
		{
			Fail(TEXT("valid scenario did not select the real practice Experience"));
			return;
		}
		// The core ticker can consume the manager transition after the world's
		// tick. Keep observing readiness until the actual widget catches up.
		if (Loading->GetVisibility() != ESlateVisibility::Collapsed || Loading->HasFailure() ||
			!Loading->GetStatusTitle().IsEmpty() || !Loading->GetStatusDetail().IsEmpty() || Loading->HasActiveStatusTicker() ||
			Root->IsGameplayInputBlockedByUI() || Root->GetGameplayInputBlockCount() != 0 || Controller->IsMiniInputBlocked())
		{
			return;
		}
		if (!CaptureTerminalUI(TEXT("Loaded"), NetModeName)) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Loading PASS: NetMode=%s State=Loaded Visible=0 Failure=0 TitleEmpty=1 DetailEmpty=1 UIBlocks=0 ControllerBlocked=0 TickerStopped=1 PendingObserved=%d Experience=%s"),
			NetModeName, bObservedLoadingBlock, *ExperienceId);
	}
	else
	{
		const FString Reason = Manager->GetFailureReason();
		if (ExperienceId != TEXT("MiniExperienceDefinition:DA_MiniDefinitelyMissing") || !Reason.Contains(TEXT("Unknown Experience ID")))
		{
			Fail(TEXT("negative scenario did not use the real unknown Experience failure"));
			return;
		}
		const UMiniTravelSubsystem* Travel = World->GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
		const UMiniConnectionStatusWidget* Modal = Travel ? Travel->GetConnectionStatusWidget() : nullptr;
		UButton* ReturnButton = Modal ? Modal->GetButton(TEXT("ReturnButton")) : nullptr;
		if (Loading->GetVisibility() != ESlateVisibility::Visible || !Loading->HasFailure() ||
			Loading->GetStatusTitle().ToString() != TEXT("玩法加载失败") || Loading->GetStatusDetail().ToString() != Reason ||
			Loading->HasActiveStatusTicker() || !Root->IsGameplayInputBlockedByUI() ||
			Root->GetGameplayInputBlockCount() != 2 || !Controller->IsMiniInputBlocked() ||
			!Travel || !Travel->GetState().bHasError || Travel->GetState().FailureCode != TEXT("MINI_EXPERIENCE_FAILED") ||
			!Modal || !Modal->IsActivated() || Modal->GetWorld() != World || Modal->GetStateListenerCount() != 1 ||
			Modal->GetTitleText().ToString() != TEXT("玩法加载失败") ||
			Modal->GetDisplayText().ToString() != TEXT("未能载入玩法资源，请返回主菜单后重试。若问题持续，请重新启动游戏。") ||
			Modal->GetDisplayText().ToString() != Travel->GetState().DetailText.ToString() ||
			!ReturnButton || !ReturnButton->GetIsEnabled() || ReturnButton->GetVisibility() != ESlateVisibility::Visible)
		{
			return;
		}
		if (!CaptureTerminalUI(TEXT("Failed"), NetModeName)) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Loading PASS: NetMode=%s State=Failed Visible=1 Failure=1 TitlePresent=1 DetailMatches=1 UIBlocks=2 ControllerBlocked=1 TickerStopped=1 PendingObserved=%d Experience=%s Reason=%s RecoveryModal=1 ReturnButton=1 ModalListener=1"),
			NetModeName, bObservedLoadingBlock, *ExperienceId, *Reason);
	}
	bPassed = true;
}
