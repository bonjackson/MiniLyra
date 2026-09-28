#include "MiniGameState.h"

#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "EngineUtils.h"
#include "GameFeatures/MiniFeatureMarkerComponent.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "TimerManager.h"

namespace
{
int32 GFeatureProbeCyclesCompleted = 0;

const TCHAR* GetMiniNetModeName(const ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone: return TEXT("Standalone");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	case NM_ListenServer: return TEXT("ListenServer");
	case NM_Client: return TEXT("Client");
	default: return TEXT("Unknown");
	}
}
}

AMiniGameState::AMiniGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ExperienceManagerComponent = CreateDefaultSubobject<UMiniExperienceManagerComponent>(TEXT("ExperienceManagerComponent"));
}

void AMiniGameState::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bProbeInitStates = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeInitStates"));
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeInitOrder="), InitProbeOrder);
#endif
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbePlayerSpawns")) || bProbeInitStates)
	{
		GetWorldTimerManager().SetTimer(PlayerSpawnProbeTimer, this, &ThisClass::LogPlayerSpawnProbeSnapshot, 0.5f, true);
		LogPlayerSpawnProbeSnapshot();
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeExperienceFlow")))
	{
		return;
	}
	if (!ExperienceManagerComponent)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFlowProbe FAIL: ExperienceManagerComponent is missing"));
		return;
	}

	ExperienceManagerComponent->CallOrRegister_OnExperienceLoaded(
		FOnMiniExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::HandleFlowProbeLoaded));
	ExperienceManagerComponent->CallOrRegister_OnExperienceFailed(
		FOnMiniExperienceFailed::FDelegate::CreateUObject(this, &ThisClass::HandleFlowProbeFailed));
}

void AMiniGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PlayerSpawnProbeTimer);
	Super::EndPlay(EndPlayReason);
}

void AMiniGameState::LogPlayerSpawnProbeSnapshot()
{
	int32 PlayerStates = 0;
	for (const APlayerState* State : PlayerArray)
	{
		PlayerStates += Cast<AMiniPlayerState>(State) ? 1 : 0;
	}

	int32 Characters = 0;
	int32 ValidCharacters = 0;
	int32 CharacterMarkers = 0;
	for (TActorIterator<AMiniCharacter> It(GetWorld()); It; ++It)
	{
		const AMiniCharacter* Character = *It;
		++Characters;
		const AMiniPlayerState* State = Character->GetPlayerState<AMiniPlayerState>();
		const UMiniPawnData* PawnData = Character->GetPawnData();
		if (State && PawnData && State->GetPawnData() == PawnData && !State->IsOnlyASpectator() &&
			PawnData->PawnClass && Character->IsA(PawnData->PawnClass.Get()))
		{
			++ValidCharacters;
		}
		const UMiniCharacterFeatureMarkerComponent* Marker = Character->FindComponentByClass<UMiniCharacterFeatureMarkerComponent>();
		CharacterMarkers += Marker && Marker->IsFeatureActive() ? 1 : 0;
	}

	int32 LocalPawn = 0;
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		const APlayerController* Controller = *It;
		LocalPawn += Controller->IsLocalController() && Cast<AMiniCharacter>(Controller->GetPawn()) ? 1 : 0;
	}
	UE_LOG(LogMiniExperience, Display,
		TEXT("MiniSpawnProbe SNAPSHOT: NetMode=%s PlayerStates=%d Characters=%d ValidCharacters=%d LocalPawn=%d CharacterMarkers=%d"),
		GetMiniNetModeName(GetNetMode()), PlayerStates, Characters, ValidCharacters, LocalPawn, CharacterMarkers);
	if (bProbeInitStates)
	{
		LogInitStateProbeSnapshot();
	}
}

void AMiniGameState::LogInitStateProbeSnapshot()
{
	int32 PlayerStates = 0;
	for (const APlayerState* State : PlayerArray)
	{
		PlayerStates += Cast<AMiniPlayerState>(State) ? 1 : 0;
	}

	int32 Characters = 0;
	int32 ExtensionDataInitialized = 0;
	int32 HeroDataInitialized = 0;
	int32 SimulatedDataInitialized = 0;
	int32 SimulatedWithoutLocalInputInitialized = 0;
	int32 GameplayReady = 0;
	int32 ProbeReleased = 0;
	int32 RepeatNotified = 0;
	for (TActorIterator<AMiniCharacter> It(GetWorld()); It; ++It)
	{
		AMiniCharacter* Character = *It;
		++Characters;
		const AMiniPlayerState* PlayerState = Character->GetPlayerState<AMiniPlayerState>();
		const bool bRealDependenciesArrived = Character->GetPawnData() && PlayerState &&
			PlayerState->GetPawnData() == Character->GetPawnData();
		if (Character->IsInitOrderProbeEnabled() && bRealDependenciesArrived)
		{
			uint8& Stage = InitProbeStages.FindOrAdd(Character);
			if (Stage == 0)
			{
				if (InitProbeOrder == TEXT("DataFirst"))
				{
					Character->ReleaseInitProbePawnData();
				}
				else
				{
					Character->ReleaseInitProbePlayerState();
				}
				Stage = 1;
				const UMiniPawnExtensionComponent* Extension = Character->GetPawnExtensionComponent();
				const UMiniHeroComponent* Hero = Character->GetHeroComponent();
				if (!Extension || !Hero || Extension->GetInitState() != MiniGameplayTags::InitState_Spawned ||
					Hero->GetInitState() != MiniGameplayTags::InitState_Spawned)
				{
					UE_LOG(LogMiniInit, Error, TEXT("MiniInitProbe ORDER_FAIL: first release advanced too early Pawn=%s"),
						*Character->GetPathName());
				}
				UE_LOG(LogMiniInit, Display, TEXT("MiniInitProbe FIRST_RELEASE: Order=%s Pawn=%s"),
					*InitProbeOrder, *Character->GetPathName());
			}
			else if (Stage == 1)
			{
				if (InitProbeOrder == TEXT("DataFirst"))
				{
					Character->ReleaseInitProbePlayerState();
				}
				else
				{
					Character->ReleaseInitProbePawnData();
				}
				Stage = 2;
				UE_LOG(LogMiniInit, Display, TEXT("MiniInitProbe SECOND_RELEASE: Order=%s Pawn=%s"),
					*InitProbeOrder, *Character->GetPathName());
			}
		}

		const UMiniPawnExtensionComponent* Extension = Character->GetPawnExtensionComponent();
		const UMiniHeroComponent* Hero = Character->GetHeroComponent();
		const bool bExtensionInitialized = Extension && Extension->HasReachedInitState(MiniGameplayTags::InitState_DataInitialized);
		const bool bHeroInitialized = Hero && Hero->HasReachedInitState(MiniGameplayTags::InitState_DataInitialized);
		ExtensionDataInitialized += bExtensionInitialized ? 1 : 0;
		HeroDataInitialized += bHeroInitialized ? 1 : 0;
		SimulatedDataInitialized += Character->GetLocalRole() == ROLE_SimulatedProxy &&
			bExtensionInitialized && bHeroInitialized ? 1 : 0;
		SimulatedWithoutLocalInputInitialized += Character->GetLocalRole() == ROLE_SimulatedProxy &&
			!Character->GetController() && !Character->HasInputComponentForProbe() &&
			bExtensionInitialized && bHeroInitialized ? 1 : 0;
		GameplayReady += (Extension && Extension->HasReachedInitState(MiniGameplayTags::InitState_GameplayReady)) ||
			(Hero && Hero->HasReachedInitState(MiniGameplayTags::InitState_GameplayReady)) ? 1 : 0;
		ProbeReleased += InitProbeStages.FindRef(Character) == 2 ? 1 : 0;

		if (bExtensionInitialized && bHeroInitialized && !RepeatedNotificationPawns.Contains(Character))
		{
			Character->NotifyInitDependenciesChanged();
			Character->NotifyInitDependenciesChanged();
			Character->NotifyInitDependenciesChanged();
			RepeatedNotificationPawns.Add(Character);
			UE_LOG(LogMiniInit, Display, TEXT("MiniInitProbe REPEAT_NOTIFIED: Pawn=%s Count=3"), *Character->GetPathName());
		}
		RepeatNotified += RepeatedNotificationPawns.Contains(Character) ? 1 : 0;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniInitProbe SNAPSHOT: NetMode=%s PlayerStates=%d Characters=%d ExtensionDataInitialized=%d HeroDataInitialized=%d SimulatedDataInitialized=%d SimulatedWithoutLocalInputInitialized=%d GameplayReady=%d ProbeReleased=%d RepeatNotified=%d"),
		GetMiniNetModeName(GetNetMode()), PlayerStates, Characters, ExtensionDataInitialized,
		HeroDataInitialized, SimulatedDataInitialized, SimulatedWithoutLocalInputInitialized,
		GameplayReady, ProbeReleased, RepeatNotified);
}

void AMiniGameState::HandleFlowProbeLoaded(const UMiniExperienceDefinition* Experience)
{
	bFlowProbeLateSubscriberCalled = false;
	ExperienceManagerComponent->CallOrRegister_OnExperienceLoaded(
		FOnMiniExperienceLoaded::FDelegate::CreateWeakLambda(this,
			[this](const UMiniExperienceDefinition* LateExperience)
			{
				bFlowProbeLateSubscriberCalled = LateExperience != nullptr;
			}));

	const TCHAR* NetModeName = GetMiniNetModeName(GetNetMode());
	if (Experience && bFlowProbeLateSubscriberCalled)
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniFlowProbe PASS: NetMode=%s ID=%s LateSubscriber=1"),
			NetModeName, *ExperienceManagerComponent->GetCurrentExperienceId().ToString());

		// A standalone, opt-in probe repeatedly tears down and recreates the
		// world in one process. This exercises action removal and plugin release
		// without changing the normal game flow or racing a remote client.
		int32 RequestedCycles = 0;
		if (GetNetMode() == NM_Standalone &&
			FParse::Value(FCommandLine::Get(), TEXT("MiniProbeFeatureCycles="), RequestedCycles) &&
			RequestedCycles > 0 && GFeatureProbeCyclesCompleted < RequestedCycles)
		{
			const int32 Cycle = ++GFeatureProbeCyclesCompleted;
			const bool bFinalCycle = Cycle == RequestedCycles;
			UE_LOG(LogMiniExperience, Display, TEXT("MiniFeatureCycle LOADED: Index=%d Total=%d"), Cycle, RequestedCycles);
			FTimerHandle TravelTimer;
			GetWorldTimerManager().SetTimer(TravelTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, bFinalCycle, Cycle]()
				{
					UE_LOG(LogMiniExperience, Display, TEXT("MiniFeatureCycle TRAVEL: Index=%d Final=%d"), Cycle, bFinalCycle);
					if (bFinalCycle)
					{
						UGameplayStatics::OpenLevel(this, TEXT("/Engine/Maps/Entry"), true, TEXT("game=/Script/Engine.GameModeBase"));
					}
					else
					{
						UGameplayStatics::OpenLevel(this, TEXT("/Game/Mini/Maps/L_MiniPractice"));
					}
				}), 0.25f, false);
		}
	}
	else
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFlowProbe FAIL: NetMode=%s ID=%s LateSubscriber=%d Experience=%s"),
			NetModeName, *ExperienceManagerComponent->GetCurrentExperienceId().ToString(),
			bFlowProbeLateSubscriberCalled, *GetNameSafe(Experience));
	}
}

void AMiniGameState::HandleFlowProbeFailed(const FString& Reason)
{
	const TCHAR* NetModeName = GetMiniNetModeName(GetNetMode());
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeInvalidExperience")))
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniFlowProbe FAIL_EXPECTED: NetMode=%s Reason=%s"), NetModeName, *Reason);
	}
	else
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFlowProbe FAIL: NetMode=%s Reason=%s"), NetModeName, *Reason);
	}
}
