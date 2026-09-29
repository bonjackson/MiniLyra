#pragma once

#include "Diagnostics/MiniTask15ProbeActor.h"
#include "GameplayAbilitySpecHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniTask15ProbeSubsystem.generated.h"

class AMiniCharacter;
class AMiniPlayerController;
class UMiniAbilitySystemComponent;
class UMiniEquipmentInstance;
class UMiniInventoryItemInstance;

/** Three-process acceptance probe for private QuickBars and public equipment. */
UCLASS()
class FPS_API UMiniTask15ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	void Fail(const TCHAR* Reason);
	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	bool TryStartServer();
	bool AllAcknowledged() const;
	void AdvanceServer(EMiniTask15Phase NewPhase);
	bool CheckServerBar(int32 Owner, int32 ActiveSlot, bool bFreshItems) const;
	bool CheckServerEquipment(int32 Owner, int32 Slot) const;
	bool SwitchBoth(int32 Slot);
	bool KillOwner(int32 Owner);
	bool CheckServerRespawn(int32 Owner);
	bool CheckNoSamePawnRefill();
	bool CheckClientBar(const AMiniPlayerController* Controller, int32 ActiveSlot,
		FGuid& OutRifleId, FGuid& OutPistolId) const;
	bool CheckClientAppearance(AMiniCharacter* Pawn, int32 Slot, FGuid ExpectedId,
		UMiniInventoryItemInstance* ExpectedPrivateItem) const;
	bool CheckClientDeath(AMiniCharacter* Pawn) const;

	EMiniTask15Phase ServerPhase = EMiniTask15Phase::Initial;
	float StageSeconds = 0.0f;
	bool bFailed = false;
	bool bServerStarted = false;
	TWeakObjectPtr<AMiniPlayerController> Controllers[2];
	TWeakObjectPtr<AMiniTask15ProbeActor> Probes[2];
	TWeakObjectPtr<AMiniCharacter> Pawns[2];
	TWeakObjectPtr<UMiniAbilitySystemComponent> AbilitySystems[2];
	/** Keep the first Rifle equipment alive until both asynchronous Q RPCs are observed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMiniEquipmentInstance>> InitialEquipment;
	TArray<FGameplayAbilitySpecHandle> InitialAbilityHandles[2];
	FGuid InitialRifleIds[2];
	FGuid InitialPistolIds[2];
	float DeathTime[2] = { 0.0f, 0.0f };

	int32 ClientCompletedPhase = INDEX_NONE;
	int32 ClientOwnerIndex = 0;
	FGuid ClientRifleId;
	FGuid ClientPistolId;
	TWeakObjectPtr<AMiniCharacter> ClientOwnerPawn;
	TWeakObjectPtr<AMiniCharacter> ClientPeerPawn;
	FString ClientOwnerPawnPath;
	FString ClientPeerPawnPath;
};
