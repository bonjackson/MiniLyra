#include "MiniTask25ProbeActor.h"

#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
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
bool Task25SameRows(const TArray<FMiniMatchPlayerRow>& A, const TArray<FMiniMatchPlayerRow>& B)
{
	if (A.Num() != B.Num()) { return false; }
	for (int32 I = 0; I < A.Num(); ++I)
	{
		if (A[I].PlayerId != B[I].PlayerId || A[I].DisplayName != B[I].DisplayName || A[I].Kills != B[I].Kills ||
			A[I].Deaths != B[I].Deaths || A[I].bConnected != B[I].bConnected) { return false; }
	}
	return true;
}
}
bool MiniTask25SamePhase(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B)
{
	return A.PhaseTag == B.PhaseTag && A.Revision == B.Revision &&
		FMath::Abs(A.PhaseStartTimeServer - B.PhaseStartTimeServer) < 0.01 && FMath::Abs(A.PhaseEndTimeServer - B.PhaseEndTimeServer) < 0.01;
}
bool MiniTask25SameMatch(const FMiniMatchState& A, const FMiniMatchState& B)
{
	return A.RoundId == B.RoundId && A.Revision == B.Revision && A.ConnectedPlayerCount == B.ConnectedPlayerCount &&
		A.MinPlayers == B.MinPlayers && A.ScoreLimit == B.ScoreLimit && A.bAcceptingScores == B.bAcceptingScores &&
		A.bHasResult == B.bHasResult && A.EndReason == B.EndReason && A.bIsDraw == B.bIsDraw &&
		A.WinnerPlayerIds == B.WinnerPlayerIds && Task25SameRows(A.Rows, B.Rows) && Task25SameRows(A.ResultRows, B.ResultRows);
}
AMiniTask25ProbeActor::AMiniTask25ProbeActor() { bReplicates = true; bOnlyRelevantToOwner = true; }
void AMiniTask25ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex); DOREPLIFETIME(ThisClass, Serial); DOREPLIFETIME(ThisClass, Iteration);
	DOREPLIFETIME(ThisClass, CheckpointName); DOREPLIFETIME(ThisClass, bExpectDead); DOREPLIFETIME(ThisClass, bExpectFresh);
	DOREPLIFETIME(ThisClass, ExpectedLife); DOREPLIFETIME(ThisClass, ExpectedPawn); DOREPLIFETIME(ThisClass, ExpectedPlayerState);
	DOREPLIFETIME(ThisClass, ExpectedPhase); DOREPLIFETIME(ThisClass, ExpectedMatch); DOREPLIFETIME(ThisClass, Command);
}
void AMiniTask25ProbeActor::InitializeServer(int32 Index) { if (HasAuthority()) { OwnerIndex = Index; ForceNetUpdate(); } }
void AMiniTask25ProbeActor::SetCommand(EMiniTask25Command Value) { if (HasAuthority()) { Command = Value; ForceNetUpdate(); } }
void AMiniTask25ProbeActor::SetCheckpoint(int32 InSerial, FName Name, int32 InIteration, bool bDead, bool bFresh,
	const FMiniGamePhaseState& Phase, const FMiniMatchState& Match)
{
	if (!HasAuthority()) { return; }
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	ExpectedPlayerState = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	ExpectedPawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	ExpectedLife = ExpectedPlayerState ? ExpectedPlayerState->GetCurrentLifeId() : 0;
	Serial = InSerial; Iteration = InIteration; CheckpointName = Name;
	bExpectDead = bDead; bExpectFresh = bFresh; ExpectedPhase = Phase; ExpectedMatch = Match;
	ForceNetUpdate();
}
void AMiniTask25ProbeActor::ServerAcknowledge_Implementation(int32 InSerial, const FMiniGamePhaseState& Phase,
	const FMiniMatchState& Match, const FMiniPlayerMatchStats& Stats, uint32 Life, AMiniCharacter* Pawn,
	AMiniPlayerState* PlayerState, double ServerClock, const FMiniTask25Observation& O)
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	const AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask25="), Mode) || !PC || PC->IsLocalController() ||
		!PS || !GS || InSerial != Serial || HasAcknowledged()) { return; }
	const FMiniPlayerMatchStats& Actual = PS->GetMatchStats();
	const bool bObserved = O.bHUDMatches && O.HUDWidgets == 4 && O.VMBindings > 0 && O.OldEquipmentSpecs == 0 &&
		(bExpectDead ? O.InputBindings == 0 && O.HeldInput == 0 && !O.bMovementTick && !O.bMapping && O.SavedMoves == 0 && O.EquipmentSpecs == 0
			: O.InputBindings == 16 && O.bMovementTick && O.bMapping && O.EquipmentSpecs > 0 && (!bExpectFresh || O.bFreshInventory));
	if (!bObserved || !MiniTask25SamePhase(Phase, ExpectedPhase) || !MiniTask25SameMatch(Match, ExpectedMatch) ||
		PS != PlayerState || PS != ExpectedPlayerState || Pawn != ExpectedPawn || PC->GetPawn() != Pawn ||
		Life != ExpectedLife || PS->GetCurrentLifeId() != Life || !Pawn || Pawn->GetHealthComponent()->IsDead() != bExpectDead ||
		Actual.RoundId != Stats.RoundId || Actual.Revision != Stats.Revision || Actual.Kills != Stats.Kills || Actual.Deaths != Stats.Deaths ||
		!FMath::IsFinite(ServerClock) || FMath::Abs(GS->GetServerWorldTimeSeconds() - ServerClock) > 1.0)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask25Probe FAIL: Owner=%d Checkpoint=%s ActualOwnerMeasurementsMismatch=1"), OwnerIndex, *CheckpointName.ToString());
		return;
	}
	AcknowledgedSerial = Serial;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe SERVER_CLIENT_ACK: Owner=%d Checkpoint=%s Iteration=%d Life=%u Deaths=%d Bindings=%d Held=%d Prediction=%d"),
		OwnerIndex, *CheckpointName.ToString(), Iteration, Life, Stats.Deaths, O.InputBindings, O.HeldInput, O.SavedMoves);
#endif
}
