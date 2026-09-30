#pragma once

#include "Diagnostics/MiniTask18ProbeActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniTask18ProbeSubsystem.generated.h"

class AMiniCharacter;
class AMiniPlayerController;

/** Three-process acceptance of owner prediction, replicated cues and reload cleanup. */
UCLASS()
class FPS_API UMiniTask18ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	bool StartServer();
	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	void Advance(EMiniTask18Phase NewPhase);
	bool AllAcknowledged() const;
	bool AimClientAtPeer(AMiniPlayerController* Controller, AMiniCharacter* Pawn,
		AMiniCharacter* Peer) const;
	void AcknowledgeClient(AMiniTask18ProbeActor* Probe, const TCHAR* Marker);
	void Fail(const TCHAR* Reason);

	EMiniTask18Phase ServerPhase = EMiniTask18Phase::Initial;
	float StageSeconds = 0.0f;
	bool bServerStarted = false;
	bool bFailed = false;
	bool bSawServerReload = false;
	bool bDeathTriggered = false;
	TWeakObjectPtr<AMiniPlayerController> Controllers[2];
	TWeakObjectPtr<AMiniCharacter> Pawns[2];
	TWeakObjectPtr<AMiniTask18ProbeActor> Probes[2];
	float RifleReloadDuration = 0.0f;

	EMiniTask18Phase ClientPhase = EMiniTask18Phase::Complete;
	float ClientPhaseSeconds = 0.0f;
	float ClientInputSeconds = 0.0f;
	bool bClientAimSet = false;
	bool bClientInputPressed = false;
	bool bClientInputReleased = false;
	bool bClientAcknowledged = false;
	bool bAudioRecordingStarted = false;
	bool bAudioRecordingFinished = false;
};
