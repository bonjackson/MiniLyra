#include "MiniTask20ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "AbilitySystem/MiniProbeAbility.h"
#include "Camera/MiniCameraComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Feedback/MiniCombatFeedbackComponent.h"
#include "GameFeatures/MiniGameFeatureAction_AddActors.h"
#include "GameFeaturesSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "Practice/MiniPracticeSupply.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "Training/MiniPracticeTarget.h"
#include "Training/MiniPracticeTargetDefinition.h"
#include "Training/MiniPracticeTargetStatusWidget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"
#include "Weapons/MiniRangedWeaponComponent.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
UMiniInventoryItemInstance* Task20Item(AMiniPlayerController* PC, int32 Slot)
{
	return PC && PC->GetQuickBar() ? PC->GetQuickBar()->GetSlotItem(Slot) : nullptr;
}

bool Task20Ammo(AMiniPlayerController* PC, int32 Slot, int32 Magazine, int32 Reserve)
{
	UMiniInventoryItemInstance* Item = Task20Item(PC, Slot);
	return Item && Item->GetStat(MiniInventoryTags::AmmoInMagazine) == Magazine &&
		Item->GetStat(MiniInventoryTags::ReserveAmmo) == Reserve;
}

UMiniAbilitySystemComponent* Task20ASC(AMiniCharacter* Pawn)
{
	AMiniPlayerState* PS = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
}

bool Task20ProductionASC(AMiniCharacter* Pawn)
{
	UMiniAbilitySystemComponent* ASC = Task20ASC(Pawn);
	if (!ASC || ASC->GetAvatarActor() != Pawn) { return false; }
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && (Spec.Ability->IsA<UMiniPawnProbeAbility>() || Spec.Ability->IsA<UMiniFeatureProbeAbility>()))
		{ return false; }
	}
	return true;
}

UMiniHUDLayout* Task20HUD(AMiniPlayerController* PC)
{
	UMiniPrimaryGameLayout* Root = PC ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	UMiniHUDLayout* Result = nullptr;
	if (Layer)
	{
		for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
		{
			if (UMiniHUDLayout* HUD = Cast<UMiniHUDLayout>(Widget))
			{
				if (Result) { return nullptr; }
				Result = HUD;
			}
		}
	}
	return Result;
}

bool Task20HUDState(AMiniPlayerController* PC, int32 Slot, int32 Magazine, int32 Reserve, int32 Hits, bool bReloading = false)
{
	UMiniHUDLayout* HUD = Task20HUD(PC);
	UMiniHUDViewModel* VM = HUD ? HUD->GetViewModel() : nullptr;
	AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	UMiniInventoryItemInstance* Item = Task20Item(PC, Slot);
	if (!VM || !VM->IsRunning() || VM->GetBindingCount() <= 0 || !Pawn || !Item ||
		HUD->GetExtensionPointCount() != 4 || HUD->GetExtensionWidgetCount() != 4 || VM->GetHitMessageCount() != Hits)
	{ return false; }
	const FMiniHUDSnapshot& S = VM->GetSnapshot();
	if (!S.bHealthReady || !S.bAmmoReady || S.Pawn != Pawn || S.World != PC->GetWorld() ||
		S.LocalPlayer != PC->GetLocalPlayer() || S.ItemId != Item->GetInstanceId() || S.WeaponName.IsEmpty() ||
		!FMath::IsNearlyEqual(S.Health, 100.0f) || !FMath::IsNearlyEqual(S.MaxHealth, 100.0f) || S.bDead ||
		S.ActiveSlot != Slot || S.MagazineAmmo != Magazine || S.ReserveAmmo != Reserve || S.bReloading != bReloading)
	{ return false; }
	TArray<UMiniHUDDataWidget*> Widgets;
	HUD->GetExtensionWidgets(Widgets);
	int32 Crosshairs = 0;
	for (UMiniHUDDataWidget* Widget : Widgets)
	{
		if (!Widget || !Widget->IsListening() || Widget->GetMessageListenerCount() <= 0 || Widget->GetDisplayText().IsEmpty()) { return false; }
		const FMiniHUDSnapshot& W = Widget->GetDisplayedSnapshot();
		if (W.Pawn != Pawn || W.World != PC->GetWorld() || W.LocalPlayer != PC->GetLocalPlayer() ||
			W.ItemId != S.ItemId || W.ActiveSlot != Slot || W.MagazineAmmo != Magazine || W.ReserveAmmo != Reserve ||
			W.bReloading != bReloading || !FMath::IsNearlyEqual(W.Health, 100.0f)) { return false; }
		if (UMiniHUDCrosshairWidget* Crosshair = Cast<UMiniHUDCrosshairWidget>(Widget))
		{
			++Crosshairs;
			if (Crosshair->GetDisplayedHitCount() != Hits) { return false; }
		}
	}
	return Crosshairs == 1;
}

bool Task20Counts(UWorld* World)
{
	int32 Targets = 0, Supplies = 0;
	for (TActorIterator<AMiniPracticeTarget> It(World); It; ++It) { Targets += !It->IsActorBeingDestroyed(); }
	for (TActorIterator<AMiniPracticeSupply> It(World); It; ++It) { Supplies += !It->IsActorBeingDestroyed(); }
	return Targets == 3 && Supplies == 1;
}

bool Task20TargetState(AMiniPracticeTarget* Target, float Health, bool bEnabled, int32 Disabled, int32 Reset)
{
	if (!Target || !Target->GetMiniAbilitySystemComponent() || !Target->GetHealthSet()) { return false; }
	const FMiniPracticeTargetState& S = Target->GetTargetState();
	UMiniAbilitySystemComponent* ASC = Target->GetMiniAbilitySystemComponent();
	UMiniPracticeTargetStatusWidget* Widget = Target->GetStatusWidget();
	const UMiniPracticeTargetDefinition* Definition = Target->TargetDefinition
		? Target->TargetDefinition.Get() : GetDefault<UMiniPracticeTargetDefinition>();
	UStaticMeshComponent* Board = Target->FindComponentByClass<UStaticMeshComponent>();
	UMaterialInstanceDynamic* Material = Board ? Cast<UMaterialInstanceDynamic>(Board->GetMaterial(0)) : nullptr;
	return S.Revision > 0 && FMath::IsNearlyEqual(S.Health, Health, 0.01f) && FMath::IsNearlyEqual(S.MaxHealth, 100.0f) &&
		S.bEnabled == bEnabled && S.DisableCount == Disabled && S.ResetCount == Reset &&
		FMath::IsNearlyEqual(Target->GetHealthSet()->GetHealth(), Health, 0.01f) &&
		ASC->GetOwnerActor() == Target && ASC->GetAvatarActor() == Target && !Target->FindComponentByClass<UMiniHealthComponent>() &&
		Widget && Widget->GetDisplayText().ToString().Contains(FString::Printf(TEXT("%d / 100"), FMath::RoundToInt(Health))) &&
		Widget->GetDisplayColor().Equals(bEnabled ? Definition->ActiveColor : Definition->DisabledColor, 0.01f) &&
		Material && Material->K2_GetVectorParameterValue(TEXT("TargetColor")).Equals(
			bEnabled ? Definition->ActiveColor : Definition->DisabledColor, 0.01f);
}

UMiniGameFeatureAction_AddActors* Task20ActorAction(UWorld* World)
{
	AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	UMiniExperienceManagerComponent* Manager = GS ? GS->FindComponentByClass<UMiniExperienceManagerComponent>() : nullptr;
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	UMiniGameFeatureAction_AddActors* Result = nullptr;
	for (const UMiniExperienceActionSet* Set : Experience ? Experience->ActionSets : TArray<TObjectPtr<UMiniExperienceActionSet>>())
	{
		if (!Set) { continue; }
		for (UGameFeatureAction* Candidate : Set->Actions)
		{
			if (UMiniGameFeatureAction_AddActors* Action = Cast<UMiniGameFeatureAction_AddActors>(Candidate))
			{
				if (Result || Action->Actors.Num() != 4) { return nullptr; }
				Result = Action;
			}
		}
	}
	return Result;
}

bool Task20ActionOwnsFour(UWorld* World)
{
	UMiniGameFeatureAction_AddActors* Action = Task20ActorAction(World);
	int32 Worlds, Actors, Pending, Bindings; bool bReady, bFailed;
	if (!Action) { return false; }
	Action->GetWorldStats(World, Worlds, Actors, Pending, Bindings, bReady, bFailed);
	return Worlds == 1 && Actors == 4 && Pending == 0 && Bindings == 0 && bReady && !bFailed;
}

void Task20Move(AMiniCharacter* Pawn, const FVector& Location)
{
	Pawn->GetCharacterMovement()->StopMovementImmediately();
	Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	Pawn->ForceNetUpdate();
}

void Task20Key(AMiniPlayerController* PC, FKey Key, EInputEvent Event)
{
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}

void Task20Aim(AMiniPlayerController* PC, AMiniCharacter* Pawn, AMiniPracticeTarget* Target)
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FMinimalViewInfo View;
		Pawn->GetMiniCameraComponent()->ResetCamera();
		Pawn->GetMiniCameraComponent()->GetCameraView(0.0f, View);
		PC->SetControlRotation((Target->GetActorLocation() - View.Location).Rotation());
	}
	Pawn->GetMiniCameraComponent()->ResetCamera();
}

bool Task20Capture(int32 Owner, const TCHAR* View)
{
	if (Owner != 1 || !FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask20Media"))) { return true; }
	FString OutputDirectory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask20MediaOutput="), OutputDirectory))
	{ OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		OutputDirectory, FString::Printf(TEXT("Task20-Owner1-%s.png"), View)));
	static TMap<FString, uint64> EligibleFrames;
	static TSet<FString> Requested;
	if (!EligibleFrames.Contains(Path)) { EligibleFrames.Add(Path, GFrameCounter); return false; }
	// Observe the real state for several rendered frames before exporting it;
	// a same-frame teleport/material update can still be queued on the render thread.
	if (GFrameCounter - EligibleFrames.FindChecked(Path) < 4) { return false; }
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return false; }
#endif
	if (!Requested.Contains(Path))
	{
		Requested.Add(Path);
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		return false;
	}
	return IFileManager::Get().FileSize(*Path) > 1024;
}
}

bool UMiniTask20ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask20"));
#else
	return false;
#endif
}

TStatId UMiniTask20ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask20ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask20ProbeSubsystem::Deinitialize()
{
	if (BoundFeedback.IsValid())
	{
		BoundFeedback->OnHitConfirmed.RemoveDynamic(this, &ThisClass::HandleLegacyHit);
		BoundFeedback->OnDamageConfirmed.RemoveDynamic(this, &ThisClass::HandleDamage);
	}
	BoundFeedback.Reset();
	Super::Deinitialize();
}

void UMiniTask20ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask20Probe FAIL: Phase=%d Reason=%s"),
			static_cast<int32>(GetWorld() && GetWorld()->GetNetMode() == NM_Client ? ClientPhase : Phase), Reason);
	}
}

void UMiniTask20ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || !GetWorld() || !GetWorld()->HasBegunPlay()) { return; }
	if (GetWorld()->GetNetMode() == NM_ListenServer) { TickServer(DeltaTime); }
	else if (GetWorld()->GetNetMode() == NM_Client) { TickClient(DeltaTime); }
}

bool UMiniTask20ProbeSubsystem::StartServer()
{
	TArray<AMiniPlayerController*> Remote;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsLocalController()) { Host = *It; }
		else { Remote.Add(*It); }
	}
	if (Remote.Num() != 2 || !Host.IsValid() || !Task20Counts(GetWorld()) || !Task20ActionOwnsFour(GetWorld()) ||
		!Task20HUDState(Host.Get(), 0, 30, 90, 0)) { return false; }
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Controllers[Index] = Remote[Index]; Pawns[Index] = Cast<AMiniCharacter>(Remote[Index]->GetPawn());
		if (!Pawns[Index].IsValid() || !Task20ProductionASC(Pawns[Index].Get()) ||
			!Task20Ammo(Remote[Index], 0, 30, 90) || !Task20Ammo(Remote[Index], 1, 12, 36)) { return false; }
	}
	for (TActorIterator<AMiniPracticeTarget> It(GetWorld()); It; ++It)
	{
		if (!Task20TargetState(*It, 100, true, 0, 0) || !It->TargetDefinition ||
			!FMath::IsNearlyEqual(It->TargetDefinition->ResetDelay, 2.0f)) { return false; }
		if (!Target.IsValid()) { Target = *It; }
	}
	for (TActorIterator<AMiniPracticeSupply> It(GetWorld()); It; ++It) { Supply = *It; }
	if (!Target.IsValid() || !Supply.IsValid() || !FMath::IsNearlyEqual(Supply->GetSupplyArea()->GetScaledSphereRadius(), 220.0f)) { return false; }
	const FVector Front = Target->GetActorLocation() - Target->GetActorForwardVector() * 900.0f;
	Task20Move(Pawns[0].Get(), Front);
	Task20Move(Pawns[1].Get(), Front + Target->GetActorRightVector() * 1100.0f);
	for (int32 Slot = 0; Slot < 2; ++Slot) { ItemIds[Slot] = Task20Item(Controllers[0].Get(), Slot)->GetInstanceId(); }
	InitialRefillCount = Supply->GetRefillCount();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FActorSpawnParameters Params; Params.Owner = Remote[Index];
		AMiniTask20ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask20ProbeActor>(AMiniTask20ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Probe) { Fail(TEXT("could not create owner-only checkpoint")); return false; }
		Probes[Index] = Probe;
		Probe->InitializeServer(Index + 1, Pawns[Index].Get(), Pawns[1 - Index].Get(), Target.Get(), Supply.Get());
	}
	bStarted = true; StageSeconds = 0;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_READY: Clients=2 HostHUD=1 Targets=3 Supplies=1 ProductionAbilities=1 ActionSetOwned=1 Rifle=30/90 Pistol=12/36"));
	return true;
}

bool UMiniTask20ProbeSubsystem::AllAcknowledged() const
{
	return Probes[0].IsValid() && Probes[1].IsValid() && Probes[0]->HasAcknowledged(Phase) && Probes[1]->HasAcknowledged(Phase);
}

void UMiniTask20ProbeSubsystem::Advance(EMiniTask20Phase Value)
{
	for (auto& Probe : Probes) { if (Probe.IsValid()) { Probe->SetServerPhase(Value); } }
	Phase = Value; StageSeconds = 0; bStageAction = false;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_PHASE: %d"), static_cast<int32>(Value));
}

bool UMiniTask20ProbeSubsystem::VerifyRejections()
{
	AMiniCharacter* Pawn = Pawns[0].Get(); AMiniPlayerController* PC = Controllers[0].Get();
	UMiniRangedWeaponComponent* Weapon = Pawn->GetRangedWeaponComponent();
	UMiniInventoryItemInstance* Pistol = Task20Item(PC, 1);
	const uint32 Shots = Weapon->GetAcceptedShotCount(); const uint32 OldSequence = Weapon->GetLastProcessedSequence();
	const FVector Origin = Pawn->GetActorLocation() + FVector(0, 0, 50);
	const FRotator PreviousRotation = PC->GetControlRotation(); const FRotator UpRotation(80, 0, 0);
	PC->SetControlRotation(UpRotation); const FVector Direction = UpRotation.Vector();
	uint32 Sequence = OldSequence + 1000;
	auto Reject = [&](const FVector& View, const FVector& Aim, uint32 Seq, FGuid Id, EMiniFireRejectionReason Reason)
	{
		return !Weapon->TryFireOnServer(View, Aim, Seq, Id) && Weapon->GetLastFireRejectionReason() == Reason && Weapon->GetAcceptedShotCount() == Shots;
	};
	bool bOK = Reject(Origin, Direction, OldSequence, ItemIds[1], EMiniFireRejectionReason::InvalidSequence) &&
		Reject(Origin, Direction, Sequence++, FGuid::NewGuid(), EMiniFireRejectionReason::WrongEquipment) &&
		Reject(Origin + FVector(10000, 0, 0), Direction, Sequence++, ItemIds[1], EMiniFireRejectionReason::InvalidView);
	// One ordinary server shot into the sky establishes a same-frame fire-rate check.
	bOK &= Weapon->TryFireOnServer(Origin, Direction, Sequence++, ItemIds[1]) &&
		Weapon->GetAcceptedShotCount() == Shots + 1 && !Weapon->GetLastHitActor() &&
		Weapon->GetLastDamageResult().TargetKind == EMiniDamageTargetKind::None && Task20Ammo(PC, 1, 11, 35);
	bOK &= !Weapon->TryFireOnServer(Origin, Direction, Sequence++, ItemIds[1]) && Weapon->GetLastFireRejectionReason() == EMiniFireRejectionReason::FireRate;
	bOK &= Pistol->SetStat(MiniInventoryTags::AmmoInMagazine, 0);
	const uint32 EmptyBefore = Weapon->GetEmptyMagazineRejectionCount();
	bOK &= !Weapon->TryFireOnServer(Origin, Direction, Sequence++, ItemIds[1]) && Weapon->GetLastFireRejectionReason() == EMiniFireRejectionReason::EmptyMagazine &&
		Weapon->GetEmptyMagazineRejectionCount() == EmptyBefore + 1 && Weapon->GetAcceptedShotCount() == Shots + 1;
	PC->SetControlRotation(PreviousRotation);
	if (!bOK) { Fail(TEXT("sequence/GUID/view/fire-rate/empty validation changed")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_REJECTIONS: OldSequence=1 WrongGUID=1 InvalidView=1 FireRate=1 Empty=1 InvalidAccepted=0 SafetyMiss=1"));
	return true;
}

void UMiniTask20ProbeSubsystem::BeginCoreDeactivation()
{
	bCoreRequested = true; FString URL;
	if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(TEXT("MiniShooterCore"), URL)) { Fail(TEXT("Core plugin URL missing")); return; }
	UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(URL, FGameFeaturePluginDeactivateComplete::CreateWeakLambda(this,
		[this](const UE::GameFeatures::FResult& Result)
		{
			if (Result.HasError()) { Fail(TEXT("Core plugin deactivation failed")); }
			else { bCoreDeactivated = true; }
		}));
}

void UMiniTask20ProbeSubsystem::TickServer(float DeltaTime)
{
	if (Phase == EMiniTask20Phase::Complete) { return; }
	StageSeconds += DeltaTime;
	if (!bStarted)
	{
		if (StageSeconds > 100) { Fail(TEXT("production gameplay, 3 targets, supply or two clients not ready")); }
		else { StartServer(); }
		return;
	}
	if (StageSeconds > 35) { Fail(TEXT("server checkpoint timed out")); return; }
	AMiniCharacter* Shooter = Pawns[0].Get(); AMiniPlayerController* PC = Controllers[0].Get();
	UMiniRangedWeaponComponent* Weapon = Shooter ? Shooter->GetRangedWeaponComponent() : nullptr;
	if (!Shooter || !PC || !Weapon || !Target.IsValid() || !Supply.IsValid() || !Task20Counts(GetWorld()) || !Task20ActionOwnsFour(GetWorld()))
	{ Fail(TEXT("server actors or Experience ownership changed")); return; }
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		if (!Task20Item(PC, Slot) || Task20Item(PC, Slot)->GetInstanceId() != ItemIds[Slot]) { Fail(TEXT("loadout item GUID changed")); return; }
	}
	switch (Phase)
	{
	case EMiniTask20Phase::Initial:
		if (AllAcknowledged()) { Advance(EMiniTask20Phase::RifleOne); }
		return;
	case EMiniTask20Phase::RifleOne:
	case EMiniTask20Phase::RifleTwo:
	case EMiniTask20Phase::RifleThree:
	case EMiniTask20Phase::RifleFour:
	{
		const int32 Number = static_cast<int32>(Phase) - static_cast<int32>(EMiniTask20Phase::RifleOne) + 1;
		if (Number == 4 && Target->GetTargetState().DisableCount == 1 && DisabledAt < 0) { DisabledAt = GetWorld()->GetTimeSeconds(); }
		if (!AllAcknowledged()) { return; }
		const FMiniDamageResult& Result = Weapon->GetLastDamageResult();
		if (Weapon->GetAcceptedShotCount() != Number || Weapon->GetLastHitActor() != Target || Weapon->GetLastHitCharacter() ||
			Result.TargetKind != EMiniDamageTargetKind::PracticeTarget || !FMath::IsNearlyEqual(Result.AppliedDamage, 25.0f) ||
			Result.bTargetDefeated != (Number == 4) || Result.IsPlayerKill() || !Task20Ammo(PC, 0, 30 - Number, 90) ||
			!Task20TargetState(Target.Get(), 100 - Number * 25, Number < 4, Number == 4 ? 1 : 0, 0))
		{ Fail(TEXT("rifle ray hit did not apply exactly 25 damage to a practice actor")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_RIFLE_HIT: Number=%d Health=%d Damage=25 Kind=PracticeTarget PlayerKill=0"), Number, 100 - Number * 25);
		Advance(static_cast<EMiniTask20Phase>(static_cast<int32>(Phase) + 1));
		return;
	}
	case EMiniTask20Phase::DisabledShot:
		if (!AllAcknowledged()) { return; }
		if (Weapon->GetAcceptedShotCount() != 5 || Weapon->GetLastHitActor() || Weapon->GetLastHitCharacter() ||
			Weapon->GetLastDamageResult().TargetKind != EMiniDamageTargetKind::None || !Task20Ammo(PC, 0, 25, 90) ||
			!Task20TargetState(Target.Get(), 0, false, 1, 0)) { Fail(TEXT("disabled target accepted duplicate damage/confirmation")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_DISABLED_SHOT: Accepted=1 Damage=0 TargetDisableCount=1 PlayerDeath=0"));
		Advance(EMiniTask20Phase::Reset); return;
	case EMiniTask20Phase::Reset:
		if (!bStageAction && Target->GetTargetState().ResetCount == 1)
		{
			const double Elapsed = GetWorld()->GetTimeSeconds() - DisabledAt;
			if (Elapsed < 1.85 || Elapsed > 2.3 || !Task20TargetState(Target.Get(), 100, true, 1, 1)) { Fail(TEXT("target reset did not occur after two seconds")); return; }
			bStageAction = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_RESET: Health=100 Enabled=1 DisableCount=1 ResetCount=1 Delay=%.3f"), Elapsed);
		}
		if (bStageAction && AllAcknowledged()) { Advance(EMiniTask20Phase::PistolSwitch); }
		return;
	case EMiniTask20Phase::PistolSwitch:
		if (AllAcknowledged()) { Advance(EMiniTask20Phase::PistolFire); } return;
	case EMiniTask20Phase::PistolFire:
		if (!AllAcknowledged()) { return; }
		if (Weapon->GetAcceptedShotCount() != 6 || Weapon->GetLastHitActor() != Target || Weapon->GetLastHitCharacter() ||
			Weapon->GetLastDamageResult().TargetKind != EMiniDamageTargetKind::PracticeTarget ||
			!FMath::IsNearlyEqual(Weapon->GetLastDamageResult().AppliedDamage, 20.0f) || Weapon->GetLastDamageResult().IsPlayerKill() ||
			!Task20Ammo(PC, 1, 11, 36) || !Task20TargetState(Target.Get(), 80, true, 1, 1))
		{ Fail(TEXT("pistol real ray hit did not apply 20 damage")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_PISTOL_HIT: Health=80 Damage=20 Ammo=11/36 Kind=PracticeTarget"));
		Advance(EMiniTask20Phase::ReloadStart); return;
	case EMiniTask20Phase::ReloadStart:
		bSawReloading |= Task20ASC(Shooter)->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading);
		if (AllAcknowledged() && bSawReloading) { Advance(EMiniTask20Phase::ReloadComplete); } return;
	case EMiniTask20Phase::ReloadComplete:
		if (!AllAcknowledged()) { return; }
		if (!bSawReloading || Task20ASC(Shooter)->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) || !Task20Ammo(PC, 1, 12, 35))
		{ Fail(TEXT("pistol reload did not conserve ammo")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_RELOAD: SawReloading=1 Ammo=12/35 AcceptedShots=6"));
		if (VerifyRejections()) { Advance(EMiniTask20Phase::Rejections); } return;
	case EMiniTask20Phase::Rejections:
		if (!AllAcknowledged()) { return; }
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			Task20Item(PC, Slot)->SetStat(MiniInventoryTags::AmmoInMagazine, 0);
			Task20Item(PC, Slot)->SetStat(MiniInventoryTags::ReserveAmmo, 0);
		}
		if (Supply->TryRefill(PC) || Supply->GetSupplyArea()->IsOverlappingActor(Shooter)) { Fail(TEXT("outside supply call refilled ammo")); return; }
		Advance(EMiniTask20Phase::SupplyEmpty); return;
	case EMiniTask20Phase::SupplyEmpty:
		if (!AllAcknowledged()) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_SUPPLY_EMPTY: Rifle=0/0 Pistol=0/0 OutsideTryRefill=0 GUIDsRetained=1"));
		Task20Move(Shooter, Supply->GetActorLocation() + FVector(0, 0, 96));
		Advance(EMiniTask20Phase::SupplyEnter); return;
	case EMiniTask20Phase::SupplyEnter:
		if (!AllAcknowledged()) { return; }
		if (!Supply->GetSupplyArea()->IsOverlappingActor(Shooter) || Supply->GetRefillCount() != InitialRefillCount + 1 ||
			!Task20Ammo(PC, 0, 30, 90) || !Task20Ammo(PC, 1, 12, 36)) { Fail(TEXT("real supply overlap did not refill both items exactly once")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_SUPPLY_REFILL: RealOverlap=1 Rifle=30/90 Pistol=12/36 GUIDsRetained=1 RefillDelta=1"));
		Task20Move(Shooter, Supply->GetActorLocation() + FVector(800, 0, 96));
		Advance(EMiniTask20Phase::SupplyLeave); return;
	case EMiniTask20Phase::SupplyLeave:
		if (!AllAcknowledged() || Supply->GetSupplyArea()->IsOverlappingActor(Shooter)) { return; }
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			Task20Item(PC, Slot)->SetStat(MiniInventoryTags::AmmoInMagazine, 0);
			Task20Item(PC, Slot)->SetStat(MiniInventoryTags::ReserveAmmo, 0);
		}
		if (Supply->TryRefill(PC)) { Fail(TEXT("leaving supply did not revoke refill eligibility")); return; }
		Advance(EMiniTask20Phase::SupplyOutside); return;
	case EMiniTask20Phase::SupplyOutside:
		if (!AllAcknowledged() || StageSeconds < 1.25f) { return; }
		if (!Task20Ammo(PC, 0, 0, 0) || !Task20Ammo(PC, 1, 0, 0) || Supply->GetRefillCount() != InitialRefillCount + 1 || Supply->TryRefill(PC))
		{ Fail(TEXT("supply timer refilled an outside player")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_SUPPLY_OUTSIDE: Rifle=0/0 Pistol=0/0 OutsideTryRefill=0 TimerRefill=0"));
		Advance(EMiniTask20Phase::CoreDeactivate); return;
	case EMiniTask20Phase::CoreDeactivate:
		if (!bCoreRequested) { BeginCoreDeactivation(); }
		if (!bCoreDeactivated || !AllAcknowledged()) { return; }
		if (!Task20TargetState(Target.Get(), 80, true, 1, 1)) { Fail(TEXT("Core deactivation revoked Experience actors")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_CORE_OFF: RealDeactivation=1 Targets=3 Supplies=1 ActionSetOwnershipRetained=1"));
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe SERVER_PASS: Clients=2 Production=1 Targets=3 Supply=1 Rifle25=4 DisabledDuplicate=0 Reset2s=1 Pistol20=1 Reload=1 Rejections=1 RealSupplyOverlap=1 GUIDs=1 PrivateHUD=1 ObserverHit=0 CoreOwnsTargets=0"));
		Advance(EMiniTask20Phase::Complete); return;
	case EMiniTask20Phase::Complete: return;
	}
}

void UMiniTask20ProbeSubsystem::HandleLegacyHit(int32 Sequence, float Damage, bool bKilled)
{
	++LegacyHits; LegacyKills += bKilled;
}

void UMiniTask20ProbeSubsystem::HandleDamage(int32 Sequence, float Damage, EMiniDamageTargetKind Kind, bool bDefeated)
{
	++DamageConfirms; TargetDefeats += bDefeated;
	InvalidConfirms += Kind != EMiniDamageTargetKind::PracticeTarget ||
		(!FMath::IsNearlyEqual(Damage, 25.0f) && !FMath::IsNearlyEqual(Damage, 20.0f));
}

void UMiniTask20ProbeSubsystem::Acknowledge(AMiniTask20ProbeActor* Probe, const TCHAR* Marker)
{
	if (!Probe || bClientAcknowledged) { return; }
	bClientAcknowledged = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe CLIENT_%s: Owner=%d"), Marker, Probe->GetOwnerIndex());
	Probe->ServerAcknowledge(Probe->GetPhase());
}

void UMiniTask20ProbeSubsystem::TickClient(float DeltaTime)
{
	AMiniTask20ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask20ProbeActor> It(GetWorld()); It; ++It) { Probe = *It; break; }
	if (!Probe || !Probe->GetOwnerPawn() || !Probe->GetPeerPawn() || !Probe->GetTarget() || !Probe->GetSupply()) { return; }
	AMiniPlayerController* PC = Cast<AMiniPlayerController>(Probe->GetOwner()); AMiniCharacter* Pawn = Probe->GetOwnerPawn();
	if (!PC || !PC->IsLocalController() || PC->GetPawn() != Pawn || !Task20ASC(Pawn) || !Pawn->GetMiniCameraComponent()) { return; }
	const bool bShooter = Probe->GetOwnerIndex() == 1;
	const EMiniTask20Phase Value = Probe->GetPhase();
	if (ClientPhase != Value)
	{
		ClientPhase = Value; StageSeconds = 0; ClientPhaseTicks = 0; bStageAction = false; bInputReleased = false; bClientAcknowledged = false;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe CLIENT_PHASE: Owner=%d Phase=%d"), Probe->GetOwnerIndex(), static_cast<int32>(Value));
	}
	if (Value == EMiniTask20Phase::Complete) { return; }
	StageSeconds += DeltaTime;
	++ClientPhaseTicks;
	if (StageSeconds > 35) { Fail(TEXT("client target/HUD checkpoint timed out")); return; }
	if (!BoundFeedback.IsValid() && Pawn->GetCombatFeedbackComponent())
	{
		BoundFeedback = Pawn->GetCombatFeedbackComponent();
		BoundFeedback->OnHitConfirmed.AddDynamic(this, &ThisClass::HandleLegacyHit);
		BoundFeedback->OnDamageConfirmed.AddDynamic(this, &ThisClass::HandleDamage);
	}
	if (!BoundFeedback.IsValid() || !Task20Counts(GetWorld())) { return; }
	if (LegacyKills != 0 || InvalidConfirms != 0 || (!bShooter && (LegacyHits != 0 || DamageConfirms != 0 ||
		Probe->GetPeerPawn()->GetCombatFeedbackComponent()->GetHitConfirmCount() != 0)))
	{ Fail(TEXT("practice target generated a player kill or observer confirmation")); return; }
	if (Value != EMiniTask20Phase::Initial)
	{
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			if (!Task20Item(PC, Slot) || Task20Item(PC, Slot)->GetInstanceId() != ItemIds[Slot]) { Fail(TEXT("client item GUID changed")); return; }
		}
	}
	// Continue the isolation checks while awaiting the server's next phase,
	// but emit each checkpoint/screenshot only once.
	if (bClientAcknowledged) { return; }
	auto HUD = [&](int32 Slot, int32 Mag, int32 Reserve, int32 Hits, bool bReload = false)
	{
		return Task20HUDState(PC, bShooter ? Slot : 0, bShooter ? Mag : 30, bShooter ? Reserve : 90, bShooter ? Hits : 0, bShooter && bReload) &&
			LegacyHits == (bShooter ? Hits : 0) && DamageConfirms == (bShooter ? Hits : 0) &&
			TargetDefeats == (bShooter && Hits >= 4 ? 1 : 0);
	};
	auto Press = [&](FKey Key, bool bAim)
	{
		if (bShooter && !bStageAction && ClientPhaseTicks >= 3 && StageSeconds >= 0.18f && Pawn->GetHeroComponent()->IsInputActive())
		{
			if (bAim) { Task20Aim(PC, Pawn, Probe->GetTarget()); }
			// Aim rotation has already been sent on preceding client ticks below.
			Task20Key(PC, Key, IE_Pressed); bStageAction = true; InputPressedAt = StageSeconds;
		}
		if (bShooter && bStageAction && !bInputReleased && StageSeconds - InputPressedAt >= 0.035f)
		{
			Task20Key(PC, Key, IE_Released); bInputReleased = true;
		}
	};
	switch (Value)
	{
	case EMiniTask20Phase::Initial:
		if (Task20ProductionASC(Pawn) && Task20TargetState(Probe->GetTarget(), 100, true, 0, 0) && HUD(0, 30, 90, 0) && Task20Ammo(PC, 1, 12, 36))
		{
			for (int32 Slot = 0; Slot < 2; ++Slot) { ItemIds[Slot] = Task20Item(PC, Slot)->GetInstanceId(); }
			if (!Task20Capture(Probe->GetOwnerIndex(), TEXT("Initial"))) { return; }
			Acknowledge(Probe, TEXT("INITIAL"));
		}
		return;
	case EMiniTask20Phase::RifleOne:
	case EMiniTask20Phase::RifleTwo:
	case EMiniTask20Phase::RifleThree:
	case EMiniTask20Phase::RifleFour:
	{
		const int32 Number = static_cast<int32>(Value) - static_cast<int32>(EMiniTask20Phase::RifleOne) + 1;
		if (bShooter && !bStageAction) { Task20Aim(PC, Pawn, Probe->GetTarget()); }
		Press(EKeys::LeftMouseButton, true);
		if ((!bShooter || bInputReleased) && Task20TargetState(Probe->GetTarget(), 100 - Number * 25, Number < 4, Number == 4 ? 1 : 0, 0) && HUD(0, 30 - Number, 90, Number))
		{
			if (Number == 4 && !Task20Capture(Probe->GetOwnerIndex(), TEXT("Disabled"))) { return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe CLIENT_TARGET_STATE: Owner=%d Number=%d Health=%d Enabled=%d HitConfirms=%d PlayerKills=0"),
				Probe->GetOwnerIndex(), Number, 100 - Number * 25, Number < 4, bShooter ? Number : 0);
			Acknowledge(Probe, *FString::Printf(TEXT("RIFLE_%d"), Number));
		}
		return;
	}
	case EMiniTask20Phase::DisabledShot:
		if (bShooter && !bStageAction) { Task20Aim(PC, Pawn, Probe->GetTarget()); }
		Press(EKeys::LeftMouseButton, true);
		if ((!bShooter || bInputReleased) && StageSeconds >= 0.25f && Task20TargetState(Probe->GetTarget(), 0, false, 1, 0) && HUD(0, 25, 90, 4))
		{ Acknowledge(Probe, TEXT("DISABLED_NO_DUPLICATE")); } return;
	case EMiniTask20Phase::Reset:
		if (Task20TargetState(Probe->GetTarget(), 100, true, 1, 1) && HUD(0, 25, 90, 4)) { Acknowledge(Probe, TEXT("RESET")); } return;
	case EMiniTask20Phase::PistolSwitch:
		Press(EKeys::Q, false);
		if ((!bShooter || bInputReleased) && HUD(1, 12, 36, 4)) { Acknowledge(Probe, TEXT("PISTOL_SWITCH")); } return;
	case EMiniTask20Phase::PistolFire:
		if (bShooter && !bStageAction) { Task20Aim(PC, Pawn, Probe->GetTarget()); }
		Press(EKeys::LeftMouseButton, true);
		if ((!bShooter || bInputReleased) && Task20TargetState(Probe->GetTarget(), 80, true, 1, 1) && HUD(1, 11, 36, 5)) { Acknowledge(Probe, TEXT("PISTOL_20")); } return;
	case EMiniTask20Phase::ReloadStart:
		Press(EKeys::R, false);
		if ((!bShooter || bInputReleased) && HUD(1, 11, 36, 5, true)) { Acknowledge(Probe, TEXT("RELOAD_START")); } return;
	case EMiniTask20Phase::ReloadComplete:
		if (HUD(1, 12, 35, 5)) { Acknowledge(Probe, TEXT("RELOAD_COMPLETE")); } return;
	case EMiniTask20Phase::Rejections:
		if (HUD(1, 0, 35, 5) && Task20TargetState(Probe->GetTarget(), 80, true, 1, 1)) { Acknowledge(Probe, TEXT("REJECTIONS_NO_EXTRA_HIT")); } return;
	case EMiniTask20Phase::SupplyEmpty:
		if (HUD(1, 0, 0, 5) && Task20Ammo(PC, 0, bShooter ? 0 : 30, bShooter ? 0 : 90)) { Acknowledge(Probe, TEXT("SUPPLY_EMPTY")); } return;
	case EMiniTask20Phase::SupplyEnter:
		if (HUD(1, 12, 36, 5) && Task20Ammo(PC, 0, 30, 90) &&
			FVector::Dist(Probe->GetOwnerIndex() == 1 ? Pawn->GetActorLocation() : Probe->GetPeerPawn()->GetActorLocation(), Probe->GetSupply()->GetActorLocation()) < 220)
		{ if (Task20Capture(Probe->GetOwnerIndex(), TEXT("Refill"))) { Acknowledge(Probe, TEXT("SUPPLY_REFILLED_GUIDS")); } } return;
	case EMiniTask20Phase::SupplyLeave:
		if (HUD(1, 12, 36, 5) && FVector::Dist(bShooter ? Pawn->GetActorLocation() : Probe->GetPeerPawn()->GetActorLocation(), Probe->GetSupply()->GetActorLocation()) > 500)
		{ Acknowledge(Probe, TEXT("SUPPLY_LEFT")); } return;
	case EMiniTask20Phase::SupplyOutside:
		if (StageSeconds >= 1.15f && HUD(1, 0, 0, 5) && Task20Ammo(PC, 0, bShooter ? 0 : 30, bShooter ? 0 : 90)) { Acknowledge(Probe, TEXT("SUPPLY_OUTSIDE_EMPTY")); } return;
	case EMiniTask20Phase::CoreDeactivate:
		if (!bCoreRequested) { BeginCoreDeactivation(); }
		if (bCoreDeactivated && Task20TargetState(Probe->GetTarget(), 80, true, 1, 1) && StageSeconds > 0.25f)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask20Probe CLIENT_CONFIRM_COUNTS: Owner=%d LegacyHits=%d PlayerKills=%d DamageConfirms=%d TargetDefeats=%d ObserverHit=%d"),
				Probe->GetOwnerIndex(), LegacyHits, LegacyKills, DamageConfirms, TargetDefeats, bShooter ? 0 : LegacyHits);
			Acknowledge(Probe, TEXT("CORE_OFF_TARGETS_RETAINED"));
		}
		return;
	case EMiniTask20Phase::Complete: return;
	}
}
