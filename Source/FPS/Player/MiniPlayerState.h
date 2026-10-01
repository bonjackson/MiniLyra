#pragma once

#include "AbilitySystemInterface.h"
#include "AbilitySystem/MiniAbilitySet.h"
#include "Arena/MiniMatchTypes.h"
#include "ModularPlayerState.h"
#include "MiniPlayerState.generated.h"

class UMiniPawnData;
class UMiniAbilitySystemComponent;
class UMiniHealthSet;
class AMiniCharacter;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMiniPlayerMatchStatsChanged, const FMiniPlayerMatchStats&);

/** Replicated ability owner; its ASC survives replacement of the Pawn avatar. */
UCLASS()
class FPS_API AMiniPlayerState : public AModularPlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMiniPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	static const FName NAME_AbilityActorReady;

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UMiniAbilitySystemComponent* GetMiniAbilitySystemComponent() const { return AbilitySystemComponent; }
	UMiniHealthSet* GetHealthSet() const { return HealthSet; }

	/** Authority only. Repeating the same assignment is safe; replacing it is not. */
	bool SetPawnData(const UMiniPawnData* InPawnData);
	const UMiniPawnData* GetPawnData() const { return PawnData; }
	/** Authority only; repeated binding of the same Avatar does not create another life. */
	bool BeginLifeForPawn(AMiniCharacter* Pawn);
	uint32 GetCurrentLifeId() const { return CurrentLifeId; }
	AMiniCharacter* GetCurrentLifePawn() const;
	const FMiniPlayerMatchStats& GetMatchStats() const { return MatchStats; }
	bool ResetMatchStats(int32 RoundId);
	bool RecordMatchKill(int32 RoundId);
	bool RecordMatchDeath(int32 RoundId);
	FOnMiniPlayerMatchStatsChanged OnMatchStatsChanged;

private:
	UFUNCTION()
	void OnRep_PawnData();
	void NotifyPawnDataChanged();
	UFUNCTION() void OnRep_MatchStats();
	void CommitMatchStats();
	UPROPERTY(ReplicatedUsing = OnRep_MatchStats)
	FMiniPlayerMatchStats MatchStats;
	UPROPERTY(Replicated)
	uint32 CurrentLifeId = 0;
	TWeakObjectPtr<AMiniCharacter> CurrentLifePawn;

	UPROPERTY(ReplicatedUsing = OnRep_PawnData, VisibleInstanceOnly, Category = "Mini|Pawn")
	TObjectPtr<const UMiniPawnData> PawnData;

	UPROPERTY(VisibleAnywhere, Category = "Mini|Abilities")
	TObjectPtr<UMiniAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, Category = "Mini|Abilities")
	TObjectPtr<UMiniHealthSet> HealthSet;

	UPROPERTY(Transient)
	TArray<FMiniAbilitySetGrantedHandles> PawnDataGrantedHandles;
};
