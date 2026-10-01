#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask20ProbeConfigSubsystem.generated.h"

/** Read-only acceptance for actual URL-selected RifleOnly/Unarmed Experiences. */
UCLASS()
class FPS_API UMiniTask20ProbeConfigSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	float WaitSeconds = 0;
	float ReadySeconds = 0;
	bool bDone = false;
};
