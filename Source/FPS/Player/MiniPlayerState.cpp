#include "MiniPlayerState.h"

#include "Character/MiniPawnData.h"
#include "Net/UnrealNetwork.h"
#include "System/MiniLogChannels.h"

AMiniPlayerState::AMiniPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
}

void AMiniPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMiniPlayerState, PawnData);
}

bool AMiniPlayerState::SetPawnData(const UMiniPawnData* InPawnData)
{
	if (!HasAuthority() || !InPawnData)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniPlayerState PawnDataRejected Reason=AuthorityOrNull PlayerState=%s"), *GetPathName());
		return false;
	}
	if (PawnData)
	{
		if (PawnData == InPawnData)
		{
			return true;
		}
		UE_LOG(LogMiniInit, Error, TEXT("MiniPlayerState PawnDataRejected Reason=AlreadySet PlayerState=%s Current=%s Requested=%s"),
			*GetPathName(), *GetPathNameSafe(PawnData.Get()), *GetPathNameSafe(InPawnData));
		return false;
	}
	FString Error;
	if (!InPawnData->ValidatePawnData(Error))
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniPlayerState PawnDataRejected Reason=%s PlayerState=%s"), *Error, *GetPathName());
		return false;
	}

	PawnData = InPawnData;
	ForceNetUpdate();
	UE_LOG(LogMiniInit, Display, TEXT("MiniPlayerState PawnDataAssigned Role=%d PlayerState=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
	return true;
}

void AMiniPlayerState::OnRep_PawnData()
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniPlayerState PawnDataReplicated Role=%d PlayerState=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
}
