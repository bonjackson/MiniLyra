#include "MiniPlayerState.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniPawnData.h"
#include "Components/GameFrameworkComponentManager.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "System/MiniLogChannels.h"

const FName AMiniPlayerState::NAME_AbilityActorReady(TEXT("MiniAbilityActorReady"));

AMiniPlayerState::AMiniPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	SetNetUpdateFrequency(100.0f);
	AbilitySystemComponent = CreateDefaultSubobject<UMiniAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	HealthSet = CreateDefaultSubobject<UMiniHealthSet>(TEXT("HealthSet"));
}

void AMiniPlayerState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, nullptr);
	}
}

void AMiniPlayerState::BeginPlay()
{
	Super::BeginPlay();
	// ModularPlayerState sends GameActorReady before Super::BeginPlay, when
	// HasActorBegunPlay is still false. Feature grants wait for this later event.
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(this, NAME_AbilityActorReady);
}

void AMiniPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnMatchStatsChanged.Clear();
	CurrentLifePawn.Reset();
	if (HasAuthority() && AbilitySystemComponent)
	{
		for (FMiniAbilitySetGrantedHandles& Handles : PawnDataGrantedHandles)
		{
			Handles.TakeFromAbilitySystem(AbilitySystemComponent);
		}
		PawnDataGrantedHandles.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

UAbilitySystemComponent* AMiniPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMiniPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMiniPlayerState, PawnData);
	DOREPLIFETIME(AMiniPlayerState, MatchStats);
	DOREPLIFETIME(AMiniPlayerState, CurrentLifeId);
}

bool AMiniPlayerState::BeginLifeForPawn(AMiniCharacter* Pawn)
{
	if (!HasAuthority() || !IsValid(Pawn) || Pawn->GetWorld() != GetWorld() ||
		Pawn->GetPlayerState<AMiniPlayerState>() != this || AbilitySystemComponent->GetAvatarActor() != Pawn) { return false; }
	if (CurrentLifePawn.Get() == Pawn && CurrentLifeId != 0) { return true; }
	CurrentLifePawn = Pawn;
	if (++CurrentLifeId == 0) { ++CurrentLifeId; }
	ForceNetUpdate();
	return true;
}

AMiniCharacter* AMiniPlayerState::GetCurrentLifePawn() const
{
	return CurrentLifePawn.Get();
}

bool AMiniPlayerState::ResetMatchStats(int32 RoundId)
{
	if (!HasAuthority() || RoundId <= 0) { return false; }
	if (MatchStats.RoundId == RoundId) { return true; }
	MatchStats.RoundId = RoundId;
	MatchStats.Kills = 0;
	MatchStats.Deaths = 0;
	CommitMatchStats();
	return true;
}

bool AMiniPlayerState::RecordMatchKill(int32 RoundId)
{
	if (!HasAuthority() || RoundId <= 0 || MatchStats.RoundId != RoundId) { return false; }
	++MatchStats.Kills;
	CommitMatchStats();
	return true;
}

bool AMiniPlayerState::RecordMatchDeath(int32 RoundId)
{
	if (!HasAuthority() || RoundId <= 0 || MatchStats.RoundId != RoundId) { return false; }
	++MatchStats.Deaths;
	CommitMatchStats();
	return true;
}

void AMiniPlayerState::CommitMatchStats()
{
	++MatchStats.Revision;
	ForceNetUpdate();
	OnRep_MatchStats();
}

void AMiniPlayerState::OnRep_MatchStats()
{
	OnMatchStatsChanged.Broadcast(MatchStats);
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
	AbilitySystemComponent->SetTagRelationshipMapping(PawnData->TagRelationshipMapping);
	for (UMiniAbilitySet* Set : PawnData->AbilitySets)
	{
		if (!Set)
		{
			continue;
		}
		FMiniAbilitySetGrantedHandles& Handles = PawnDataGrantedHandles.AddDefaulted_GetRef();
		if (!Set->GiveToAbilitySystem(AbilitySystemComponent, Handles, const_cast<UMiniPawnData*>(PawnData.Get())))
		{
			PawnDataGrantedHandles.Pop();
		}
	}
	ForceNetUpdate();
	NotifyPawnDataChanged();
	UE_LOG(LogMiniInit, Display, TEXT("MiniPlayerState PawnDataAssigned Role=%d PlayerState=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
	return true;
}

void AMiniPlayerState::OnRep_PawnData()
{
	if (AbilitySystemComponent && PawnData)
	{
		AbilitySystemComponent->SetTagRelationshipMapping(PawnData->TagRelationshipMapping);
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniPlayerState PawnDataReplicated Role=%d PlayerState=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
	NotifyPawnDataChanged();
}

void AMiniPlayerState::NotifyPawnDataChanged()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AMiniCharacter> It(World); It; ++It)
	{
		AMiniCharacter* Character = *It;
		if (Character && Character->GetPlayerState<AMiniPlayerState>() == this)
		{
			Character->NotifyInitDependenciesChanged();
		}
	}
}
