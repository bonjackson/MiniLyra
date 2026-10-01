#include "MiniTask20ProbeActor.h"

#include "Character/MiniCharacter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "Practice/MiniPracticeSupply.h"
#include "System/MiniLogChannels.h"
#include "Training/MiniPracticeTarget.h"

AMiniTask20ProbeActor::AMiniTask20ProbeActor()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
}

void AMiniTask20ProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex);
	DOREPLIFETIME(ThisClass, OwnerPawn);
	DOREPLIFETIME(ThisClass, PeerPawn);
	DOREPLIFETIME(ThisClass, Target);
	DOREPLIFETIME(ThisClass, Supply);
	DOREPLIFETIME(ThisClass, Phase);
}

void AMiniTask20ProbeActor::InitializeServer(int32 Index, AMiniCharacter* Pawn, AMiniCharacter* Peer,
	AMiniPracticeTarget* InTarget, AMiniPracticeSupply* InSupply)
{
	if (HasAuthority() && Index >= 1 && Index <= 2 && IsValid(Pawn) && IsValid(Peer) && Pawn != Peer)
	{
		OwnerIndex = Index; OwnerPawn = Pawn; PeerPawn = Peer; Target = InTarget; Supply = InSupply;
		ForceNetUpdate();
	}
}

void AMiniTask20ProbeActor::SetServerPhase(EMiniTask20Phase Value)
{
	if (HasAuthority() && static_cast<int32>(Value) == static_cast<int32>(Phase) + 1)
	{
		Phase = Value;
		ForceNetUpdate();
	}
}

bool AMiniTask20ProbeActor::HasAcknowledged(EMiniTask20Phase Value) const
{
	return LastAcknowledged >= static_cast<int32>(Value);
}

void AMiniTask20ProbeActor::ServerAcknowledge_Implementation(EMiniTask20Phase Value)
{
#if !UE_BUILD_SHIPPING
	const AMiniPlayerController* PC = Cast<AMiniPlayerController>(GetOwner());
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask20")) || !PC || PC->IsLocalController() ||
		Value != Phase || static_cast<int32>(Value) != LastAcknowledged + 1)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask20Probe FAIL: invalid owner checkpoint Owner=%d"), OwnerIndex);
		return;
	}
	LastAcknowledged = static_cast<int32>(Value);
#endif
}
