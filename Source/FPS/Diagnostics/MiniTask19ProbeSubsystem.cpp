#include "MiniTask19ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Camera/MiniCameraComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "CommonLocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "GameFeatures/MiniGameFeatureAction_AddWidgets.h"
#include "GameFeaturesSubsystem.h"
#include "GameModes/MiniGameMode.h"
#include "GameUIManagerSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Weapons/MiniRangedWeaponComponent.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace
{
UMiniPrimaryGameLayout* Task19RootFor(AMiniPlayerController* PC)
{
	return PC ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
}

UMiniHUDLayout* Task19HUDFor(AMiniPlayerController* PC)
{
	UMiniPrimaryGameLayout* Root = Task19RootFor(PC);
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	UMiniHUDLayout* Result = nullptr;
	if (Layer)
	{
		for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
		{
			if (UMiniHUDLayout* HUD = Cast<UMiniHUDLayout>(Widget))
			{
				if (Result) { return nullptr; } // duplicates cannot accidentally pass.
				Result = HUD;
			}
		}
	}
	return Result;
}

int32 Task19HUDCount(AMiniPlayerController* PC)
{
	UMiniPrimaryGameLayout* Root = Task19RootFor(PC);
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	int32 Count = 0;
	if (Layer)
	{
		for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
		{
			Count += Cast<UMiniHUDLayout>(Widget) ? 1 : 0;
		}
	}
	return Count;
}

UMiniGameFeatureAction_AddWidgets* Task19WidgetAction()
{
	for (TObjectIterator<UMiniGameFeatureAction_AddWidgets> It; It; ++It)
	{
		if (!It->IsTemplate() && It->GetFName() == TEXT("MiniTask19_AddWidgets")) { return *It; }
	}
	return nullptr;
}

UMiniAbilitySystemComponent* Task19ASCFor(AMiniCharacter* Pawn)
{
	AMiniPlayerState* PS = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
}

UMiniInventoryItemInstance* Task19ItemFor(AMiniPlayerController* PC, int32 Slot)
{
	return PC && PC->GetQuickBar() ? PC->GetQuickBar()->GetSlotItem(Slot) : nullptr;
}

bool Task19AmmoIs(AMiniPlayerController* PC, int32 Slot, int32 Magazine, int32 Reserve)
{
	UMiniInventoryItemInstance* Item = Task19ItemFor(PC, Slot);
	return Item && Item->GetStat(MiniInventoryTags::AmmoInMagazine) == Magazine &&
		Item->GetStat(MiniInventoryTags::ReserveAmmo) == Reserve;
}

float Task19HealthFor(AMiniCharacter* Pawn)
{
	AMiniPlayerState* PS = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return PS && PS->GetHealthSet() ? PS->GetHealthSet()->GetHealth() : -1.0f;
}

bool Task19SnapshotIs(AMiniPlayerController* PC, float Health, int32 Slot, int32 Magazine, int32 Reserve)
{
	UMiniHUDLayout* Layout = Task19HUDFor(PC);
	UMiniHUDViewModel* VM = Layout ? Layout->GetViewModel() : nullptr;
	AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	UMiniInventoryItemInstance* Item = Task19ItemFor(PC, Slot);
	if (!VM || !VM->IsRunning() || VM->GetBindingCount() <= 0 || !Pawn || !Item ||
		Layout->GetExtensionPointCount() != 4 || Layout->GetExtensionWidgetCount() != 4)
	{
		return false;
	}
	const FMiniHUDSnapshot& S = VM->GetSnapshot();
	if (!S.bHealthReady || !S.bAmmoReady || S.Pawn.Get() != Pawn ||
		S.LocalPlayer.Get() != PC->GetLocalPlayer() || S.World.Get() != PC->GetWorld() ||
		S.ItemId != Item->GetInstanceId() || !FMath::IsNearlyEqual(S.Health, Health, 0.01f) ||
		!FMath::IsNearlyEqual(S.MaxHealth, 100.0f, 0.01f) || S.WeaponName.IsEmpty() ||
		S.ActiveSlot != Slot || S.MagazineAmmo != Magazine || S.ReserveAmmo != Reserve)
	{
		return false;
	}
	TArray<UMiniHUDDataWidget*> Widgets;
	Layout->GetExtensionWidgets(Widgets);
	if (Widgets.Num() != 4) { return false; }
	for (UMiniHUDDataWidget* Widget : Widgets)
	{
		if (!Widget || !Widget->IsListening() || Widget->GetMessageListenerCount() <= 0 ||
			Widget->GetDisplayText().IsEmpty()) { return false; }
		const FMiniHUDSnapshot& W = Widget->GetDisplayedSnapshot();
		if (!W.bHealthReady || !W.bAmmoReady || W.LocalPlayer != S.LocalPlayer || W.World != S.World ||
			W.Pawn.Get() != Pawn || W.ItemId != S.ItemId || W.ActiveSlot != S.ActiveSlot ||
			!FMath::IsNearlyEqual(W.Health, S.Health, 0.01f) || W.MagazineAmmo != S.MagazineAmmo ||
			W.ReserveAmmo != S.ReserveAmmo) { return false; }
	}
	return true;
}

int32 Task19DisplayedHitCount(UMiniHUDLayout* Layout)
{
	TArray<UMiniHUDDataWidget*> Widgets;
	if (Layout) { Layout->GetExtensionWidgets(Widgets); }
	int32 Result = INDEX_NONE;
	for (UMiniHUDDataWidget* Widget : Widgets)
	{
		if (UMiniHUDCrosshairWidget* Crosshair = Cast<UMiniHUDCrosshairWidget>(Widget))
		{
			if (Result != INDEX_NONE) { return INDEX_NONE; }
			Result = Crosshair->GetDisplayedHitCount();
		}
	}
	return Result;
}

void Task19SendKey(AMiniPlayerController* PC, FKey Key, EInputEvent Event)
{
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}

void Task19AimAt(AMiniPlayerController* PC, AMiniCharacter* Pawn, AMiniCharacter* Peer)
{
	if (!PC || !Pawn || !Peer || !Pawn->GetMiniCameraComponent()) { return; }
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		FMinimalViewInfo View;
		Pawn->GetMiniCameraComponent()->ResetCamera();
		Pawn->GetMiniCameraComponent()->GetCameraView(0.0f, View);
		PC->SetControlRotation((Peer->GetActorLocation() + FVector(0, 0, 45) - View.Location).Rotation());
	}
	Pawn->GetMiniCameraComponent()->ResetCamera();
}

bool Task19ContributionsEmpty(UWorld* World)
{
	UMiniGameFeatureAction_AddWidgets* Action = Task19WidgetAction();
	if (!Action) { return false; }
	const FMiniWidgetContributionCounts C = Action->GetProbeContributionCounts(World);
	return C.HUDs == 0 && C.Layouts == 0 && C.Elements == 0 && C.PendingLoads == 0;
}

void Task19CaptureUI(int32 Owner, const TCHAR* View)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask19Media"))) { return; }
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
		TEXT("Screenshots"), FString::Printf(TEXT("Task19-Owner%d-%s.png"), Owner, View)));
	// ShowUI=true is essential: a scene-only screenshot cannot prove a UMG HUD.
	FScreenshotRequest::RequestScreenshot(Path, true, false);
}
}

bool UMiniTask19ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask19"));
#else
	return false;
#endif
}

TStatId UMiniTask19ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask19ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask19ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask19Probe FAIL: Phase=%d Reason=%s"), static_cast<int32>(Phase), Reason);
	}
}

void UMiniTask19ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || !GetWorld() || !GetWorld()->HasBegunPlay()) { return; }
	if (GetWorld()->GetNetMode() == NM_ListenServer) { TickServer(DeltaTime); }
	else if (GetWorld()->GetNetMode() == NM_Client) { TickClient(DeltaTime); }
}

bool UMiniTask19ProbeSubsystem::StartServer()
{
	TArray<AMiniPlayerController*> Remote;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsLocalController()) { HostController = *It; }
		else { Remote.Add(*It); }
	}
	if (Remote.Num() != 2 || !HostController.IsValid() ||
		!Task19SnapshotIs(HostController.Get(), 100.0f, 0, 30, 90)) { return false; }
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Controllers[Index] = Remote[Index];
		Pawns[Index] = Cast<AMiniCharacter>(Remote[Index]->GetPawn());
		if (!Pawns[Index].IsValid() || !Task19ASCFor(Pawns[Index].Get()) ||
			Task19ASCFor(Pawns[Index].Get())->GetAvatarActor() != Pawns[Index].Get() ||
			!Task19AmmoIs(Remote[Index], 0, 30, 90)) { return false; }
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Pawns[Index]->GetCharacterMovement()->StopMovementImmediately();
		Pawns[Index]->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Pawns[Index]->SetActorLocation(FVector(Index == 0 ? 0.0 : 800.0, 0.0, 3000.0),
			false, nullptr, ETeleportType::TeleportPhysics);
		Pawns[Index]->ForceNetUpdate();
		FActorSpawnParameters Params;
		Params.Owner = Remote[Index];
		AMiniTask19ProbeActor* Actor = GetWorld()->SpawnActor<AMiniTask19ProbeActor>(
			AMiniTask19ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Actor) { Fail(TEXT("could not spawn independent owner probe")); return false; }
		Probes[Index] = Actor;
		Actor->InitializeServer(Index + 1, Pawns[Index].Get(), Pawns[1 - Index].Get());
	}
	bStarted = true;
	StageSeconds = 0;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe SERVER_READY: Clients=2 HostHUD=1"));
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe HOST_SNAPSHOT: Health=100 Slot=0 Ammo=30/90 OwnPawn=1"));
	return true;
}

bool UMiniTask19ProbeSubsystem::AllAcknowledged() const
{
	return Probes[0].IsValid() && Probes[1].IsValid() &&
		Probes[0]->HasAcknowledged(Phase) && Probes[1]->HasAcknowledged(Phase);
}

bool UMiniTask19ProbeSubsystem::ApplyHostAuthorityChange()
{
	AMiniPlayerController* Host = HostController.Get();
	AMiniCharacter* HostPawn = Host ? Cast<AMiniCharacter>(Host->GetPawn()) : nullptr;
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	UMiniInventoryItemInstance* Rifle = Task19ItemFor(Host, 0);
	UMiniHUDLayout* HostHUD = Task19HUDFor(Host);
	UMiniHUDViewModel* VM = HostHUD ? HostHUD->GetViewModel() : nullptr;
	if (!HostPawn || !GM || !Rifle || !VM || !Task19SnapshotIs(Host, 100, 0, 30, 90))
	{ Fail(TEXT("host authority mutation dependencies missing")); return false; }
	const int32 RefreshCountBefore = VM->GetStateRefreshCount();
	// These are ordinary authority APIs. There is no OnRep call or manual HUD
	// refresh: the local listen-server HUD must observe the real notifications.
	if (!GM->TryApplyTestDamage(Controllers[1].Get(), HostPawn, 10.0f) ||
		!Rifle->SetStat(MiniInventoryTags::AmmoInMagazine, 27) ||
		!Rifle->SetStat(MiniInventoryTags::ReserveAmmo, 84))
	{ Fail(TEXT("could not mutate host authority health/ammo")); return false; }
	if (!Task19SnapshotIs(Host, 90, 0, 27, 84) || VM->GetStateRefreshCount() <= RefreshCountBefore ||
		!FMath::IsNearlyEqual(Task19HealthFor(HostPawn), 90.0f) || !Task19AmmoIs(Host, 0, 27, 84))
	{ Fail(TEXT("listen host HUD did not refresh synchronously from authority changes")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe HOST_AUTHORITY_CHANGE: Health=90 Slot=0 Ammo=27/84 Immediate=1 RepNotifyNeeded=0"));
	return true;
}

void UMiniTask19ProbeSubsystem::Advance(EMiniTask19Phase Value)
{
	for (auto& Probe : Probes) { if (Probe.IsValid()) { Probe->SetServerPhase(Value); } }
	Phase = Value;
	StageSeconds = 0;
	bStageAction = false;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe SERVER_PHASE: %d"), static_cast<int32>(Value));
}

void UMiniTask19ProbeSubsystem::TickServer(float DeltaTime)
{
	if (Phase == EMiniTask19Phase::Complete) { return; }
	StageSeconds += DeltaTime;
	if (!bStarted)
	{
		if (StageSeconds > 100) { Fail(TEXT("host HUD or two gameplay-ready clients missing")); }
		else { StartServer(); }
		return;
	}
	if (StageSeconds > 45) { Fail(TEXT("server UI phase timed out")); return; }
	AMiniCharacter* Shooter = Pawns[0].Get();
	AMiniCharacter* Target = Pawns[1].Get();
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	if (!Target || !GM || !Controllers[0].IsValid()) { Fail(TEXT("server dependencies disappeared")); return; }
	switch (Phase)
	{
	case EMiniTask19Phase::Initial:
		if (AllAcknowledged() && ApplyHostAuthorityChange()) { Advance(EMiniTask19Phase::HostAuthorityState); }
		return;
	case EMiniTask19Phase::HostAuthorityState:
		if (AllAcknowledged() && Task19SnapshotIs(HostController.Get(), 90, 0, 27, 84))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe HOST_AUTHORITY_PASS: LocalRefresh=1 RemoteIsolation=1"));
			Advance(EMiniTask19Phase::RifleFire);
		}
		return;
	case EMiniTask19Phase::RifleFire:
		if (!AllAcknowledged()) { return; }
		if (!Shooter || Shooter->GetRangedWeaponComponent()->GetAcceptedShotCount() != 1 ||
			!Task19AmmoIs(Controllers[0].Get(), 0, 29, 90) || !FMath::IsNearlyEqual(Task19HealthFor(Target), 75.0f))
		{ Fail(TEXT("one rifle shot did not match server ammo and damage")); return; }
		Advance(EMiniTask19Phase::SwitchWeapon);
		return;
	case EMiniTask19Phase::SwitchWeapon:
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::LateHUDRemoved); }
		return;
	case EMiniTask19Phase::LateHUDRemoved:
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::LateHUDState); }
		return;
	case EMiniTask19Phase::LateHUDState:
		if (!bStageAction)
		{
			UMiniInventoryItemInstance* Pistol = Task19ItemFor(Controllers[0].Get(), 1);
			if (!Pistol || !GM->TryApplyTestDamage(Controllers[1].Get(), Shooter, 35.0f) ||
				!Pistol->SetStat(MiniInventoryTags::AmmoInMagazine, 7) ||
				!Pistol->SetStat(MiniInventoryTags::ReserveAmmo, 19))
			{ Fail(TEXT("could not mutate authority state while HUD was absent")); return; }
			bStageAction = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe SERVER_LATE_STATE: Health=65 Pistol=7/19"));
		}
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::LateHUDRestored); }
		return;
	case EMiniTask19Phase::LateHUDRestored:
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::MenuOpen); }
		return;
	case EMiniTask19Phase::MenuOpen:
		if (!AllAcknowledged()) { return; }
		if (!Shooter || Shooter->GetRangedWeaponComponent()->GetAcceptedShotCount() != 1 ||
			!Task19AmmoIs(Controllers[0].Get(), 1, 7, 19))
		{ Fail(TEXT("a real CommonUI menu allowed gameplay fire")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe SERVER_MENU_BLOCK: ExtraShots=0"));
		Advance(EMiniTask19Phase::MenuClosed);
		return;
	case EMiniTask19Phase::MenuClosed:
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::RootRemoved); }
		return;
	case EMiniTask19Phase::RootRemoved:
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::RootRestored); }
		return;
	case EMiniTask19Phase::RootRestored:
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::Death); }
		return;
	case EMiniTask19Phase::Death:
		if (!bStageAction)
		{
			PreviousPawn = Shooter;
			if (!GM->TryApplyTestDamage(Controllers[1].Get(), Shooter, 150.0f))
			{ Fail(TEXT("could not trigger authoritative respawn")); return; }
			bStageAction = true;
		}
		if (AllAcknowledged()) { Advance(EMiniTask19Phase::Respawn); }
		return;
	case EMiniTask19Phase::Respawn:
		{
			AMiniCharacter* Replacement = Cast<AMiniCharacter>(Controllers[0]->GetPawn());
			if (!Replacement || Replacement == PreviousPawn.Get() || !Task19ASCFor(Replacement) ||
				Task19ASCFor(Replacement)->GetAvatarActor() != Replacement) { return; }
			Pawns[0] = Replacement;
			for (int32 Index = 0; Index < 2; ++Index)
			{ Probes[Index]->UpdateServerPawns(Pawns[Index].Get(), Pawns[1 - Index].Get()); }
			if (AllAcknowledged())
			{
				if (!Task19AmmoIs(Controllers[0].Get(), 0, 30, 90) || Task19HealthFor(Replacement) != 100.0f)
				{ Fail(TEXT("respawn authority state was not fresh")); return; }
				Advance(EMiniTask19Phase::FeatureMenu);
			}
		}
		return;
	case EMiniTask19Phase::FeatureMenu:
		if (!bStageAction)
		{
			UMiniHUDLayout* HostHUD = Task19HUDFor(HostController.Get());
			if (!HostHUD || !HostHUD->OpenDebugMenu()) { return; }
			bStageAction = true;
		}
		if (AllAcknowledged())
		{
			UMiniHUDLayout* HostHUD = Task19HUDFor(HostController.Get());
			UMiniPrimaryGameLayout* HostRoot = Task19RootFor(HostController.Get());
			AMiniCharacter* HostPawn = Cast<AMiniCharacter>(HostController->GetPawn());
			UMiniHeroComponent* HostHero = HostPawn ? HostPawn->GetHeroComponent() : nullptr;
			if (!HostHUD || !HostHUD->GetDebugMenu() || !HostHUD->GetDebugMenu()->IsActivated() ||
				!HostRoot || HostRoot->GetGameplayInputBlockCount() <= 0 || !HostHero ||
				HostHero->IsInputActive() || HostHero->GetInputBindingCount() != 0)
			{ Fail(TEXT("listen host menu did not acquire its gameplay input gate")); return; }
			Advance(EMiniTask19Phase::FeatureOff);
		}
		return;
	case EMiniTask19Phase::FeatureOff:
		if (!bFeatureRequested)
		{
			RetainUI(Task19HUDFor(HostController.Get()));
			BeginFeatureDeactivation();
		}
		if (AllAcknowledged() && bFeatureDeactivated && RecreateRootAfterFeatureOff(HostController.Get()))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe HOST_FEATURE_OFF_ROOT_RECREATED: Viewport=1 HUDs=0 ActionCounts=0 InputBlocks=0"));
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe HOST_FEATURE_OFF: Widgets=0 Listeners=0 InputBlocks=0"));
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe SERVER_PASS: OwnSnapshots=1 HostAuthorityRefresh=1 LateHUD=1 MenuStack=1 RootLifecycle=1 RespawnRebind=1 ObserverHit=0 FeatureDeactivated=1 Cleanup=1"));
			Advance(EMiniTask19Phase::Complete);
		}
		return;
	case EMiniTask19Phase::Complete:
		return;
	}
}

void UMiniTask19ProbeSubsystem::RetainUI(UMiniHUDLayout* Layout)
{
	RetainedLayout = Layout;
	RetainedViewModel = Layout ? Layout->GetViewModel() : nullptr;
	RetainedMenu = Layout ? Layout->GetDebugMenu() : nullptr;
	RetainedWidgets.Empty();
	if (Layout)
	{
		TArray<UMiniHUDDataWidget*> Widgets;
		Layout->GetExtensionWidgets(Widgets);
		for (UMiniHUDDataWidget* Widget : Widgets) { RetainedWidgets.Add(Widget); }
	}
}

bool UMiniTask19ProbeSubsystem::RetainedUIStopped() const
{
	if (!RetainedLayout || !RetainedViewModel || RetainedViewModel->IsRunning() ||
		RetainedViewModel->GetBindingCount() != 0 || RetainedLayout->IsActivated() ||
		RetainedLayout->GetExtensionPointCount() != 0 || RetainedLayout->GetExtensionWidgetCount() != 0 ||
		RetainedLayout->GetDebugMenu() || RetainedWidgets.Num() != 4) { return false; }
	if (RetainedMenu && RetainedMenu->IsActivated()) { return false; }
	const FMiniHUDSnapshot& Snapshot = RetainedViewModel->GetSnapshot();
	if (Snapshot.LocalPlayer || Snapshot.World || Snapshot.Pawn || Snapshot.bHealthReady || Snapshot.bAmmoReady)
	{ return false; }
	return RetainedWidgetsStopped();
}

bool UMiniTask19ProbeSubsystem::RetainedWidgetsStopped() const
{
	if (RetainedWidgets.Num() != 4) { return false; }
	for (UMiniHUDDataWidget* Widget : RetainedWidgets)
	{
		if (!Widget || Widget->IsListening() || Widget->GetMessageListenerCount() != 0) { return false; }
		const FMiniHUDSnapshot& Displayed = Widget->GetDisplayedSnapshot();
		if (Displayed.LocalPlayer || Displayed.World || Displayed.Pawn || Displayed.bHealthReady || Displayed.bAmmoReady)
		{ return false; }
	}
	return true;
}

bool UMiniTask19ProbeSubsystem::CaptureCheckpoint(int32 Owner, const TCHAR* View)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask19Media"))) { return true; }
	if (!bScreenshotRequested)
	{
		Task19CaptureUI(Owner, View);
		bScreenshotRequested = true;
		ScreenshotRequestedAt = StageSeconds;
		return false;
	}
	// RequestScreenshot captures on a future render. Keep this checkpoint alive
	// through several 30 Hz frames before the server can dismantle the UI.
	return StageSeconds - ScreenshotRequestedAt >= 0.35f;
}

void UMiniTask19ProbeSubsystem::BeginFeatureDeactivation()
{
	bFeatureRequested = true;
	FString URL;
	if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(TEXT("MiniShooterCore"), URL))
	{ Fail(TEXT("could not resolve MiniShooterCore feature URL")); return; }
	UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(URL,
		FGameFeaturePluginDeactivateComplete::CreateWeakLambda(this,
			[this](const UE::GameFeatures::FResult& Result)
			{
				if (Result.HasError()) { Fail(TEXT("real GameFeature deactivation failed")); }
				else { bFeatureDeactivated = true; UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe FEATURE_DEACTIVATED: RealSubsystem=1")); }
			}));
}

bool UMiniTask19ProbeSubsystem::RecreateRootAfterFeatureOff(AMiniPlayerController* Controller)
{
	if (!Controller || !RetainedMenu || !RetainedUIStopped() || !Task19ContributionsEmpty(GetWorld()) ||
		Task19HUDCount(Controller) != 0) { return false; }
	UGameUIManagerSubsystem* Manager = Controller->GetGameInstance()->GetSubsystem<UGameUIManagerSubsystem>();
	UCommonLocalPlayer* LocalPlayer = Cast<UCommonLocalPlayer>(Controller->GetLocalPlayer());
	if (!Manager || !LocalPlayer)
	{ Fail(TEXT("feature-off root lifecycle dependencies disappeared")); return false; }
	UMiniPrimaryGameLayout* Root = Task19RootFor(Controller);
	if (!bFeatureOffRootReleased)
	{
		if (!Root || !Root->IsLayoutReady() || Root->GetGameplayInputBlockCount() != 0 ||
			Root->GetLayerWidget(UMiniPrimaryGameLayout::GetMenuLayerTag())->GetActiveWidget()) { return false; }
		RetainedRoot = Root;
		Manager->NotifyPlayerDestroyed(LocalPlayer);
		bFeatureOffRootReleased = true;
		return false;
	}
	if (!bFeatureOffRootRecreated)
	{
		if (Root || !RetainedRoot || RetainedRoot->IsLayoutReady() || RetainedRoot->IsInViewport() ||
			RetainedRoot->GetGameplayInputBlockCount() != 0) { return false; }
		Manager->NotifyPlayerAdded(LocalPlayer);
		bFeatureOffRootRecreated = true;
		FeatureOffRootRecreatedAt = StageSeconds;
		return false;
	}
	// Trigger real policy/receiver lifecycle after plugin deactivation, then
	// observe several frames to catch any late contribution resurrection.
	return Root && Root != RetainedRoot && Root->IsLayoutReady() && Root->IsInViewport() &&
		!RetainedRoot->IsInViewport() && !RetainedRoot->IsLayoutReady() &&
		Root->GetGameplayInputBlockCount() == 0 && Task19HUDCount(Controller) == 0 &&
		Task19ContributionsEmpty(GetWorld()) && RetainedUIStopped() &&
		!Root->GetLayerWidget(UMiniPrimaryGameLayout::GetMenuLayerTag())->GetActiveWidget() &&
		StageSeconds - FeatureOffRootRecreatedAt >= 0.5f;
}

void UMiniTask19ProbeSubsystem::Acknowledge(AMiniTask19ProbeActor* Actor, const TCHAR* Marker)
{
	if (bClientAcknowledged) { return; }
	bClientAcknowledged = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe CLIENT_%s: Owner=%d"), Marker, Actor->GetOwnerIndex());
	Actor->ServerAcknowledge(Actor->GetPhase());
}

void UMiniTask19ProbeSubsystem::TickClient(float DeltaTime)
{
	AMiniTask19ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask19ProbeActor> It(GetWorld()); It; ++It) { Probe = *It; break; }
	AMiniPlayerController* PC = Probe ? Cast<AMiniPlayerController>(Probe->GetOwner()) : nullptr;
	if (!Probe || !PC || !PC->IsLocalController() || Probe->GetOwnerIndex() < 1) { return; }
	Phase = Probe->GetPhase();
	if (ClientPhase != Phase)
	{
		ClientPhase = Phase;
		StageSeconds = 0;
		bStageAction = false;
		bKeyReleased = false;
		bClientAcknowledged = false;
		bScreenshotRequested = false;
	}
	if (Phase == EMiniTask19Phase::Complete || bClientAcknowledged) { return; }
	StageSeconds += DeltaTime;
	if (StageSeconds > 45) { Fail(TEXT("client UI checkpoint timed out")); return; }
	const bool bShooter = Probe->GetOwnerIndex() == 1;
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(PC->GetPawn());
	UMiniPrimaryGameLayout* Root = Task19RootFor(PC);
	UMiniHUDLayout* Layout = Task19HUDFor(PC);
	UMiniHUDViewModel* VM = Layout ? Layout->GetViewModel() : nullptr;
	UMiniAbilitySystemComponent* ASC = Task19ASCFor(Pawn);
	UMiniHeroComponent* Hero = Pawn ? Pawn->GetHeroComponent() : nullptr;
	auto ExpectedLate = [PC, bShooter]()
	{ return Task19SnapshotIs(PC, bShooter ? 65.0f : 75.0f, bShooter ? 1 : 0, bShooter ? 7 : 30, bShooter ? 19 : 90); };
	switch (Phase)
	{
	case EMiniTask19Phase::Initial:
		if (Task19SnapshotIs(PC, 100, 0, 30, 90) && Root && Root->IsLayoutReady() && Task19WidgetAction())
		{
			const FMiniWidgetContributionCounts C = Task19WidgetAction()->GetProbeContributionCounts(GetWorld());
			if (C.HUDs != 1 || C.Layouts != 1 || C.Elements != 4 || C.PendingLoads != 0)
			{ Fail(TEXT("initial UI contribution counts are wrong")); return; }
			PreviousPawn = Pawn;
			Acknowledge(Probe, TEXT("INITIAL_SNAPSHOT"));
		}
		return;
	case EMiniTask19Phase::HostAuthorityState:
		if (Task19SnapshotIs(PC, 100, 0, 30, 90) && VM && VM->GetHitMessageCount() == 0)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe CLIENT_HOST_AUTHORITY_ISOLATED: Owner=%d Health=100 Slot=0 Ammo=30/90"),
				Probe->GetOwnerIndex());
			Acknowledge(Probe, TEXT("HOST_AUTHORITY_ISOLATION"));
		}
		return;
	case EMiniTask19Phase::RifleFire:
		if (bShooter && Pawn && Probe->GetPeerPawn() && Hero && Hero->IsInputActive() && !bStageAction)
		{ Task19AimAt(PC, Pawn, Probe->GetPeerPawn()); bStageAction = true; }
		if (bShooter && bStageAction && StageSeconds >= 0.6f && !bKeyReleased)
		{
			Task19SendKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			// Rifle release is sent on the next phase tick, before its 0.12s repeat.
			bKeyReleased = true;
			return;
		}
		if (bShooter && bKeyReleased) { Task19SendKey(PC, EKeys::LeftMouseButton, IE_Released); }
		if (VM && Task19SnapshotIs(PC, bShooter ? 100.0f : 75.0f, 0, bShooter ? 29 : 30, 90) &&
			VM->GetHitMessageCount() == (bShooter ? 1 : 0) && Task19DisplayedHitCount(Layout) == (bShooter ? 1 : 0))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe CLIENT_HIT_ISOLATION: Owner=%d HitMessages=%d"),
				Probe->GetOwnerIndex(), VM->GetHitMessageCount());
			Acknowledge(Probe, TEXT("FIRE_SNAPSHOT"));
		}
		return;
	case EMiniTask19Phase::SwitchWeapon:
		if (bShooter && !bStageAction) { Task19SendKey(PC, EKeys::Q, IE_Pressed); bStageAction = true; return; }
		if (bShooter) { Task19SendKey(PC, EKeys::Q, IE_Released); }
		if (Task19SnapshotIs(PC, bShooter ? 100.0f : 75.0f, bShooter ? 1 : 0, bShooter ? 12 : 30, bShooter ? 36 : 90))
		{ Acknowledge(Probe, TEXT("SWITCH_SNAPSHOT")); }
		return;
	case EMiniTask19Phase::LateHUDRemoved:
		if (!bStageAction && Layout && Task19WidgetAction())
		{
			RetainUI(Layout);
			Task19WidgetAction()->SetProbeSuspended(GetWorld(), true);
			RetainedRefreshCount = RetainedViewModel->GetStateRefreshCount();
			bStageAction = true;
		}
		if (bStageAction && RetainedUIStopped() && Task19ContributionsEmpty(GetWorld()) && Task19HUDCount(PC) == 0)
		{ Acknowledge(Probe, TEXT("LATE_HUD_REMOVED")); }
		return;
	case EMiniTask19Phase::LateHUDState:
		if (RetainedUIStopped() && StageSeconds > 0.3f &&
			RetainedViewModel->GetStateRefreshCount() == RetainedRefreshCount &&
			(!bShooter || (Pawn && Task19HealthFor(Pawn) == 65 && Task19AmmoIs(PC, 1, 7, 19))))
		{ Acknowledge(Probe, TEXT("STOPPED_LISTENERS_SILENT")); }
		return;
	case EMiniTask19Phase::LateHUDRestored:
		if (!bStageAction && Task19WidgetAction()) { Task19WidgetAction()->SetProbeSuspended(GetWorld(), false); bStageAction = true; }
		// CommonUI pools the layout/VM. Its four injected data widgets are newly
		// created, and the retained old widgets must remain completely stopped.
		if (Layout && ExpectedLate() && RetainedWidgetsStopped())
		{
			TArray<UMiniHUDDataWidget*> CurrentWidgets;
			Layout->GetExtensionWidgets(CurrentWidgets);
			for (UMiniHUDDataWidget* Old : RetainedWidgets)
			{
				if (CurrentWidgets.Contains(Old)) { Fail(TEXT("revoked HUD element was reused with live listeners")); return; }
			}
			if (CaptureCheckpoint(Probe->GetOwnerIndex(), TEXT("HUD")))
			{ Acknowledge(Probe, TEXT("LATE_HUD_FRESH_SNAPSHOT")); }
		}
		return;
	case EMiniTask19Phase::MenuOpen:
	case EMiniTask19Phase::FeatureMenu:
		if (!bStageAction && Layout)
		{
			if (!Layout->OpenDebugMenu()) { Fail(TEXT("formal HUD menu entry did not push a menu")); return; }
			bStageAction = true;
		}
		if (!Root || !Layout || !Layout->GetDebugMenu() || !Layout->GetDebugMenu()->IsActivated() ||
			Root->GetLayerWidget(Root->GetMenuLayerTag())->GetActiveWidget() != Layout->GetDebugMenu() ||
			Root->GetGameplayInputBlockCount() < 1 || !Hero || Hero->IsInputActive() ||
			Hero->GetInputBindingCount() != 0 || !ASC || ASC->GetHeldInputCount() != 0) { return; }
		if (!bKeyReleased)
		{
			Task19SendKey(PC, EKeys::LeftMouseButton, IE_Pressed);
			Task19SendKey(PC, EKeys::SpaceBar, IE_Pressed);
			bKeyReleased = true;
		}
		if (StageSeconds >= 0.5f)
		{
			Task19SendKey(PC, EKeys::LeftMouseButton, IE_Released);
			Task19SendKey(PC, EKeys::SpaceBar, IE_Released);
			if (Phase == EMiniTask19Phase::MenuOpen && !CaptureCheckpoint(Probe->GetOwnerIndex(), TEXT("Menu"))) { return; }
			Acknowledge(Probe, Phase == EMiniTask19Phase::MenuOpen ? TEXT("REAL_MENU_BLOCKED") : TEXT("FEATURE_OWNED_MENU_OPEN"));
		}
		return;
	case EMiniTask19Phase::MenuClosed:
		if (!bStageAction && Layout) { Layout->CloseDebugMenu(); bStageAction = true; }
		if (Root && Root->GetGameplayInputBlockCount() == 0 && Layout && !Layout->GetDebugMenu() &&
			Hero && Hero->IsInputActive() && Hero->GetInputBindingCount() > 0 && ExpectedLate())
		{ Acknowledge(Probe, TEXT("MENU_GAMEPLAY_RESTORED")); }
		return;
	case EMiniTask19Phase::RootRemoved:
		if (!bStageAction && Layout && Root)
		{
			RetainUI(Layout);
			RetainedRoot = Root;
			UGameUIManagerSubsystem* Manager = PC->GetGameInstance()->GetSubsystem<UGameUIManagerSubsystem>();
			Manager->NotifyPlayerDestroyed(CastChecked<UCommonLocalPlayer>(PC->GetLocalPlayer()));
			bStageAction = true;
		}
		if (bStageAction && RetainedRoot && !Task19RootFor(PC) && !RetainedRoot->IsInViewport() &&
			!RetainedRoot->IsLayoutReady() &&
			RetainedRoot->GetGameplayInputBlockCount() == 0 && RetainedUIStopped() && Task19ContributionsEmpty(GetWorld()))
		{ Acknowledge(Probe, TEXT("REAL_ROOT_RELEASED")); }
		return;
	case EMiniTask19Phase::RootRestored:
		if (!bStageAction)
		{
			PC->GetGameInstance()->GetSubsystem<UGameUIManagerSubsystem>()->NotifyPlayerAdded(
				CastChecked<UCommonLocalPlayer>(PC->GetLocalPlayer()));
			bStageAction = true;
		}
		if (Root && Root != RetainedRoot && Root->IsLayoutReady() && Root->IsInViewport() &&
			!RetainedRoot->IsInViewport() && ExpectedLate() && RetainedUIStopped())
		{ Acknowledge(Probe, TEXT("REAL_ROOT_RECREATED")); }
		return;
	case EMiniTask19Phase::Death:
		if (VM && bShooter && VM->GetSnapshot().bDead && VM->GetSnapshot().Health == 0)
		{
			if (!Root || !Hero || !ASC) { return; }
			// Releasing a UI source during death must leave the death gate intact.
			Root->SetGameplayInputBlockedForSource(this, true);
			Root->SetGameplayInputBlockedForSource(this, false);
			if (Hero->IsInputActive() || Hero->GetInputBindingCount() != 0 || ASC->GetHeldInputCount() != 0)
			{ Fail(TEXT("releasing a UI input gate reactivated a dead Pawn")); return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe CLIENT_DEATH_UI_GATE: GameplayActive=0 Bindings=0 HeldInput=0"));
			PreviousPawn = Pawn;
			Acknowledge(Probe, TEXT("DEATH_SNAPSHOT"));
		}
		else if (!bShooter && Task19SnapshotIs(PC, 75, 0, 30, 90)) { Acknowledge(Probe, TEXT("DEATH_ISOLATED")); }
		return;
	case EMiniTask19Phase::Respawn:
		if (bShooter && Pawn && Pawn != PreviousPawn.Get() && Task19SnapshotIs(PC, 100, 0, 30, 90) &&
			VM && !VM->GetSnapshot().bDead && VM->GetSnapshot().Pawn.Get() == Probe->GetOwnerPawn())
		{ Acknowledge(Probe, TEXT("RESPAWN_NEW_PAWN")); }
		else if (!bShooter && Task19SnapshotIs(PC, 75, 0, 30, 90) && VM->GetHitMessageCount() == 0)
		{ Acknowledge(Probe, TEXT("RESPAWN_PEER_ISOLATED")); }
		return;
	case EMiniTask19Phase::FeatureOff:
		if (!bFeatureRequested) { RetainUI(Layout); BeginFeatureDeactivation(); }
		if (bFeatureDeactivated && RecreateRootAfterFeatureOff(PC))
		{
			if (!CaptureCheckpoint(Probe->GetOwnerIndex(), TEXT("FeatureOff"))) { return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe CLIENT_FEATURE_OFF_ROOT_RECREATED: Owner=%d Viewport=1 HUDs=0 ActionCounts=0 InputBlocks=0"), Probe->GetOwnerIndex());
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe CLIENT_FEATURE_COUNTS: Owner=%d Widgets=0 VM=0 Bindings=0 Listeners=0 InputBlocks=0 PendingLoads=0"),
				Probe->GetOwnerIndex());
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask19Probe CLIENT_RETAINED_MENU_STOPPED: Owner=%d Activated=0 InputBlocks=0"), Probe->GetOwnerIndex());
			Acknowledge(Probe, TEXT("FEATURE_REAL_CLEANUP"));
		}
		return;
	case EMiniTask19Phase::Complete:
		return;
	}
}
