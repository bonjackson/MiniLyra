#pragma once

#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MiniTravelSubsystem.generated.h"

class AGameStateBase;
class AMiniPlayerController;
class UCommonLocalPlayer;
class UNetDriver;
class UMiniConnectionStatusWidget;
class UMiniExperienceDefinition;
class UMiniExperienceManagerComponent;
class UMiniGameUIPolicy;
class UMiniPrimaryGameLayout;

UENUM(BlueprintType)
enum class EMiniTravelOperation : uint8
{
	None,
	Practice,
	Host,
	Join,
	Restart,
	ReturnToFrontEnd,
	Quit
};

USTRUCT(BlueprintType)
struct FPS_API FMiniTravelState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bBusy = false;
	UPROPERTY(BlueprintReadOnly) bool bHasError = false;
	UPROPERTY(BlueprintReadOnly) FText StatusText;
	UPROPERTY(BlueprintReadOnly) FText DetailText;
	UPROPERTY(BlueprintReadOnly) FString TargetAddress;
	UPROPERTY(BlueprintReadOnly) FString FailureCode;
	UPROPERTY(BlueprintReadOnly) EMiniTravelOperation Operation = EMiniTravelOperation::None;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FMiniTravelStateChanged, const FMiniTravelState&);

/** Local travel state outlives ordinary Worlds; every World/UI callback is generation scoped. */
UCLASS()
class FPS_API UMiniTravelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool StartPractice();
	bool HostArena();
	bool JoinAddress(const FString& Address);
	bool RestartArena();
	void ReturnToFrontEnd();
	void QuitGame();
	void DismissError();
	const FMiniTravelState& GetState() const { return State; }
	uint32 GetRequestGeneration() const { return RequestGeneration; }
	UMiniConnectionStatusWidget* GetConnectionStatusWidget() const;
	FMiniTravelStateChanged OnStateChanged;
	/** Called by the new local Controller in every ordinary World. */
	void ObserveLocalController(AMiniPlayerController* Controller);
	/** The engine return RPC otherwise discards its text reason. */
	void NotifyReturnReason(const FText& Reason);
	static bool TryNormalizeAddress(const FString& Address, FString& OutAddress);

private:
	void BindPolicy();
	void ObserveWorld(UWorld* World);
	void BindGameState(AGameStateBase* GameState);
	void HandlePostLoadMap(UWorld* World);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandleExperienceLoaded(const UMiniExperienceDefinition* Experience,
		TWeakObjectPtr<UWorld> World, uint32 WorldGeneration);
	void HandleExperienceFailed(const FString& Reason, TWeakObjectPtr<UWorld> World, uint32 WorldGeneration);
	void HandleRootReady(UCommonLocalPlayer* Player, UMiniPrimaryGameLayout* Root);
	void HandleRootUnavailable(UCommonLocalPlayer* Player, UMiniPrimaryGameLayout* Root);
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, int32 FailureType, const FString& Reason);
	void HandleTravelFailure(UWorld* World, int32 FailureType, const FString& Reason);
	void HandlePreClientTravel(const FString& URL, ETravelType Type, bool bSeamless);
	bool IsOwnNetworkContext(UWorld* World, UNetDriver* Driver) const;
	bool IsFrontEndReady() const;
	AMiniPlayerController* GetLocalController() const;
	bool BeginRequest(EMiniTravelOperation Operation, const FString& Map, const FText& Status);
	void BeginReturn(bool bPreserveError);
	void CommitState();
	void SetError(const FString& Code, const FText& Title, const FText& Detail);
	void RefreshModal();
	void RemoveModal();
	void StartWatchdog();
	void StopWatchdog();
	bool TickWatchdog(float DeltaTime);
	void ClearWorldBinding();
	void RequestExit();

	FMiniTravelState State;
	TWeakObjectPtr<UWorld> ObservedWorld;
	TWeakObjectPtr<UWorld> RequestSourceWorld;
	TWeakObjectPtr<AMiniPlayerController> LocalController;
	TWeakObjectPtr<UMiniExperienceManagerComponent> ObservedExperience;
	TWeakObjectPtr<UNetDriver> PendingRequestDriver;
	TWeakObjectPtr<UMiniGameUIPolicy> Policy;
	TWeakObjectPtr<UMiniPrimaryGameLayout> CurrentRoot;
	TWeakObjectPtr<UMiniPrimaryGameLayout> ModalRoot;
	TWeakObjectPtr<UMiniConnectionStatusWidget> Modal;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle PreClientTravelHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle GameStateSetHandle;
	FDelegateHandle RootReadyHandle;
	FDelegateHandle RootUnavailableHandle;
	FTSTicker::FDelegateHandle WatchdogHandle;
	FString TargetMap;
	double RequestDeadline = 0.0;
	uint32 RequestGeneration = 0;
	uint32 WorldGeneration = 0;
	bool bInitialized = false;
	bool bRefreshingModal = false;
	bool bQuitAfterReturn = false;
};
