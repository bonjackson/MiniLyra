#include "MiniPlayerController.h"

#include "Character/MiniCharacter.h"

void AMiniPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (AMiniCharacter* MiniPawn = Cast<AMiniCharacter>(GetPawn()))
	{
		MiniPawn->NotifyInitDependenciesChanged();
	}
}
