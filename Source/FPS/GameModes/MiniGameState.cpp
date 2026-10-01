#include "MiniGameState.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "AbilitySystem/MiniProbeAbility.h"
#include "AbilitySystem/MiniProbeAttributeSet.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "EngineUtils.h"
#include "GameFeatures/MiniFeatureMarkerComponent.h"
#include "GameFeatures/MiniGameFeatureAction_AddAbilities.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "Arena/MiniArenaRulesComponent.h"
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

struct FMiniAbilityProbeTotals
{
	int32 PlayerStates = 0;
	int32 PawnAbilities = 0;
	int32 FeatureAbilities = 0;
	int32 ActiveEffects = 0;
	int32 ProbeAttributes = 0;
	int32 HealthSets = 0;
	int32 BoundAvatars = 0;
};

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
	PhaseAbilitySystemComponent = CreateDefaultSubobject<UMiniAbilitySystemComponent>(TEXT("PhaseAbilitySystemComponent"));
	PhaseAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
}

UAbilitySystemComponent* AMiniGameState::GetAbilitySystemComponent() const
{
	return PhaseAbilitySystemComponent;
}

void AMiniGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	PhaseAbilitySystemComponent->InitAbilityActorInfo(this, this);
}

void AMiniGameState::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bProbeInitStates = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeInitStates"));
	bProbeAbilities = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeAbilities"));
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeInitOrder="), InitProbeOrder);
#endif
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbePlayerSpawns")) || bProbeInitStates || bProbeAbilities)
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
	if (UMiniArenaRulesComponent* Rules = FindComponentByClass<UMiniArenaRulesComponent>()) { Rules->StopArenaPhases(); }
	if (UMiniGamePhaseSubsystem* Phases = GetWorld() ? GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>() : nullptr)
	{
		Phases->ShutdownPhases();
	}
	PhaseAbilitySystemComponent->ClearActorInfo();
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
	if (bProbeAbilities)
	{
		LogAbilityProbeSnapshot();
	}
}

void AMiniGameState::LogAbilityProbeSnapshot()
{
	FMiniAbilityProbeTotals Totals;
	for (APlayerState* State : PlayerArray)
	{
		AMiniPlayerState* PlayerState = Cast<AMiniPlayerState>(State);
		if (!PlayerState)
		{
			continue;
		}
		++Totals.PlayerStates;
		UMiniAbilitySystemComponent* ASC = PlayerState->GetMiniAbilitySystemComponent();
		if (!ASC)
		{
			continue;
		}
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			Totals.PawnAbilities += Spec.Ability && Spec.Ability->IsA<UMiniPawnProbeAbility>() ? 1 : 0;
			Totals.FeatureAbilities += Spec.Ability && Spec.Ability->IsA<UMiniFeatureProbeAbility>() ? 1 : 0;
		}
		Totals.ActiveEffects += ASC->GetNumActiveGameplayEffects();
		for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
		{
			Totals.ProbeAttributes += Set && Set->IsA<UMiniProbeAttributeSet>() ? 1 : 0;
		}
		Totals.HealthSets += PlayerState->GetHealthSet() &&
			ASC->GetAttributeSet(UMiniHealthSet::StaticClass()) == PlayerState->GetHealthSet() ? 1 : 0;
		AActor* Avatar = ASC->GetAvatarActor();
		const AMiniCharacter* Character = Cast<AMiniCharacter>(Avatar);
		Totals.BoundAvatars += Character && Character->GetPlayerState<AMiniPlayerState>() == PlayerState ? 1 : 0;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniAbilityProbe SNAPSHOT: NetMode=%s Stage=%d PlayerStates=%d PawnAbilities=%d FeatureAbilities=%d Effects=%d ProbeAttributes=%d HealthSets=%d BoundAvatars=%d"),
		GetMiniNetModeName(GetNetMode()), AbilityProbeStage, Totals.PlayerStates,
		Totals.PawnAbilities, Totals.FeatureAbilities, Totals.ActiveEffects,
		Totals.ProbeAttributes, Totals.HealthSets, Totals.BoundAvatars);

	if (!HasAuthority() || Totals.PlayerStates < 2 || AbilityProbeStage >= 4)
	{
		return;
	}
	const UMiniExperienceDefinition* Experience = ExperienceManagerComponent
		? ExperienceManagerComponent->GetCurrentExperience() : nullptr;
	UMiniGameFeatureAction_AddAbilities* Action = nullptr;
	if (Experience)
	{
		for (UGameFeatureAction* Candidate : Experience->Actions)
		{
			if (Candidate && Candidate->GetFName() == TEXT("MiniTask09_AddAbilities"))
			{
				Action = Cast<UMiniGameFeatureAction_AddAbilities>(Candidate);
				break;
			}
		}
	}
	if (!Action)
	{
		return;
	}
	const bool bBaseReady = Totals.PawnAbilities == Totals.PlayerStates &&
		Totals.FeatureAbilities == Totals.PlayerStates && Totals.ActiveEffects == Totals.PlayerStates &&
		Totals.ProbeAttributes == Totals.PlayerStates && Totals.HealthSets == Totals.PlayerStates &&
		Totals.BoundAvatars == Totals.PlayerStates;
	if (AbilityProbeStage == 0 && bBaseReady)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniAbilityProbe BASELINE_PASS: Players=%d"), Totals.PlayerStates);
		Action->SetProbeSuspended(GetWorld(), true);
		AbilityProbeStage = 1;
	}
	else if (AbilityProbeStage == 1 && Totals.PawnAbilities == Totals.PlayerStates &&
		Totals.FeatureAbilities == 0 && Totals.ActiveEffects == 0 && Totals.ProbeAttributes == 0)
	{
		if (++AbilityProbeSuspendedTicks == 1)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniAbilityProbe REVOKE_PASS: PawnAbilities=%d FeatureAbilities=0"),
				Totals.PawnAbilities);
		}
		if (AbilityProbeSuspendedTicks >= 5)
		{
			Action->SetProbeSuspended(GetWorld(), false);
			AbilityProbeStage = 2;
		}
	}
	else if (AbilityProbeStage == 2 && bBaseReady)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniAbilityProbe RESTORE_PASS: Players=%d"), Totals.PlayerStates);
		APlayerController* RemoteController = nullptr;
		for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
		{
			if (!It->IsLocalController() && Cast<AMiniCharacter>(It->GetPawn()))
			{
				RemoteController = *It;
				break;
			}
		}
		AMiniGameMode* GameMode = GetWorld()->GetAuthGameMode<AMiniGameMode>();
		AMiniCharacter* OldPawn = RemoteController ? Cast<AMiniCharacter>(RemoteController->GetPawn()) : nullptr;
		AbilityProbeRespawnState = RemoteController ? RemoteController->GetPlayerState<AMiniPlayerState>() : nullptr;
		AbilityProbeRespawnASC = AbilityProbeRespawnState.IsValid()
			? AbilityProbeRespawnState->GetMiniAbilitySystemComponent() : nullptr;
		if (!GameMode || !OldPawn || !AbilityProbeRespawnASC.IsValid())
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniAbilityProbe RESPAWN_FAIL: no remote controller or ASC"));
			AbilityProbeStage = 255;
			return;
		}
		RemoteController->UnPossess();
		GameMode->RestartPlayer(RemoteController);
		AMiniCharacter* NewPawn = Cast<AMiniCharacter>(RemoteController->GetPawn());
		if (!NewPawn || NewPawn == OldPawn || AbilityProbeRespawnASC->GetAvatarActor() != NewPawn ||
			RemoteController->GetPlayerState<AMiniPlayerState>() != AbilityProbeRespawnState.Get())
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniAbilityProbe RESPAWN_FAIL: new Pawn did not reuse PlayerState ASC"));
			AbilityProbeStage = 255;
			return;
		}
		if (UMiniPawnExtensionComponent* OldExtension = OldPawn->GetPawnExtensionComponent())
		{
			OldExtension->UninitializeAbilitySystem(true);
		}
		if (AbilityProbeRespawnASC->GetAvatarActor() != NewPawn)
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniAbilityProbe RESPAWN_FAIL: old Pawn cleared new Avatar"));
			AbilityProbeStage = 255;
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniAbilityProbe RESPAWN_BOUND: PlayerState=%s ASC=%s OldPawn=%s NewPawn=%s"),
			*GetPathNameSafe(AbilityProbeRespawnState.Get()), *GetPathNameSafe(AbilityProbeRespawnASC.Get()),
			*GetPathNameSafe(OldPawn), *GetPathNameSafe(NewPawn));
		OldPawn->Destroy();
		AbilityProbeStage = 3;
	}
	else if (AbilityProbeStage == 3 && bBaseReady && AbilityProbeRespawnState.IsValid() &&
		AbilityProbeRespawnASC.IsValid() &&
		AbilityProbeRespawnASC->GetOwnerActor() == AbilityProbeRespawnState.Get())
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniAbilityProbe PASS: PlayerStateASCReused=1 OldPawnCleanupSafe=1"));
		AbilityProbeStage = 4;
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
