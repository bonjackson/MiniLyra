#pragma once

#include "GameplayEffect.h"
#include "MiniDeathGameplayEffect.generated.h"

/** Infinite active effect that owns the replicated State.Dead tag until respawn cleanup. */
UCLASS()
class FPS_API UMiniDeathGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMiniDeathGameplayEffect();
};
