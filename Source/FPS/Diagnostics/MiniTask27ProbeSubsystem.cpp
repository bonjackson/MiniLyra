#include "MiniTask27ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Components/ActorComponent.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "RenderTimer.h"
#include "RHIGlobals.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "System/MiniTravelSubsystem.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UObject/UObjectArray.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
AMiniPlayerController* Task27PC(UWorld* World)
{
	if (World) { for (TActorIterator<AMiniPlayerController> It(World); It; ++It) { if (It->IsLocalController()) { return *It; } } }
	return nullptr;
}
AMiniPlayerState* Task27PS(UWorld* World) { AMiniPlayerController* PC = Task27PC(World); return PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr; }
UMiniAbilitySystemComponent* Task27ASC(UWorld* World) { AMiniPlayerState* PS = Task27PS(World); return PS ? PS->GetMiniAbilitySystemComponent() : nullptr; }
UMiniPrimaryGameLayout* Task27Root(UWorld* World)
{
	AMiniPlayerController* PC = Task27PC(World);
	return PC && PC->GetLocalPlayer() ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
}
UMiniHUDLayout* Task27HUD(UWorld* World)
{
	UMiniPrimaryGameLayout* Root = Task27Root(World);
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(UMiniPrimaryGameLayout::GetGameLayerTag()) : nullptr;
	UMiniHUDLayout* Result = nullptr;
	if (Layer) { for (UCommonActivatableWidget* Widget : Layer->GetWidgetList()) { if (UMiniHUDLayout* HUD = Cast<UMiniHUDLayout>(Widget); HUD && HUD->IsActivated()) { if (Result) { return nullptr; } Result = HUD; } } }
	return Result;
}
UMiniExperienceManagerComponent* Task27Manager(UWorld* World)
{
	AMiniGameState* GS = World ? World->GetGameState<AMiniGameState>() : nullptr;
	return GS ? GS->GetExperienceManagerComponent() : nullptr;
}
int32 Task27Round(UWorld* World)
{
	UMiniMatchSubsystem* Match = World ? World->GetSubsystem<UMiniMatchSubsystem>() : nullptr;
	return Match ? Match->GetCurrentMatchState().RoundId : 0;
}
double Task27CVar(const TCHAR* Name)
{
	const IConsoleVariable* Value = IConsoleManager::Get().FindConsoleVariable(Name);
	return Value ? Value->GetFloat() : -1.0;
}
double Task27Percentile(TArray<double> Values, double Fraction)
{
	if (Values.IsEmpty()) { return -1; }
	Values.Sort(); const int32 Index = FMath::Clamp(FMath::CeilToInt(Fraction * Values.Num()) - 1, 0, Values.Num() - 1);
	return Values[Index];
}
bool Task27SafeToken(const FString& Value)
{
	if (Value.IsEmpty()) { return false; }
	for (TCHAR C : Value) { if (!FChar::IsAlnum(C) && C != TEXT('-') && C != TEXT('_')) { return false; } }
	return true;
}
}

bool UMiniTask27ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Value; return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask27="), Value);
#else
	return false;
#endif
}
void UMiniTask27ProbeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask27="), Mode);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27Role="), Role);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27Peer="), Peer);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27Output="), Output);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27Signals="), Signals);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27Profile="), Profile);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27RunId="), RunId);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27Restarts="), Restarts);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27TimeoutSeconds="), TimeoutSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27WarmupSeconds="), WarmupSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask27SampleSeconds="), SampleSeconds);
	FString Task23Mode; FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask23="), Task23Mode);
	StartedAt = FPlatformTime::Seconds(); bInitialized = true;
	if (!Task27SafeToken(Peer) || !Task27SafeToken(RunId) || Output.IsEmpty() || Signals.IsEmpty() ||
		(Role != TEXT("Server") && Role != TEXT("Client")) || (Profile != TEXT("Current") && Profile != TEXT("Performance")) ||
		(Role == TEXT("Server") ? Peer != TEXT("Server") : (Peer != TEXT("ClientA") && Peer != TEXT("ClientB") && Peer != TEXT("ClientC"))) ||
		(Mode == TEXT("Practice") && Role != TEXT("Server")) ||
		(Mode == TEXT("ObserveArena") && Task23Mode != TEXT("Arena")) ||
		(Restarts < 1 || Restarts > 10) || WarmupSeconds < 2 || SampleSeconds < 5 || TimeoutSeconds < 60 ||
		(Mode != TEXT("Four") && Mode != TEXT("RestartTrend") && Mode != TEXT("Practice") && Mode != TEXT("ObserveArena")))
	{ Fail(TEXT("invalid diagnostic parameters")); return; }
	IFileManager::Get().MakeDirectory(*Output, true); IFileManager::Get().MakeDirectory(*Signals, true);
	SnapshotCSV = TEXT("Cycle,Stage,WallSeconds,Round,Actors,Components,PlayerStates,Pawns,Controllers,OwnedInventoryEntries,GlobalObjects,ClaimedObjectSlots,OldWorldWeakAlive,WorkingSetBytes,CommittedBytes,PeakWorkingSetBytes,PeakCommittedBytes,HUDLayouts,HUDWidgets,HUDExtensionPoints,VMBindings,HUDMessageListeners,HeroBindings,AbilitySpecs,RootInputGates,DiagnosticArrayStringBytes\n");
	NetworkCSV = TEXT("Cycle,WallSeconds,IntervalSeconds,InBytes,OutBytes,InPackets,OutPackets,OutReliableBunches,InBytesPerSecond,OutBytesPerSecond\n");
}
void UMiniTask27ProbeSubsystem::Deinitialize()
{
	bInitialized = false;
	// All observations are weak or plain values; no old World/actor is retained.
	Frames.Reset(); GPUFrames.Reset(); OldWorlds.Reset(); ObservedWorld.Reset(); ObservedPlayerState.Reset(); ObservedASC.Reset(); MeasuredDriver.Reset();
	Super::Deinitialize();
}
bool UMiniTask27ProbeSubsystem::IsTickable() const { return !IsTemplate() && bInitialized && !bFailed && !bDone; }
TStatId UMiniTask27ProbeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask27ProbeSubsystem, STATGROUP_Tickables); }
UWorld* UMiniTask27ProbeSubsystem::GetTickableGameObjectWorld() const { return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr; }
void UMiniTask27ProbeSubsystem::Fail(const TCHAR* Reason)
{
	bFailed = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask27Probe FAIL: Mode=%s Peer=%s Cycle=%d Reason=%s"), *Mode, *Peer, Cycle, Reason);
}
void UMiniTask27ProbeSubsystem::LogWait(const TCHAR* Reason)
{
	const double Now = FPlatformTime::Seconds(); if (Now - LastWaitAt < 5) { return; } LastWaitAt = Now;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe WAIT: Mode=%s Peer=%s Cycle=%d Stage=%d Reason=%s"), *Mode, *Peer, Cycle, int32(Stage), Reason);
}
bool UMiniTask27ProbeSubsystem::ObserveWorld()
{
	UWorld* World = GetTickableGameObjectWorld();
	if (ObservedWorld.Get() == World) { return true; }
	// The host may process the final Measured signal before a client ticks AwaitPeers.
	const bool bRestartAcknowledged = Stage == EStage::AwaitPeers && Mode == TEXT("RestartTrend") && Cycle < Restarts + 1 && AllPeersHave(TEXT("Measured"));
	if (Stage == EStage::Sampling || (Stage == EStage::AwaitPeers && !bRestartAcknowledged)) { Fail(TEXT("World changed within a measured window")); return false; }
	const bool bPractice = Mode == TEXT("Practice");
	if (!World || !World->HasBegunPlay() || !World->GetMapName().EndsWith(bPractice ? TEXT("L_MiniPractice") : TEXT("L_MiniArena"))) { return false; }
	if (SeenWorlds.Contains(FObjectKey(World))) { Fail(TEXT("revisited an old World rather than ordinary travel")); return false; }
	if (Cycle > 0) { OldWorlds.Add(ObservedWorld); }
	SeenWorlds.Add(FObjectKey(World)); ObservedWorld = World; ++Cycle; Stage = EStage::AwaitReady;
	ObservedPlayerState.Reset(); ObservedASC.Reset(); MeasuredDriver.Reset(); LastNetworkAt = 0; ReadyAt = 0; bRenderingValidated = false; bServerJoinReady = false;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe WORLD: Peer=%s Cycle=%d Map=%s NetMode=%d NewWorld=1"), *Peer, Cycle, *World->GetName(), int32(World->GetNetMode()));
	return true;
}
bool UMiniTask27ProbeSubsystem::GameplayReady() const
{
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task27PC(World);
	AMiniPlayerState* PS = Task27PS(World); UMiniAbilitySystemComponent* ASC = Task27ASC(World); AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	UMiniExperienceManagerComponent* Manager = Task27Manager(World); UMiniPrimaryGameLayout* Root = Task27Root(World); UMiniHUDLayout* HUD = Task27HUD(World);
	UMiniHUDViewModel* VM = HUD ? HUD->GetViewModel() : nullptr;
	if (!World || !PC || !PS || !ASC || !Pawn || !Pawn->GetHealthComponent() || Pawn->GetHealthComponent()->IsDead() ||
		ASC->GetOwnerActor() != PS || ASC->GetAvatarActor() != Pawn || !Manager || !Manager->IsExperienceLoaded() || !Root || Root->GetGameplayInputBlockCount() ||
		PC->IsMiniInputBlocked() || !Pawn->GetHeroComponent()->IsInputActive() || Pawn->GetHeroComponent()->GetInputBindingCount() != 16 ||
		!Pawn->GetHeroComponent()->OwnsInputMapping() || !Pawn->GetEquipmentManager()->GetCurrentEquipment() || PC->GetInventoryManager()->GetEntries().Num() != 2 ||
		!HUD || HUD->GetExtensionWidgetCount() != 4 || !VM || !VM->IsRunning() || VM->GetSnapshot().World != World || VM->GetSnapshot().Pawn != Pawn || !VM->GetSnapshot().bHealthReady)
	{ return false; }
	if (Mode == TEXT("Practice")) { return World->GetNetMode() == NM_Standalone && Manager->GetCurrentExperienceId().PrimaryAssetName == TEXT("DA_MiniPracticeExperience"); }
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>(); UMiniMatchSubsystem* Match = World->GetSubsystem<UMiniMatchSubsystem>();
	return Manager->GetCurrentExperienceId().PrimaryAssetName == TEXT("DA_MiniArenaExperience") && Phases && Match &&
		Phases->GetCurrentPhaseState().PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Playing && Match->GetCurrentMatchState().ConnectedPlayerCount == 4 &&
		Match->GetCurrentMatchState().ScoreLimit == 10 && FMath::Abs(Phases->GetCurrentPhaseState().PhaseEndTimeServer - Phases->GetCurrentPhaseState().PhaseStartTimeServer - 300.0) < 0.1;
}
bool UMiniTask27ProbeSubsystem::StableContext() const
{
	UWorld* World = GetTickableGameObjectWorld(); UMiniExperienceManagerComponent* Manager = Task27Manager(World);
	if (!World || World != ObservedWorld.Get() || Task27PS(World) != ObservedPlayerState.Get() || Task27ASC(World) != ObservedASC.Get() || !Manager || !Manager->IsExperienceLoaded() || !GameplayReady()) { return false; }
	if (Mode == TEXT("Practice")) { return true; }
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>(); UMiniMatchSubsystem* Match = World->GetSubsystem<UMiniMatchSubsystem>();
	return Phases && Match && Phases->GetCurrentPhaseState().PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Playing &&
		Match->GetCurrentMatchState().ConnectedPlayerCount == 4 && (Mode == TEXT("ObserveArena") || Match->GetCurrentMatchState().RoundId == RoundAtStart);
}
bool UMiniTask27ProbeSubsystem::ValidateRendering()
{
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	if (!Viewport || !Viewport->Viewport || !GDynamicRHI) { return false; }
	const FIntPoint Size = Viewport->Viewport->GetSizeXY(); const FString RHI(GDynamicRHI->GetName());
	if (Size != FIntPoint(1920, 1080) || RHI.Contains(TEXT("Null")) || Task27CVar(TEXT("r.ScreenPercentage")) != 100 ||
		Task27CVar(TEXT("r.VSync")) != 0 || Task27CVar(TEXT("r.DynamicRes.OperationMode")) != 0 || Task27CVar(TEXT("t.IdleWhenNotForeground")) != 0 ||
		FApp::UseFixedTimeStep() || FApp::IsBenchmarking())
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe RENDER_MISMATCH: Peer=%s Viewport=%dx%d RHI=%s ScreenPercentage=%.3f VSync=%.3f DynamicRes=%.3f BackgroundIdle=%.3f FixedStep=%d Benchmark=%d"),
			*Peer, Size.X, Size.Y, *RHI, Task27CVar(TEXT("r.ScreenPercentage")), Task27CVar(TEXT("r.VSync")), Task27CVar(TEXT("r.DynamicRes.OperationMode")),
			Task27CVar(TEXT("t.IdleWhenNotForeground")), FApp::UseFixedTimeStep() ? 1 : 0, FApp::IsBenchmarking() ? 1 : 0);
		Fail(TEXT("actual rendering conditions differ from 1080p normal frame timing")); return false;
	}
	if (!bRenderingValidated)
	{
		bRenderingValidated = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe RENDER: Peer=%s Cycle=%d Viewport=%dx%d RHI=%s Adapter=%s Driver=%s Vendor=%u ScreenPercentage=100 VSync=0 DynamicRes=0 BackgroundIdle=0 MaxFPS=%.1f EditorCompiled=%d AppActive=%d Profile=%s"),
			*Peer, Cycle, Size.X, Size.Y, *RHI, *GRHIAdapterName, *GRHIAdapterUserDriverVersion, GRHIVendorId, Task27CVar(TEXT("t.MaxFPS")), WITH_EDITOR ? 1 : 0,
			FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive() ? 1 : 0, *Profile);
		for (const TCHAR* Name : { TEXT("sg.ViewDistanceQuality"), TEXT("sg.AntiAliasingQuality"), TEXT("sg.ShadowQuality"), TEXT("sg.GlobalIlluminationQuality"), TEXT("sg.ReflectionQuality"), TEXT("sg.PostProcessQuality"), TEXT("sg.TextureQuality"), TEXT("sg.EffectsQuality"), TEXT("sg.FoliageQuality"), TEXT("sg.ShadingQuality"), TEXT("r.Lumen.DiffuseIndirect.Allow"), TEXT("r.Lumen.Reflections.Allow"), TEXT("r.RayTracing"), TEXT("r.Shadow.Virtual.Enable") })
		{ UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe SETTING: Peer=%s Cycle=%d Name=%s Value=%.3f"), *Peer, Cycle, Name, Task27CVar(Name)); }
	}
	return true;
}
bool UMiniTask27ProbeSubsystem::Media()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniTask27Media")) || bScreenshotDone) { return true; }
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return false; }
#endif
	if (!PendingScreenshot.IsEmpty())
	{
		if (IFileManager::Get().FileSize(*PendingScreenshot) > 1024)
		{ bScreenshotDone = true; UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe SCREENSHOT_SAVED: Peer=%s Size=1920x1080 Path=%s"), *Peer, *PendingScreenshot); return true; }
		if (FPlatformTime::Seconds() - ScreenshotAt > 30) { Fail(TEXT("actual 1080p screenshot did not save")); }
		return false;
	}
	PendingScreenshot = FPaths::Combine(Output, FString::Printf(TEXT("%s-1080p.png"), *Peer));
	IFileManager::Get().Delete(*PendingScreenshot, false, true); ScreenshotAt = FPlatformTime::Seconds();
	FScreenshotRequest::RequestScreenshot(PendingScreenshot, true, false); return false;
}
void UMiniTask27ProbeSubsystem::DrainGPU(bool bRecord, double Now, int32 Round)
{
	uint64 Cycles64 = 0;
	for (;;)
	{
		const FRHIGPUFrameTimeHistory::EResult Result = GPUHistory.PopFrameCycles(Cycles64);
		if (Result == FRHIGPUFrameTimeHistory::EResult::Empty) { break; }
		const double MS = FPlatformTime::ToMilliseconds64(Cycles64);
		if (bRecord) { GPUFrames.Add({ Cycle, Round, Now - StartedAt, MS, Result == FRHIGPUFrameTimeHistory::EResult::Disjoint }); }
		if (bRecord && (!FMath::IsFinite(MS) || MS <= 0))
		{
			// Preserve an invalid result as observed; filtering it would hide a
			// measurement failure while retaining a misleading positive subset.
			SaveData();
			Fail(TEXT("measured GPU history returned a zero/invalid timing; capture is incomplete"));
			return;
		}
		if (bRecord && Result == FRHIGPUFrameTimeHistory::EResult::Disjoint)
		{
			// One Disjoint is a gap of unknown size, not one lost frame. Retain
			// the actual partial capture, but never accept a biased distribution.
			SaveData();
			Fail(TEXT("measured GPU history contains an unknown-size gap; capture is incomplete"));
			return;
		}
	}
}
void UMiniTask27ProbeSubsystem::RecordFrame(double Now, int32 Round)
{
	const double MS = (Now - LastWall) * 1000.0;
	if (LastWall > 0 && FMath::IsFinite(MS) && MS > 0)
	{ Frames.Add({ Cycle, Round, Now - StartedAt, MS, FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(GRHIThreadTime) }); }
}
void UMiniTask27ProbeSubsystem::RecordNetwork(double Now)
{
	UNetDriver* Driver = GetTickableGameObjectWorld() ? GetTickableGameObjectWorld()->GetNetDriver() : nullptr;
	if (!Driver) { return; } // Practice is Standalone, not a fake zero-traffic multiplayer test.
	if (MeasuredDriver.Get() != Driver || LastNetworkAt == 0)
	{
		MeasuredDriver = Driver; LastNetworkAt = Now; LastInBytes = Driver->InTotalBytes; LastOutBytes = Driver->OutTotalBytes;
		LastInPackets = Driver->InTotalPackets; LastOutPackets = Driver->OutTotalPackets; LastReliableOut = Driver->OutTotalReliableBunches; return;
	}
	const double Seconds = Now - LastNetworkAt; if (Seconds < 1) { return; }
	const uint32 In = Driver->InTotalBytes - LastInBytes, Out = Driver->OutTotalBytes - LastOutBytes;
	NetworkCSV += FString::Printf(TEXT("%d,%.6f,%.6f,%u,%u,%u,%u,%u,%.3f,%.3f\n"), Cycle, Now - StartedAt, Seconds, In, Out,
		Driver->InTotalPackets - LastInPackets, Driver->OutTotalPackets - LastOutPackets, Driver->OutTotalReliableBunches - LastReliableOut, In / Seconds, Out / Seconds);
	LastNetworkAt = Now; LastInBytes = Driver->InTotalBytes; LastOutBytes = Driver->OutTotalBytes;
	LastInPackets = Driver->InTotalPackets; LastOutPackets = Driver->OutTotalPackets; LastReliableOut = Driver->OutTotalReliableBunches;
}
bool UMiniTask27ProbeSubsystem::Snapshot(const TCHAR* StageName)
{
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task27PC(World); AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	UMiniHUDLayout* HUD = Task27HUD(World); UMiniHUDViewModel* VM = HUD ? HUD->GetViewModel() : nullptr; UMiniPrimaryGameLayout* Root = Task27Root(World);
	if (!GameplayReady() || !PC || !Pawn || !HUD || !VM || !Root || !Task27ASC(World)) { return false; }
	int32 Actors = 0, Components = 0, PlayerStates = 0, Pawns = 0, Controllers = 0, Entries = 0, Objects = 0, WeakOldAlive = 0, Listeners = 0;
	for (TActorIterator<AActor> It(World); It; ++It) { if (!It->IsActorBeingDestroyed()) { ++Actors; PlayerStates += It->IsA<AMiniPlayerState>() ? 1 : 0; Pawns += It->IsA<AMiniCharacter>() ? 1 : 0; } }
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It) { if (!It->IsActorBeingDestroyed()) { ++Controllers; Entries += It->GetInventoryManager()->GetEntries().Num(); } }
	for (TObjectIterator<UActorComponent> It; It; ++It) { Components += !It->IsTemplate() && It->GetWorld() == World && It->IsRegistered() ? 1 : 0; }
	for (TObjectIterator<UObject> It; It; ++It) { Objects += !It->IsTemplate() ? 1 : 0; }
	for (const TWeakObjectPtr<UWorld>& Old : OldWorlds) { WeakOldAlive += Old.IsValid() ? 1 : 0; }
	TArray<UMiniHUDDataWidget*> Widgets; HUD->GetExtensionWidgets(Widgets); for (const UMiniHUDDataWidget* Widget : Widgets) { Listeners += Widget ? Widget->GetMessageListenerCount() : 0; }
	const int32 Bindings = VM->GetBindingCount(), Specs = Task27ASC(World)->GetActivatableAbilities().Num();
	if (BaselineBindings < 0) { BaselineBindings = Bindings; BaselineSpecs = Specs; BaselineHUDListeners = Listeners; }
	const bool bBindingsStable = Bindings == BaselineBindings && Specs == BaselineSpecs && Listeners == BaselineHUDListeners && HUD->GetExtensionPointCount() == 4 && !Root->GetGameplayInputBlockCount();
	bool bWorldStable = true;
	if (Mode == TEXT("RestartTrend"))
	{
		if (BaselineActorCount < 0) { BaselineActorCount = Actors; BaselineComponentCount = Components; }
		bWorldStable = Actors == BaselineActorCount && Components == BaselineComponentCount && !WeakOldAlive && PlayerStates == 4 && Pawns == 4 &&
			Controllers == (Role == TEXT("Server") ? 4 : 1) && Entries == (Role == TEXT("Server") ? 8 : 2);
	}
	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
	const uint64 DiagnosticArrayStringBytes = Frames.GetAllocatedSize() + GPUFrames.GetAllocatedSize() + SnapshotCSV.GetAllocatedSize() + NetworkCSV.GetAllocatedSize();
	SnapshotCSV += FString::Printf(TEXT("%d,%s,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llu,%llu,%llu,%llu,1,%d,%d,%d,%d,%d,%d,%d,%llu\n"),
		Cycle, StageName, FPlatformTime::Seconds() - StartedAt, Task27Round(World), Actors, Components, PlayerStates, Pawns, Controllers, Entries, Objects,
		GUObjectArray.GetObjectArrayNumMinusAvailable(), WeakOldAlive, Memory.UsedPhysical, Memory.UsedVirtual, Memory.PeakUsedPhysical, Memory.PeakUsedVirtual,
		HUD->GetExtensionWidgetCount(), HUD->GetExtensionPointCount(), Bindings, Listeners, Pawn->GetHeroComponent()->GetInputBindingCount(), Specs, Root->GetGameplayInputBlockCount(), DiagnosticArrayStringBytes);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe SNAPSHOT: Peer=%s Cycle=%d Stage=%s Actors=%d Components=%d Objects=%d OldWorldWeakAlive=%d WorkingSetMiB=%.2f CommittedMiB=%.2f HUD=1 Widgets=4 Bindings=%d Listeners=%d Specs=%d"),
		*Peer, Cycle, StageName, Actors, Components, Objects, WeakOldAlive, double(Memory.UsedPhysical) / 1048576.0, double(Memory.UsedVirtual) / 1048576.0, Bindings, Listeners, Specs);
	if (!bBindingsStable || !bWorldStable)
	{
		// Preserve actual failure counts; a failed trend must remain diagnosable.
		SaveData();
		Fail(!bBindingsStable ? TEXT("equivalent live snapshot accumulated UI bindings or ability specs") : TEXT("ordinary restarts accumulated live World actors/components or owner inventory"));
		return false;
	}
	return true;
}
void UMiniTask27ProbeSubsystem::BeginSampling(double Now)
{
	SampleAt = Now; RoundAtStart = Task27Round(GetTickableGameObjectWorld()); FrameStartIndex = Frames.Num(); GPUStartIndex = GPUFrames.Num();
	LastNetworkAt = 0; MeasuredDriver.Reset(); DrainGPU(false, Now, RoundAtStart); Stage = EStage::Sampling;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe SAMPLE_BEGIN: Peer=%s Cycle=%d Round=%d WarmupSeconds=%.1f DurationSeconds=%.1f"), *Peer, Cycle, RoundAtStart, WarmupSeconds, SampleSeconds);
}
bool UMiniTask27ProbeSubsystem::SaveData() const
{
	FString FrameCSV = TEXT("Cycle,Round,WallSeconds,FrameMS,GameBusyMS,RenderBusyMS,RHIBusyMS\n");
	for (const FMiniTask27Frame& F : Frames) { FrameCSV += FString::Printf(TEXT("%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f\n"), F.Cycle, F.Round, F.Wall, F.FrameMS, F.GameMS, F.RenderMS, F.RHIMS); }
	FString GPUCSV = TEXT("Cycle,Round,ReadWallSeconds,UniqueGPUFrameMS,Disjoint\n");
	for (const FMiniTask27GPU& F : GPUFrames) { GPUCSV += FString::Printf(TEXT("%d,%d,%.6f,%.6f,%d\n"), F.Cycle, F.Round, F.Wall, F.MS, F.bDisjoint ? 1 : 0); }
	return FFileHelper::SaveStringToFile(FrameCSV, *FPaths::Combine(Output, Peer + TEXT("-frames.csv"))) &&
		FFileHelper::SaveStringToFile(GPUCSV, *FPaths::Combine(Output, Peer + TEXT("-gpu.csv"))) &&
		FFileHelper::SaveStringToFile(SnapshotCSV, *FPaths::Combine(Output, Peer + TEXT("-snapshots.csv"))) &&
		FFileHelper::SaveStringToFile(NetworkCSV, *FPaths::Combine(Output, Peer + TEXT("-network.csv")));
}
bool UMiniTask27ProbeSubsystem::CompleteCycle(double Now)
{
	if (!Snapshot(TEXT("SettledEnd"))) { return false; }
	TArray<double> Wall, Game, Render, RHI, GPU; int32 OverBudget = 0, Disjoint = 0; double Sum = 0;
	for (int32 I = FrameStartIndex; I < Frames.Num(); ++I)
	{ const FMiniTask27Frame& F = Frames[I]; Wall.Add(F.FrameMS); Game.Add(F.GameMS); Render.Add(F.RenderMS); RHI.Add(F.RHIMS); Sum += F.FrameMS; OverBudget += F.FrameMS > 16.666667 ? 1 : 0; }
	for (int32 I = GPUStartIndex; I < GPUFrames.Num(); ++I) { GPU.Add(GPUFrames[I].MS); Disjoint += GPUFrames[I].bDisjoint ? 1 : 0; }
	const int32 MinimumFrames = Mode == TEXT("RestartTrend") ? 20 : 60;
	const int32 MinimumGPUFrames = Mode == TEXT("RestartTrend") ? 10 : 30;
	if (Wall.Num() < MinimumFrames || GPU.Num() < MinimumGPUFrames || Sum <= 0 || Disjoint != 0)
	{
		SaveData();
		Fail(TEXT("missing normal rendered frame/GPU samples or incomplete GPU history"));
		return false;
	}
	if (!SaveData()) { Fail(TEXT("measurement files could not be saved")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe SAMPLE_END: Peer=%s Cycle=%d Frames=%d UniqueGPUFrames=%d GPUDisjoint=%d MeasuredSeconds=%.3f FPS=%.3f FrameMedianMS=%.3f FrameP95MS=%.3f FrameP99MS=%.3f GameP95MS=%.3f RenderP95MS=%.3f RHIP95MS=%.3f GPUP95MS=%.3f Over16_67Percent=%.2f Target60Met=%d Near60TargetMet=%d Near60MinimumFPS=59 Near60MaximumP95MS=18"),
		*Peer, Cycle, Wall.Num(), GPU.Num(), Disjoint, Sum / 1000.0, 1000.0 * Wall.Num() / Sum, Task27Percentile(Wall, 0.5), Task27Percentile(Wall, 0.95), Task27Percentile(Wall, 0.99),
		Task27Percentile(Game, 0.95), Task27Percentile(Render, 0.95), Task27Percentile(RHI, 0.95), Task27Percentile(GPU, 0.95), 100.0 * OverBudget / Wall.Num(),
		(1000.0 * Wall.Num() / Sum >= 60.0 && Task27Percentile(Wall, 0.95) <= 16.666667) ? 1 : 0,
		(1000.0 * Wall.Num() / Sum >= 59.0 && Task27Percentile(Wall, 0.95) <= 18.0) ? 1 : 0);
	if (!WriteSignal(TEXT("Measured"))) { Fail(TEXT("measurement signal could not be saved")); return false; }
	Stage = EStage::AwaitPeers; return true;
}
FString UMiniTask27ProbeSubsystem::SignalPath(const TCHAR* Kind, const FString& Name) const
{ return FPaths::Combine(Signals, FString::Printf(TEXT("Cycle%d-%s-%s.signal"), Cycle, Kind, *Name)); }
bool UMiniTask27ProbeSubsystem::WriteSignal(const TCHAR* Kind) const
{ return FFileHelper::SaveStringToFile(TEXT("Observed actual state by this peer."), *SignalPath(Kind, Peer)); }
bool UMiniTask27ProbeSubsystem::HasSignal(const TCHAR* Kind, const FString& Name) const
{ return IFileManager::Get().FileExists(*SignalPath(Kind, Name)); }
bool UMiniTask27ProbeSubsystem::AllPeersHave(const TCHAR* Kind) const
{
	if (Mode == TEXT("Practice")) { return HasSignal(Kind, TEXT("Server")); }
	for (const TCHAR* Name : { TEXT("Server"), TEXT("ClientA"), TEXT("ClientB"), TEXT("ClientC") }) { if (!HasSignal(Kind, Name)) { return false; } }
	return true;
}
void UMiniTask27ProbeSubsystem::TickObserveArena(double Now)
{
	UWorld* World = GetTickableGameObjectWorld(); UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>(); UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	if (!Matches || !Phases) { return; }
	const FMiniMatchState& Match = Matches->GetCurrentMatchState(); const FMiniGamePhaseState& Phase = Phases->GetCurrentPhaseState();
	if (Match.bHasResult) { CompletedRounds.Add(Match.RoundId); }
	if (!bObservedReady)
	{
		if (!GameplayReady() || !ValidateRendering() || !Media()) { return; }
		ObservedPlayerState = Task27PS(World); ObservedASC = Task27ASC(World);
		if (!Snapshot(TEXT("ObserveBegin"))) { return; }
		bObservedReady = true; FrameStartIndex = Frames.Num(); GPUStartIndex = GPUFrames.Num();
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe READY: Peer=%s Cycle=%d ActualRoster=4 ObserveTask23=1"), *Peer, Cycle);
	}
	if (Task27PS(World) != ObservedPlayerState.Get() || Task27ASC(World) != ObservedASC.Get() || Match.ConnectedPlayerCount != 4)
	{ Fail(TEXT("combat observation replaced PS/ASC or lost its real four-player roster")); return; }
	if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing)
	{ DrainGPU(false, Now, Match.RoundId); RecordNetwork(Now); return; }
	if (ObserveRound != Match.RoundId) { ObserveRound = Match.RoundId; ObserveRoundAt = Now; DrainGPU(false, Now, Match.RoundId); }
	const bool bMeasure = Now - ObserveRoundAt > WarmupSeconds;
	if (bMeasure) { RecordFrame(Now, Match.RoundId); SampledRounds.Add(Match.RoundId); ObserveMeasuredSeconds += Now - LastWall; }
	DrainGPU(bMeasure, Now, Match.RoundId); RecordNetwork(Now);
	if (bFailed) { return; }
	if (Match.RoundId >= 3 && CompletedRounds.Contains(1) && CompletedRounds.Contains(2) && SampledRounds.Contains(1) && SampledRounds.Contains(2) && bMeasure)
	{
		if (!CompleteCycle(Now)) { return; }
		bDone = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe CAPTURE_PASS: Mode=ObserveArena Peer=%s Cycles=1 CompletedRounds=2 PlayingSamplesSeconds=%.3f ActualTask23Combat=1 TargetAssessmentInSampleEnd=1"), *Peer, ObserveMeasuredSeconds);
	}
}
void UMiniTask27ProbeSubsystem::Tick(float DeltaTime)
{
	if (!IsTickable() || LastFrame == GFrameCounter) { return; } LastFrame = GFrameCounter;
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds) { Fail(TEXT("performance acceptance timed out")); return; }
	UWorld* World = GetTickableGameObjectWorld(); if (!World || !World->HasBegunPlay() || World->bIsTearingDown) { LastWall = Now; return; }
	if (!ObserveWorld()) { LastWall = Now; return; }
	UMiniExperienceManagerComponent* Manager = Task27Manager(World);
	if (!bServerJoinReady && Role == TEXT("Server") && Mode != TEXT("Practice") && World->GetNetMode() == NM_ListenServer && World->GetNetDriver() && Manager && Manager->IsExperienceLoaded())
	{
		bServerJoinReady = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe SERVER_JOIN_READY: Peer=%s Cycle=%d ActualListen=1 ProductionLoaded=1 NetDriver=%s"), *Peer, Cycle, *World->GetNetDriver()->GetName());
	}
	if (Mode == TEXT("ObserveArena")) { TickObserveArena(Now); LastWall = Now; return; }
	const int32 Round = Task27Round(World);
	if (Stage != EStage::Sampling) { DrainGPU(false, Now, Round); }
	if (Stage == EStage::AwaitReady)
	{
		if (!GameplayReady() || !ValidateRendering() || !Media()) { LogWait(TEXT("ProductionGameplayReadyAndNormalRendering")); LastWall = Now; return; }
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { ReadyAt = 0; LastWall = Now; return; }
#endif
		if (ReadyAt == 0) { ReadyAt = Now; }
		if (Now - ReadyAt < WarmupSeconds) { LastWall = Now; return; }
		AMiniPlayerState* PS = Task27PS(World); UMiniAbilitySystemComponent* ASC = Task27ASC(World);
		if (SeenPlayerStates.Contains(FObjectKey(PS)) || SeenASCs.Contains(FObjectKey(ASC))) { Fail(TEXT("ordinary restart reused an old PlayerState or ASC")); return; }
		ObservedPlayerState = PS; ObservedASC = ASC;
		if (!Snapshot(TEXT("SettledBegin"))) { LastWall = Now; return; }
		if (!WriteSignal(TEXT("Ready"))) { Fail(TEXT("ready signal could not be saved")); return; }
		SeenPlayerStates.Add(FObjectKey(PS)); SeenASCs.Add(FObjectKey(ASC));
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe READY: Peer=%s Cycle=%d ActualRoster=%d NewPSASC=1 WarmupSeconds=%.1f"), *Peer, Cycle, Mode == TEXT("Practice") ? 1 : 4, WarmupSeconds);
		Stage = EStage::AwaitStart;
	}
	if (Stage == EStage::AwaitStart)
	{
		if (Role == TEXT("Server") && AllPeersHave(TEXT("Ready")) && !HasSignal(TEXT("Start"), TEXT("Server")))
		{ if (!WriteSignal(TEXT("Start"))) { Fail(TEXT("start signal failed")); return; } }
		if (HasSignal(TEXT("Start"), TEXT("Server"))) { BeginSampling(Now); }
	}
	if (Stage == EStage::Sampling)
	{
		if (!StableContext() || !ValidateRendering()) { Fail(TEXT("production four-player context changed while sampling")); return; }
		const double Elapsed = Now - SampleAt; const bool bMeasured = Elapsed >= 1 && Elapsed < SampleSeconds - 1;
		if (bMeasured) { RecordFrame(Now, Round); }
		DrainGPU(bMeasured, Now, Round); RecordNetwork(Now);
		if (bFailed) { LastWall = Now; return; }
		if (Elapsed >= SampleSeconds && !CompleteCycle(Now)) { LastWall = Now; return; }
	}
	if (Stage == EStage::AwaitPeers && AllPeersHave(TEXT("Measured")))
	{
		if (Mode != TEXT("RestartTrend") || Cycle == Restarts + 1)
		{
			bDone = true; Stage = EStage::Finished;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe CAPTURE_PASS: Mode=%s Peer=%s Cycles=%d ActualRestarts=%d NormalRendered=1 MemoryTrendMeasured=%d TargetAssessmentInSampleEnd=1"), *Mode, *Peer, Cycle, Mode == TEXT("RestartTrend") ? Cycle - 1 : 0, Mode == TEXT("RestartTrend") ? 1 : 0);
		}
		else
		{
			Stage = EStage::AwaitRestart;
			if (Role == TEXT("Server"))
			{
				UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
				if (!Travel || !Travel->RestartArena()) { Fail(TEXT("production RestartArena request rejected")); return; }
				UE_LOG(LogMiniInit, Display, TEXT("MiniTask27Probe REAL_RESTART: Peer=%s FromCycle=%d OrdinaryTravel=1 ProductionAPI=1"), *Peer, Cycle);
			}
		}
	}
	LastWall = Now;
}
