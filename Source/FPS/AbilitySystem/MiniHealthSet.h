#pragma once

#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MiniHealthSet.generated.h"

#define MINI_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/** Baseline replicated attributes owned by PlayerState; damage rules come later. */
UCLASS()
class FPS_API UMiniHealthSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UMiniHealthSet();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "Mini|Health")
	FGameplayAttributeData Health;
	MINI_ATTRIBUTE_ACCESSORS(UMiniHealthSet, Health)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "Mini|Health")
	FGameplayAttributeData MaxHealth;
	MINI_ATTRIBUTE_ACCESSORS(UMiniHealthSet, MaxHealth)

private:
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
};
