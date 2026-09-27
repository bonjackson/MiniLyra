#include "MiniCharacter.h"

#include "Character/MiniPawnData.h"
#include "Net/UnrealNetwork.h"
#include "System/MiniLogChannels.h"

AMiniCharacter::AMiniCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
}

void AMiniCharacter::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogMiniInit, Display, TEXT("MiniCharacter BeginPlay Role=%d Pawn=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
}

void AMiniCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMiniCharacter, PawnData);
}

bool AMiniCharacter::SetPawnData(const UMiniPawnData* InPawnData)
{
	if (!HasAuthority() || !InPawnData)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=AuthorityOrNull Pawn=%s"), *GetPathName());
		return false;
	}
	if (PawnData)
	{
		if (PawnData == InPawnData)
		{
			return true;
		}
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=AlreadySet Pawn=%s Current=%s Requested=%s"),
			*GetPathName(), *GetPathNameSafe(PawnData.Get()), *GetPathNameSafe(InPawnData));
		return false;
	}
	if (HasActorBegunPlay())
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=AfterBeginPlay Pawn=%s"), *GetPathName());
		return false;
	}
	FString Error;
	if (!InPawnData->ValidatePawnData(Error))
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=%s Pawn=%s"), *Error, *GetPathName());
		return false;
	}

	PawnData = InPawnData;
	ForceNetUpdate();
	UE_LOG(LogMiniInit, Display, TEXT("MiniCharacter PawnDataAssigned Role=%d Pawn=%s PawnData=%s BeforeBeginPlay=1"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
	return true;
}

void AMiniCharacter::OnRep_PawnData()
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniCharacter PawnDataReplicated Role=%d Pawn=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
}
