#pragma once

#include "ModularCharacter.h"
#include "MiniCharacter.generated.h"

class UMiniPawnData;

/** A modular, replicated pawn whose data is assigned before deferred spawning finishes. */
UCLASS(Blueprintable)
class FPS_API AMiniCharacter : public AModularCharacter
{
	GENERATED_BODY()

public:
	AMiniCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority only. The first assignment must happen before FinishSpawning. */
	bool SetPawnData(const UMiniPawnData* InPawnData);
	const UMiniPawnData* GetPawnData() const { return PawnData; }

private:
	UFUNCTION()
	void OnRep_PawnData();

	UPROPERTY(ReplicatedUsing = OnRep_PawnData, VisibleInstanceOnly, Category = "Mini|Pawn")
	TObjectPtr<const UMiniPawnData> PawnData;
};
