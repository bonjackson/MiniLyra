#if WITH_EDITOR && !UE_BUILD_SHIPPING

#include "AbilitySystemGlobals.h"
#include "GameFeaturesSubsystem.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "GameplayTagsManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "System/MiniLogChannels.h"

namespace
{
const FString CuePath(TEXT("/MiniShooterCore/GameplayCues"));

bool HasPluginPath()
{
	return UAbilitySystemGlobals::Get().GetGameplayCueNotifyPaths().Contains(CuePath);
}

bool HasProbeCue()
{
	UGameplayCueManager* Manager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	const UGameplayCueSet* CueSet = Manager ? Manager->GetRuntimeCueSet() : nullptr;
	const FGameplayTag ProbeTag = UGameplayTagsManager::Get().RequestGameplayTag(
		TEXT("GameplayCue.Mini.AssetProbe"), false);
	if (!CueSet || !ProbeTag.IsValid())
	{
		return false;
	}
	for (const FGameplayCueNotifyData& Data : CueSet->GameplayCueData)
	{
		if (Data.GameplayCueTag == ProbeTag &&
			Data.GameplayCueNotifyObj.ToString().StartsWith(CuePath + TEXT("/")))
		{
			return true;
		}
	}
	return false;
}

void Finish(bool bPassed, const TCHAR* Reason)
{
	if (bPassed)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask18CuePath RESULT: PASS Reason=%s"), Reason);
	}
	else
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask18CuePath RESULT: FAIL Reason=%s"), Reason);
	}
	FPlatformMisc::RequestExit(false);
}

void RunCuePathLifecycleProbe()
{
	FString PluginURL;
	if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(TEXT("MiniShooterCore"), PluginURL))
	{
		Finish(false, TEXT("plugin not registered"));
		return;
	}
	const bool bInitiallyClear = !HasPluginPath() && !HasProbeCue();
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask18CuePath INITIAL: Path=%d Probe=%d"),
		HasPluginPath() ? 1 : 0, HasProbeCue() ? 1 : 0);
	if (!bInitiallyClear)
	{
		Finish(false, TEXT("cue path or probe already registered before feature activation"));
		return;
	}
	UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(PluginURL,
		FGameFeaturePluginLoadComplete::CreateLambda([PluginURL](const UE::GameFeatures::FResult& Result)
		{
			const bool bActive = Result.HasValue() && HasPluginPath() && HasProbeCue();
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18CuePath ACTIVE: Result=%d Path=%d Probe=%d"),
				Result.HasValue() ? 1 : 0, HasPluginPath() ? 1 : 0, HasProbeCue() ? 1 : 0);
			if (!bActive)
			{
				Finish(false, TEXT("feature activation did not register and scan cue asset"));
				return;
			}
			UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(PluginURL,
				FGameFeaturePluginDeactivateComplete::CreateLambda(
					[](const UE::GameFeatures::FResult& DeactivateResult)
					{
						const bool bCleared = DeactivateResult.HasValue() &&
							!HasPluginPath() && !HasProbeCue();
						UE_LOG(LogMiniInit, Display,
							TEXT("MiniTask18CuePath INACTIVE: Result=%d Path=%d Probe=%d"),
							DeactivateResult.HasValue() ? 1 : 0,
							HasPluginPath() ? 1 : 0, HasProbeCue() ? 1 : 0);
						Finish(bCleared, bCleared ? TEXT("lifecycle complete") :
							TEXT("feature deactivation left cue path or asset mapped"));
					}));
		}));
}

FAutoConsoleCommand CuePathLifecycleCommand(
	TEXT("Mini.Task18CuePathLifecycle"),
	TEXT("Activate and deactivate MiniShooterCore, checking Cue path and asset discovery."),
	FConsoleCommandDelegate::CreateStatic(&RunCuePathLifecycleProbe));
}

#endif
