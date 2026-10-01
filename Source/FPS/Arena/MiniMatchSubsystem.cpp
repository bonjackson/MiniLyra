#include "MiniMatchSubsystem.h"

#include "Arena/MiniMatchRulesComponent.h"
#include "Engine/World.h"

bool UMiniMatchSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UMiniMatchRulesComponent* UMiniMatchSubsystem::GetMatchRulesComponent() const
{
	return Source.Get();
}

bool UMiniMatchSubsystem::RegisterMatchSource(UMiniMatchRulesComponent* Component)
{
	if (!Component || Component->GetWorld() != GetWorld() || Component->GetOwner() != GetWorld()->GetGameState() ||
		!Component->IsFFAConfigured() || !Component->IsMatchContextAvailable() || (Source.IsValid() && Source.Get() != Component)) { return false; }
	Source = Component;
	CurrentState = Component->GetMatchState();
	OnMatchStateChanged.Broadcast(CurrentState);
	return true;
}

void UMiniMatchSubsystem::PublishMatchState(UMiniMatchRulesComponent* Component, const FMiniMatchState& State)
{
	if (!Component || Source.Get() != Component || !Component->IsMatchContextAvailable()) { return; }
	if (CurrentState.Revision != State.Revision || CurrentState.RoundId != State.RoundId)
	{
		CurrentState = State;
		OnMatchStateChanged.Broadcast(CurrentState);
	}
}

void UMiniMatchSubsystem::UnregisterMatchSource(UMiniMatchRulesComponent* Component)
{
	if (Source.Get() != Component) { return; }
	Source.Reset();
	CurrentState = FMiniMatchState();
	OnMatchStateChanged.Broadcast(CurrentState);
}

void UMiniMatchSubsystem::Deinitialize()
{
	Source.Reset();
	CurrentState = FMiniMatchState();
	OnMatchStateChanged.Clear();
	Super::Deinitialize();
}
