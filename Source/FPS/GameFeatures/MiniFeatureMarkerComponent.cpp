#include "GameFeatures/MiniFeatureMarkerComponent.h"

#include "Character/MiniCharacter.h"
#include "Engine/World.h"
#include "GameModes/MiniGameState.h"
#include "System/MiniLogChannels.h"

namespace
{
const TCHAR* GetMarkerNetModeName(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone: return TEXT("Standalone");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	case NM_ListenServer: return TEXT("ListenServer");
	case NM_Client: return TEXT("Client");
	default: return TEXT("Unknown");
	}
}

int32 CountActiveMarkersOfExactClass(const AActor* Owner, const UClass* MarkerClass)
{
	if (!Owner)
	{
		return 0;
	}

	TArray<UMiniFeatureMarkerComponent*> Components;
	Owner->GetComponents<UMiniFeatureMarkerComponent>(Components);
	int32 Count = 0;
	for (const UMiniFeatureMarkerComponent* Component : Components)
	{
		Count += Component && Component->GetClass() == MarkerClass && Component->IsFeatureActive() ? 1 : 0;
	}
	return Count;
}
}

UMiniFeatureMarkerComponent::UMiniFeatureMarkerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UMiniFeatureMarkerComponent::BeginPlay()
{
	Super::BeginPlay();
	bFeatureActive = true;

	const UWorld* World = GetWorld();
	const int32 ActiveCount = CountActiveMarkersOfExactClass(GetOwner(), GetClass());
	UE_LOG(LogMiniExperience, Display, TEXT("MiniFeatureMarker ADDED: Type=%s NetMode=%s Owner=%s Count=%d World=%s"),
		*GetClass()->GetName(), World ? GetMarkerNetModeName(World->GetNetMode()) : TEXT("Unknown"),
		*GetPathNameSafe(GetOwner()), ActiveCount, *GetNameSafe(World));
	if (ActiveCount > 1)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFeatureMarker DUPLICATE: Type=%s Owner=%s Count=%d"),
			*GetClass()->GetName(), *GetPathNameSafe(GetOwner()), ActiveCount);
	}
	const bool bCharacterMarker = IsA<UMiniCharacterFeatureMarkerComponent>();
	const bool bExpectedOwner = GetOwner() && (bCharacterMarker
		? GetOwner()->IsA<AMiniCharacter>()
		: GetOwner()->IsA<AMiniGameState>());
	ensureMsgf(bExpectedOwner,
		TEXT("MiniFeatureMarkerComponent %s has the wrong owner %s"),
		*GetClass()->GetName(), *GetNameSafe(GetOwner()));
}

void UMiniFeatureMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bFeatureActive = false;
	const UWorld* World = GetWorld();
	const int32 ActiveCount = CountActiveMarkersOfExactClass(GetOwner(), GetClass());
	UE_LOG(LogMiniExperience, Display, TEXT("MiniFeatureMarker REMOVED: Type=%s NetMode=%s Owner=%s Count=%d World=%s Reason=%d"),
		*GetClass()->GetName(), World ? GetMarkerNetModeName(World->GetNetMode()) : TEXT("Unknown"),
		*GetPathNameSafe(GetOwner()), ActiveCount, *GetNameSafe(World), static_cast<int32>(EndPlayReason));

	Super::EndPlay(EndPlayReason);
}
