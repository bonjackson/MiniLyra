#pragma once

#include "CoreMinimal.h"
#include "MiniMatchTypes.generated.h"

class AMiniCharacter;
class AMiniPlayerState;

UENUM(BlueprintType)
enum class EMiniMatchEndReason : uint8
{
	None,
	ScoreLimit,
	TimeLimit,
	InsufficientPlayers
};

UENUM()
enum class EMiniPlayerDeathCause : uint8
{
	Player,
	Suicide,
	Environment
};

/** Captured during this damage execution, never reconstructed from a later ASC Avatar. */
USTRUCT()
struct FPS_API FMiniPlayerDeathInfo
{
	GENERATED_BODY()
	TWeakObjectPtr<AMiniCharacter> VictimPawn;
	TWeakObjectPtr<AMiniPlayerState> VictimPlayerState;
	TWeakObjectPtr<AMiniPlayerState> InstigatorPlayerState;
	TWeakObjectPtr<AMiniCharacter> InstigatorPawn;
	int32 RoundId = 0;
	uint32 VictimLifeId = 0;
	EMiniPlayerDeathCause Cause = EMiniPlayerDeathCause::Environment;
};

USTRUCT(BlueprintType)
struct FPS_API FMiniPlayerMatchStats
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 RoundId = 0;
	UPROPERTY(BlueprintReadOnly) int32 Kills = 0;
	UPROPERTY(BlueprintReadOnly) int32 Deaths = 0;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
};

USTRUCT(BlueprintType)
struct FPS_API FMiniMatchPlayerRow
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 PlayerId = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) int32 Kills = 0;
	UPROPERTY(BlueprintReadOnly) int32 Deaths = 0;
	UPROPERTY(BlueprintReadOnly) bool bConnected = false;
};

/** Scores are derived from PlayerState. ResultRows and winners freeze once per round. */
USTRUCT(BlueprintType)
struct FPS_API FMiniMatchState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 RoundId = 0;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
	UPROPERTY(BlueprintReadOnly) int32 ConnectedPlayerCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 MinPlayers = 2;
	UPROPERTY(BlueprintReadOnly) int32 ScoreLimit = 10;
	UPROPERTY(BlueprintReadOnly) bool bAcceptingScores = false;
	UPROPERTY(BlueprintReadOnly) bool bHasResult = false;
	UPROPERTY(BlueprintReadOnly) EMiniMatchEndReason EndReason = EMiniMatchEndReason::None;
	UPROPERTY(BlueprintReadOnly) bool bIsDraw = false;
	UPROPERTY(BlueprintReadOnly) TArray<FMiniMatchPlayerRow> Rows;
	UPROPERTY(BlueprintReadOnly) TArray<FMiniMatchPlayerRow> ResultRows;
	UPROPERTY(BlueprintReadOnly) TArray<int32> WinnerPlayerIds;
};
