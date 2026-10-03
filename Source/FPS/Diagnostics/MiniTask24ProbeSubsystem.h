#pragma once

#include "Diagnostics/MiniTask24ProbeActor.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "MiniTask24ProbeSubsystem.generated.h"

class AMiniPlayerController;
class AMiniPlayerState;
class UMiniAbilitySystemComponent;
class UMiniFrontEndWidget;
class UMiniConnectionStatusWidget;
class UMiniHUDLayout;
class UMiniHUDViewModel;
class UMiniDebugMenuWidget;
class UMiniPrimaryGameLayout;
class UWidget;
class UNetDriver;
struct FKey;

/** Development-only real UI/network flow acceptance, persistent across ordinary travel. */
UCLASS()
class FPS_API UMiniTask24ProbeSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
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
	bool FrontEndReady(bool bError = false);
	bool GameReady(bool bArena = true);
	bool VerifyError(const FString& Code);
	bool CloseError();
	bool Click(UWidget* Widget, FName Action);
	bool TypeAddress(const FString& Address);
	bool JoinThroughUI(const FString& Address);
	bool MenuAction(FName Action);
	bool Media(FName Stage);
	bool SendKey(FKey Key, bool bControl = false);
	void SaveIdentity();
	bool VerifyNewIdentity();
	void HandleCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, int32 Type, const FString& Reason);
	void DiscoverOwners();
	bool Checkpoint(FName Name, int32 ExpectedClients);
	AMiniTask24ProbeActor* LocalProbe() const;
	bool AcknowledgeCheckpoint(FName Name);
	void TickServer();
	void TickClient();
	void TickFlowServer();
	void TickFlowClient();
	void TickFailuresClient();
	void TickCapacityServer();
	void TickCapacityClient();
	void TickOverflow();
	void TickHostLossClient();
	void TickQuitServer();
	void TickQuitClient();
	void TickQuitFrontEnd();
	void TickHostFailure();
	void TickPracticeRoundTrip(const TCHAR* Marker);
	bool WriteSignal(const TCHAR* Name) const;
	bool HasSignal(const TCHAR* Name) const;
	void LogWait(const TCHAR* Reason);
	FString Mode;
	FString Role;
	FString Peer;
	FString Address;
	FString UnusedAddress;
	FString SignalDirectory;
	FString MediaDirectory;
	FName PublishedName;
	FMiniGamePhaseState PublishedPhase;
	FMiniMatchState PublishedMatch;
	TArray<TWeakObjectPtr<AMiniTask24ProbeActor>> OwnerProbes;
	TMap<TWeakObjectPtr<AMiniPlayerController>, TWeakObjectPtr<AMiniPlayerState>> CapacityPlayerStates;
	TMap<TWeakObjectPtr<AMiniPlayerController>, TWeakObjectPtr<UMiniAbilitySystemComponent>> CapacityASCs;
	TWeakObjectPtr<AMiniPlayerController> LastVictim;
	TWeakObjectPtr<AMiniCharacter> KilledPawn;
	TWeakObjectPtr<UWorld> SavedWorld;
	TWeakObjectPtr<AMiniPlayerState> SavedPlayerState;
	TWeakObjectPtr<UMiniAbilitySystemComponent> SavedASC;
	TWeakObjectPtr<UWorld> OwnerProbeWorld;
	UPROPERTY(Transient) TObjectPtr<UMiniFrontEndWidget> OldFrontEnd;
	UPROPERTY(Transient) TObjectPtr<UMiniConnectionStatusWidget> OldModal;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDLayout> OldHUD;
	UPROPERTY(Transient) TObjectPtr<UMiniHUDViewModel> OldVM;
	UPROPERTY(Transient) TObjectPtr<UMiniDebugMenuWidget> OldMenu;
	UPROPERTY(Transient) TObjectPtr<UMiniPrimaryGameLayout> OldRoot;
	TSet<FName> CapturedMedia;
	FName PendingMedia;
	FString PendingMediaPath;
	double StartedAt = 0.0;
	double StepStartedAt = 0.0;
	double LastInputAt = -5.0;
	double LastWaitAt = -5.0;
	double MediaRequestedAt = 0.0;
	int32 Step = 0;
	int32 PracticeStep = 0;
	int32 Serial = 0;
	int32 ClientLastSerial = 0;
	int32 NextOwnerIndex = 1;
	int32 NetworkFailures = 0;
	int32 CleanupCount = 0;
	int32 SavedCleanupCount = 0;
	bool bInitialized = false;
	bool bDone = false;
	bool bFailed = false;
	bool bMenuEscapeSent = false;
	bool bJoinTyped = false;
	bool bFrontLogged = false;
	bool bHostLogged = false;
	bool bIdentitySaved = false;
	bool bCapacityReleased = false;
	FDelegateHandle CleanupHandle;
	FDelegateHandle NetworkHandle;
	FDelegateHandle WidgetRebuildHandle;
};
