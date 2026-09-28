#include "MiniProbeAttributeSet.h"

#include "Net/UnrealNetwork.h"

UMiniProbeAttributeSet::UMiniProbeAttributeSet()
{
	InitProbeValue(1.0f);
}

void UMiniProbeAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UMiniProbeAttributeSet, ProbeValue, COND_None, REPNOTIFY_Always);
}

void UMiniProbeAttributeSet::OnRep_ProbeValue(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UMiniProbeAttributeSet, ProbeValue, OldValue);
}
