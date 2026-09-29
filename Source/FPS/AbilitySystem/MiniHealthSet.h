#pragma once

#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "MiniHealthSet.generated.h"

#define MINI_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/** PlayerState-owned health; transient damage is converted into replicated Health. */
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

	/** Transient execution input. Never replicated or treated as persistent health. */
	UPROPERTY(BlueprintReadOnly, Category = "Mini|Health")
	FGameplayAttributeData IncomingDamage;
	MINI_ATTRIBUTE_ACCESSORS(UMiniHealthSet, IncomingDamage)

protected:
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

private:
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;

	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
};
