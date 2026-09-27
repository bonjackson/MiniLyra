#include "MiniGameMode.h"

#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameState.h"
#include "GameModes/MiniWorldSettings.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "System/MiniAssetManager.h"
#include "System/MiniLogChannels.h"
#include "TimerManager.h"

AMiniGameMode::AMiniGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GameStateClass = AMiniGameState::StaticClass();

	// Task 07 will gate actual player spawning on Experience readiness. The modular
	// base otherwise defaults to a pawn, so Task 05 keeps players as spectators.
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
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
