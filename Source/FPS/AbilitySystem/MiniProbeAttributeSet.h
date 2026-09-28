#pragma once

#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MiniProbeAttributeSet.generated.h"

/** Attribute granted and removed with the feature AbilitySet. */
UCLASS()
class FPS_API UMiniProbeAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMiniProbeAttributeSet();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ProbeValue, Category = "Mini|Probe")
	FGameplayAttributeData ProbeValue;
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UMiniProbeAttributeSet, ProbeValue)
	GAMEPLAYATTRIBUTE_VALUE_GETTER(ProbeValue)
	GAMEPLAYATTRIBUTE_VALUE_SETTER(ProbeValue)
	GAMEPLAYATTRIBUTE_VALUE_INITTER(ProbeValue)

private:
	UFUNCTION()
	void OnRep_ProbeValue(const FGameplayAttributeData& OldValue);
};
