#include "MiniTask22ProbeActor.h"

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
bool SameRows(const TArray<FMiniMatchPlayerRow>& A, const TArray<FMiniMatchPlayerRow>& B)
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

bool MiniTask22SamePhaseState(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B)
{
	return A.PhaseTag == B.PhaseTag && A.Revision == B.Revision &&
		FMath::Abs(A.PhaseStartTimeServer - B.PhaseStartTimeServer) < 0.01 &&
		FMath::Abs(A.PhaseEndTimeServer - B.PhaseEndTimeServer) < 0.01;
}

bool MiniTask22SameMatchState(const FMiniMatchState& A, const FMiniMatchState& B)
{
	return A.RoundId == B.RoundId && A.Revision == B.Revision &&
		A.ConnectedPlayerCount == B.ConnectedPlayerCount && A.MinPlayers == B.MinPlayers &&
		A.ScoreLimit == B.ScoreLimit && A.bAcceptingScores == B.bAcceptingScores &&
		A.bHasResult == B.bHasResult && A.EndReason == B.EndReason && A.bIsDraw == B.bIsDraw &&
		A.WinnerPlayerIds == B.WinnerPlayerIds && SameRows(A.Rows, B.Rows) && SameRows(A.ResultRows, B.ResultRows);
}

AMiniTask22ProbeActor::AMiniTask22ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
}

void AMiniTask22ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex);
	DOREPLIFETIME(ThisClass, CheckpointSerial);
	DOREPLIFETIME(ThisClass, CheckpointName);
	DOREPLIFETIME(ThisClass, ExpectedPhase);
	DOREPLIFETIME(ThisClass, ExpectedMatch);
	DOREPLIFETIME(ThisClass, bRulesRemoved);
}

void AMiniTask22ProbeActor::InitializeServer(int32 Index)
{
	if (HasAuthority() && Index > 0) { OwnerIndex = Index; ForceNetUpdate(); }
}

void AMiniTask22ProbeActor::SetCheckpoint(int32 Serial, FName Name, const FMiniGamePhaseState& Phase,
	const FMiniMatchState& Match, bool bRemoved)
{
	if (!HasAuthority()) { return; }
	CheckpointSerial = Serial;
	CheckpointName = Name;
	ExpectedPhase = Phase;
	ExpectedMatch = Match;
	bRulesRemoved = bRemoved;
	ForceNetUpdate();
}

void AMiniTask22ProbeActor::ServerAcknowledge_Implementation(int32 Serial,
	const FMiniGamePhaseState& Phase, const FMiniMatchState& Match, const FMiniPlayerMatchStats& Stats,
	double ServerClock, bool bHUDMatches, bool bMutationRejected)
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	const AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask22="), Mode) || !PC ||
		PC->IsLocalController() || !PS || !GS || Serial != CheckpointSerial || HasAcknowledged()) { return; }
	const FMiniPlayerMatchStats& Actual = PS->GetMatchStats();
	const double Error = FMath::Abs(GS->GetServerWorldTimeSeconds() - ServerClock);
	if (!bHUDMatches || !bMutationRejected || !FMath::IsFinite(ServerClock) || Error > 1.0 ||
		!MiniTask22SamePhaseState(Phase, ExpectedPhase) || !MiniTask22SameMatchState(Match, ExpectedMatch) ||
		Stats.RoundId != Actual.RoundId || Stats.Kills != Actual.Kills || Stats.Deaths != Actual.Deaths ||
		Stats.Revision != Actual.Revision)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask22Probe FAIL: client replicated state/HUD/clock mismatch Owner=%d Checkpoint=%s ClockError=%.3f"),
			OwnerIndex, *CheckpointName.ToString(), Error);
		return;
	}
	AcknowledgedSerial = Serial;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_CLIENT_ACK: Owner=%d Checkpoint=%s Round=%d Kills=%d Deaths=%d Result=%d ClockError=%.3f HUD=1 MutationsRejected=1"),
		OwnerIndex, *CheckpointName.ToString(), Match.RoundId, Stats.Kills, Stats.Deaths, Match.bHasResult ? 1 : 0, Error);
#endif
}

void AMiniTask22ProbeActor::ClientLeaveServer_Implementation()
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask22="), Mode)) { return; }
	if (AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner()))
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe CLIENT_LEAVE: Owner=%d ActualDisconnect=1"), OwnerIndex);
		PC->ConsoleCommand(TEXT("disconnect"), true);
	}
#endif
}
