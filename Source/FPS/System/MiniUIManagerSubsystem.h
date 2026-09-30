#pragma once

#include "GameUIManagerSubsystem.h"

#include "MiniUIManagerSubsystem.generated.h"

class UMiniPrimaryGameLayout;
class AMiniPlayerController;

/** Owns UI policy and bridges the root's source-counted input gate per local player. */
UCLASS()
class FPS_API UMiniUIManagerSubsystem : public UGameUIManagerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void NotifyPlayerAdded(UCommonLocalPlayer* LocalPlayer) override;
	virtual void NotifyPlayerRemoved(UCommonLocalPlayer* LocalPlayer) override;
	virtual void NotifyPlayerDestroyed(UCommonLocalPlayer* LocalPlayer) override;

private:
	struct FPlayerUIBinding
	{
		TWeakObjectPtr<UMiniPrimaryGameLayout> Root;
		TWeakObjectPtr<AMiniPlayerController> Controller;
		FDelegateHandle InputGateHandle;
		FDelegateHandle ControllerHandle;
	};
	TMap<TWeakObjectPtr<UCommonLocalPlayer>, FPlayerUIBinding> PlayerBindings;
	FDelegateHandle RootReadyHandle;
	FDelegateHandle RootUnavailableHandle;
	void TrackPlayer(UCommonLocalPlayer* LocalPlayer);
	void UntrackPlayer(UCommonLocalPlayer* LocalPlayer);
	void HandleRootReady(UCommonLocalPlayer* LocalPlayer, UMiniPrimaryGameLayout* Root);
	void HandleRootUnavailable(UCommonLocalPlayer* LocalPlayer, UMiniPrimaryGameLayout* Root);
	void RefreshInputGate(UCommonLocalPlayer* LocalPlayer);
};
