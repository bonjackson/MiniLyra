#pragma once

#include "Arena/MiniMatchTypes.h"
#include "GameplayEffectTypes.h"
#include "GameFramework/Actor.h"
#include "MiniTask26NetworkProbeActor.generated.h"

class AMiniCharacter;
class AMiniPlayerController;
class UMiniEquipmentInstance;
class UAbilitySystemComponent;
class UMiniRangedWeaponComponent;
struct FGameplayEffectSpec;

UENUM()
enum class EMiniTask26NetPhase : uint8
{
	Idle, Input, Replay, BadView, Recover, Rate, RateRecover, Empty, Reload,
	ReloadRecover, Switch, StaleEquipment, SwitchRecover, Lethal, SuiteDone, Score, Restore, Complete
};

/** Owner-only diagnostic commands. Gameplay always uses the existing private ServerFire RPC. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask26NetworkProbeActor final : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask26NetworkProbeActor();
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index, AMiniCharacter* Target);
	void StartSuite();
	void Publish(EMiniTask26NetPhase NewPhase);
	void FinishClient();
	bool IsReady() const { return bReadyAck && bEchoAck; }
	bool IsSuiteDone() const { return bSuiteDone; }
	bool HasAck() const { return AckSerial == CommandSerial; }
	int32 GetOwnerIndex() const { return OwnerIndex; }
	AMiniCharacter* GetTarget() const { return TargetPawn; }
	const FMiniPlayerDeathInfo& GetRealDeath() const { return RealDeath; }
private:
	UFUNCTION(Server, Reliable) void ServerEcho(int32 Sample, double SentAt);
	UFUNCTION(Client, Reliable) void ClientEcho(int32 Sample, double SentAt);
	UFUNCTION(Server, Reliable) void ServerEchoDone(int32 Samples, double Median, double P95);
	UFUNCTION(Server, Reliable) void ServerReady();
	UFUNCTION(Server, Reliable) void ServerAcknowledge(int32 Serial, int32 Kills, int32 Deaths, int32 Round, bool bMutationRejected);
	UFUNCTION(Client, Reliable) void ClientFinish();
	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	void Fail(const TCHAR* Reason);
	void OnDamageGE(UAbilitySystemComponent* SourceASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle);
	bool Aim(FVector& Origin, FVector& Direction) const;
	void SendRaw(uint32 Sequence, bool bBadView = false, bool bOldEquipment = false);
	bool ValidateResult(int32 ShotDelta, int32 GEDelta, int32 AmmoDelta, float Damage, uint32 LastSequence, int32 Reason);
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> TargetPawn;
	UPROPERTY(Replicated) EMiniTask26NetPhase Phase = EMiniTask26NetPhase::Idle;
	UPROPERTY(Replicated) int32 CommandSerial = 0;
	UPROPERTY(Transient) TObjectPtr<UMiniEquipmentInstance> OldEquipment;
	TWeakObjectPtr<UAbilitySystemComponent> ObservedTargetASC;
	FDelegateHandle DamageHandle;
	FMiniPlayerDeathInfo RealDeath;
	FGuid OldItemId;
	TArray<double> RTTSamples;
	uint32 BaseShots = 0;
	uint32 BaseSequence = 0;
	int32 BaseAmmo = 0;
	int32 BaseReserve = 0;
	int32 BaseGE = 0;
	int32 DamageGECount = 0;
	float BaseHealth = 0;
	int32 AckSerial = 0;
	int32 ClientSerial = INDEX_NONE;
	int32 OutstandingEcho = INDEX_NONE;
	double EchoSentAt = 0;
	double StepAt = 0;
	double ClientStepAt = 0;
	double InputPressedAt = 0;
	bool bReadySent = false;
	bool bReadyAck = false;
	bool bEchoAck = false;
	bool bReadbackPrinted = false;
	bool bClientActed = false;
	bool bInputPressed = false;
	bool bInputReleased = false;
	bool bSawReload = false;
	bool bSuiteDone = false;
	bool bFailed = false;
};
