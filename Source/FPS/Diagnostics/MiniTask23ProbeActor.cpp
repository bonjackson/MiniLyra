#include "MiniTask23ProbeActor.h"

#include "Character/MiniCharacter.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniLogChannels.h"

namespace
{
bool Task23SameRows(const TArray<FMiniMatchPlayerRow>& A, const TArray<FMiniMatchPlayerRow>& B)
{
	if (A.Num() != B.Num()) { return false; }
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (A[Index].PlayerId != B[Index].PlayerId || A[Index].DisplayName != B[Index].DisplayName ||
			A[Index].Kills != B[Index].Kills || A[Index].Deaths != B[Index].Deaths ||
			A[Index].bConnected != B[Index].bConnected) { return false; }
	}
	return true;
}
}

bool MiniTask23SamePhase(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B)
{
	return A.PhaseTag == B.PhaseTag && A.Revision == B.Revision &&
		FMath::Abs(A.PhaseStartTimeServer - B.PhaseStartTimeServer) < 0.01 &&
		FMath::Abs(A.PhaseEndTimeServer - B.PhaseEndTimeServer) < 0.01;
}

bool MiniTask23SameMatch(const FMiniMatchState& A, const FMiniMatchState& B)
{
	return A.RoundId == B.RoundId && A.Revision == B.Revision &&
		A.ConnectedPlayerCount == B.ConnectedPlayerCount && A.MinPlayers == B.MinPlayers &&
		A.ScoreLimit == B.ScoreLimit && A.bAcceptingScores == B.bAcceptingScores &&
		A.bHasResult == B.bHasResult && A.EndReason == B.EndReason && A.bIsDraw == B.bIsDraw &&
		A.WinnerPlayerIds == B.WinnerPlayerIds && Task23SameRows(A.Rows, B.Rows) && Task23SameRows(A.ResultRows, B.ResultRows);
}

AMiniTask23ProbeActor::AMiniTask23ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
}

void AMiniTask23ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex);
	DOREPLIFETIME(ThisClass, CheckpointSerial);
	DOREPLIFETIME(ThisClass, CheckpointName);
	DOREPLIFETIME(ThisClass, ExpectedPhase);
	DOREPLIFETIME(ThisClass, ExpectedMatch);
	DOREPLIFETIME(ThisClass, ExpectedLifeId);
	DOREPLIFETIME(ThisClass, ExpectedPawn);
	DOREPLIFETIME(ThisClass, bRequireLivePawn);
}

void AMiniTask23ProbeActor::InitializeServer(int32 Index)
{
	if (HasAuthority() && Index > 0) { OwnerIndex = Index; ForceNetUpdate(); }
}

void AMiniTask23ProbeActor::SetCheckpoint(int32 Serial, FName Name, const FMiniGamePhaseState& Phase,
	const FMiniMatchState& Match, bool bLivePawn)
{
	if (!HasAuthority()) { return; }
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	const AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	CheckpointSerial = Serial;
	CheckpointName = Name;
	ExpectedPhase = Phase;
	ExpectedMatch = Match;
	ExpectedLifeId = PS ? PS->GetCurrentLifeId() : 0;
	ExpectedPawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	bRequireLivePawn = bLivePawn;
	ForceNetUpdate();
}

void AMiniTask23ProbeActor::ServerAcknowledge_Implementation(int32 Serial,
	const FMiniGamePhaseState& Phase, const FMiniMatchState& Match, const FMiniPlayerMatchStats& Stats,
	uint32 LifeId, AMiniCharacter* Pawn, double ServerClock, bool bHUDMatches, bool bMapMatches)
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	const AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask23="), Mode) || !PC ||
		PC->IsLocalController() || !PS || !GS || Serial != CheckpointSerial || HasAcknowledged()) { return; }
	const FMiniPlayerMatchStats& Actual = PS->GetMatchStats();
	const double ClockError = FMath::Abs(GS->GetServerWorldTimeSeconds() - ServerClock);
	if (!bHUDMatches || !bMapMatches || !FMath::IsFinite(ServerClock) || ClockError > 1.0 ||
		!MiniTask23SamePhase(Phase, ExpectedPhase) || !MiniTask23SameMatch(Match, ExpectedMatch) ||
		Stats.RoundId != Actual.RoundId || Stats.Kills != Actual.Kills || Stats.Deaths != Actual.Deaths ||
		Stats.Revision != Actual.Revision || LifeId != ExpectedLifeId || Pawn != ExpectedPawn ||
		PS->GetCurrentLifeId() != LifeId || PC->GetPawn() != Pawn)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask23Probe FAIL: replicated map/state/avatar/HUD mismatch Owner=%d Checkpoint=%s ClockError=%.3f"),
			OwnerIndex, *CheckpointName.ToString(), ClockError);
		return;
	}
	AcknowledgedSerial = Serial;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_CLIENT_ACK: Owner=%d Checkpoint=%s Round=%d Life=%u Kills=%d Deaths=%d Map=1 HUD=1 ClockError=%.3f"),
		OwnerIndex, *CheckpointName.ToString(), Match.RoundId, LifeId, Stats.Kills, Stats.Deaths, ClockError);
#endif
}
