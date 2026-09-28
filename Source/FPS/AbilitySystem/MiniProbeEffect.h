#pragma once

#include "GameplayEffect.h"
#include "MiniProbeEffect.generated.h"

/** Reversible infinite effect used to verify action withdrawal. */
UCLASS()
class FPS_API UMiniProbeEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMiniProbeEffect();
};
