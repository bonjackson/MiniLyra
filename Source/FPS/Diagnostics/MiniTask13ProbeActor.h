#pragma once

#include "GameFramework/Actor.h"
#include "MiniTask13ProbeActor.generated.h"

class AController;
class AMiniCharacter;
class AMiniGameMode;
class AMiniPlayerController;
class UMiniAbilitySystemComponent;
class UMiniHealthSet;

/** Remote-owned RPC bridge for the Task 13 authority and respawn acceptance probe. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask13ProbeActor : public AActor
{
	GENERATED_BODY()

public:
	AMiniTask13ProbeActor();

	UFUNCTION(Server, Reliable)
	void ServerApplyNonlethal();

	UFUNCTION(Server, Reliable)
	void ServerApplyLethal(int32 Cycle);

	UFUNCTION(Server, Reliable)
	void ServerVerifyRespawn(int32 Cycle);

	/** Force the third death through UnPossess instead of waiting for its timer. */
	UFUNCTION(Server, Reliable)
	void ServerForceRespawnAfterDeath();

private:
	enum class EPhase : uint8
	{
		Initial,
		NonlethalApplied,
		FirstDead,
		FirstRespawnVerified,
		SecondDead,
		SecondRespawnVerified,
		ForcedRestartPending,
		Done
	};

	AMiniPlayerController* GetProbeController() const;
	AController* GetOtherController() const;
	AMiniGameMode* GetMiniGameMode() const;
	UMiniAbilitySystemComponent* GetProbeASC() const;
	UMiniHealthSet* GetProbeHealthSet() const;
	void Fail(const TCHAR* Reason);
	bool CheckAliveBaseline(AMiniCharacter* Pawn, float ExpectedHealth) const;
	int32 GetAbilityCount(const UMiniAbilitySystemComponent* ASC) const;
	void VerifyForcedRespawn();

	EPhase Phase = EPhase::Initial;
	TWeakObjectPtr<AMiniCharacter> LastDeadPawn;
	TWeakObjectPtr<AMiniCharacter> ForcedOldPawn;
	TWeakObjectPtr<UMiniAbilitySystemComponent> OriginalASC;
	FString LastDeadPawnPath;
	FString ForcedOldPawnPath;
	int32 OriginalAbilityCount = INDEX_NONE;
	float LastDeathTime = 0.0f;
	float ForcedRestartTime = 0.0f;
	bool bFailed = false;
};
