#pragma once

#include "EnhancedPlayerInput.h"
#include "MiniPlayerInput.generated.h"

class UInputMappingContext;

/** Enhanced PlayerInput with a read-only mapping count for lifecycle diagnostics. */
UCLASS()
class FPS_API UMiniPlayerInput : public UEnhancedPlayerInput
{
	GENERATED_BODY()

public:
	int32 GetMappingRegistrationCount(const UInputMappingContext* MappingContext) const;
};
