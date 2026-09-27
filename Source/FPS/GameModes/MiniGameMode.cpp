#include "MiniGameMode.h"

#include "Character/MiniCharacter.h"
#include "Character/MiniPawnData.h"
#include "EngineUtils.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniGameState.h"
#include "GameModes/MiniWorldSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniHUD.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniAssetManager.h"
#include "System/MiniLogChannels.h"
#include "TimerManager.h"

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
	Super::RestartPlayer(NewPlayer);
	if (const APawn* Pawn = NewPlayer->GetPawn())
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniSpawn COMMITTED: Controller=%s Pawn=%s Class=%s ExperienceId=%s"),
			*GetNameSafe(NewPlayer), *Pawn->GetPathName(), *Pawn->GetClass()->GetPathName(),
			*Manager->GetCurrentExperienceId().ToString());
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

	UMiniAssetManager* AssetManager = UMiniAssetManager::GetMiniAssetManager();
	if (!AssetManager)
	{
		ExperienceManager->FailExperienceSelection(TEXT("MiniAssetManager is not configured"));
		return;
	}

	FPrimaryAssetId ExperienceId;
	FString Error;
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
