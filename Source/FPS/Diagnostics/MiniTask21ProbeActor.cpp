#include "MiniTask21ProbeActor.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"

AMiniTask21ProbeActor::AMiniTask21ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	PrimaryActorTick.bCanEverTick = false;
}

void AMiniTask21ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex);
	DOREPLIFETIME(ThisClass, Checkpoint);
	DOREPLIFETIME(ThisClass, ExpectedState);
}

void AMiniTask21ProbeActor::InitializeServer(int32 Index)
{
	if (HasAuthority() && Index >= 1 && Index <= 2) { OwnerIndex = Index; ForceNetUpdate(); }
}

void AMiniTask21ProbeActor::SetCheckpoint(EMiniTask21Checkpoint Value, const FMiniGamePhaseState& State)
{
	if (!HasAuthority()) { return; }
	Checkpoint = Value;
	ExpectedState = State;
	ForceNetUpdate();
}

bool AMiniTask21ProbeActor::HasAcknowledged(EMiniTask21Checkpoint Value) const
{
	return (AcknowledgedMask & (1u << static_cast<uint8>(Value))) != 0;
}

void AMiniTask21ProbeActor::ServerAcknowledge_Implementation(EMiniTask21Checkpoint Value,
	const FMiniGamePhaseState& ClientState, double ClientServerClock, bool bMutationRejected)
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask21="), Mode) || Mode != TEXT("Core") ||
		!PC || PC->IsLocalController() || !GS || Value != Checkpoint || HasAcknowledged(Value))
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask21Probe FAIL: invalid owner checkpoint Owner=%d"), OwnerIndex);
		return;
	}
	const double ClockError = FMath::Abs(GS->GetServerWorldTimeSeconds() - ClientServerClock);
	const bool bRemoved = Value == EMiniTask21Checkpoint::Removed;
	if (!bMutationRejected || !FMath::IsFinite(ClientServerClock) || ClockError > 1.0 ||
		ClientState.PhaseTag != ExpectedState.PhaseTag ||
		FMath::Abs(ClientState.PhaseEndTimeServer - ExpectedState.PhaseEndTimeServer) > 0.01 ||
		(!bRemoved && (ClientState.Revision != ExpectedState.Revision ||
			FMath::Abs(ClientState.PhaseStartTimeServer - ExpectedState.PhaseStartTimeServer) > 0.01)))
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask21Probe FAIL: client phase/deadline/clock/mutation mismatch Owner=%d Checkpoint=%d ClockError=%.3f"),
			OwnerIndex, static_cast<int32>(Value), ClockError);
		return;
	}
	AcknowledgedMask |= 1u << static_cast<uint8>(Value);
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask21Probe SERVER_CLIENT_ACK: Owner=%d Checkpoint=%d Tag=%s Revision=%u Deadline=%.3f ClockError=%.3f MutationRejected=1"),
		OwnerIndex, static_cast<int32>(Value), *ClientState.PhaseTag.ToString(), ClientState.Revision,
		ClientState.PhaseEndTimeServer, ClockError);
#endif
}
