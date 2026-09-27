#pragma once

#include "ModularPlayerState.h"
#include "MiniPlayerState.generated.h"

class UMiniPawnData;

/** Replicated player-owned PawnData; ability ownership is added in Task 09. */
UCLASS()
class FPS_API AMiniPlayerState : public AModularPlayerState
{
	GENERATED_BODY()

public:
	AMiniPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Authority only. Repeating the same assignment is safe; replacing it is not. */
	bool SetPawnData(const UMiniPawnData* InPawnData);
	const UMiniPawnData* GetPawnData() const { return PawnData; }

private:
	UFUNCTION()
	void OnRep_PawnData();

	UPROPERTY(ReplicatedUsing = OnRep_PawnData, VisibleInstanceOnly, Category = "Mini|Pawn")
	TObjectPtr<const UMiniPawnData> PawnData;
};
