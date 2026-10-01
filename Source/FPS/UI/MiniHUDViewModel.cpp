#include "MiniHUDViewModel.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "CommonLocalPlayer.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Feedback/MiniCombatFeedbackComponent.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "Weapons/MiniRangedWeaponComponent.h"

namespace
{
bool HasItemStat(const UMiniInventoryItemInstance* Item, FGameplayTag Tag)
{
	for (const FMiniInventoryStat& Stat : Item->GetStats())
	{
		if (Stat.Tag == Tag)
		{
			return true;
		}
	}
	return false;
}

bool SameSnapshot(const FMiniHUDSnapshot& A, const FMiniHUDSnapshot& B)
{
	return A.LocalPlayer == B.LocalPlayer && A.World == B.World && A.Pawn == B.Pawn &&
		A.ItemId == B.ItemId && A.WeaponName.EqualTo(B.WeaponName) && A.ActiveSlot == B.ActiveSlot &&
		FMath::IsNearlyEqual(A.Health, B.Health) && FMath::IsNearlyEqual(A.MaxHealth, B.MaxHealth) &&
		A.MagazineAmmo == B.MagazineAmmo && A.ReserveAmmo == B.ReserveAmmo &&
		A.bHealthReady == B.bHealthReady && A.bAmmoReady == B.bAmmoReady &&
		A.bDead == B.bDead && A.bReloading == B.bReloading &&
		A.bHasMatchData == B.bHasMatchData && A.Score == B.Score && A.Deaths == B.Deaths &&
		A.MatchState.RoundId == B.MatchState.RoundId && A.MatchState.Revision == B.MatchState.Revision &&
		A.RemainingSeconds == B.RemainingSeconds &&
		A.bHasPhaseData == B.bHasPhaseData && A.PhaseTag == B.PhaseTag && A.PhaseRemainingSeconds == B.PhaseRemainingSeconds;
}
}

void UMiniHUDViewModel::Start(ULocalPlayer* LocalPlayer, UWorld* World)
{
	if (bRunning && BoundLocalPlayer.Get() == LocalPlayer && BoundWorld.Get() == World)
	{
		RebindSources();
		return;
	}
	Stop();
	UCommonLocalPlayer* CommonPlayer = Cast<UCommonLocalPlayer>(LocalPlayer);
	if (!CommonPlayer || !World || !World->IsGameWorld() || CommonPlayer->GetGameInstance() != World->GetGameInstance())
	{
		return;
	}
	BoundLocalPlayer = CommonPlayer;
	BoundWorld = World;
	bRunning = true;
	ControllerSetHandle = CommonPlayer->OnPlayerControllerSet.AddUObject(this, &ThisClass::HandleLocalController);
	PlayerStateSetHandle = CommonPlayer->OnPlayerStateSet.AddUObject(this, &ThisClass::HandleLocalPlayerState);
	PawnSetHandle = CommonPlayer->OnPlayerPawnSet.AddUObject(this, &ThisClass::HandleLocalPawn);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleWorldCleanup);
	BoundPhaseSubsystem = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	if (UMiniGamePhaseSubsystem* Phases = BoundPhaseSubsystem.Get())
	{
		PhaseStateHandle = Phases->OnPhaseStateChanged.AddUObject(this, &ThisClass::HandlePhaseStateChanged);
	}
	BoundMatchSubsystem = World->GetSubsystem<UMiniMatchSubsystem>();
	if (UMiniMatchSubsystem* Match = BoundMatchSubsystem.Get())
	{
		MatchStateHandle = Match->OnMatchStateChanged.AddUObject(this, &ThisClass::HandleMatchStateChanged);
	}
	// Phase listeners belong to this World, independently of Pawn rebindings.
	HandlePhaseStateChanged(BoundPhaseSubsystem.IsValid() ? BoundPhaseSubsystem->GetCurrentPhaseState() : FMiniGamePhaseState());
	// Read current objects after installing identity listeners; readiness never
	// depends on having witnessed their original one-time broadcasts.
	RebindSources();
}

void UMiniHUDViewModel::Stop()
{
	bRunning = false;
	if (UWorld* World = BoundWorld.Get()) { World->GetTimerManager().ClearTimer(PhaseCountdownTimer); }
	PhaseCountdownTimer.Invalidate();
	if (UMiniGamePhaseSubsystem* Phases = BoundPhaseSubsystem.Get()) { Phases->OnPhaseStateChanged.Remove(PhaseStateHandle); }
	PhaseStateHandle.Reset();
	BoundPhaseSubsystem.Reset();
	if (UMiniMatchSubsystem* Match = BoundMatchSubsystem.Get()) { Match->OnMatchStateChanged.Remove(MatchStateHandle); }
	MatchStateHandle.Reset();
	BoundMatchSubsystem.Reset();
	UnbindSources();
	if (UCommonLocalPlayer* LocalPlayer = BoundLocalPlayer.Get())
	{
		LocalPlayer->OnPlayerControllerSet.Remove(ControllerSetHandle);
		LocalPlayer->OnPlayerStateSet.Remove(PlayerStateSetHandle);
		LocalPlayer->OnPlayerPawnSet.Remove(PawnSetHandle);
	}
	ControllerSetHandle.Reset();
	PlayerStateSetHandle.Reset();
	PawnSetHandle.Reset();
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	WorldCleanupHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundWorld.Reset();
	// Release world/Pawn references in the reflected snapshot before travel/GC.
	Snapshot = FMiniHUDSnapshot();
}

void UMiniHUDViewModel::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

void UMiniHUDViewModel::UnbindSources()
{
	if (UMiniAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UMiniHealthSet::GetHealthAttribute()).Remove(HealthHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UMiniHealthSet::GetMaxHealthAttribute()).Remove(MaxHealthHandle);
		ASC->UnregisterGameplayTagEvent(ReloadHandle, MiniGameplayTags::State_Reloading);
		ASC->UnregisterGameplayTagEvent(DeathHandle, MiniGameplayTags::State_Dead);
	}
	if (UMiniInventoryManagerComponent* Inventory = BoundInventory.Get()) { Inventory->OnChanged.Remove(InventoryHandle); }
	if (UMiniQuickBarComponent* QuickBar = BoundQuickBar.Get()) { QuickBar->OnChanged.Remove(QuickBarHandle); }
	if (UMiniInventoryItemInstance* Item = BoundItem.Get()) { Item->OnChanged.Remove(ItemHandle); }
	if (UMiniCombatFeedbackComponent* Feedback = BoundFeedback.Get())
	{
		Feedback->OnHitConfirmed.RemoveDynamic(this, &ThisClass::HandleHitConfirmed);
	}
	if (UMiniRangedWeaponComponent* Weapon = BoundWeapon.Get())
	{
		Weapon->OnEmptyMagazine.RemoveDynamic(this, &ThisClass::HandleEmptyMagazine);
	}
	if (AMiniCharacter* Pawn = BoundPawn.Get())
	{
		if (UCommonLocalPlayer* LocalPlayer = BoundLocalPlayer.Get())
		{
			if (UGameFrameworkComponentManager* Manager = LocalPlayer->GetGameInstance()->GetSubsystem<UGameFrameworkComponentManager>())
			{
				Manager->UnregisterActorInitStateDelegate(Pawn, PawnInitHandle);
			}
		}
	}
	HealthHandle.Reset(); MaxHealthHandle.Reset(); ReloadHandle.Reset(); DeathHandle.Reset();
	InventoryHandle.Reset(); QuickBarHandle.Reset(); ItemHandle.Reset(); PawnInitHandle.Reset();
	bHitDelegateBound = false; bEmptyDelegateBound = false;
	BoundController.Reset(); BoundPawn.Reset(); BoundASC.Reset(); BoundInventory.Reset();
	BoundQuickBar.Reset(); BoundItem.Reset(); BoundFeedback.Reset(); BoundWeapon.Reset();
}

void UMiniHUDViewModel::RebindSources()
{
	if (!bRunning)
	{
		return;
	}
	UnbindSources();
	UCommonLocalPlayer* LocalPlayer = BoundLocalPlayer.Get();
	UWorld* World = BoundWorld.Get();
	AMiniPlayerController* Controller = LocalPlayer && World
		? Cast<AMiniPlayerController>(LocalPlayer->GetPlayerController(World)) : nullptr;
	if (!Controller || Controller->GetWorld() != World || !Controller->IsLocalController())
	{
		RefreshSnapshot();
		return;
	}
	BoundController = Controller;
	BoundPawn = Cast<AMiniCharacter>(Controller->GetPawn());
	AMiniPlayerState* PlayerState = Controller->GetPlayerState<AMiniPlayerState>();
	BoundASC = PlayerState ? PlayerState->GetMiniAbilitySystemComponent() : nullptr;
	BoundInventory = Controller->GetInventoryManager();
	BoundQuickBar = Controller->GetQuickBar();
	if (UMiniAbilitySystemComponent* ASC = BoundASC.Get())
	{
		HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UMiniHealthSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
		MaxHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UMiniHealthSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::HandleAttributeChanged);
		ReloadHandle = ASC->RegisterGameplayTagEvent(MiniGameplayTags::State_Reloading).AddUObject(this, &ThisClass::HandleStateTagChanged);
		DeathHandle = ASC->RegisterGameplayTagEvent(MiniGameplayTags::State_Dead).AddUObject(this, &ThisClass::HandleStateTagChanged);
	}
	if (UMiniInventoryManagerComponent* Inventory = BoundInventory.Get())
	{
		InventoryHandle = Inventory->OnChanged.AddUObject(this, &ThisClass::HandleInventoryChanged);
	}
	if (UMiniQuickBarComponent* QuickBar = BoundQuickBar.Get())
	{
		QuickBarHandle = QuickBar->OnChanged.AddUObject(this, &ThisClass::HandleInventoryChanged);
	}
	if (AMiniCharacter* Pawn = BoundPawn.Get())
	{
		BoundFeedback = Pawn->GetCombatFeedbackComponent();
		BoundWeapon = Pawn->GetRangedWeaponComponent();
		if (UMiniCombatFeedbackComponent* Feedback = BoundFeedback.Get())
		{
			Feedback->OnHitConfirmed.AddUniqueDynamic(this, &ThisClass::HandleHitConfirmed);
			bHitDelegateBound = true;
		}
		if (UMiniRangedWeaponComponent* Weapon = BoundWeapon.Get())
		{
			Weapon->OnEmptyMagazine.AddUniqueDynamic(this, &ThisClass::HandleEmptyMagazine);
			bEmptyDelegateBound = true;
		}
		if (UGameFrameworkComponentManager* Manager = LocalPlayer->GetGameInstance()->GetSubsystem<UGameFrameworkComponentManager>())
		{
			PawnInitHandle = Manager->RegisterAndCallForActorInitState(Pawn,
				UMiniPawnExtensionComponent::NAME_ActorFeatureName, MiniGameplayTags::InitState_DataInitialized,
				FActorInitStateChangedDelegate::CreateUObject(this, &ThisClass::HandlePawnInitState), false);
		}
	}
	BindActiveItem();
	RefreshSnapshot();
}

void UMiniHUDViewModel::BindActiveItem()
{
	UMiniQuickBarComponent* QuickBar = BoundQuickBar.Get();
	UMiniInventoryItemInstance* Item = QuickBar ? QuickBar->GetSlotItem(QuickBar->GetActiveSlotIndex()) : nullptr;
	if (Item == BoundItem.Get())
	{
		return;
	}
	if (UMiniInventoryItemInstance* OldItem = BoundItem.Get()) { OldItem->OnChanged.Remove(ItemHandle); }
	ItemHandle.Reset();
	BoundItem = Item;
	if (Item) { ItemHandle = Item->OnChanged.AddUObject(this, &ThisClass::HandleItemChanged); }
}

void UMiniHUDViewModel::RefreshSnapshot()
{
	if (!bRunning)
	{
		return;
	}
	FMiniHUDSnapshot Current;
	Current.LocalPlayer = BoundLocalPlayer.Get();
	Current.World = BoundWorld.Get();
	Current.Pawn = BoundPawn.Get();
	if (UMiniGamePhaseSubsystem* Phases = BoundPhaseSubsystem.Get())
	{
		const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
		Current.bHasPhaseData = Phases->HasArenaContext();
		Current.PhaseTag = Phase.PhaseTag;
		const double Remaining = Phases->GetRemainingSeconds();
		Current.PhaseRemainingSeconds = Remaining < 0.0 ? -1 : FMath::CeilToInt32(Remaining);
	}
	if (UMiniMatchSubsystem* Match = BoundMatchSubsystem.Get())
	{
		Current.bHasMatchData = Match->HasMatchContext();
		if (Current.bHasMatchData)
		{
			Current.MatchState = Match->GetCurrentMatchState();
			Current.RemainingSeconds = Current.PhaseRemainingSeconds;
			const AMiniPlayerState* LocalState = BoundController.IsValid()
				? BoundController->GetPlayerState<AMiniPlayerState>() : nullptr;
			if (LocalState)
			{
				for (const FMiniMatchPlayerRow& Row : Current.MatchState.Rows)
				{
					if (Row.PlayerId == LocalState->GetPlayerId()) { Current.Score = Row.Kills; Current.Deaths = Row.Deaths; break; }
				}
			}
		}
	}
	UMiniAbilitySystemComponent* ASC = BoundASC.Get();
	Current.bHealthReady = Current.Pawn && ASC && ASC->GetAvatarActor() == Current.Pawn;
	if (Current.bHealthReady)
	{
		Current.Health = ASC->GetNumericAttribute(UMiniHealthSet::GetHealthAttribute());
		Current.MaxHealth = ASC->GetNumericAttribute(UMiniHealthSet::GetMaxHealthAttribute());
		Current.bDead = ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) ||
			(Current.Pawn->GetHealthComponent() && Current.Pawn->GetHealthComponent()->IsDead());
		Current.bReloading = ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading);
	}
	UMiniQuickBarComponent* QuickBar = BoundQuickBar.Get();
	Current.ActiveSlot = QuickBar ? QuickBar->GetActiveSlotIndex() : INDEX_NONE;
	UMiniInventoryItemInstance* Item = BoundItem.Get();
	Current.bAmmoReady = Item && Item->GetInstanceId().IsValid() && Item->GetItemDefinition() &&
		HasItemStat(Item, MiniInventoryTags::AmmoInMagazine) && HasItemStat(Item, MiniInventoryTags::ReserveAmmo);
	if (Current.bAmmoReady)
	{
		Current.ItemId = Item->GetInstanceId();
		Current.WeaponName = GetDefault<UMiniInventoryItemDefinition>(Item->GetItemDefinition())->DisplayName;
		Current.MagazineAmmo = Item->GetStat(MiniInventoryTags::AmmoInMagazine);
		Current.ReserveAmmo = Item->GetStat(MiniInventoryTags::ReserveAmmo);
	}
	if (!SameSnapshot(Snapshot, Current))
	{
		Current.Revision = ++StateRefreshCount;
		Snapshot = Current;
		if (UGameplayMessageSubsystem::HasInstance(this))
		{
			FMiniHUDStateMessage Message;
			Message.Source = this;
			Message.Snapshot = Snapshot;
			UGameplayMessageSubsystem::Get(this).BroadcastMessage(MiniHUDTags::StateChanged, Message);
		}
	}
}

void UMiniHUDViewModel::HandleLocalController(UCommonLocalPlayer* LocalPlayer, APlayerController* Controller)
{
	if (LocalPlayer == BoundLocalPlayer.Get()) { RebindSources(); }
}
void UMiniHUDViewModel::HandleLocalPlayerState(UCommonLocalPlayer* LocalPlayer, APlayerState* PlayerState)
{
	if (LocalPlayer == BoundLocalPlayer.Get()) { RebindSources(); }
}
void UMiniHUDViewModel::HandleLocalPawn(UCommonLocalPlayer* LocalPlayer, APawn* Pawn)
{
	if (LocalPlayer == BoundLocalPlayer.Get()) { RebindSources(); }
}
void UMiniHUDViewModel::HandlePawnInitState(const FActorInitStateChangedParams& Params) { RefreshSnapshot(); }
void UMiniHUDViewModel::HandleAttributeChanged(const FOnAttributeChangeData& Data) { RefreshSnapshot(); }
void UMiniHUDViewModel::HandleStateTagChanged(FGameplayTag Tag, int32 NewCount) { RefreshSnapshot(); }
void UMiniHUDViewModel::HandleInventoryChanged() { BindActiveItem(); RefreshSnapshot(); }
void UMiniHUDViewModel::HandleItemChanged() { BindActiveItem(); RefreshSnapshot(); }
void UMiniHUDViewModel::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World == BoundWorld.Get()) { Stop(); }
}

void UMiniHUDViewModel::HandlePhaseStateChanged(const FMiniGamePhaseState& State)
{
	if (!bRunning) { return; }
	if (UWorld* World = BoundWorld.Get())
	{
		if (State.PhaseTag.IsValid() && State.PhaseEndTimeServer > 0.0)
		{
			if (!World->GetTimerManager().IsTimerActive(PhaseCountdownTimer))
			{
				World->GetTimerManager().SetTimer(PhaseCountdownTimer, this, &ThisClass::RefreshSnapshot, 0.2f, true);
			}
		}
		else { World->GetTimerManager().ClearTimer(PhaseCountdownTimer); }
	}
	RefreshSnapshot();
}

bool UMiniHUDViewModel::IsPhaseCountdownRunning() const
{
	const UWorld* World = BoundWorld.Get();
	return bRunning && World && World->GetTimerManager().IsTimerActive(PhaseCountdownTimer);
}

void UMiniHUDViewModel::HandleMatchStateChanged(const FMiniMatchState& State)
{
	RefreshSnapshot();
}

void UMiniHUDViewModel::HandleHitConfirmed(int32 ShotSequence, float AppliedDamage, bool bKilled)
{
	AMiniCharacter* Pawn = BoundPawn.Get();
	if (!bRunning || !Pawn || !Pawn->IsLocallyControlled() || Pawn->GetWorld() != BoundWorld.Get() ||
		ShotSequence <= 0 || AppliedDamage <= 0.0f || !UGameplayMessageSubsystem::HasInstance(this))
	{
		return;
	}
	FMiniHUDHitMessage Message;
	Message.Source = this;
	Message.LocalPlayer = BoundLocalPlayer.Get(); Message.World = BoundWorld.Get(); Message.Pawn = Pawn;
	Message.ShotSequence = ShotSequence; Message.AppliedDamage = AppliedDamage; Message.bKilled = bKilled;
	++HitMessageCount;
	UGameplayMessageSubsystem::Get(this).BroadcastMessage(MiniHUDTags::HitConfirmed, Message);
}

void UMiniHUDViewModel::HandleEmptyMagazine(FGuid ItemId)
{
	if (!bRunning || ItemId != Snapshot.ItemId || !UGameplayMessageSubsystem::HasInstance(this))
	{
		return;
	}
	FMiniHUDEmptyMessage Message;
	Message.Source = this;
	Message.LocalPlayer = BoundLocalPlayer.Get(); Message.World = BoundWorld.Get(); Message.ItemId = ItemId;
	++EmptyMessageCount;
	UGameplayMessageSubsystem::Get(this).BroadcastMessage(MiniHUDTags::EmptyMagazine, Message);
}

int32 UMiniHUDViewModel::GetBindingCount() const
{
	const FDelegateHandle Handles[] = { ControllerSetHandle, PlayerStateSetHandle, PawnSetHandle,
		WorldCleanupHandle, PawnInitHandle, HealthHandle, MaxHealthHandle, ReloadHandle, DeathHandle,
		InventoryHandle, QuickBarHandle, ItemHandle, PhaseStateHandle, MatchStateHandle };
	int32 Count = (bHitDelegateBound ? 1 : 0) + (bEmptyDelegateBound ? 1 : 0);
	for (const FDelegateHandle& Handle : Handles) { Count += Handle.IsValid() ? 1 : 0; }
	return Count;
}
