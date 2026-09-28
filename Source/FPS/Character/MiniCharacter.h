#pragma once

#include "ModularCharacter.h"
#include "MiniCharacter.generated.h"

class UMiniPawnData;
class UMiniPawnExtensionComponent;
class UMiniHeroComponent;
class AMiniPlayerState;
class AController;
class UInputComponent;

/** A modular, replicated pawn whose data is assigned before deferred spawning finishes. */
UCLASS(Blueprintable)
class FPS_API AMiniCharacter : public AModularCharacter
{
	GENERATED_BODY()

public:
	AMiniCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Authority only. The first assignment must happen before FinishSpawning. */
	bool SetPawnData(const UMiniPawnData* InPawnData);
	const UMiniPawnData* GetPawnData() const { return PawnData; }
	const UMiniPawnData* GetPawnDataForInitialization() const;
	AMiniPlayerState* GetPlayerStateForInitialization() const;
	UMiniPawnExtensionComponent* GetPawnExtensionComponent() const { return PawnExtensionComponent; }
	UMiniHeroComponent* GetHeroComponent() const { return HeroComponent; }
	UInputComponent* GetPlayerInputComponent() const { return InputComponent; }
	void NotifyInitDependenciesChanged();

	/** Editor game-process probe only: expose dependencies in a controlled order. */
	bool IsInitOrderProbeEnabled() const { return bInitOrderProbeEnabled; }
	bool HasInputComponentForProbe() const { return InputComponent != nullptr; }
	void ReleaseInitProbePawnData();
	void ReleaseInitProbePlayerState();

private:
	UFUNCTION()
	void OnRep_PawnData();

	UPROPERTY(ReplicatedUsing = OnRep_PawnData, VisibleInstanceOnly, Category = "Mini|Pawn")
	TObjectPtr<const UMiniPawnData> PawnData;

	UPROPERTY(VisibleAnywhere, Category = "Mini|Initialization")
	TObjectPtr<UMiniPawnExtensionComponent> PawnExtensionComponent;

	UPROPERTY(VisibleAnywhere, Category = "Mini|Initialization")
	TObjectPtr<UMiniHeroComponent> HeroComponent;

	bool bInitOrderProbeEnabled = false;
	bool bInitProbePawnDataVisible = true;
	bool bInitProbePlayerStateVisible = true;
};
