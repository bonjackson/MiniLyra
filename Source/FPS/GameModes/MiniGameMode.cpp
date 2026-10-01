#include "MiniGameMode.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniDamageGameplayEffect.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniPawnData.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniGameState.h"
#include "GameModes/MiniWorldSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Interfaces/MovementBaseInterface.h"
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

void AMiniGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPendingRespawnsForMatch();
	Super::EndPlay(EndPlayReason);
}

UMiniMatchRulesComponent* AMiniGameMode::GetMatchRules() const
{
	const AGameStateBase* State = GetGameState<AGameStateBase>();
	return State ? State->FindComponentByClass<UMiniMatchRulesComponent>() : nullptr;
}

void AMiniGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (UMiniMatchRulesComponent* Match = GetMatchRules())
	{
		Match->NotifyRosterChanged();
		if (NewPlayer && !NewPlayer->GetPawn() && Match->IsFFAConfigured() && Match->CanRespawnPlayer(NewPlayer))
		{
			RestartPlayer(NewPlayer);
		}
	}
}

void AMiniGameMode::Logout(AController* Exiting)
{
	CancelPendingRespawn(Exiting);
	CancelOutOfWorldRecoveriesForController(Exiting);
	if (UMiniMatchRulesComponent* Match = GetMatchRules())
	{
		// Notify while the leaving Controller and PlayerState are still available.
		Match->NotifyPlayerLogout(Exiting);
	}
	Super::Logout(Exiting);
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
	UMiniMatchRulesComponent* Match = GetMatchRules();
	const bool bFFA = Match && Match->IsFFAConfigured();
	if (bFFA && !Match->CanRespawnPlayer(NewPlayer))
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
	if (bFFA)
	{
		// The engine's FindPlayerStart/RestartPlayer fallbacks can reuse a blocked
		// StartSpot or spawn at WorldSettings. FFA commits only a checked start.
		AActor* Start = ChoosePlayerStart(NewPlayer);
		if (!Start)
		{
			QueueSpawnRetry(NewPlayer);
			return;
		}
		RestartPlayerAtPlayerStart(NewPlayer, Start);
		if (!NewPlayer->GetPawn())
		{
			QueueSpawnRetry(NewPlayer);
		}
	}
	else
	{
		Super::RestartPlayer(NewPlayer);
	}
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
	return ApplyPlayerDamageEffect(SourceState, SourcePawn, Target, Amount);
}

bool AMiniGameMode::ApplyPlayerDamageEffect(AMiniPlayerState* SourceState, AMiniCharacter* SourcePawn,
	AMiniCharacter* Target, float Amount, AActor* EnvironmentCauser)
{
	if (!HasAuthority() || !IsValid(Target) || Target->GetWorld() != GetWorld() ||
		!FMath::IsFinite(Amount) || Amount <= 0.0f)
	{
		return false;
	}
	AMiniPlayerState* TargetState = Target->GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* TargetASC = TargetState ? TargetState->GetMiniAbilitySystemComponent() : nullptr;
	const UMiniHealthSet* HealthSet = TargetState ? TargetState->GetHealthSet() : nullptr;
	const UMiniHealthComponent* Health = Target->GetHealthComponent();
	if (!TargetASC || TargetASC->GetAvatarActor() != Target || !HealthSet || HealthSet->GetHealth() <= 0.0f ||
		!Health || Health->IsDead() || TargetASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead))
	{
		return false;
	}
	UMiniAbilitySystemComponent* SourceASC = SourceState ? SourceState->GetMiniAbilitySystemComponent() : nullptr;
	UMiniAbilitySystemComponent* ApplyingASC = SourceASC ? SourceASC : TargetASC;
	FGameplayEffectContextHandle Context = ApplyingASC->MakeEffectContext();
	// PlayerState implements IAbilitySystemInterface. Keeping it as instigator
	// preserves the source ASC. Environment explicitly clears MakeEffectContext's
	// default victim instigator instead of accidentally crediting a suicide.
	Context.AddInstigator(SourceState, SourceState ? SourcePawn : EnvironmentCauser);
	Context.AddSourceObject(SourceState ? static_cast<UObject*>(SourcePawn) : static_cast<UObject*>(EnvironmentCauser));
	FGameplayEffectSpecHandle Spec = ApplyingASC->MakeOutgoingSpec(
		UMiniDamageGameplayEffect::StaticClass(), 1.0f, Context);
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(MiniGameplayTags::Data_Damage, Amount);
	const float OldHealth = HealthSet->GetHealth();
	ApplyingASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
	const float NewHealth = HealthSet->GetHealth();
	if (NewHealth >= OldHealth)
	{
		return false;
	}
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniHealth DAMAGE_APPLIED: Source=%s Target=%s Amount=%.1f Health=%.1f/%.1f"),
		*GetPathNameSafe(SourcePawn), *Target->GetPathName(), Amount, NewHealth, HealthSet->GetMaxHealth());
	return true;
}

bool AMiniGameMode::TryApplySuicideDamage(AMiniCharacter* Target, float Amount)
{
	AMiniPlayerState* State = IsValid(Target) ? Target->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State && ApplyPlayerDamageEffect(State, Target, Target, Amount);
}

bool AMiniGameMode::TryApplyEnvironmentDamage(AMiniCharacter* Target, float Amount, AActor* EffectCauser)
{
	if (EffectCauser && (!IsValid(EffectCauser) || EffectCauser->GetWorld() != GetWorld()))
	{
		return false;
	}
	return ApplyPlayerDamageEffect(nullptr, nullptr, Target, Amount, EffectCauser);
}

bool AMiniGameMode::IsCurrentRecoveryLife(const FPendingOutOfWorldRecovery& Work) const
{
	AMiniCharacter* Pawn = Work.Pawn.Get();
	AController* Controller = Work.Controller.Get();
	AMiniPlayerState* State = Work.PlayerState.Get();
	const UMiniHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
	const UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	return HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown && IsValid(Pawn) &&
		!Pawn->IsActorBeingDestroyed() && Pawn->GetWorld() == GetWorld() && IsValid(Controller) &&
		Controller->GetWorld() == GetWorld() && Controller->GetPawn() == Pawn && Pawn->GetController() == Controller &&
		IsValid(State) && Controller->GetPlayerState<AMiniPlayerState>() == State &&
		!State->IsOnlyASpectator() && !State->IsInactive() && Work.LifeId != 0 &&
		State->GetCurrentLifeId() == Work.LifeId && State->GetCurrentLifePawn() == Pawn &&
		ASC && ASC->GetAvatarActor() == Pawn && Health && !Health->IsDead() &&
		!ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead);
}

bool AMiniGameMode::IsRecoveryContextCurrent(const FPendingOutOfWorldRecovery& Work) const
{
	const UMiniMatchRulesComponent* Match = GetMatchRules();
	return IsCurrentRecoveryLife(Work) && Match == Work.Match.Get() &&
		(Work.bFFA ? Match && Match->IsFFAConfigured() && Match->GetRoundId() == Work.RoundId &&
			Match->GetRulesGeneration() == Work.RulesGeneration : !Match || !Match->IsFFAConfigured());
}

bool AMiniGameMode::HandlePlayerFellOutOfWorld(AMiniCharacter* Pawn)
{
	if (!HasAuthority() || !IsValid(Pawn) || !Pawn->HasAuthority() || Pawn->GetWorld() != GetWorld() ||
		!GetWorld() || GetWorld()->bIsTearingDown)
	{
		return false;
	}
	UMiniHealthComponent* Health = Pawn->GetHealthComponent();
	if (Health && Health->IsDead())
	{
		// The existing death work owns this corpse until its life-aware respawn.
		CancelPendingOutOfWorldRecovery(Pawn);
		return true;
	}
	if (PendingOutOfWorldRecoveries.Contains(Pawn))
	{
		// CharacterMovement still checks KillZ while MOVE_None. Do not repeat GE,
		// spawn queries, or timer setup on each of those engine callbacks.
		return true;
	}
	AController* Controller = Pawn->GetController();
	AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniMatchRulesComponent* Match = GetMatchRules();
	FPendingOutOfWorldRecovery Work;
	Work.Pawn = Pawn;
	Work.Controller = Controller;
	Work.PlayerState = State;
	Work.Match = Match;
	Work.LifeId = State ? State->GetCurrentLifeId() : 0;
	Work.bFFA = Match && Match->IsFFAConfigured();
	Work.RoundId = Work.bFFA ? Match->GetRoundId() : 0;
	Work.RulesGeneration = Work.bFFA ? Match->GetRulesGeneration() : 0;
	if (!IsCurrentRecoveryLife(Work))
	{
		return false;
	}
	Work.WorkSerial = ++NextOutOfWorldRecoveryWorkSerial;
	PendingOutOfWorldRecoveries.Add(Pawn, Work);
	// Saving the work before GE/teleport also guards their synchronous overlap
	// and health callbacks. Never retain a TMap reference across either call.
	const UMiniHealthSet* HealthSet = State->GetHealthSet();
	if ((!Work.bFFA || Match->CanApplyPlayerDamage(Pawn)) && HealthSet &&
		FMath::IsFinite(HealthSet->GetHealth()) && HealthSet->GetHealth() > 0.0f)
	{
		TryApplyEnvironmentDamage(Pawn, FMath::Max(HealthSet->GetHealth(), 1.0f));
		if (!IsValid(Pawn) || Health->IsDead())
		{
			CancelPendingOutOfWorldRecovery(Pawn);
			UE_LOG(LogMiniInit, Display, TEXT("MiniFall ENVIRONMENT_DEATH: Pawn=%s Round=%d Life=%u"),
				*GetPathNameSafe(Pawn), Work.RoundId, Work.LifeId);
			return true;
		}
	}
	// A rejected GE (phase, protection, deadline, or stopped rules) never turns
	// into destruction or a fresh life. Later retries only finish this recovery.
	FinishOutOfWorldRecovery(Pawn, Work.WorkSerial);
	return true;
}

bool AMiniGameMode::IsOutOfWorldRecoveryLocationSafe(AMiniCharacter* Pawn, AController* Controller,
	const FVector& Location, const FQuat& Rotation) const
{
	const AWorldSettings* Settings = GetWorld() ? GetWorld()->GetWorldSettings() : nullptr;
	const UCapsuleComponent* Capsule = Pawn ? Pawn->GetCapsuleComponent() : nullptr;
	const UCharacterMovementComponent* Movement = Pawn ? Pawn->GetCharacterMovement() : nullptr;
	if (!Settings || !Capsule || !Movement || Location.ContainsNaN() || Rotation.ContainsNaN()) { return false; }
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0.0f || HalfHeight < Radius ||
		Location.Z - HalfHeight <= Settings->KillZ || FMath::Abs(Location.X) + Radius >= HALF_WORLD_MAX ||
		FMath::Abs(Location.Y) + Radius >= HALF_WORLD_MAX || FMath::Abs(Location.Z) + HalfHeight >= HALF_WORLD_MAX)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MiniFallRecovery), false);
	Query.AddIgnoredActor(Pawn);
	Query.AddIgnoredActor(Controller);
	if (GetWorld()->OverlapBlockingTestByProfile(Location, Rotation, Capsule->GetCollisionProfileName(),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Query))
	{
		return false;
	}
	FHitResult FloorHit;
	return GetWorld()->LineTraceSingleByChannel(FloorHit, Location,
		Location - FVector(0.0f, 0.0f, HalfHeight + 250.0f), ECC_Pawn, Query) &&
		FloorHit.ImpactPoint.Z > Settings->KillZ && !Cast<APawn>(FloorHit.GetActor()) && Movement->IsWalkable(FloorHit);
}

void AMiniGameMode::FinishOutOfWorldRecovery(TWeakObjectPtr<AMiniCharacter> PawnKey, uint32 WorkSerial)
{
	const FPendingOutOfWorldRecovery* SavedWork = PendingOutOfWorldRecoveries.Find(PawnKey);
	if (!SavedWork || SavedWork->WorkSerial != WorkSerial) { return; }
	const FPendingOutOfWorldRecovery Work = *SavedWork;
	AMiniCharacter* Pawn = Work.Pawn.Get();
	if (!IsRecoveryContextCurrent(Work))
	{
		CancelOutOfWorldRecovery(PawnKey);
		return;
	}
	AController* Controller = Work.Controller.Get();
	UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
	TArray<APlayerStart*> Starts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) { Starts.Add(*It); }
	Starts.Sort([](const APlayerStart& A, const APlayerStart& B) { return A.GetPathName() < B.GetPathName(); });
	if (APlayerStart* Preferred = Cast<APlayerStart>(ChoosePlayerStart(Controller)))
	{
		Starts.Remove(Preferred);
		Starts.Insert(Preferred, 0);
	}
	SavedWork = PendingOutOfWorldRecoveries.Find(PawnKey);
	if (!SavedWork || SavedWork->WorkSerial != WorkSerial) { return; }
	if (!IsRecoveryContextCurrent(Work))
	{
		CancelOutOfWorldRecovery(PawnKey);
		return;
	}
	for (APlayerStart* Start : Starts)
	{
		const FRotator Rotation(0.0f, Start->GetActorRotation().Yaw, 0.0f);
		if (!IsOutOfWorldRecoveryLocationSafe(Pawn, Controller, Start->GetActorLocation(), FQuat(Rotation))) { continue; }
		SavedWork = PendingOutOfWorldRecoveries.Find(PawnKey);
		if (!SavedWork || SavedWork->WorkSerial != WorkSerial) { return; }
		if (!IsRecoveryContextCurrent(Work))
		{
			CancelOutOfWorldRecovery(PawnKey);
			return;
		}
		Pawn->StopJumping();
		Movement->StopMovementImmediately();
		Movement->ClearAccumulatedForces();
		Pawn->SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
		if (!Pawn->TeleportTo(Start->GetActorLocation(), Rotation, false, false)) { continue; }
		SavedWork = PendingOutOfWorldRecoveries.Find(PawnKey);
		if (!SavedWork || SavedWork->WorkSerial != WorkSerial) { return; }
		if (!IsRecoveryContextCurrent(*SavedWork))
		{
			CancelOutOfWorldRecovery(PawnKey);
			return;
		}
		// TeleportTo may adjust the requested point. Validate its actual result.
		if (!IsOutOfWorldRecoveryLocationSafe(Pawn, Controller, Pawn->GetActorLocation(), Pawn->GetActorQuat())) { continue; }
		CancelPendingOutOfWorldRecovery(Pawn);
		if (IsCurrentRecoveryLife(Work))
		{
			Movement->ForceClientAdjustment();
			Pawn->ForceNetUpdate();
			UE_LOG(LogMiniInit, Display, TEXT("MiniFall RECOVERED: Pawn=%s Start=%s Round=%d Life=%u"),
				*Pawn->GetPathName(), *Start->GetPathName(), Work.RoundId, Work.LifeId);
		}
		return;
	}
	SavedWork = PendingOutOfWorldRecoveries.Find(PawnKey);
	if (!SavedWork || SavedWork->WorkSerial != WorkSerial) { return; }
	if (!IsRecoveryContextCurrent(*SavedWork))
	{
		CancelOutOfWorldRecovery(PawnKey);
		return;
	}
	if (Movement && !SavedWork->bMovementSuspended && Movement->MovementMode != MOVE_None)
	{
		FPendingOutOfWorldRecovery* MutableWork = PendingOutOfWorldRecoveries.Find(PawnKey);
		MutableWork->SavedMovementMode = Movement->MovementMode;
		MutableWork->SavedCustomMovementMode = Movement->CustomMovementMode;
		MutableWork->bMovementSuspended = true;
		Pawn->StopJumping();
		Movement->StopMovementImmediately();
		Movement->ClearAccumulatedForces();
		Pawn->SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
		Movement->DisableMovement();
		Movement->ForceClientAdjustment();
		Pawn->ForceNetUpdate();
		UE_LOG(LogMiniInit, Display, TEXT("MiniFall RECOVERY_BLOCKED: Pawn=%s Round=%d Life=%u Retry=0.50"),
			*Pawn->GetPathName(), Work.RoundId, Work.LifeId);
	}
	QueueOutOfWorldRecoveryRetry(PawnKey, WorkSerial);
}

void AMiniGameMode::QueueOutOfWorldRecoveryRetry(TWeakObjectPtr<AMiniCharacter> PawnKey, uint32 WorkSerial)
{
	if (FPendingOutOfWorldRecovery* Work = PendingOutOfWorldRecoveries.Find(PawnKey))
	{
		if (Work->WorkSerial == WorkSerial)
		{
			GetWorldTimerManager().SetTimer(Work->Timer,
				FTimerDelegate::CreateUObject(this, &ThisClass::FinishOutOfWorldRecovery, PawnKey, WorkSerial), 0.5f, false);
		}
	}
}

void AMiniGameMode::RestoreOutOfWorldRecoveryMovement(const FPendingOutOfWorldRecovery& Work)
{
	// Round/source changes cancel retries, but must still release a pause owned
	// by this exact live Pawn. Never enable movement on a dead or replacement life.
	if (!Work.bMovementSuspended || !IsCurrentRecoveryLife(Work)) { return; }
	AMiniCharacter* Pawn = Work.Pawn.Get();
	UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
	if (Movement && Movement->MovementMode == MOVE_None)
	{
		Movement->SetMovementMode(static_cast<EMovementMode>(Work.SavedMovementMode), Work.SavedCustomMovementMode);
		if (IsCurrentRecoveryLife(Work) && !PendingOutOfWorldRecoveries.Contains(Pawn))
		{
			Movement->OnTeleported();
			Movement->ForceClientAdjustment();
			Pawn->ForceNetUpdate();
		}
	}
}

void AMiniGameMode::CancelPendingOutOfWorldRecovery(AMiniCharacter* Pawn)
{
	if (Pawn) { CancelOutOfWorldRecovery(Pawn); }
}

void AMiniGameMode::CancelOutOfWorldRecovery(TWeakObjectPtr<AMiniCharacter> PawnKey)
{
	FPendingOutOfWorldRecovery Work;
	if (PendingOutOfWorldRecoveries.RemoveAndCopyValue(PawnKey, Work))
	{
		GetWorldTimerManager().ClearTimer(Work.Timer);
		RestoreOutOfWorldRecoveryMovement(Work);
	}
}

void AMiniGameMode::CancelOutOfWorldRecoveriesForController(AController* Controller)
{
	TArray<TWeakObjectPtr<AMiniCharacter>> Keys;
	for (const TPair<TWeakObjectPtr<AMiniCharacter>, FPendingOutOfWorldRecovery>& Entry : PendingOutOfWorldRecoveries)
	{
		if (Entry.Value.Controller.Get() == Controller) { Keys.Add(Entry.Key); }
	}
	for (const TWeakObjectPtr<AMiniCharacter>& Key : Keys) { CancelOutOfWorldRecovery(Key); }
}

void AMiniGameMode::CancelPendingOutOfWorldRecoveriesForMatch()
{
	TArray<FPendingOutOfWorldRecovery> Works;
	PendingOutOfWorldRecoveries.GenerateValueArray(Works);
	PendingOutOfWorldRecoveries.Reset();
	++NextOutOfWorldRecoveryWorkSerial;
	for (FPendingOutOfWorldRecovery& Work : Works) { GetWorldTimerManager().ClearTimer(Work.Timer); }
	for (const FPendingOutOfWorldRecovery& Work : Works) { RestoreOutOfWorldRecoveryMovement(Work); }
}

void AMiniGameMode::NotifyPlayerDeath(const FMiniPlayerDeathInfo& DeathInfo)
{
	if (HasAuthority())
	{
		CancelPendingOutOfWorldRecovery(DeathInfo.VictimPawn.Get());
		if (UMiniMatchRulesComponent* Match = GetMatchRules())
		{
			Match->NotifyPlayerDeath(DeathInfo);
		}
	}
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
		!DeadPawn->GetHealthComponent()->IsDead())
	{
		return;
	}
	AController* Controller = DeadPawn->GetController();
	AMiniPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniMatchRulesComponent* Match = GetMatchRules();
	if (!IsValid(Controller) || !PlayerState || Controller->GetPawn() != DeadPawn ||
		(Match && Match->IsFFAConfigured() && !Match->CanRespawnPlayer(Controller)))
	{
		return;
	}
	if (const FPendingRespawn* Previous = PendingRespawns.Find(Controller))
	{
		if (Previous->DeadPawn.Get() == DeadPawn)
		{
			return;
		}
		// A new life can die before an older Avatar-binding retry has run.
		CancelPendingRespawn(Controller);
	}
	FPendingRespawn& Work = PendingRespawns.Add(Controller);
	Work.DeadPawn = DeadPawn;
	Work.PlayerState = PlayerState;
	Work.WorkSerial = ++NextRespawnWorkSerial;
	Work.VictimLifeId = PlayerState->GetCurrentLifeId();
	Work.bFFA = Match && Match->IsFFAConfigured();
	Work.Match = Work.bFFA ? Match : nullptr;
	Work.RoundId = Work.bFFA ? Match->GetRoundId() : 0;
	Work.RulesGeneration = Work.bFFA ? Match->GetRulesGeneration() : 0;
	const float Delay = Work.bFFA ? Match->GetRespawnDelaySeconds() : 3.0f;
	QueueRespawnRetry(Controller, Work.WorkSerial, Delay);
	UE_LOG(LogMiniInit, Display, TEXT("MiniHealth RESPAWN_SCHEDULED: Pawn=%s Delay=%.2f Round=%d Life=%u"),
		*DeadPawn->GetPathName(), Delay, Work.RoundId, Work.VictimLifeId);
}

void AMiniGameMode::CancelPendingRespawn(AController* Controller)
{
	if (FPendingRespawn* Work = PendingRespawns.Find(Controller))
	{
		GetWorldTimerManager().ClearTimer(Work->Timer);
		PendingRespawns.Remove(Controller);
	}
}

void AMiniGameMode::CancelPendingRespawnsForMatch()
{
	CancelPendingOutOfWorldRecoveriesForMatch();
	for (TPair<TWeakObjectPtr<AController>, FPendingRespawn>& Entry : PendingRespawns)
	{
		GetWorldTimerManager().ClearTimer(Entry.Value.Timer);
	}
	PendingRespawns.Reset();
	++NextRespawnWorkSerial;
}

void AMiniGameMode::DestroyPawnForRestart(AController* Controller)
{
	if (!Controller)
	{
		return;
	}
	APawn* OldPawn = Controller->GetPawn();
	// UnPossessed already owns equipment removal, Health cleanup and ASC unbinding.
	// Reuse that lifecycle instead of recreating weapon or init-state behavior.
	if (OldPawn)
	{
		Controller->UnPossess();
		OldPawn->Destroy();
	}
	if (AMiniPlayerState* State = Controller->GetPlayerState<AMiniPlayerState>())
	{
		UMiniAbilitySystemComponent* ASC = State->GetMiniAbilitySystemComponent();
		const UMiniHealthSet* Health = State->GetHealthSet();
		if (ASC && Health && !ASC->GetAvatarActor())
		{
			ASC->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), Health->GetMaxHealth());
			ASC->SetNumericAttributeBase(UMiniHealthSet::GetIncomingDamageAttribute(), 0.0f);
		}
	}
}

void AMiniGameMode::ResetPlayersForRound()
{
	UMiniMatchRulesComponent* Match = GetMatchRules();
	if (!HasAuthority() || !Match || !Match->IsFFAConfigured())
	{
		return;
	}
	CancelPendingRespawnsForMatch();
	TArray<APlayerController*> Players;
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		if (Match->CanRespawnPlayer(*It))
		{
			Players.Add(*It);
		}
	}
	// Release all old capsules before selecting any new starts.
	for (APlayerController* Controller : Players)
	{
		DestroyPawnForRestart(Controller);
	}
	for (APlayerController* Controller : Players)
	{
		RestartPlayer(Controller);
	}
}

void AMiniGameMode::QueueSpawnRetry(AController* Controller)
{
	if (!IsValid(Controller) || PendingRespawns.Contains(Controller))
	{
		return;
	}
	AMiniPlayerState* State = Controller->GetPlayerState<AMiniPlayerState>();
	UMiniMatchRulesComponent* Match = GetMatchRules();
	if (!State || !Match || !Match->IsFFAConfigured() || !Match->CanRespawnPlayer(Controller))
	{
		return;
	}
	FPendingRespawn& Work = PendingRespawns.Add(Controller);
	Work.PlayerState = State;
	Work.Match = Match;
	Work.WorkSerial = ++NextRespawnWorkSerial;
	Work.VictimLifeId = State->GetCurrentLifeId();
	Work.RoundId = Match->GetRoundId();
	Work.RulesGeneration = Match->GetRulesGeneration();
	Work.bFFA = true;
	QueueRespawnRetry(Controller, Work.WorkSerial, 0.5f);
	UE_LOG(LogMiniInit, Display, TEXT("MiniSpawn RETRY_BLOCKED: Controller=%s Round=%d Life=%u"),
		*Controller->GetPathName(), Work.RoundId, Work.VictimLifeId);
}

void AMiniGameMode::FinishRespawn(TWeakObjectPtr<AController> DeadController, uint32 WorkSerial)
{
	FPendingRespawn* Work = PendingRespawns.Find(DeadController);
	if (!Work || Work->WorkSerial != WorkSerial)
	{
		return;
	}
	AController* Controller = DeadController.Get();
	AMiniCharacter* OldPawn = Work->DeadPawn.Get();
	AMiniPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = PlayerState ? PlayerState->GetMiniAbilitySystemComponent() : nullptr;
	UMiniHealthSet* HealthSet = PlayerState ? PlayerState->GetHealthSet() : nullptr;
	UMiniMatchRulesComponent* Match = GetMatchRules();
	if (!IsValid(Controller) || Controller->GetWorld() != GetWorld() || !ASC || !HealthSet ||
		PlayerState != Work->PlayerState.Get() || PlayerState->IsOnlyASpectator() || PlayerState->IsInactive() ||
		(Work->bFFA && (Match != Work->Match.Get() || !Match || !Match->IsFFAConfigured() ||
			Match->GetRoundId() != Work->RoundId || Match->GetRulesGeneration() != Work->RulesGeneration ||
			!Match->CanRespawnPlayer(Controller))) ||
		(!Work->bFFA && Match && Match->IsFFAConfigured()))
	{
		PendingRespawns.Remove(DeadController);
		return;
	}
	AMiniCharacter* Replacement = Work->ReplacementPawn.Get();
	if (Replacement)
	{
		if (Controller->GetPawn() != Replacement ||
			(PlayerState->GetCurrentLifeId() != Work->VictimLifeId && PlayerState->GetCurrentLifePawn() != Replacement))
		{
			PendingRespawns.Remove(DeadController);
			return;
		}
		Replacement->NotifyInitDependenciesChanged();
		Work = PendingRespawns.Find(DeadController);
		if (!Work || Work->WorkSerial != WorkSerial)
		{
			return;
		}
		if (ASC->GetAvatarActor() == Replacement && PlayerState->GetCurrentLifePawn() == Replacement)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniHealth RESPAWNED: OldPawn=%s NewPawn=%s Health=%.1f"),
				*GetPathNameSafe(OldPawn), *Replacement->GetPathName(), HealthSet->GetHealth());
			PendingRespawns.Remove(DeadController);
		}
		else if (++Work->AvatarBindingChecks > 20)
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniHealth AVATAR_BIND_TIMEOUT: Replacement=%s"), *Replacement->GetPathName());
			DestroyPawnForRestart(Controller);
			Work = PendingRespawns.Find(DeadController);
			if (!Work || Work->WorkSerial != WorkSerial)
			{
				return;
			}
			Work->ReplacementPawn.Reset();
			Work->AvatarBindingChecks = 0;
			Work->VictimLifeId = PlayerState->GetCurrentLifeId();
			QueueRespawnRetry(DeadController, WorkSerial, 1.0f);
		}
		else
		{
			QueueRespawnRetry(DeadController, WorkSerial, 0.25f);
		}
		return;
	}
	if (PlayerState->GetCurrentLifeId() != Work->VictimLifeId ||
		(IsValid(OldPawn) && (!OldPawn->GetHealthComponent() || !OldPawn->GetHealthComponent()->IsDead())) ||
		(Controller->GetPawn() && Controller->GetPawn() != OldPawn) ||
		(ASC->GetAvatarActor() && ASC->GetAvatarActor() != OldPawn))
	{
		PendingRespawns.Remove(DeadController);
		return;
	}
	if (Controller->GetPawn() == OldPawn && OldPawn)
	{
		DestroyPawnForRestart(Controller);
	}
	else if (!Controller->GetPawn() && !ASC->GetAvatarActor())
	{
		ASC->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), HealthSet->GetMaxHealth());
		ASC->SetNumericAttributeBase(UMiniHealthSet::GetIncomingDamageAttribute(), 0.0f);
	}
	RestartPlayer(Controller);
	// Restart may invoke external hooks. Reacquire our entry before using it.
	Work = PendingRespawns.Find(DeadController);
	if (!Work || Work->WorkSerial != WorkSerial)
	{
		return;
	}
	Work->ReplacementPawn = Cast<AMiniCharacter>(Controller->GetPawn());
	if (!Work->ReplacementPawn.IsValid())
	{
		QueueRespawnRetry(DeadController, WorkSerial, 0.5f);
		return;
	}
	if (ASC->GetAvatarActor() == Work->ReplacementPawn.Get() && PlayerState->GetCurrentLifePawn() == Work->ReplacementPawn.Get())
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniHealth RESPAWNED: OldPawn=%s NewPawn=%s Health=%.1f"),
			*GetPathNameSafe(OldPawn), *Work->ReplacementPawn->GetPathName(), HealthSet->GetHealth());
		PendingRespawns.Remove(DeadController);
		return;
	}
	// Possession may finish ASC initialization in a later dependency callback.
	QueueRespawnRetry(DeadController, WorkSerial, 0.25f);
}

void AMiniGameMode::QueueRespawnRetry(TWeakObjectPtr<AController> DeadController, uint32 WorkSerial, float Delay)
{
	if (FPendingRespawn* Work = PendingRespawns.Find(DeadController))
	{
		if (Work->WorkSerial == WorkSerial)
		{
			GetWorldTimerManager().SetTimer(Work->Timer,
				FTimerDelegate::CreateUObject(this, &ThisClass::FinishRespawn, DeadController, WorkSerial),
				FMath::Max(Delay, 0.01f), false);
		}
	}
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

bool AMiniGameMode::ShouldSpawnAtStartSpot(AController* Player)
{
	const UMiniMatchRulesComponent* Match = GetMatchRules();
	return Match && Match->IsFFAConfigured() ? false : Super::ShouldSpawnAtStartSpot(Player);
}

AActor* AMiniGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	const UMiniMatchRulesComponent* Match = GetMatchRules();
	if (!Match || !Match->IsFFAConfigured()) { return Super::FindPlayerStart_Implementation(Player, IncomingName); }
	if (AActor* Start = ChoosePlayerStart(Player)) { return Start; }
	if (!Match->CanRespawnPlayer(Player))
	{
		// Login only requires a named map start. Occupied starts must not reject a
		// connection before the roster exists. RestartPlayer later selects a clear
		// point or saves a retry, and never uses this admission fallback to spawn.
		APlayerStart* AdmissionStart = nullptr;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (!AdmissionStart || It->GetPathName() < AdmissionStart->GetPathName()) { AdmissionStart = *It; }
		}
		return AdmissionStart;
	}
	return nullptr;
}

bool AMiniGameMode::IsSpawnLocationClear(AController* Player, const FVector& Location, const FQuat& Rotation,
	const AActor* IgnoreActor) const
{
	const UMiniPawnData* Data = GetPawnDataForController(Player);
	const AMiniCharacter* DefaultPawn = Data && Data->PawnClass ? Cast<AMiniCharacter>(Data->PawnClass->GetDefaultObject()) : nullptr;
	const UCapsuleComponent* Capsule = DefaultPawn ? DefaultPawn->GetCapsuleComponent() : nullptr;
	if (!GetWorld() || !Capsule)
	{
		return false;
	}
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MiniFFASpawn), false);
	Query.AddIgnoredActor(IgnoreActor);
	if (Player)
	{
		Query.AddIgnoredActor(Player->GetPawn());
	}
	return !GetWorld()->OverlapBlockingTestByProfile(Location, Rotation, Capsule->GetCollisionProfileName(),
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query);
}

AActor* AMiniGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	UMiniMatchRulesComponent* Match = GetMatchRules();
	if (!Match || !Match->IsFFAConfigured())
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}
	// InitNewPlayer resolves a start before PostLogin adds this Controller to
	// the match roster. Actual Pawn creation is separately gated in RestartPlayer
	// and SpawnDefaultPawnAtTransform; selecting a login start cannot require it.
	TArray<APlayerStart*> Starts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		Starts.Add(*It);
	}
	Starts.Sort([](const APlayerStart& A, const APlayerStart& B) { return A.GetPathName() < B.GetPathName(); });
	APlayerStart* Best = nullptr;
	double BestDistanceSquared = -1.0;
	const int32 Offset = Starts.IsEmpty() ? 0 : NextSpawnSelectionIndex % Starts.Num();
	for (int32 Index = 0; Index < Starts.Num(); ++Index)
	{
		APlayerStart* Start = Starts[(Index + Offset) % Starts.Num()];
		const FQuat Rotation(FRotator(0.0f, Start->GetActorRotation().Yaw, 0.0f));
		if (!IsSpawnLocationClear(Player, Start->GetActorLocation(), Rotation))
		{
			continue;
		}
		double ClosestPlayerSquared = TNumericLimits<double>::Max();
		for (TActorIterator<AMiniCharacter> PawnIt(GetWorld()); PawnIt; ++PawnIt)
		{
			const AMiniCharacter* OtherPawn = *PawnIt;
			if (OtherPawn != (Player ? Player->GetPawn() : nullptr) && OtherPawn->GetController() &&
				OtherPawn->GetHealthComponent() && !OtherPawn->GetHealthComponent()->IsDead())
			{
				ClosestPlayerSquared = FMath::Min(ClosestPlayerSquared,
					FVector::DistSquared(Start->GetActorLocation(), OtherPawn->GetActorLocation()));
			}
		}
		if (!Best || ClosestPlayerSquared > BestDistanceSquared + 1.0)
		{
			Best = Start;
			BestDistanceSquared = ClosestPlayerSquared;
		}
	}
	if (Best)
	{
		NextSpawnSelectionIndex = (Starts.IndexOfByKey(Best) + 1) % Starts.Num();
	}
	return Best;
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
	UMiniMatchRulesComponent* Match = GetMatchRules();
	const bool bFFA = Match && Match->IsFFAConfigured();
	if (bFFA && (!Match->CanRespawnPlayer(NewPlayer) ||
		!IsSpawnLocationClear(NewPlayer, SpawnTransform.GetLocation(), SpawnTransform.GetRotation())))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.SpawnCollisionHandlingOverride = bFFA ? ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding
		: ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
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
	if (bFFA && !IsSpawnLocationClear(NewPlayer, Pawn->GetActorLocation(), Pawn->GetActorQuat(), Pawn))
	{
		Pawn->Destroy();
		return nullptr;
	}
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
