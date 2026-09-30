#include "MiniTask19ProbeActor.h"

#include "Character/MiniCharacter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"

AMiniTask19ProbeActor::AMiniTask19ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
}

void AMiniTask19ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex);
	DOREPLIFETIME(ThisClass, OwnerPawn);
	DOREPLIFETIME(ThisClass, PeerPawn);
	DOREPLIFETIME(ThisClass, Phase);
}

void AMiniTask19ProbeActor::InitializeServer(int32 Index, AMiniCharacter* Pawn, AMiniCharacter* Peer)
{
	if (HasAuthority() && Index >= 1 && Index <= 2)
	{
		OwnerIndex = Index;
		UpdateServerPawns(Pawn, Peer);
	}
}

void AMiniTask19ProbeActor::UpdateServerPawns(AMiniCharacter* Pawn, AMiniCharacter* Peer)
{
	if (HasAuthority() && IsValid(Pawn) && IsValid(Peer) && Pawn != Peer)
	{
		OwnerPawn = Pawn;
		PeerPawn = Peer;
		ForceNetUpdate();
	}
}

void AMiniTask19ProbeActor::SetServerPhase(EMiniTask19Phase Value)
{
	if (HasAuthority() && static_cast<int32>(Value) == static_cast<int32>(Phase) + 1)
	{
		Phase = Value;
		ForceNetUpdate();
	}
}

bool AMiniTask19ProbeActor::HasAcknowledged(EMiniTask19Phase Value) const
{
	return LastAcknowledged >= static_cast<int32>(Value);
}

void AMiniTask19ProbeActor::ServerAcknowledge_Implementation(EMiniTask19Phase Value)
{
#if !UE_BUILD_SHIPPING
	const AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask19")) || !Controller ||
		Controller->IsLocalController() || Value != Phase ||
		static_cast<int32>(Value) != LastAcknowledged + 1)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask19Probe FAIL: invalid owner checkpoint Owner=%d"), OwnerIndex);
		return;
	}
	LastAcknowledged = static_cast<int32>(Value);
#endif
}
