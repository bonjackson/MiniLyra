#pragma once

#include "Blueprint/UserWidget.h"
#include "MiniPracticeTargetStatusWidget.generated.h"

class UTextBlock;
struct FMiniPracticeTargetState;

/** World-space UMG text uses Slate's font fallback, including Chinese labels. */
UCLASS()
class FPS_API UMiniPracticeTargetStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetTargetState(const FText& Label, const FMiniPracticeTargetState& State,
		float ResetDelay, const FLinearColor& Color);
	FText GetDisplayText() const;
	FLinearColor GetDisplayColor() const { return DisplayColor; }
protected:
	virtual void NativeOnInitialized() override;
private:
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	FLinearColor DisplayColor = FLinearColor::White;
};
