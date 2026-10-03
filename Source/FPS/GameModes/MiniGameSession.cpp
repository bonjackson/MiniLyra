#include "MiniGameSession.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "System/MiniLogChannels.h"

AMiniGameSession::AMiniGameSession(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MaxPlayers = PlayerLimit;
	MaxSpectators = 0;
	MaxSplitscreensPerConnection = 1;
}

void AMiniGameSession::InitOptions(const FString& Options)
{
	Super::InitOptions(Options);
	MaxPlayers = PlayerLimit;
	MaxSpectators = 0;
	MaxSplitscreensPerConnection = 1;
}

bool AMiniGameSession::AtCapacity(bool bSpectator)
{
	if (!GetWorld() || GetNetMode() == NM_Standalone) { return false; }
	if (bSpectator) { return true; }
	int32 Connected = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->PlayerState)
		{
			++Connected; // A dead player or a player awaiting its Avatar still occupies a slot.
		}
	}
	return Connected >= PlayerLimit;
}

FString AMiniGameSession::ApproveLogin(const FString& Options)
{
	if (UGameplayStatics::GetIntOption(Options, TEXT("SpectatorOnly"), 0) != 0)
	{
		return TEXT("MINI_SPECTATORS_DISABLED");
	}
	if (AtCapacity(false))
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniLogin REJECTED: Code=MINI_SERVER_FULL Limit=4"));
		return TEXT("MINI_SERVER_FULL");
	}
	// Base approval uses our fixed AtCapacity override, retaining its splitscreen checks.
	return Super::ApproveLogin(Options);
}
