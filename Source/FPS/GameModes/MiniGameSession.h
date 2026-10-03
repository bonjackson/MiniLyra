#pragma once

#include "GameFramework/GameSession.h"
#include "MiniGameSession.generated.h"

/** Native approval is repeated at PreLogin and Login; URL/CVars cannot raise the four-player limit. */
UCLASS()
class FPS_API AMiniGameSession : public AGameSession
{
	GENERATED_BODY()
public:
	AMiniGameSession(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void InitOptions(const FString& Options) override;
	virtual FString ApproveLogin(const FString& Options) override;
	virtual bool AtCapacity(bool bSpectator) override;
	static constexpr int32 PlayerLimit = 4;
};
