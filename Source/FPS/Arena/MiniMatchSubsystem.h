#pragma once

#include "Arena/MiniMatchTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniMatchSubsystem.generated.h"

class UMiniMatchRulesComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMiniMatchStateChanged, const FMiniMatchState&);

/** Local discovery/notification for the single replicated FFA component in this world. */
UCLASS()
class FPS_API UMiniMatchSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	FMiniMatchState GetCurrentMatchState() const { return CurrentState; }
	bool HasMatchContext() const { return Source.IsValid(); }
	UMiniMatchRulesComponent* GetMatchRulesComponent() const;
	FOnMiniMatchStateChanged OnMatchStateChanged;
	bool RegisterMatchSource(UMiniMatchRulesComponent* Component);
	void UnregisterMatchSource(UMiniMatchRulesComponent* Component);
	void PublishMatchState(UMiniMatchRulesComponent* Component, const FMiniMatchState& State);
private:
	TWeakObjectPtr<UMiniMatchRulesComponent> Source;
	FMiniMatchState CurrentState;
};
