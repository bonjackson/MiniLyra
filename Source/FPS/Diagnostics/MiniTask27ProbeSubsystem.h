#pragma once

#include "GPUProfiler.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UObject/ObjectKey.h"
#include "MiniTask27ProbeSubsystem.generated.h"

class AMiniPlayerController;
class AMiniPlayerState;
class UMiniAbilitySystemComponent;
class UNetDriver;

struct FMiniTask27Frame
{
	int32 Cycle = 0;
	int32 Round = 0;
	double Wall = 0;
	double FrameMS = 0;
	double GameMS = 0;
	double RenderMS = 0;
	double RHIMS = 0;
};
struct FMiniTask27GPU
{
	int32 Cycle = 0;
	int32 Round = 0;
	double Wall = 0;
	double MS = 0;
	bool bDisjoint = false;
};

/** Opt-in Development measurements. It never changes phase, score or Pawn state. */
UCLASS()
class FPS_API UMiniTask27ProbeSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
private:
	enum class EStage : uint8 { AwaitReady, AwaitStart, Sampling, AwaitPeers, AwaitRestart, Finished };
	void Fail(const TCHAR* Reason);
	void LogWait(const TCHAR* Reason);
	bool GameplayReady() const;
	bool StableContext() const;
	bool ObserveWorld();
	bool ValidateRendering();
	bool Media();
	void DrainGPU(bool bRecord, double Now, int32 Round);
	void RecordFrame(double Now, int32 Round);
	void RecordNetwork(double Now);
	bool Snapshot(const TCHAR* StageName);
	void BeginSampling(double Now);
	bool CompleteCycle(double Now);
	bool SaveData() const;
	void TickObserveArena(double Now);
	bool WriteSignal(const TCHAR* Kind) const;
	bool HasSignal(const TCHAR* Kind, const FString& Name) const;
	bool AllPeersHave(const TCHAR* Kind) const;
	FString SignalPath(const TCHAR* Kind, const FString& Name) const;
	FString Mode;
	FString Role;
	FString Peer;
	FString Output;
	FString Signals;
	FString Profile;
	FString RunId;
	FString PendingScreenshot;
	FString SnapshotCSV;
	FString NetworkCSV;
	TArray<FMiniTask27Frame> Frames;
	TArray<FMiniTask27GPU> GPUFrames;
	TArray<TWeakObjectPtr<UWorld>> OldWorlds;
	TSet<FObjectKey> SeenWorlds;
	TSet<FObjectKey> SeenPlayerStates;
	TSet<FObjectKey> SeenASCs;
	TSet<int32> CompletedRounds;
	TSet<int32> SampledRounds;
	TWeakObjectPtr<UWorld> ObservedWorld;
	TWeakObjectPtr<AMiniPlayerState> ObservedPlayerState;
	TWeakObjectPtr<UMiniAbilitySystemComponent> ObservedASC;
	TWeakObjectPtr<UNetDriver> MeasuredDriver;
	FRHIGPUFrameTimeHistory::FState GPUHistory;
	EStage Stage = EStage::AwaitReady;
	int32 Cycle = 0;
	int32 Restarts = 5;
	int32 TimeoutSeconds = 900;
	int32 RoundAtStart = 0;
	int32 ObserveRound = 0;
	int32 FrameStartIndex = 0;
	int32 GPUStartIndex = 0;
	int32 BaselineBindings = -1;
	int32 BaselineSpecs = -1;
	int32 BaselineHUDListeners = -1;
	int32 BaselineActorCount = -1;
	int32 BaselineComponentCount = -1;
	uint32 LastInBytes = 0;
	uint32 LastOutBytes = 0;
	uint32 LastInPackets = 0;
	uint32 LastOutPackets = 0;
	uint32 LastReliableOut = 0;
	uint64 LastFrame = MAX_uint64;
	double StartedAt = 0;
	double ReadyAt = 0;
	double SampleAt = 0;
	double ObserveRoundAt = 0;
	double LastWall = 0;
	double LastNetworkAt = 0;
	double LastWaitAt = 0;
	double ScreenshotAt = 0;
	double WarmupSeconds = 10;
	double SampleSeconds = 45;
	double ObserveMeasuredSeconds = 0;
	bool bInitialized = false;
	bool bFailed = false;
	bool bDone = false;
	bool bScreenshotDone = false;
	bool bRenderingValidated = false;
	bool bObservedReady = false;
	bool bServerJoinReady = false;
};
