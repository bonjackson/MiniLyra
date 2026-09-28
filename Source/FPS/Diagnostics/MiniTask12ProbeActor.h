#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "MiniTask12ProbeActor.generated.h"

class AMiniCharacter;
class AMiniPlayerController;
class UMiniAbilitySystemComponent;

/** Owned by the remote player so the headless client can request authoritative probe steps. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask12ProbeActor : public AActor
{
	GENERATED_BODY()

public:
	AMiniTask12ProbeActor();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(Server, Reliable)
	void ServerCancelWithTag(FGameplayTag BlockingTag, FGameplayTag ActiveAbilityInputTag);

	UFUNCTION(Server, Reliable)
	void ServerCheckRejected(FGameplayTag BlockingTag, FGameplayTag AbilityInputTag);

	UFUNCTION(Server, Reliable)
	void ServerClearTag(FGameplayTag BlockingTag);

	UFUNCTION(Server, Reliable)
	void ServerRespawnWhileAiming();

private:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetTag(FGameplayTag Tag, bool bEnabled);

	AMiniPlayerController* GetProbeController() const;
	UMiniAbilitySystemComponent* GetProbeASC() const;
	bool IsAllowedBlockingTag(FGameplayTag Tag) const;
	void Fail(const TCHAR* Reason);
	void FinishRespawn();

	FGameplayTag PendingCancelTag;
	FGameplayTag PendingActiveInputTag;
	FGameplayTag CancelCheckTag;
	FGameplayTag CancelCheckInputTag;
	float PendingSeconds = 0.0f;
	TWeakObjectPtr<AMiniCharacter> OldPawn;
	FTimerHandle RespawnTimer;
	bool bPendingRespawn = false;
	bool bFailed = false;
};
