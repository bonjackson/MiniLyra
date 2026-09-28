#pragma once

#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "MiniAbilitySystemComponent.generated.h"

class UMiniAbilityTagRelationshipMapping;

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
	bool IsAbilityInputBlocked() const;
	void SetTagRelationshipMapping(const UMiniAbilityTagRelationshipMapping* Mapping);
	const UMiniAbilityTagRelationshipMapping* GetTagRelationshipMapping() const { return TagRelationshipMapping; }
	bool AreAbilityTagRequirementsMet(const FGameplayTagContainer& AbilityTags,
		FGameplayTagContainer* OutFailureTags = nullptr) const;
	/** Releases and cancels abilities started by held input, then forgets all pending input. */
	void ClearAbilityInput();
	int32 GetHeldInputCount() const { return InputHeldSpecHandles.Num(); }

protected:
	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;
	virtual void OnTagUpdated(const FGameplayTag& Tag, bool TagExists) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<const UMiniAbilityTagRelationshipMapping> TagRelationshipMapping;

	TArray<FGameplayAbilitySpecHandle> InputPressedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputReleasedSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputHeldSpecHandles;
	TArray<FGameplayAbilitySpecHandle> InputActivatedSpecHandles;
};
