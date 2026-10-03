#include "MiniTask24ProbeActor.h"

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
bool Task24SameRows(const TArray<FMiniMatchPlayerRow>& A, const TArray<FMiniMatchPlayerRow>& B)
{
	if (A.Num() != B.Num()) { return false; }
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (A[Index].PlayerId != B[Index].PlayerId || A[Index].DisplayName != B[Index].DisplayName ||
			A[Index].Kills != B[Index].Kills || A[Index].Deaths != B[Index].Deaths || A[Index].bConnected != B[Index].bConnected) { return false; }
	}
	return true;
}
}

bool MiniTask24SamePhase(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B)
{
	return A.PhaseTag == B.PhaseTag && A.Revision == B.Revision &&
		FMath::Abs(A.PhaseStartTimeServer - B.PhaseStartTimeServer) < 0.01 && FMath::Abs(A.PhaseEndTimeServer - B.PhaseEndTimeServer) < 0.01;
}

bool MiniTask24SameMatch(const FMiniMatchState& A, const FMiniMatchState& B)
{
	return A.RoundId == B.RoundId && A.Revision == B.Revision && A.ConnectedPlayerCount == B.ConnectedPlayerCount &&
		A.MinPlayers == B.MinPlayers && A.ScoreLimit == B.ScoreLimit && A.bAcceptingScores == B.bAcceptingScores &&
		A.bHasResult == B.bHasResult && A.EndReason == B.EndReason && A.bIsDraw == B.bIsDraw &&
		A.WinnerPlayerIds == B.WinnerPlayerIds && Task24SameRows(A.Rows, B.Rows) && Task24SameRows(A.ResultRows, B.ResultRows);
}

AMiniTask24ProbeActor::AMiniTask24ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
}

void AMiniTask24ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex);
	DOREPLIFETIME(ThisClass, Serial);
	DOREPLIFETIME(ThisClass, CheckpointName);
	DOREPLIFETIME(ThisClass, ExpectedPhase);
	DOREPLIFETIME(ThisClass, ExpectedMatch);
	DOREPLIFETIME(ThisClass, ExpectedLifeId);
	DOREPLIFETIME(ThisClass, ExpectedPawn);
	DOREPLIFETIME(ThisClass, Command);
}

void AMiniTask24ProbeActor::InitializeServer(int32 Index)
{
	if (HasAuthority()) { OwnerIndex = Index; ForceNetUpdate(); }
}

void AMiniTask24ProbeActor::SetCheckpoint(int32 InSerial, FName Name, const FMiniGamePhaseState& Phase, const FMiniMatchState& Match)
{
	if (!HasAuthority()) { return; }
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	const AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	Serial = InSerial;
	CheckpointName = Name;
	ExpectedPhase = Phase;
	ExpectedMatch = Match;
	ExpectedLifeId = PS ? PS->GetCurrentLifeId() : 0;
	ExpectedPawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	ForceNetUpdate();
}

void AMiniTask24ProbeActor::SetCommand(EMiniTask24Command InCommand)
{
	if (HasAuthority()) { Command = InCommand; ForceNetUpdate(); }
}

void AMiniTask24ProbeActor::ServerAcknowledge_Implementation(int32 InSerial, FName Experience,
	const FMiniGamePhaseState& Phase, const FMiniMatchState& Match, const FMiniPlayerMatchStats& Stats, uint32 LifeId, AMiniCharacter* Pawn,
	double ServerClock, bool bHUDReady, bool bUIGateClear)
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	const AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask24="), Mode) || !PC || PC->IsLocalController() ||
		!PS || !GS || InSerial != Serial || HasAcknowledged()) { return; }
	const FMiniPlayerMatchStats& Actual = PS->GetMatchStats();
	if (!bHUDReady || !bUIGateClear || Experience != TEXT("DA_MiniArenaExperience") ||
		!MiniTask24SamePhase(Phase, ExpectedPhase) || !MiniTask24SameMatch(Match, ExpectedMatch) ||
		Stats.RoundId != Actual.RoundId || Stats.Revision != Actual.Revision || Stats.Kills != Actual.Kills || Stats.Deaths != Actual.Deaths ||
		LifeId != ExpectedLifeId || Pawn != ExpectedPawn || PS->GetCurrentLifeId() != LifeId || PC->GetPawn() != Pawn ||
		!FMath::IsFinite(ServerClock) || FMath::Abs(GS->GetServerWorldTimeSeconds() - ServerClock) > 1.0)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask24Probe FAIL: Owner=%d Checkpoint=%s ReplicatedStateAvatarHUDMismatch=1"), OwnerIndex, *CheckpointName.ToString());
		return;
	}
	AcknowledgedSerial = Serial;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe SERVER_CLIENT_ACK: Owner=%d Checkpoint=%s Round=%d Life=%u HUD=1 UIGates=0"),
		OwnerIndex, *CheckpointName.ToString(), Match.RoundId, LifeId);
#endif
}
