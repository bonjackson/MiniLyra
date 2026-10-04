#pragma once

#include "GameFeatures/MiniRequiredActionResources.h"
#include "Engine/AssetManagerTypes.h"

#if !UE_BUILD_SHIPPING
class UMiniExperienceManagerComponent;
struct FStreamableHandle;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FMiniTask26ResourceRequestsPrepared,
	UMiniExperienceManagerComponent*, UWorld*, TArray<FMiniRequiredActionClassRequest>&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FMiniTask26ActionWorldPrepared, UGameFeatureAction*, UWorld*);
DECLARE_MULTICAST_DELEGATE_FourParams(FMiniTask26StreamableHandlePrepared,
	UObject*, UWorld*, TSharedPtr<FStreamableHandle>, bool& /*HoldStalled*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMiniTask26ExperienceSelectionPrepared,
	UMiniExperienceManagerComponent*, UWorld*, FPrimaryAssetId&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMiniTask26PrimaryLoadPrepared,
	UMiniExperienceManagerComponent*, UWorld*, bool& /*UseStalledRegisteredPath*/);

/** A probe must own one claimed World in its GI. Hooks only edit/capture snapshots;
 *  return/travel must be requested later after observing a real pending handle. */
struct FMiniTask26ActionProbeHooks
{
	static FMiniTask26ExperienceSelectionPrepared& SelectionPrepared()
	{
		static FMiniTask26ExperienceSelectionPrepared Signal;
		return Signal;
	}
	static FMiniTask26PrimaryLoadPrepared& PrimaryPrepared()
	{
		static FMiniTask26PrimaryLoadPrepared Signal;
		return Signal;
	}
	static FMiniTask26ResourceRequestsPrepared& ResourcesPrepared()
	{
		static FMiniTask26ResourceRequestsPrepared Signal;
		return Signal;
	}
	static FMiniTask26ActionWorldPrepared& ActionWorldPrepared()
	{
		static FMiniTask26ActionWorldPrepared Signal;
		return Signal;
	}
	static FMiniTask26StreamableHandlePrepared& HandlePrepared()
	{
		static FMiniTask26StreamableHandlePrepared Signal;
		return Signal;
	}
};
#endif
