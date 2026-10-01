#include "MiniTask20ProbeTravelSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "GameFeatures/MiniGameFeatureAction_AddActors.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "HAL/PlatformTime.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Practice/MiniPracticeSupply.h"
#include "System/MiniLogChannels.h"
#include "Training/MiniPracticeTarget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace
{
AMiniPlayerController* Travel20Controller(UWorld* World)
{
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It) { if (It->IsLocalController()) { return *It; } }
	return nullptr;
}

UMiniHUDLayout* Travel20HUD(AMiniPlayerController* PC)
{
	UMiniPrimaryGameLayout* Root = PC ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	UMiniHUDLayout* Result = nullptr;
	if (Layer)
	{
		for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
		{
			if (UMiniHUDLayout* HUD = Cast<UMiniHUDLayout>(Widget)) { if (Result) { return nullptr; } Result = HUD; }
		}
	}
	return Result;
}

UMiniGameFeatureAction_AddActors* Travel20Action(UWorld* World)
{
	AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	UMiniExperienceManagerComponent* Manager = GS ? GS->FindComponentByClass<UMiniExperienceManagerComponent>() : nullptr;
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	if (!Experience) { return nullptr; }
	for (const UMiniExperienceActionSet* Set : Experience->ActionSets)
	{
		if (!Set) { continue; }
		for (UGameFeatureAction* Candidate : Set->Actions)
		{
			if (UMiniGameFeatureAction_AddActors* Action = Cast<UMiniGameFeatureAction_AddActors>(Candidate)) { return Action; }
		}
	}
	return nullptr;
}
}

bool UMiniTask20ProbeTravelSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask20Travel"));
#else
	return false;
#endif
}

void UMiniTask20ProbeTravelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddUObject(this, &ThisClass::HandlePostWorldCleanup);
	bInitialized = true;
}

void UMiniTask20ProbeTravelSubsystem::Deinitialize()
{
	FWorldDelegates::OnPostWorldCleanup.Remove(CleanupHandle);
	bInitialized = false;
	CleanupHandle.Reset(); ReleaseRetainedObjects();
	Super::Deinitialize();
}

bool UMiniTask20ProbeTravelSubsystem::IsTickable() const
{
	return !IsTemplate() && bInitialized && !bFailed && !bDone;
}

UWorld* UMiniTask20ProbeTravelSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

TStatId UMiniTask20ProbeTravelSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask20ProbeTravelSubsystem, STATGROUP_Tickables);
}

void UMiniTask20ProbeTravelSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask20Travel FAIL: Travel=%d Reason=%s"), TravelsRequested, Reason);
	}
}

bool UMiniTask20ProbeTravelSubsystem::ReadyWorld(UWorld* World)
{
	if (!World || !World->IsGameWorld() || !World->HasBegunPlay() || World->GetNetMode() != NM_Standalone || World == OldWorld) { return false; }
	int32 Targets = 0, Supplies = 0;
	for (TActorIterator<AMiniPracticeTarget> It(World); It; ++It)
	{
		++Targets;
		if (OldTargets.Contains(*It) || !It->IsTargetEnabled() || !FMath::IsNearlyEqual(It->GetTargetState().Health, 100) ||
			It->GetTargetState().DisableCount != 0 || It->GetTargetState().ResetCount != 0 || !It->GetStatusWidget()) { return false; }
	}
	for (TActorIterator<AMiniPracticeSupply> It(World); It; ++It) { ++Supplies; if (*It == OldSupply) { return false; } }
	UMiniGameFeatureAction_AddActors* Action = Travel20Action(World);
	int32 Worlds, Actors, Pending, Bindings; bool bReady, bActionFailed;
	if (Targets != 3 || Supplies != 1 || !Action) { return false; }
	Action->GetWorldStats(World, Worlds, Actors, Pending, Bindings, bReady, bActionFailed);
	if (Worlds != 1 || Actors != 4 || Pending || Bindings || !bReady || bActionFailed) { return false; }
	AMiniPlayerController* PC = Travel20Controller(World);
	AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	UMiniHUDLayout* HUD = Travel20HUD(PC);
	UMiniHUDViewModel* VM = HUD ? HUD->GetViewModel() : nullptr;
	UMiniPrimaryGameLayout* Root = PC ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	if (!Pawn || !VM || !VM->IsRunning() || VM->GetBindingCount() <= 0 || !Root || Root->GetGameplayInputBlockCount() != 0 ||
		HUD->GetExtensionWidgetCount() != 4 || HUD->GetDebugMenu()) { return false; }
	const FMiniHUDSnapshot& S = VM->GetSnapshot();
	return S.World == World && S.Pawn == Pawn && S.LocalPlayer == PC->GetLocalPlayer() &&
		S.bHealthReady && S.bAmmoReady && S.ActiveSlot == 0 && S.MagazineAmmo == 30 && S.ReserveAmmo == 90 && FMath::IsNearlyEqual(S.Health, 100);
}

void UMiniTask20ProbeTravelSubsystem::HandlePostWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (bFailed || !bAwaitingTravel || World != OldWorld) { return; }
	if (!OldActorAction || !OldVM || !OldHUD || !OldSupply || !OldRoot || !OldMenu) { Fail(TEXT("retained cleanup observations missing")); return; }
	int32 Worlds, Actors, Pending, Bindings; bool bReady, bActionFailed;
	OldActorAction->GetWorldStats(World, Worlds, Actors, Pending, Bindings, bReady, bActionFailed);
	if (Worlds || Actors || Pending || Bindings || OldVM->IsRunning() || OldVM->GetBindingCount() != 0 ||
		OldHUD->GetExtensionWidgetCount() != 0 || OldHUD->GetExtensionPointCount() != 0 || OldMenu->IsActivated() ||
		OldRoot->GetGameplayInputBlockCount() != 0)
	{ Fail(TEXT("old Experience actors or HUD/menu listeners survived world cleanup")); return; }
	for (UMiniHUDDataWidget* Widget : OldWidgets)
	{
		if (Widget && (Widget->IsListening() || Widget->GetMessageListenerCount() != 0)) { Fail(TEXT("old HUD data widget still listens")); return; }
	}
	for (AMiniPracticeTarget* Target : OldTargets)
	{
		UMiniAbilitySystemComponent* ASC = Target ? Target->GetMiniAbilitySystemComponent() : nullptr;
		if (!Target || !ASC || ASC->GetOwnerActor() || ASC->GetAvatarActor() || Target->GetStatusWidget() || Target->HasActorBegunPlay())
		{ Fail(TEXT("old target did not release ASC, status widget or EndPlay state")); return; }
	}
	if (OldSupply->HasActorBegunPlay()) { Fail(TEXT("old supply did not finish EndPlay")); return; }
	bCleanupVerified = true; CleanupWallTime = FPlatformTime::Seconds(); ++CleanupsVerified;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Travel OLD_WORLD_RELEASED: Travel=%d ActionActors=0 PendingLoads=0 BeginPlayBindings=0 TargetASC=0 TargetWidgets=0 HUDWidgets=0 HUDListeners=0 VM=0 MenuBlocks=0"), TravelsRequested);
}

bool UMiniTask20ProbeTravelSubsystem::OldCallbacksStayedSilent() const
{
	// GC may already have released these EndPlay objects. A live retained object
	// must remain silent; a collected object cannot invoke a later timer callback.
	if (OldSupply && OldSupply->GetRefillCount() != SupplyRefillAtTravel) { return false; }
	for (int32 Index = 0; Index < OldTargets.Num(); ++Index)
	{
		const AMiniPracticeTarget* Target = OldTargets[Index];
		if (Target && (Target->GetTargetState().ResetCount != 0 || Target->GetTargetState().DisableCount != (Index == 0 ? 1 : 0))) { return false; }
	}
	for (UMiniHUDDataWidget* Widget : OldWidgets)
	{
		if (Widget && (Widget->IsListening() || Widget->GetMessageListenerCount() != 0)) { return false; }
	}
	return true;
}

bool UMiniTask20ProbeTravelSubsystem::PrepareAndTravel(UWorld* World)
{
	AMiniPlayerController* PC = Travel20Controller(World);
	AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	AMiniGameMode* GM = World->GetAuthGameMode<AMiniGameMode>();
	OldWorld = World; OldActorAction = Travel20Action(World); OldHUD = Travel20HUD(PC);
	OldVM = OldHUD ? OldHUD->GetViewModel() : nullptr;
	OldRoot = PC ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	for (TActorIterator<AMiniPracticeTarget> It(World); It; ++It) { OldTargets.Add(*It); }
	for (TActorIterator<AMiniPracticeSupply> It(World); It; ++It) { OldSupply = *It; }
	if (!GM || !Pawn || OldTargets.Num() != 3 || !OldSupply || !OldHUD || !OldVM || !OldRoot)
	{ Fail(TEXT("travel preparation missing ready gameplay objects")); return false; }
	TArray<UMiniHUDDataWidget*> Widgets;
	OldHUD->GetExtensionWidgets(Widgets);
	for (UMiniHUDDataWidget* Widget : Widgets) { OldWidgets.Add(Widget); }
	OldMenu = OldHUD->OpenDebugMenu();
	FMiniDamageResult Damage;
	if (!OldMenu || !GM->TryApplyDamageToActor(Pawn, OldTargets[0], 150, Damage) || Damage.IsPlayerKill() ||
		Damage.TargetKind != EMiniDamageTargetKind::PracticeTarget || !Damage.bTargetDefeated || OldTargets[0]->GetTargetState().DisableCount != 1)
	{ Fail(TEXT("could not arm target reset timer before actual travel")); return false; }
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		UMiniInventoryItemInstance* Item = PC->GetQuickBar()->GetSlotItem(Slot);
		if (!Item || !Item->SetStat(MiniInventoryTags::AmmoInMagazine, 0) || !Item->SetStat(MiniInventoryTags::ReserveAmmo, 0))
		{ Fail(TEXT("could not prepare supply lifecycle observation")); return false; }
	}
	Pawn->GetCharacterMovement()->StopMovementImmediately(); Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Pawn->SetActorLocation(OldSupply->GetActorLocation() + FVector(0, 0, 96), false, nullptr, ETeleportType::TeleportPhysics);
	if (!OldSupply->GetSupplyArea()->IsOverlappingActor(Pawn) || OldSupply->GetRefillCount() < 1)
	{ Fail(TEXT("travel supply did not observe a real overlap")); return false; }
	SupplyRefillAtTravel = OldSupply->GetRefillCount();
	++TravelsRequested; bAwaitingTravel = true; bCleanupVerified = false; WaitSeconds = 0;
	const bool bOpenLevel = TravelsRequested % 2 == 1;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Travel REQUEST: Travel=%d Round=%d Method=%s TargetResetPending=1 SupplyOverlap=1 MenuOpen=1"),
		TravelsRequested, (TravelsRequested + 1) / 2, bOpenLevel ? TEXT("OpenLevel") : TEXT("ServerTravel"));
	const FString Map(TEXT("/Game/Mini/Maps/L_MiniPractice"));
	if (bOpenLevel) { UGameplayStatics::OpenLevel(GetGameInstance(), FName(*Map), true); }
	else
	{
		GM->bUseSeamlessTravel = false;
		if (!World->ServerTravel(Map, true)) { Fail(TEXT("real ServerTravel request was rejected")); return false; }
	}
	return true;
}

void UMiniTask20ProbeTravelSubsystem::ReleaseRetainedObjects()
{
	OldTargets.Reset(); OldWidgets.Reset(); OldSupply = nullptr; OldActorAction = nullptr;
	OldHUD = nullptr; OldVM = nullptr; OldRoot = nullptr; OldMenu = nullptr; OldWorld = nullptr;
}

void UMiniTask20ProbeTravelSubsystem::Tick(float DeltaTime)
{
	if (bFailed || bDone) { return; }
	WaitSeconds += DeltaTime;
	if (WaitSeconds > 100) { Fail(TEXT("actual travel or next gameplay world timed out")); return; }
	UWorld* World = GetTickableGameObjectWorld();
	if (bAwaitingTravel)
	{
		if (!bCleanupVerified || !World || World == OldWorld || FPlatformTime::Seconds() - CleanupWallTime < 2.3) { return; }
		if (!OldCallbacksStayedSilent()) { Fail(TEXT("old reset/supply/HUD callback fired after world release")); return; }
		if (!ReadyWorld(World)) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Travel OLD_CALLBACKS_SILENT: Travel=%d WaitOverResetDelay=1 TargetResetCallbacks=0 SupplyRefillCallbacks=0 HUDListeners=0"), TravelsRequested);
		ReleaseRetainedObjects(); bAwaitingTravel = false; WaitSeconds = 0;
	}
	else if (!ReadyWorld(World)) { return; }
	++WorldsVerified;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Travel WORLD_READY: Generation=%d Targets=3 Supplies=1 ActionActors=4 PendingLoads=0 NewTargetStates=1 NewOwnerHUD=1"), WorldsVerified);
	if (TravelsRequested == 6)
	{
		if (CleanupsVerified != 6 || WorldsVerified != 7) { Fail(TEXT("three rounds did not produce six actual cleanups and seven worlds")); return; }
		bDone = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Travel PASS: Rounds=3 OpenLevel=3 ServerTravel=3 Cleanups=6 Worlds=7 OldASC=0 OldTargetWidgets=0 OldHUDListeners=0 OldTimerCallbacks=0 NewTargets=3 NewSupply=1"));
		return;
	}
	PrepareAndTravel(World);
}
