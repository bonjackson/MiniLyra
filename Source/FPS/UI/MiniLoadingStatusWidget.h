#pragma once

#include "CommonUserWidget.h"
#include "Containers/Ticker.h"
#include "MiniLoadingStatusWidget.generated.h"

class AGameStateBase;
class UBorder;
class UTextBlock;
class UMiniPrimaryGameLayout;
class UMiniExperienceManagerComponent;

/** Base local status UI; survives failure or withdrawal of gameplay plugin actions. */
UCLASS()
class FPS_API UMiniLoadingStatusWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	void StartMonitoring(UWorld* World);
	void StopMonitoring();
	bool HasFailure() const { return bHasFailure; }
	bool HasActiveStatusTicker() const { return StatusTickerHandle.IsValid(); }
	FText GetStatusTitle() const { return StatusTitle; }
	FText GetStatusDetail() const { return StatusDetail; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	void HandleGameStateSet(AGameStateBase* GameState);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void BindCurrentGameState();
	bool RefreshStatus();
	bool TickStatus(float DeltaTime);
	void EnsureStatusTicker();
	void SetStatus(const FText& Title, const FText& Detail, bool bFailure, bool bHidden);

	UPROPERTY(Transient)
	TObjectPtr<UBorder> StatusPanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailText;

	TWeakObjectPtr<UWorld> BoundWorld;
	TWeakObjectPtr<UMiniExperienceManagerComponent> ExperienceManager;
	TWeakObjectPtr<UMiniPrimaryGameLayout> InputGateLayout;
	FDelegateHandle GameStateSetHandle;
	FDelegateHandle WorldCleanupHandle;
	FTSTicker::FDelegateHandle StatusTickerHandle;
	FText StatusTitle;
	FText StatusDetail;
	bool bHasFailure = false;
	bool bMonitoring = false;
};
