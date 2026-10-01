#include "MiniGameMode.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniDamageGameplayEffect.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniPawnData.h"
#include "EngineUtils.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniGameState.h"
#include "GameModes/MiniWorldSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniHUD.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniAssetManager.h"
#include "System/MiniLogChannels.h"
#include "System/MiniGameplayTags.h"
#include "TimerManager.h"
#include "Training/MiniPracticeTarget.h"

AMiniGameMode::AMiniGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GameStateClass = AMiniGameState::StaticClass();
	PlayerControllerClass = AMiniPlayerController::StaticClass();
	PlayerStateClass = AMiniPlayerState::StaticClass();
	HUDClass = AMiniHUD::StaticClass();

	// A missing or failed Experience must never fall back to a default Pawn.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = false;
}

void AMiniGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// The GameState is not available during InitGame. Select after world setup.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ThisClass::HandleMatchAssignmentIfNotExpectingOne));
	}
}

void AMiniGameMode::InitGameState()
{
	Super::InitGameState();
	if (UMiniExperienceManagerComponent* Manager = GetExperienceManager())
	{
		Manager->CallOrRegister_OnExperienceLoaded(
			FOnMiniExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::HandleExperienceLoaded));
	}
	else
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniSpawn cannot register Experience gate: manager missing"));
	}
}

UMiniExperienceManagerComponent* AMiniGameMode::GetExperienceManager() const
{
	const AMiniGameState* MiniGameState = GetGameState<AMiniGameState>();
	return MiniGameState ? MiniGameState->GetExperienceManagerComponent() : nullptr;
}

const UMiniPawnData* AMiniGameMode::GetPawnDataForController(const AController* Controller) const
{
	if (const AMiniPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr)
	{
		if (const UMiniPawnData* Assigned = PlayerState->GetPawnData())
		{
			return Assigned;
		}
	}
	const UMiniExperienceManagerComponent* Manager = GetExperienceManager();
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	return Experience ? Experience->DefaultPawnData.Get() : nullptr;
}

void AMiniGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	const UMiniExperienceManagerComponent* Manager = GetExperienceManager();
	if (!Manager || !Manager->IsExperienceLoaded())
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniSpawn GATED: Controller=%s State=%s"),
			*GetNameSafe(NewPlayer), Manager ? *Manager->GetLoadingDebugString() : TEXT("NoManager"));
		return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AMiniGameMode::HandleExperienceLoaded(const UMiniExperienceDefinition* Experience)
{
	if (!HasAuthority() || !Experience || !GetExperienceManager() || !GetExperienceManager()->IsExperienceLoaded())
	{
		return;
	}
	UE_LOG(LogMiniExperience, Display, TEXT("MiniSpawn GATE_OPEN: Experience=%s"), *Experience->GetPrimaryAssetId().ToString());
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		APlayerController* Controller = *It;
		if (Controller && !Controller->GetPawn() && !MustSpectate(Controller) && PlayerCanRestart(Controller))
		{
			RestartPlayer(Controller);
		}
	}
}

void AMiniGameMode::RestartPlayer(AController* NewPlayer)
{
	const UMiniExperienceManagerComponent* Manager = GetExperienceManager();
	if (!NewPlayer || !Manager || !Manager->IsExperienceLoaded())
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniSpawn GATED: Restart Controller=%s"), *GetNameSafe(NewPlayer));
		return;
	}
	if (NewPlayer->GetPawn())
	{
		return;
	}
	AMiniPlayerState* PlayerState = NewPlayer->GetPlayerState<AMiniPlayerState>();
	const UMiniPawnData* PawnData = GetPawnDataForController(NewPlayer);
	if (!PlayerState || !PawnData || !PawnData->PawnClass || !PawnData->PawnClass->IsChildOf(AMiniCharacter::StaticClass()) ||
		!PlayerState->SetPawnData(PawnData))
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniSpawn BLOCKED: Controller=%s PlayerState=%s PawnData=%s PawnClass=%s"),
			*GetNameSafe(NewPlayer), *GetNameSafe(PlayerState), *GetNameSafe(PawnData),
			PawnData ? *GetNameSafe(PawnData->PawnClass.Get()) : TEXT("None"));
		return;
	}
	UE_LOG(LogMiniExperience, Display, TEXT("MiniSpawn PawnDataAssigned: Controller=%s PawnData=%s PawnClass=%s"),
		*GetNameSafe(NewPlayer), *PawnData->GetPathName(), *PawnData->PawnClass->GetPathName());
	// The PlayerState ASC outlives Pawns. A replacement spawned by another
	// server path must also begin with usable health.
	if (UMiniHealthSet* HealthSet = PlayerState->GetHealthSet())
	{
		if (HealthSet->GetHealth() <= 0.0f)
		{
			PlayerState->GetMiniAbilitySystemComponent()->SetNumericAttributeBase(
				UMiniHealthSet::GetHealthAttribute(), HealthSet->GetMaxHealth());
		}
		PlayerState->GetMiniAbilitySystemComponent()->SetNumericAttributeBase(
			UMiniHealthSet::GetIncomingDamageAttribute(), 0.0f);
	}
	Super::RestartPlayer(NewPlayer);
	if (const APawn* Pawn = NewPlayer->GetPawn())
	{
		if (AMiniCharacter* MiniPawn = Cast<AMiniCharacter>(NewPlayer->GetPawn()))
		{
			MiniPawn->NotifyInitDependenciesChanged();
		}
		UE_LOG(LogMiniExperience, Display, TEXT("MiniSpawn COMMITTED: Controller=%s Pawn=%s Class=%s ExperienceId=%s"),
			*GetNameSafe(NewPlayer), *Pawn->GetPathName(), *Pawn->GetClass()->GetPathName(),
			*Manager->GetCurrentExperienceId().ToString());
	}
}

bool AMiniGameMode::TryApplyTestDamage(AController* InstigatorController, AMiniCharacter* Target, float Amount)
{
	return IsValid(InstigatorController)
		? TryApplyDamage(Cast<AMiniCharacter>(InstigatorController->GetPawn()), Target, Amount)
		: false;
}

bool AMiniGameMode::TryApplyDamage(AMiniCharacter* SourcePawn, AMiniCharacter* Target, float Amount)
{
	if (!HasAuthority() || !IsValid(SourcePawn) || !IsValid(Target) ||
		Target->GetWorld() != GetWorld() || !FMath::IsFinite(Amount) || Amount <= 0.0f)
	{
		return false;
	}
	AController* InstigatorController = SourcePawn->GetController();
	if (!IsValid(InstigatorController) || SourcePawn->GetWorld() != GetWorld() ||
		InstigatorController->GetPawn() != SourcePawn)
	{
		return false;
	}
	AMiniPlayerState* SourceState = InstigatorController->GetPlayerState<AMiniPlayerState>();
	AMiniPlayerState* TargetState = Target->GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* SourceASC = SourceState ? SourceState->GetMiniAbilitySystemComponent() : nullptr;
	UMiniAbilitySystemComponent* TargetASC = TargetState ? TargetState->GetMiniAbilitySystemComponent() : nullptr;
	const UMiniHealthSet* HealthSet = TargetState ? TargetState->GetHealthSet() : nullptr;
	const UMiniHealthComponent* SourceHealth = SourcePawn ? SourcePawn->GetHealthComponent() : nullptr;
	const UMiniHealthComponent* TargetHealth = Target->GetHealthComponent();
	if (!SourcePawn || SourcePawn == Target || !SourceASC || SourceASC->GetAvatarActor() != SourcePawn ||
		!SourceHealth || SourceHealth->IsDead() || !TargetASC || TargetASC->GetAvatarActor() != Target ||
		!TargetHealth || TargetHealth->IsDead() || !HealthSet || HealthSet->GetHealth() <= 0.0f ||
		TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead))
	{
		return false;
	}

	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	// PlayerState implements IAbilitySystemInterface. Keeping it as instigator
	// preserves the source ASC in the effect context; the Pawn is effect causer.
	Context.AddInstigator(SourceState, SourcePawn);
	Context.AddSourceObject(SourcePawn);
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(
		UMiniDamageGameplayEffect::StaticClass(), 1.0f, Context);
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(MiniGameplayTags::Data_Damage, Amount);
	const float OldHealth = HealthSet->GetHealth();
	SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
	const float NewHealth = HealthSet->GetHealth();
	if (NewHealth >= OldHealth)
	{
		return false;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniHealth DAMAGE_APPLIED: Source=%s Target=%s Amount=%.1f Health=%.1f/%.1f"),
		*SourcePawn->GetPathName(), *Target->GetPathName(), Amount, NewHealth, HealthSet->GetMaxHealth());
	return true;
}

bool AMiniGameMode::TryApplyDamageToActor(AMiniCharacter* SourcePawn, AActor* Target, float Amount,
	FMiniDamageResult& OutResult)
{
	OutResult = FMiniDamageResult();
	if (!HasAuthority() || !IsValid(SourcePawn) || !IsValid(Target) || SourcePawn == Target ||
		SourcePawn->GetWorld() != GetWorld() || Target->GetWorld() != GetWorld() ||
		!FMath::IsFinite(Amount) || Amount <= 0.0f)
	{
		return false;
	}
	if (AMiniCharacter* PlayerTarget = Cast<AMiniCharacter>(Target))
	{
		const AMiniPlayerState* TargetState = PlayerTarget->GetPlayerState<AMiniPlayerState>();
		const UMiniHealthSet* TargetHealth = TargetState ? TargetState->GetHealthSet() : nullptr;
		if (!TargetHealth) { return false; }
		const float OldHealth = TargetHealth->GetHealth();
		// All existing player-state/avatar/death/filtering rules remain centralized
		// in this established entry. Future team/FFA rules belong there as well.
		if (!TryApplyDamage(SourcePawn, PlayerTarget, Amount)) { return false; }
		OutResult.TargetKind = EMiniDamageTargetKind::Player;
		OutResult.AppliedDamage = FMath::Max(0.0f, OldHealth - TargetHealth->GetHealth());
		OutResult.bTargetDefeated = PlayerTarget->GetHealthComponent() && PlayerTarget->GetHealthComponent()->IsDead();
		return OutResult.AppliedDamage > 0.0f;
	}
	AMiniPracticeTarget* PracticeTarget = Cast<AMiniPracticeTarget>(Target);
	if (!PracticeTarget || !PracticeTarget->CanReceiveDamage()) { return false; }
	AController* InstigatorController = SourcePawn->GetController();
	AMiniPlayerState* SourceState = InstigatorController ? InstigatorController->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* SourceASC = SourceState ? SourceState->GetMiniAbilitySystemComponent() : nullptr;
	const UMiniHealthComponent* SourceHealth = SourcePawn->GetHealthComponent();
	const UMiniHealthSet* SourceHealthSet = SourceState ? SourceState->GetHealthSet() : nullptr;
	UMiniAbilitySystemComponent* TargetASC = PracticeTarget->GetMiniAbilitySystemComponent();
	const UMiniHealthSet* TargetHealth = PracticeTarget->GetHealthSet();
	if (!SourcePawn->HasAuthority() || !InstigatorController || InstigatorController->GetPawn() != SourcePawn ||
		!SourceASC || SourceASC->GetOwnerActor() != SourceState || SourceASC->GetAvatarActor() != SourcePawn ||
		!SourceHealth || SourceHealth->IsDead() || !SourceHealthSet || SourceHealthSet->GetHealth() <= 0.0f ||
		SourceASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) || !TargetASC || !TargetHealth)
	{
		return false;
	}
	FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext();
	Context.AddInstigator(SourceState, SourcePawn);
	Context.AddSourceObject(SourcePawn);
	FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UMiniDamageGameplayEffect::StaticClass(), 1.0f, Context);
	if (!Spec.IsValid()) { return false; }
	Spec.Data->SetSetByCallerMagnitude(MiniGameplayTags::Data_Damage, Amount);
	const float OldHealth = TargetHealth->GetHealth();
	SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
	const float NewHealth = TargetHealth->GetHealth();
	if (NewHealth >= OldHealth) { return false; }
	OutResult.TargetKind = EMiniDamageTargetKind::PracticeTarget;
	OutResult.AppliedDamage = FMath::Max(0.0f, OldHealth - NewHealth);
	OutResult.bTargetDefeated = NewHealth <= 0.0f && !PracticeTarget->IsTargetEnabled();
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniDamage APPLIED: Source=%s Target=%s Kind=PracticeTarget Damage=%.1f Health=%.1f Defeated=%d PlayerKill=0"),
		*SourcePawn->GetPathName(), *Target->GetPathName(), OutResult.AppliedDamage, NewHealth, OutResult.bTargetDefeated ? 1 : 0);
	return OutResult.AppliedDamage > 0.0f;
}

void AMiniGameMode::ScheduleRespawn(AMiniCharacter* DeadPawn)
{
	if (!HasAuthority() || !IsValid(DeadPawn) || !DeadPawn->GetHealthComponent() ||
		!DeadPawn->GetHealthComponent()->IsDead() || PendingRespawns.Contains(DeadPawn))
	{
		return;
	}
	AController* Controller = DeadPawn->GetController();
	if (!IsValid(Controller) || Controller->GetPawn() != DeadPawn)
	{
		return;
	}
	PendingRespawns.Add(DeadPawn);
	FTimerHandle Timer;
	GetWorldTimerManager().SetTimer(Timer,
		FTimerDelegate::CreateUObject(this, &ThisClass::FinishRespawn,
			TWeakObjectPtr<AController>(Controller), TWeakObjectPtr<AMiniCharacter>(DeadPawn)),
		3.0f, false);
	UE_LOG(LogMiniInit, Display, TEXT("MiniHealth RESPAWN_SCHEDULED: Pawn=%s Delay=3.0"),
		*DeadPawn->GetPathName());
}

void AMiniGameMode::FinishRespawn(TWeakObjectPtr<AController> DeadController,
	TWeakObjectPtr<AMiniCharacter> DeadPawn)
{
	PendingRespawns.Remove(DeadPawn);
	AController* Controller = DeadController.Get();
	AMiniCharacter* OldPawn = DeadPawn.Get();
	AMiniPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetMiniAbilitySystemComponent() : nullptr;
	UMiniHealthSet* HealthSet = PlayerState ? PlayerState->GetHealthSet() : nullptr;
	if (!IsValid(Controller) || !ASC || !HealthSet)
	{
		return;
	}
	if (IsValid(OldPawn) &&
		(!OldPawn->GetHealthComponent() || !OldPawn->GetHealthComponent()->IsDead()))
	{
		return;
	}
	if (AMiniCharacter* Replacement = Cast<AMiniCharacter>(Controller->GetPawn()))
	{
		if (Replacement != OldPawn)
		{
			Replacement->NotifyInitDependenciesChanged();
			if (ASC->GetAvatarActor() == Replacement)
			{
				PendingAvatarBindingChecks.Remove(DeadPawn);
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniHealth RESPAWNED: OldPawn=%s NewPawn=%s Health=%.1f"),
					*GetPathNameSafe(OldPawn), *Replacement->GetPathName(), HealthSet->GetHealth());
				if (IsValid(OldPawn))
				{
					OldPawn->Destroy();
				}
			}
			else
			{
				int32& Checks = PendingAvatarBindingChecks.FindOrAdd(DeadPawn);
				if (++Checks > 20)
				{
					UE_LOG(LogMiniInit, Error,
						TEXT("MiniHealth AVATAR_BIND_TIMEOUT: Pawn=%s Replacement=%s"),
						*GetPathNameSafe(OldPawn), *Replacement->GetPathName());
					PendingAvatarBindingChecks.Remove(DeadPawn);
					Replacement->Destroy();
					QueueRespawnRetry(DeadController, DeadPawn, 1.0f);
				}
				else
				{
					QueueRespawnRetry(DeadController, DeadPawn, 0.25f);
				}
			}
			return;
		}
	}
	if (IsValid(OldPawn) && Controller->GetPawn() == OldPawn && ASC->GetAvatarActor() == OldPawn)
	{
		OldPawn->GetHealthComponent()->RemoveDeathEffect();
		Controller->UnPossess();
		ASC->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), HealthSet->GetMaxHealth());
		ASC->SetNumericAttributeBase(UMiniHealthSet::GetIncomingDamageAttribute(), 0.0f);
	}
	else if (!IsValid(OldPawn) && !Controller->GetPawn() && !ASC->GetAvatarActor())
	{
		// The corpse was destroyed before the timer fired. Its EndPlay has
		// already removed the death effect and unbound the old Avatar.
		ASC->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), HealthSet->GetMaxHealth());
		ASC->SetNumericAttributeBase(UMiniHealthSet::GetIncomingDamageAttribute(), 0.0f);
	}
	else if (Controller->GetPawn() || ASC->GetAvatarActor())
	{
		// Another server flow already possessed a replacement.
		return;
	}
	RestartPlayer(Controller);
	if (!Controller->GetPawn())
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniHealth RESPAWN_FAILED: OldPawn=%s Controller=%s"),
			*GetPathNameSafe(OldPawn), *GetPathNameSafe(Controller));
		QueueRespawnRetry(DeadController, DeadPawn, 1.0f);
		return;
	}
	// Possession may finish ASC initialization in a later dependency callback.
	QueueRespawnRetry(DeadController, DeadPawn, 0.25f);
}

void AMiniGameMode::QueueRespawnRetry(TWeakObjectPtr<AController> DeadController,
	TWeakObjectPtr<AMiniCharacter> DeadPawn, float Delay)
{
	PendingRespawns.Add(DeadPawn);
	FTimerHandle RetryTimer;
	GetWorldTimerManager().SetTimer(RetryTimer,
		FTimerDelegate::CreateUObject(this, &ThisClass::FinishRespawn, DeadController, DeadPawn),
		Delay, false);
}

UClass* AMiniGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const UMiniExperienceManagerComponent* Manager = GetExperienceManager();
	if (!Manager || !Manager->IsExperienceLoaded())
	{
		return nullptr;
	}
	const UMiniPawnData* PawnData = GetPawnDataForController(InController);
	return PawnData && PawnData->PawnClass && PawnData->PawnClass->IsChildOf(AMiniCharacter::StaticClass())
		? PawnData->PawnClass.Get() : nullptr;
}

APawn* AMiniGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	const UMiniPawnData* PawnData = GetPawnDataForController(NewPlayer);
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	UWorld* World = GetWorld();
	if (!World || !NewPlayer || !PawnData || !PawnClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnInfo.bDeferConstruction = true;
	AMiniCharacter* Pawn = World->SpawnActor<AMiniCharacter>(PawnClass, SpawnTransform, SpawnInfo);
	if (!Pawn)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniSpawn BLOCKED: deferred spawn failed for %s"), *GetNameSafe(PawnClass));
		return nullptr;
	}
	if (!Pawn->SetPawnData(PawnData))
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniSpawn BLOCKED: PawnData rejected before FinishSpawning for %s"), *GetNameSafe(Pawn));
		Pawn->Destroy();
		return nullptr;
	}
	Pawn->FinishSpawning(SpawnTransform);
	return Pawn;
}

void AMiniGameMode::HandleMatchAssignmentIfNotExpectingOne()
{
	if (!HasAuthority())
	{
		return;
	}

	AMiniGameState* MiniGameState = GetGameState<AMiniGameState>();
	UMiniExperienceManagerComponent* ExperienceManager =
		MiniGameState ? MiniGameState->GetExperienceManagerComponent() : nullptr;
	if (!ExperienceManager)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("Cannot select Experience: MiniGameState or its ExperienceManager is missing"));
		return;
	}
	if (ExperienceManager->HasExperienceSelection())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// An isolated negative probe exercises the manager's invalid-ID failure path.
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeInvalidExperience")))
	{
		const FPrimaryAssetId MissingId(FMiniPrimaryAssetTypes::Experience, TEXT("DA_MiniDefinitelyMissing"));
		UE_LOG(LogMiniExperience, Display, TEXT("MiniGameMode selecting probe ID %s"), *MissingId.ToString());
		ExperienceManager->SetCurrentExperience(MissingId);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeMissingGameFeature")))
	{
		const FPrimaryAssetId ProbeId(FMiniPrimaryAssetTypes::Experience, TEXT("DA_MiniMissingFeatureExperience"));
		UE_LOG(LogMiniExperience, Display, TEXT("MiniGameMode selecting missing-feature probe ID %s"), *ProbeId.ToString());
		ExperienceManager->SetCurrentExperience(ProbeId);
		return;
	}
	int32 Task20LegacyCycles = 0;
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeLegacyExperience")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeAbilities")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask10")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbePlayerSpawns")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeInitStates")) ||
		FParse::Value(FCommandLine::Get(), TEXT("MiniProbeFeatureCycles="), Task20LegacyCycles))
	{
		const FPrimaryAssetId DiagnosticsId(FMiniPrimaryAssetTypes::Experience, TEXT("DA_MiniDiagnosticsExperience"));
		ExperienceManager->SetCurrentExperience(DiagnosticsId);
		return;
	}
#endif

	UMiniAssetManager* AssetManager = UMiniAssetManager::GetMiniAssetManager();
	if (!AssetManager)
	{
		ExperienceManager->FailExperienceSelection(TEXT("MiniAssetManager is not configured"));
		return;
	}

	FPrimaryAssetId ExperienceId;
	FString Error;
	// A host selects an assembled mode through travel options. Only the
	// authority chooses; clients receive the selected ID from the GameState.
	const FString RequestedExperience = UGameplayStatics::ParseOption(OptionsString, TEXT("Experience"));
	if (!RequestedExperience.IsEmpty())
	{
		ExperienceId = FPrimaryAssetId(FMiniPrimaryAssetTypes::Experience, FName(*RequestedExperience));
		if (!AssetManager->TryValidateExperienceId(ExperienceId, Error))
		{
			ExperienceManager->FailExperienceSelection(FString::Printf(TEXT("Travel Experience is invalid: %s"), *Error));
			return;
		}
		UE_LOG(LogMiniExperience, Display, TEXT("MiniGameMode selected travel Experience %s"), *ExperienceId.ToString());
		ExperienceManager->SetCurrentExperience(ExperienceId);
		return;
	}
	const AMiniWorldSettings* WorldSettings =
		GetWorld() ? Cast<AMiniWorldSettings>(GetWorld()->GetWorldSettings()) : nullptr;
	if (WorldSettings && !WorldSettings->DefaultGameplayExperience.IsNull())
	{
		ExperienceId = WorldSettings->GetDefaultGameplayExperience(&Error);
		if (!ExperienceId.IsValid())
		{
			ExperienceManager->FailExperienceSelection(FString::Printf(
				TEXT("Map Experience override is invalid: %s"), *Error));
			return;
		}
		UE_LOG(LogMiniExperience, Display, TEXT("MiniGameMode selected map Experience %s"), *ExperienceId.ToString());
	}
	else
	{
		if (!AssetManager->TryGetDefaultExperienceId(ExperienceId, Error))
		{
			ExperienceManager->FailExperienceSelection(FString::Printf(
				TEXT("Project default Experience is invalid: %s"), *Error));
			return;
		}
		UE_LOG(LogMiniExperience, Display, TEXT("MiniGameMode selected project default Experience %s"), *ExperienceId.ToString());
	}

	ExperienceManager->SetCurrentExperience(ExperienceId);
}
