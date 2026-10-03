#pragma once

#include "Diagnostics/MiniTask25ProbeActor.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UObject/ObjectKey.h"
#include "MiniTask25ProbeSubsystem.generated.h"

class AMiniGameMode;
class AMiniPlayerController;
class UMiniAbilitySystemComponent;
class UMiniEquipmentInstance;
class UMiniInventoryItemInstance;
class UMiniHeroComponent;
class UMiniHUDLayout;
class UMiniHUDViewModel;
class UMiniHUDDataWidget;
class UMiniDebugMenuWidget;
class UMiniPrimaryGameLayout;
class UMiniFrontEndWidget;
class UMiniConnectionStatusWidget;
class AMiniPracticeTarget;
class AMiniPracticeSupply;
class UMiniGameFeatureAction_AddActors;
class UMiniGameFeatureAction_AddWidgets;
class UWidget;
class UNetDriver;

/** Strong references preserve measurements after EndPlay; they do not prevent EndPlay. */
USTRUCT()
struct FMiniTask25OwnerRecord
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<AMiniPlayerController> PC;
	UPROPERTY(Transient) TObjectPtr<AMiniPlayerState> PS;
	UPROPERTY(Transient) TObjectPtr<UMiniAbilitySystemComponent> ASC;
	UPROPERTY(Transient) TObjectPtr<AMiniCharacter> Pawn;
	UPROPERTY(Transient) TObjectPtr<UMiniEquipmentInstance> CurrentEquipment;
	UPROPERTY(Transient) TObjectPtr<UMiniInventoryItemInstance> CurrentItem;
	UPROPERTY(Transient) TObjectPtr<AMiniCharacter> DeadPawn;
	UPROPERTY(Transient) TObjectPtr<UMiniEquipmentInstance> DeadEquipment;
	UPROPERTY(Transient) TObjectPtr<UMiniInventoryItemInstance> DeadItem;
	UPROPERTY(Transient) TObjectPtr<AMiniTask25ProbeActor> Probe;
	int32 Index = 0;
	uint32 Life = 0;
	int32 InitialKills = 0;
	int32 InitialDeaths = 0;
	int32 DeathsSeen = 0;
	int32 RespawnsSeen = 0;
	int32 VMBindings = -1;
	int32 AbilitySpecs = -1;
	int32 LastDeadIteration = 0;
	int32 LastLiveIteration = -1;
	int32 FrozenMagazine = -1;
	int32 FrozenReserve = -1;
	bool bFrozenAmmo = false;
	TSet<FGuid> SeenItemIds;
};

USTRUCT()
struct FMiniTask25OldWorld
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWorld> World;
	UPROPERTY(Transient) TObjectPtr<AMiniGameMode> GM;
	UPROPERTY(Transient) TObjectPtr<AMiniPlayerController> PC;
	UPROPERTY(Transient) TObjectPtr<AMiniPlayerState> PS;
	UPROPERTY(Transient) TObjectPtr<AMiniCharacter> Pawn;
	UPROPERTY(Transient) TObjectPtr<UMiniAbilitySystemComponent> ASC;
	UPROPERTY(Transient) TObjectPtr<UMiniHeroComponent> Hero;
	UPROPERTY(Transient) TObjectPtr<UMiniEquipmentInstance> Equipment;
	UPROPERTY(Transient) TObjectPtr<UMiniInventoryItemInstance> Item;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDLayout> HUD;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDViewModel> VM;
	UPROPERTY(Transient) TObjectPtr<UMiniDebugMenuWidget> Menu;
	UPROPERTY(Transient) TObjectPtr<UMiniPrimaryGameLayout> Root;
	UPROPERTY(Transient) TObjectPtr<UMiniFrontEndWidget> Front;
	UPROPERTY(Transient) TObjectPtr<UMiniConnectionStatusWidget> Modal;
	UPROPERTY(Transient) TArray<TObjectPtr<UMiniHUDDataWidget>> Widgets;
	UPROPERTY(Transient) TArray<TObjectPtr<AMiniPracticeTarget>> Targets;
	UPROPERTY(Transient) TObjectPtr<AMiniPracticeSupply> Supply;
	UPROPERTY(Transient) TArray<TObjectPtr<UMiniGameFeatureAction_AddActors>> ActorActions;
	UPROPERTY(Transient) TArray<TObjectPtr<UMiniGameFeatureAction_AddWidgets>> WidgetActions;
	TArray<int32> TargetResets;
	TArray<int32> TargetDisables;
	FObjectKey PSKey;
	FObjectKey ASCKey;
	int32 SupplyRefills = -1;
	int32 VMRefreshes = -1;
	int32 Magazine = -1;
	int32 Reserve = -1;
	FVector PawnLocation = FVector::ZeroVector;
	double CleanupTime = 0;
	bool bRetained = false;
	bool bCleaned = false;
};

/** Development-only lifecycle acceptance. Travel uses production APIs, not fabricated UI delegates. */
UCLASS()
class FPS_API UMiniTask25ProbeSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
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
	void Fail(const TCHAR* Reason);
	void Pass(const TCHAR* Marker, const TCHAR* Evidence);
	void LogWait(const TCHAR* Reason);
	bool FrontEndReady(bool bError = false);
	bool GameplayReady(bool bArena, bool bFresh = true);
	bool VerifyLateLoaded();
	void DiscoverOwners();
	bool CaptureLife(FMiniTask25OwnerRecord& Record, int32 DeathIteration);
	bool CheckDead(FMiniTask25OwnerRecord& Record);
	bool ObserveOwner(AMiniPlayerController* PC, bool bDead, bool bFresh, FMiniTask25Observation& Out);
	bool Checkpoint(FName Name, int32 ExpectedClients, bool bDead = false, bool bFresh = true);
	AMiniTask25ProbeActor* LocalProbe() const;
	bool Acknowledge(AMiniTask25ProbeActor* Probe);
	bool DriveArmInput(AMiniTask25ProbeActor* Probe);
	bool Media(FName Stage);
	bool Click(UWidget* Widget, FName Action);
	void TickStressServer();
	void TickClient();
	void TickRoundTrip();
	void TickRecovery();
	bool BeginPausedFall(AMiniPlayerController* PC);
	void MakeSpawnBlockers();
	void RemoveSpawnBlockers();
	bool RevokeActualAction();
	bool PreparePracticeTravel();
	bool RetainWorld(bool bOpenMenu);
	void HandleCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	bool OldStateSilent(bool bWaitDelay = true);
	bool VerifyNewWorldIdentity();
	void ReleaseOld();
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, int32 Failure, const FString& Reason);
	bool WriteSignal(const TCHAR* Name) const;
	FString Mode;
	FString Role;
	FString Peer;
	FString Address;
	FString SignalDirectory;
	FString MediaDirectory;
	UPROPERTY(Transient) TArray<FMiniTask25OwnerRecord> Owners;
	UPROPERTY(Transient) FMiniTask25OwnerRecord LocalLife;
	UPROPERTY(Transient) FMiniTask25OldWorld Old;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> SpawnBlockers;
	UPROPERTY(Transient) TObjectPtr<AMiniPlayerController> RecoveryPC;
	UPROPERTY(Transient) TObjectPtr<AMiniCharacter> RecoveryPawn;
	UPROPERTY(Transient) TObjectPtr<AMiniPlayerState> RecoveryPS;
	UPROPERTY(Transient) TObjectPtr<UMiniAbilitySystemComponent> RecoveryASC;
	UPROPERTY(Transient) TObjectPtr<UMiniEquipmentInstance> RecoveryEquipment;
	TWeakObjectPtr<UWorld> LoadedObservedWorld;
	TWeakObjectPtr<UWorld> ClientProbeWorld;
	TSet<FObjectKey> SeenWorlds;
	TSet<FObjectKey> SeenPlayerStates;
	TSet<FObjectKey> SeenASCs;
	TSet<FName> MediaDone;
	TSet<int32> ArmedSerials;
	FString PendingMediaPath;
	FName PendingMediaStage;
	FName PublishedName;
	FMiniGamePhaseState PublishedPhase;
	FMiniMatchState PublishedMatch;
	uint32 LoadedGeneration = 0;
	uint32 RecoveryLife = 0;
	int32 LoadedCalls = 0;
	int32 Step = 0;
	int32 Iteration = 0;
	int32 Serial = 0;
	int32 LastClientSerial = 0;
	int32 NextOwnerIndex = 1;
	int32 ArmStep = 0;
	int32 ClientDeaths = 0;
	int32 ClientRespawns = 0;
	int32 CleanupCount = 0;
	int32 WorldsCount = 0;
	int32 PracticeCount = 0;
	int32 ArenaCount = 0;
	int32 NetworkFailures = 0;
	int32 TimeoutSeconds = 480;
	int32 InitialRound = 0;
	FVector MoveStart = FVector::ZeroVector;
	FVector RecoverySafeLocation = FVector::ZeroVector;
	double StartedAt = 0;
	double StepStartedAt = 0;
	double LastWaitAt = 0;
	double ArmStartedAt = 0;
	double MoveStartedAt = 0;
	double MediaStartedAt = 0;
	bool bInitialized = false;
	bool bDone = false;
	bool bFailed = false;
	bool bPublished = false;
	bool bLeaveRequested = false;
	bool bRejoinRequested = false;
	bool bHostLossArmed = false;
	bool bDismissRequested = false;
	bool bMovePressed = false;
	bool bMoveVerified = false;
	bool bInitialIdentityCaptured = false;
	FDelegateHandle CleanupHandle;
	FDelegateHandle NetworkHandle;
};
