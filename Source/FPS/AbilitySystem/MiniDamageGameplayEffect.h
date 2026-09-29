#pragma once

#include "GameplayEffect.h"
#include "MiniDamageGameplayEffect.generated.h"

/** Instant SetByCaller Data.Damage modifier feeding the transient damage attribute. */
UCLASS()
class FPS_API UMiniDamageGameplayEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UMiniDamageGameplayEffect();
};
