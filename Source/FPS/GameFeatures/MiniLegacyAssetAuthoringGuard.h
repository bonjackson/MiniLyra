#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

// Legacy lesson bridges may repair probe assets, but cannot reattach them to
// the production Experience, PawnData, input assets or GameFeatureData.
inline bool MiniIsDiagnosticsAuthoringAsset(const UObject* Object)
{
	return Object && Object->GetPathName().StartsWith(TEXT("/Game/Mini/Diagnostics/"));
}
