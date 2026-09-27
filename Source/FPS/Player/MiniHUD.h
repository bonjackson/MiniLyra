#pragma once

#include "GameFramework/HUD.h"
#include "MiniHUD.generated.h"

/** Minimal HUD actor that participates in ModularGameplay extension events. */
UCLASS()
class FPS_API AMiniHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
