#include "MiniUIManagerSubsystem.h"

#include "CommonLocalPlayer.h"
#include "Player/MiniPlayerController.h"
#include "UI/MiniGameUIPolicy.h"
#include "UI/MiniPrimaryGameLayout.h"

void UMiniUIManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UMiniGameUIPolicy* Policy = Cast<UMiniGameUIPolicy>(GetCurrentUIPolicy()))
	{
		RootReadyHandle = Policy->OnRootLayoutReady.AddUObject(this, &ThisClass::HandleRootReady);
		RootUnavailableHandle = Policy->OnRootLayoutUnavailable.AddUObject(this, &ThisClass::HandleRootUnavailable);
	}
}

void UMiniUIManagerSubsystem::Deinitialize()
{
	if (UMiniGameUIPolicy* Policy = Cast<UMiniGameUIPolicy>(GetCurrentUIPolicy()))
	{
		// Notify gameplay actions before layers and their pooled widgets disappear.
		Policy->ShutdownLayouts();
		Policy->OnRootLayoutReady.Remove(RootReadyHandle);
		Policy->OnRootLayoutUnavailable.Remove(RootUnavailableHandle);
	}
	const auto BindingsToRemove = PlayerBindings;
	for (const auto& Pair : BindingsToRemove)
	{
		UntrackPlayer(Pair.Key.Get());
	}
	PlayerBindings.Empty();
	RootReadyHandle.Reset();
	RootUnavailableHandle.Reset();
	Super::Deinitialize();
}

void UMiniUIManagerSubsystem::TrackPlayer(UCommonLocalPlayer* LocalPlayer)
{
	if (!LocalPlayer || PlayerBindings.Contains(LocalPlayer))
	{
		return;
	}
	PlayerBindings.Add(LocalPlayer);
	const FDelegateHandle Handle = LocalPlayer->CallAndRegister_OnPlayerControllerSet(
		UCommonLocalPlayer::FPlayerControllerSetDelegate::FDelegate::CreateWeakLambda(this,
			[this](UCommonLocalPlayer* Player, APlayerController*) { RefreshInputGate(Player); }));
	PlayerBindings.FindChecked(LocalPlayer).ControllerHandle = Handle;
}

void UMiniUIManagerSubsystem::UntrackPlayer(UCommonLocalPlayer* LocalPlayer)
{
	FPlayerUIBinding* Binding = PlayerBindings.Find(LocalPlayer);
	if (!Binding)
	{
		return;
	}
	if (UMiniPrimaryGameLayout* Root = Binding->Root.Get())
	{
		Root->OnGameplayInputBlockChanged.Remove(Binding->InputGateHandle);
	}
	if (AMiniPlayerController* Controller = Binding->Controller.Get())
	{
		Controller->SetMiniUIInputBlocked(false);
	}
	if (LocalPlayer)
	{
		LocalPlayer->OnPlayerControllerSet.Remove(Binding->ControllerHandle);
	}
	PlayerBindings.Remove(LocalPlayer);
}

void UMiniUIManagerSubsystem::NotifyPlayerAdded(UCommonLocalPlayer* LocalPlayer)
{
	TrackPlayer(LocalPlayer);
	Super::NotifyPlayerAdded(LocalPlayer);
	if (UMiniGameUIPolicy* Policy = Cast<UMiniGameUIPolicy>(GetCurrentUIPolicy()))
	{
		if (UMiniPrimaryGameLayout* Root = Cast<UMiniPrimaryGameLayout>(Policy->GetRootLayout(LocalPlayer)))
		{
			if (Root->IsLayoutReady()) { HandleRootReady(LocalPlayer, Root); }
		}
	}
}

void UMiniUIManagerSubsystem::NotifyPlayerRemoved(UCommonLocalPlayer* LocalPlayer)
{
	Super::NotifyPlayerRemoved(LocalPlayer);
	UntrackPlayer(LocalPlayer);
}

void UMiniUIManagerSubsystem::NotifyPlayerDestroyed(UCommonLocalPlayer* LocalPlayer)
{
	Super::NotifyPlayerDestroyed(LocalPlayer);
	UntrackPlayer(LocalPlayer);
}

void UMiniUIManagerSubsystem::HandleRootReady(UCommonLocalPlayer* LocalPlayer, UMiniPrimaryGameLayout* Root)
{
	TrackPlayer(LocalPlayer);
	FPlayerUIBinding* Binding = PlayerBindings.Find(LocalPlayer);
	if (!Binding || !Root || Binding->Root.Get() == Root)
	{
		return;
	}
	if (UMiniPrimaryGameLayout* Previous = Binding->Root.Get())
	{
		Previous->OnGameplayInputBlockChanged.Remove(Binding->InputGateHandle);
	}
	Binding->Root = Root;
	Binding->InputGateHandle = Root->OnGameplayInputBlockChanged.AddWeakLambda(this,
		[this, WeakPlayer = TWeakObjectPtr<UCommonLocalPlayer>(LocalPlayer)](UMiniPrimaryGameLayout*, bool)
		{
			RefreshInputGate(WeakPlayer.Get());
		});
	RefreshInputGate(LocalPlayer);
}

void UMiniUIManagerSubsystem::HandleRootUnavailable(UCommonLocalPlayer* LocalPlayer, UMiniPrimaryGameLayout* Root)
{
	FPlayerUIBinding* Binding = PlayerBindings.Find(LocalPlayer);
	if (Binding && Binding->Root.Get() == Root)
	{
		Root->OnGameplayInputBlockChanged.Remove(Binding->InputGateHandle);
		Binding->InputGateHandle.Reset();
		Binding->Root.Reset();
		RefreshInputGate(LocalPlayer);
	}
}

void UMiniUIManagerSubsystem::RefreshInputGate(UCommonLocalPlayer* LocalPlayer)
{
	FPlayerUIBinding* Binding = PlayerBindings.Find(LocalPlayer);
	if (!Binding || !LocalPlayer)
	{
		return;
	}
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(LocalPlayer->GetPlayerController(GetWorld()));
	if (Binding->Controller.Get() != Controller)
	{
		if (AMiniPlayerController* Previous = Binding->Controller.Get()) { Previous->SetMiniUIInputBlocked(false); }
		Binding->Controller = Controller;
	}
	if (Controller)
	{
		UMiniPrimaryGameLayout* Root = Binding->Root.Get();
		Controller->SetMiniUIInputBlocked(Root && Root->IsGameplayInputBlockedByUI());
	}
}
