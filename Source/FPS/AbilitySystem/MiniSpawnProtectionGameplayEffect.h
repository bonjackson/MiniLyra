#pragma once

#include "GameplayEffect.h"
#include "MiniSpawnProtectionGameplayEffect.generated.h"

/** A duration effect owned and removed by the HealthComponent of one life. */
UCLASS()
class FPS_API UMiniSpawnProtectionGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMiniSpawnProtectionGameplayEffect();
};
