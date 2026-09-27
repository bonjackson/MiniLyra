#pragma once

#include "ModularGameMode.h"
#include "MiniGameMode.generated.h"

// The server selects an Experience; the GameState component loads it on each peer.
UCLASS()
class FPS_API AMiniGameMode : public AModularGameModeBase
{
	GENERATED_BODY()

public:
	AMiniGameMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

private:
	void HandleMatchAssignmentIfNotExpectingOne();
};
