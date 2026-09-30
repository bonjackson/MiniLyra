#include "MiniTask17ProbeActor.h"

#include "Character/MiniCharacter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"

AMiniTask17ProbeActor::AMiniTask17ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
}

void AMiniTask17ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex);
	DOREPLIFETIME(ThisClass, OwnerPawn);
	DOREPLIFETIME(ThisClass, PeerPawn);
	DOREPLIFETIME(ThisClass, Phase);
}

void AMiniTask17ProbeActor::InitializeServer(int32 InOwnerIndex,
	AMiniCharacter* InOwnerPawn, AMiniCharacter* InPeerPawn)
{
	if (!HasAuthority() || InOwnerIndex < 1 || InOwnerIndex > 2 ||
		!IsValid(InOwnerPawn) || !IsValid(InPeerPawn) || InOwnerPawn == InPeerPawn)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask17Probe FAIL: invalid probe actor initialization"));
		return;
	}
	OwnerIndex = InOwnerIndex;
	OwnerPawn = InOwnerPawn;
	PeerPawn = InPeerPawn;
	ForceNetUpdate();
}

void AMiniTask17ProbeActor::SetServerPhase(EMiniTask17Phase InPhase)
{
	if (!HasAuthority() || static_cast<int32>(InPhase) != static_cast<int32>(Phase) + 1)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask17Probe FAIL: invalid phase transition Owner=%d"), OwnerIndex);
		return;
	}
	Phase = InPhase;
	ForceNetUpdate();
}

bool AMiniTask17ProbeActor::HasAcknowledged(EMiniTask17Phase InPhase) const
{
	return LastAcknowledgedPhase >= static_cast<int32>(InPhase);
}

void AMiniTask17ProbeActor::ServerAcknowledge_Implementation(EMiniTask17Phase AcknowledgedPhase)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask17")))
	{
		return;
	}
	const AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	if (!HasAuthority() || !Controller || Controller->IsLocalController() ||
		OwnerIndex < 1 || OwnerIndex > 2 || AcknowledgedPhase != Phase ||
		static_cast<int32>(AcknowledgedPhase) != LastAcknowledgedPhase + 1)
	{
		UE_LOG(LogMiniInit, Error,
			TEXT("MiniTask17Probe FAIL: invalid client checkpoint Owner=%d Phase=%d Ack=%d Last=%d"),
			OwnerIndex, static_cast<int32>(Phase), static_cast<int32>(AcknowledgedPhase), LastAcknowledgedPhase);
		return;
	}
	LastAcknowledgedPhase = static_cast<int32>(AcknowledgedPhase);
#endif
}
