#include "MiniHealthSet.h"

#include "Net/UnrealNetwork.h"

UMiniHealthSet::UMiniHealthSet()
{
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
}

void UMiniHealthSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UMiniHealthSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UMiniHealthSet, MaxHealth, COND_None, REPNOTIFY_Always);
}

void UMiniHealthSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMiniHealthSet, Health, OldValue);
}

void UMiniHealthSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMiniHealthSet, MaxHealth, OldValue);
}
