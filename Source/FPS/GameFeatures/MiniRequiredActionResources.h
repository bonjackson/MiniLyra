#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class UGameFeatureAction;
class UWorld;

enum class EMiniRequiredActionClassKind : uint8
{
	Layout,
	HUDElement,
	ReplicatedActor
};

/** An immutable, per-Experience class request. Soft references alone are not readiness. */
struct FMiniRequiredActionClassRequest
{
	TWeakObjectPtr<UGameFeatureAction> Action;
	FSoftObjectPath ClassPath;
	FString Entry;
	EMiniRequiredActionClassKind Kind = EMiniRequiredActionClassKind::Layout;
};

/** Action generations identify a live contribution in one exact World. */
DECLARE_MULTICAST_DELEGATE_FourParams(FMiniRequiredActionFailed,
	UWorld* /*World*/, UGameFeatureAction* /*Action*/, uint64 /*ActionGeneration*/, const FString& /*Reason*/);
