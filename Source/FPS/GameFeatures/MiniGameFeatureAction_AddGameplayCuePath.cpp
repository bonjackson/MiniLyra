#include "MiniGameFeatureAction_AddGameplayCuePath.h"

#include "AbilitySystemGlobals.h"
#include "GameplayCueManager.h"
#include "System/MiniLogChannels.h"

namespace
{
// The cue manager is process-wide, while a GameFeature Action can activate in
// more than one PIE/world context. Only the final owner removes its path.
TMap<FString, int32> ActiveCuePathReferences;
}

void UMiniGameFeatureAction_AddGameplayCuePath::OnGameFeatureActivating(
	FGameFeatureActivatingContext& Context)
{
	Super::OnGameFeatureActivating(Context);
	if (ActiveContextPaths.Contains(Context))
	{
		return;
	}
	if (!CuePath.StartsWith(TEXT("/MiniShooterCore/")) || CuePath.EndsWith(TEXT("/")))
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCuePath INVALID: Action=%s Path=%s"),
			*GetPathName(), *CuePath);
		return;
	}
	UGameplayCueManager* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	if (!Manager)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCuePath NO_MANAGER: Action=%s"), *GetPathName());
		return;
	}
	int32& References = ActiveCuePathReferences.FindOrAdd(CuePath);
	if (References++ == 0)
	{
		// Rescan now: the plugin content has been mounted before this Action runs.
		Manager->AddGameplayCueNotifyPath(CuePath, true);
	}
	ActiveContextPaths.Add(Context, CuePath);
	UE_LOG(LogMiniInit, Display, TEXT("MiniCuePath REGISTERED: Action=%s Path=%s References=%d"),
		*GetPathName(), *CuePath, References);
}

void UMiniGameFeatureAction_AddGameplayCuePath::OnGameFeatureDeactivating(
	FGameFeatureDeactivatingContext& Context)
{
	if (FString* Path = ActiveContextPaths.Find(Context))
	{
		const FString PathCopy = *Path;
		ActiveContextPaths.Remove(Context);
		ReleasePath(PathCopy);
		UE_LOG(LogMiniInit, Display, TEXT("MiniCuePath UNREGISTERED: Action=%s Path=%s"),
			*GetPathName(), *PathCopy);
	}
	Super::OnGameFeatureDeactivating(Context);
}

void UMiniGameFeatureAction_AddGameplayCuePath::OnGameFeatureUnregistering()
{
	// Defensive cleanup if a feature is torn down without the usual deactivation.
	for (const TPair<FGameFeatureStateChangeContext, FString>& Pair : ActiveContextPaths)
	{
		ReleasePath(Pair.Value);
	}
	ActiveContextPaths.Empty();
	Super::OnGameFeatureUnregistering();
}

void UMiniGameFeatureAction_AddGameplayCuePath::ReleasePath(const FString& Path)
{
	int32* References = ActiveCuePathReferences.Find(Path);
	if (!References)
	{
		return;
	}
	if (--*References > 0)
	{
		return;
	}
	ActiveCuePathReferences.Remove(Path);
	if (UGameplayCueManager* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager())
	{
		Manager->RemoveGameplayCueNotifyPath(Path, true);
	}
}
