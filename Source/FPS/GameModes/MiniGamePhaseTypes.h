#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MiniGamePhaseTypes.generated.h"

/** One durable snapshot; events are local notifications, never network history. */
USTRUCT(BlueprintType)
struct FPS_API FMiniGamePhaseState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag PhaseTag;
	UPROPERTY(BlueprintReadOnly)
	double PhaseStartTimeServer = 0.0;
	/** Zero means no deadline. */
	UPROPERTY(BlueprintReadOnly)
	double PhaseEndTimeServer = 0.0;
	UPROPERTY()
	uint32 Revision = 0;

	bool operator==(const FMiniGamePhaseState& Other) const
	{
		return PhaseTag == Other.PhaseTag && PhaseStartTimeServer == Other.PhaseStartTimeServer &&
			PhaseEndTimeServer == Other.PhaseEndTimeServer && Revision == Other.Revision;
	}
};

UENUM()
enum class EMiniGamePhaseEndReason : uint8
{
	Completed,
	Cancelled,
	Failed
};
