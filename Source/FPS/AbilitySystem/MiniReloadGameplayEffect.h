#pragma once

#include "GameplayEffect.h"
#include "MiniReloadGameplayEffect.generated.h"

/** Server-applied effect replicating the reload lock to the owning player. */
UCLASS()
class FPS_API UMiniReloadGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMiniReloadGameplayEffect();
};
