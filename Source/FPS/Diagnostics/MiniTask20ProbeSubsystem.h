#pragma once

#include "Combat/MiniDamageResult.h"
#include "Diagnostics/MiniTask20ProbeActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniTask20ProbeSubsystem.generated.h"

class AMiniPlayerController;
class UMiniCombatFeedbackComponent;

/** Opt-in, non-shipping acceptance using one listen server and two independent clients. */
UCLASS()
class FPS_API UMiniTask20ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	bool StartServer();
	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	void Advance(EMiniTask20Phase Value);
	bool AllAcknowledged() const;
	void Acknowledge(AMiniTask20ProbeActor* Probe, const TCHAR* Marker);
	void Fail(const TCHAR* Reason);
	bool VerifyRejections();
	void BeginCoreDeactivation();
	UFUNCTION() void HandleLegacyHit(int32 Sequence, float Damage, bool bKilled);
	UFUNCTION() void HandleDamage(int32 Sequence, float Damage, EMiniDamageTargetKind Kind, bool bDefeated);

	EMiniTask20Phase Phase = EMiniTask20Phase::Initial;
	EMiniTask20Phase ClientPhase = EMiniTask20Phase::Complete;
	float StageSeconds = 0;
	float InputPressedAt = 0;
	int32 ClientPhaseTicks = 0;
	double DisabledAt = -1;
	bool bStarted = false;
	bool bFailed = false;
	bool bStageAction = false;
	bool bInputReleased = false;
	bool bClientAcknowledged = false;
	bool bSawReloading = false;
	bool bCoreRequested = false;
	bool bCoreDeactivated = false;
	int32 InitialRefillCount = 0;
	int32 LegacyHits = 0;
	int32 LegacyKills = 0;
	int32 DamageConfirms = 0;
	int32 TargetDefeats = 0;
	int32 InvalidConfirms = 0;
	FGuid ItemIds[2];
	TWeakObjectPtr<AMiniPlayerController> Host;
	TWeakObjectPtr<AMiniPlayerController> Controllers[2];
	TWeakObjectPtr<AMiniCharacter> Pawns[2];
	TWeakObjectPtr<AMiniTask20ProbeActor> Probes[2];
	TWeakObjectPtr<AMiniPracticeTarget> Target;
	TWeakObjectPtr<AMiniPracticeSupply> Supply;
	TWeakObjectPtr<UMiniCombatFeedbackComponent> BoundFeedback;
};
