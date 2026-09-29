#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask14ProbeSubsystem.generated.h"

class AMiniPlayerController;
class AMiniTask14ProbeActor;
class UMiniInventoryItemInstance;
class UMiniInventoryManagerComponent;

/** Two-client acceptance probe for private FastArray inventory and item subobjects. */
UCLASS()
class FPS_API UMiniTask14ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	enum class EStage : uint8
	{
		WaitSnapshot,
		WaitStatChanged,
		WaitRemoved,
		WaitReadded,
		Done
	};

	void TickServer();
	void TickClient(float DeltaTime);
	void SetStage(EStage NewStage);
	void Fail(const TCHAR* Reason);
	bool CheckClientPrivacy(AMiniPlayerController*& OutController,
		AMiniTask14ProbeActor*& OutProbe) const;
	bool CheckEntries(const UMiniInventoryManagerComponent* Inventory, bool bExpectRifle,
		FGuid ExpectedPistolId, FGuid ExcludedRifleId,
		FGuid& OutRifleId, UMiniInventoryItemInstance*& OutRifle,
		int32 ExpectedRifleAmmo = 30) const;

	EStage Stage = EStage::WaitSnapshot;
	float StageSeconds = 0.0f;
	bool bFailed = false;
	bool bServerStarted = false;
	int32 OwnerIndex = 0;
	FGuid InitialRifleId;
	FGuid InitialPistolId;
	TWeakObjectPtr<UMiniInventoryItemInstance> InitialRifle;
};
