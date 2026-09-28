#pragma once

#include "AbilitySystemComponent.h"
#include "MiniAbilitySystemComponent.generated.h"

/** PlayerState-owned ASC. Input tag routing is added in Task 10. */
UCLASS()
class FPS_API UMiniAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UMiniAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
