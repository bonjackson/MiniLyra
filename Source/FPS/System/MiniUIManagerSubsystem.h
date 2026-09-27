#pragma once

#include "GameUIManagerSubsystem.h"

#include "MiniUIManagerSubsystem.generated.h"

// CommonGameInstance always notifies this subsystem when local players join.
// A policy and actual UI layers will be supplied with the HUD in task 19.
UCLASS()
class FPS_API UMiniUIManagerSubsystem : public UGameUIManagerSubsystem
{
	GENERATED_BODY()
};
