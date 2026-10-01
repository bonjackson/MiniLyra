#pragma once

#include "CoreMinimal.h"
#include "MiniDamageResult.generated.h"

/** Defeating a practice target must never be recorded as a player kill. */
UENUM(BlueprintType)
enum class EMiniDamageTargetKind : uint8
{
	None,
	Player,
	PracticeTarget
};

/** Authority-side result of one successful damage application. */
USTRUCT(BlueprintType)
struct FPS_API FMiniDamageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EMiniDamageTargetKind TargetKind = EMiniDamageTargetKind::None;
	UPROPERTY(BlueprintReadOnly)
	float AppliedDamage = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	bool bTargetDefeated = false;

	bool IsPlayerKill() const
	{
		return TargetKind == EMiniDamageTargetKind::Player && bTargetDefeated;
	}
};

FPS_API const TCHAR* MiniDamageTargetKindToString(EMiniDamageTargetKind Kind);
