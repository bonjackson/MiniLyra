#pragma once

#include "UI/MiniActivatableWidget.h"
#include "MiniConnectionStatusWidget.generated.h"

class UButton;
class UTextBlock;
class UMiniTravelSubsystem;
struct FMiniTravelState;

/** Modal connection progress/error screen; the travel subsystem owns its stack contribution. */
UCLASS()
class FPS_API UMiniConnectionStatusWidget : public UMiniActivatableWidget
{
	GENERATED_BODY()
public:
	UMiniConnectionStatusWidget(const FObjectInitializer& ObjectInitializer);
	UButton* GetButton(FName Name) const;
	FText GetTitleText() const;
	FText GetDisplayText() const;
	int32 GetStateListenerCount() const { return TravelStateHandle.IsValid() ? 1 : 0; }
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
private:
	void StartListening();
	void StopListening();
	void HandleTravelState(const FMiniTravelState& State);
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleReturnClicked();
	UPROPERTY(Transient) TObjectPtr<UButton> CancelButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ReturnButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AddressText;
	TWeakObjectPtr<UMiniTravelSubsystem> TravelSubsystem;
	FDelegateHandle TravelStateHandle;
};
