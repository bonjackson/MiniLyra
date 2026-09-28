#pragma once

#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "MiniAbilitySystemComponent.generated.h"

/** PlayerState-owned ASC with tag-routed, frame-processed ability input. */
UCLASS()
class FPS_API UMiniAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UMiniAbilitySystemComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void AbilityInputTagPressed(const FGameplayTag& InputTag);
	void AbilityInputTagReleased(const FGameplayTag& InputTag);
	void ProcessAbilityInput(float DeltaTime, bool bGamePaused);
	/** Releases and cancels abilities started by held input, then forgets all pending input. */
	void ClearAbilityInput();
	int32 GetHeldInputCount() const { return InputHeldSpecHandles.Num(); }

protected:
	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;

private:
	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputActivatedSpecHandles;
};
