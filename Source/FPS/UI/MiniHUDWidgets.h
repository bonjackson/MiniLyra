#pragma once

#include "CommonUserWidget.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "UI/MiniActivatableWidget.h"
#include "UI/MiniHUDMessages.h"
#include "TimerManager.h"
#include "MiniHUDWidgets.generated.h"

class UButton;
class UTextBlock;
class UMiniHUDViewModel;

/** Snapshot first, then local messages. StopListening also works for pooled widgets. */
UCLASS(Abstract)
class FPS_API UMiniHUDDataWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	void SetViewModel(UMiniHUDViewModel* Model);
	void StartListening();
	void StopListening();
	bool IsListening() const { return bListening; }
	int32 GetMessageListenerCount() const;
	const FMiniHUDSnapshot& GetDisplayedSnapshot() const { return DisplayedSnapshot; }
	FText GetDisplayText() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void RenderSnapshot(const FMiniHUDSnapshot& State) {}
	virtual bool WantsHitMessages() const { return false; }
	virtual bool WantsEmptyMessages() const { return false; }
	virtual void ReceiveHit(const FMiniHUDHitMessage& Message) {}
	virtual void ReceiveEmpty(const FMiniHUDEmptyMessage& Message) {}
	virtual void OnStoppedListening() {}
	UTextBlock* GetTextBlock() const { return StatusText; }

private:
	void HandleStateMessage(FGameplayTag Channel, const FMiniHUDStateMessage& Message);
	void HandleHitMessage(FGameplayTag Channel, const FMiniHUDHitMessage& Message);
	void HandleEmptyMessage(FGameplayTag Channel, const FMiniHUDEmptyMessage& Message);
	bool MatchesContext(ULocalPlayer* LocalPlayer, UWorld* World, UMiniHUDViewModel* Source) const;

	UPROPERTY(Transient)
	TObjectPtr<UMiniHUDViewModel> ViewModel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient)
	FMiniHUDSnapshot DisplayedSnapshot;
	FGameplayMessageListenerHandle StateMessageHandle;
	FGameplayMessageListenerHandle HitMessageHandle;
	FGameplayMessageListenerHandle EmptyMessageHandle;
	bool bListening = false;
};

UCLASS()
class FPS_API UMiniHUDHealthWidget : public UMiniHUDDataWidget
{
	GENERATED_BODY()
protected:
	virtual void RenderSnapshot(const FMiniHUDSnapshot& State) override;
};

UCLASS()
class FPS_API UMiniHUDAmmoWidget : public UMiniHUDDataWidget
{
	GENERATED_BODY()
protected:
	virtual void RenderSnapshot(const FMiniHUDSnapshot& State) override;
	virtual bool WantsEmptyMessages() const override { return true; }
	virtual void ReceiveEmpty(const FMiniHUDEmptyMessage& Message) override;
	virtual void OnStoppedListening() override;
private:
	FGuid PendingEmptyItemId;
	FGuid EmptyItemId;
	int32 MagazineAtEmptyNotice = 0;
	int32 ReserveAtEmptyNotice = 0;
};

UCLASS()
class FPS_API UMiniHUDCrosshairWidget : public UMiniHUDDataWidget
{
	GENERATED_BODY()
public:
	int32 GetDisplayedHitCount() const { return DisplayedHitCount; }
protected:
	virtual void RenderSnapshot(const FMiniHUDSnapshot& State) override;
	virtual bool WantsHitMessages() const override { return true; }
	virtual void ReceiveHit(const FMiniHUDHitMessage& Message) override;
	virtual void OnStoppedListening() override;
private:
	void ClearHitMarker();
	FTimerHandle HitMarkerTimer;
	TWeakObjectPtr<UWorld> HitMarkerWorld;
	bool bHitMarkerVisible = false;
	bool bKillMarker = false;
	int32 DisplayedHitCount = 0;
};

UCLASS()
class FPS_API UMiniHUDMatchWidget : public UMiniHUDDataWidget
{
	GENERATED_BODY()
protected:
	virtual void RenderSnapshot(const FMiniHUDSnapshot& State) override;
};

/** Real CommonUI menu used by the HUD and the lifecycle verification. */
UCLASS()
class FPS_API UMiniDebugMenuWidget : public UMiniActivatableWidget
{
	GENERATED_BODY()
public:
	UMiniDebugMenuWidget(const FObjectInitializer& ObjectInitializer);
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
private:
	UFUNCTION()
	void HandleCloseClicked();
	UPROPERTY(Transient)
	TObjectPtr<UButton> ContinueButton;
};
