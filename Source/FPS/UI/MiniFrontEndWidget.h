#pragma once

#include "UI/MiniActivatableWidget.h"
#include "Components/EditableTextBox.h"
#include "MiniFrontEndWidget.generated.h"

class UButton;
class UTextBlock;
class UMiniTravelSubsystem;
struct FMiniTravelState;

/** Native front-end contribution. Travel and connection state belong to the subsystem. */
UCLASS()
class FPS_API UMiniFrontEndWidget : public UMiniActivatableWidget
{
	GENERATED_BODY()
public:
	UMiniFrontEndWidget(const FObjectInitializer& ObjectInitializer);
	UButton* GetButton(FName Name) const;
	UEditableTextBox* GetAddressEntry() const { return AddressEntry; }
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
private:
	void StartListening();
	void StopListening();
	void HandleTravelState(const FMiniTravelState& State);
	UFUNCTION() void HandlePracticeClicked();
	UFUNCTION() void HandleHostClicked();
	UFUNCTION() void HandleJoinClicked();
	UFUNCTION() void HandleQuitClicked();
	UFUNCTION() void HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UPROPERTY(Transient) TObjectPtr<UButton> PracticeButton;
	UPROPERTY(Transient) TObjectPtr<UButton> HostArenaButton;
	UPROPERTY(Transient) TObjectPtr<UButton> JoinButton;
	UPROPERTY(Transient) TObjectPtr<UButton> QuitButton;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> AddressEntry;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	TWeakObjectPtr<UMiniTravelSubsystem> TravelSubsystem;
	FDelegateHandle TravelStateHandle;
};
