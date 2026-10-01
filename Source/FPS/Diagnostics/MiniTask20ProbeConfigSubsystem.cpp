#include "MiniTask20ProbeConfigSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniGameplayAbility_RangedFire.h"
#include "AbilitySystem/MiniGameplayAbility_Reload.h"
#include "AbilitySystem/MiniProbeAbility.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniLoadoutDefinition.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "Practice/MiniPracticeSupply.h"
#include "System/MiniLogChannels.h"
#include "Training/MiniPracticeTarget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

bool UMiniTask20ProbeConfigSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Variant;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask20Config="), Variant);
#else
	return false;
#endif
}

TStatId UMiniTask20ProbeConfigSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask20ProbeConfigSubsystem, STATGROUP_Tickables);
}

void UMiniTask20ProbeConfigSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDone || !GetWorld() || !GetWorld()->HasBegunPlay() || GetWorld()->GetNetMode() != NM_Standalone) { return; }
	WaitSeconds += DeltaTime;
	auto Fail = [&](const TCHAR* Reason)
	{
		bDone = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask20Config FAIL: Reason=%s"), Reason);
	};
	if (WaitSeconds > 90) { Fail(TEXT("selected configuration never became ready")); return; }
	FString Variant;
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask20Config="), Variant);
	if (Variant != TEXT("RifleOnly") && Variant != TEXT("Unarmed")) { Fail(TEXT("unsupported configuration variant")); return; }
	AGameStateBase* GS = GetWorld()->GetGameState();
	UMiniExperienceManagerComponent* Manager = GS ? GS->FindComponentByClass<UMiniExperienceManagerComponent>() : nullptr;
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	AMiniPlayerController* PC = nullptr;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It) { if (It->IsLocalController()) { PC = *It; break; } }
	AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
	UMiniPrimaryGameLayout* Root = PC ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	UMiniHUDLayout* HUD = nullptr;
	if (Layer)
	{
		for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
		{
			if (UMiniHUDLayout* Candidate = Cast<UMiniHUDLayout>(Widget))
			{
				if (HUD) { Fail(TEXT("duplicate HUD")); return; }
				HUD = Candidate;
			}
		}
	}
	UMiniHUDViewModel* VM = HUD ? HUD->GetViewModel() : nullptr;
	if (!Experience || !Pawn || !ASC || ASC->GetAvatarActor() != Pawn || !Pawn->GetPawnData() ||
		!Pawn->GetHeroComponent() || !Pawn->GetHeroComponent()->IsInputActive() || Pawn->GetHeroComponent()->GetInputBindingCount() <= 0 ||
		!VM || !VM->IsRunning() || VM->GetBindingCount() <= 0 || HUD->GetExtensionWidgetCount() != 4) { return; }
	const FString ExpectedExperience = FString::Printf(TEXT("DA_Mini%sExperience"), *Variant);
	const FString ExpectedPawn = FString::Printf(TEXT("DA_Mini%sPawnData"), *Variant);
	const FString ExpectedLoadout = FString::Printf(TEXT("DA_Mini%sLoadout"), *Variant);
	const UMiniPawnData* Data = Pawn->GetPawnData();
	if (Experience->GetName() != ExpectedExperience || Manager->GetCurrentExperienceId().PrimaryAssetName != FName(*ExpectedExperience) ||
		Data->GetName() != ExpectedPawn || !Data->DefaultLoadout || Data->DefaultLoadout->GetName() != ExpectedLoadout ||
		Experience->ActionSets.Num() != 1 || !Experience->ActionSets[0] || Experience->ActionSets[0]->GetName() != TEXT("DA_MiniCombatActionSet"))
	{ Fail(TEXT("URL-selected Experience/PawnData/Loadout differs from requested variant")); return; }
	int32 Targets = 0, Supplies = 0;
	for (TActorIterator<AMiniPracticeTarget> It(GetWorld()); It; ++It) { ++Targets; }
	for (TActorIterator<AMiniPracticeSupply> It(GetWorld()); It; ++It) { ++Supplies; }
	if (Targets || Supplies) { Fail(TEXT("combat-only variant created training actors")); return; }
	UMiniEquipmentInstance* Equipment = Pawn->GetEquipmentManager()->GetCurrentEquipment();
	int32 Fire = 0, Reload = 0;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability) { continue; }
		if (Spec.Ability->IsA<UMiniPawnProbeAbility>() || Spec.Ability->IsA<UMiniFeatureProbeAbility>()) { Fail(TEXT("production ASC retained a probe ability")); return; }
		if (Spec.Ability->IsA<UMiniGameplayAbility_RangedFire>())
		{
			++Fire;
			if (!Equipment || Spec.SourceObject.Get() != Equipment) { Fail(TEXT("Fire grant did not come from current equipment")); return; }
		}
		if (Spec.Ability->IsA<UMiniGameplayAbility_Reload>())
		{
			++Reload;
			if (!Equipment || Spec.SourceObject.Get() != Equipment) { Fail(TEXT("Reload grant did not come from current equipment")); return; }
		}
	}
	const bool bRifle = Variant == TEXT("RifleOnly");
	UMiniQuickBarComponent* Bar = PC->GetQuickBar();
	const int32 Items = PC->GetInventoryManager()->GetEntries().Num();
	const FMiniHUDSnapshot& S = VM->GetSnapshot();
	if (!S.bHealthReady || S.Pawn != Pawn || S.World != GetWorld() || S.LocalPlayer != PC->GetLocalPlayer() ||
		!FMath::IsNearlyEqual(S.Health, 100) || S.bDead || S.bReloading || Items != (bRifle ? 1 : 0) ||
		Fire != (bRifle ? 1 : 0) || Reload != (bRifle ? 1 : 0) || Bar->GetActiveSlotIndex() != (bRifle ? 0 : INDEX_NONE) || Bar->GetSlotItem(1))
	{ Fail(TEXT("runtime inventory, ability grants or owner HUD differ from data configuration")); return; }
	if (bRifle)
	{
		UMiniInventoryItemInstance* Item = Bar->GetSlotItem(0);
		if (!Equipment || !Item || Equipment->GetSourceItemId() != Item->GetInstanceId() ||
			!Equipment->GetEquipmentDefinition()->IsChildOf(UMiniRifleEquipmentDefinition::StaticClass()) ||
			!S.bAmmoReady || S.ItemId != Item->GetInstanceId() || S.MagazineAmmo != 30 || S.ReserveAmmo != 90)
		{ Fail(TEXT("rifle-only data did not create one working rifle")); return; }
	}
	else if (Equipment || Bar->GetSlotItem(0) || S.bAmmoReady || S.ItemId.IsValid() || S.ActiveSlot != INDEX_NONE)
	{ Fail(TEXT("unarmed data retained equipment or ammo HUD")); return; }
	TArray<UMiniHUDDataWidget*> Widgets; HUD->GetExtensionWidgets(Widgets);
	for (UMiniHUDDataWidget* Widget : Widgets)
	{
		if (!Widget || !Widget->IsListening() || Widget->GetMessageListenerCount() <= 0 || Widget->GetDisplayText().IsEmpty() ||
			Widget->GetDisplayedSnapshot().Pawn != Pawn || Widget->GetDisplayedSnapshot().bAmmoReady != bRifle)
		{ Fail(TEXT("HUD widgets did not consume selected private owner state")); return; }
	}
	ReadySeconds += DeltaTime;
	if (ReadySeconds < 0.5f) { return; }
	bDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Config PASS: Variant=%s Experience=%s Items=%d Fire=%d Reload=%d EquipmentSource=1 ProbeAbilities=0 Targets=0 Supply=0 OwnerHUD=1 Input=1 RuntimeLoadoutMutation=0"),
		*Variant, *ExpectedExperience, Items, Fire, Reload);
}
