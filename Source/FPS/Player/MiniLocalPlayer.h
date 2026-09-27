#pragma once

#include "CommonLocalPlayer.h"
#include "MiniLocalPlayer.generated.h"

/** Local-player entry point for future input, camera and UI ownership. */
UCLASS(config = Engine, transient)
class FPS_API UMiniLocalPlayer : public UCommonLocalPlayer
{
	GENERATED_BODY()
};
