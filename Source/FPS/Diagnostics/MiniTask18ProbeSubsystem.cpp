#include "MiniTask18ProbeSubsystem.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AbilitySystem/MiniGameplayAbility_Reload.h"
#include "Camera/MiniCameraComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Engine/World.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Feedback/MiniCombatFeedbackComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "Weapons/MiniRangedWeaponComponent.h"
#include "UnrealClient.h"

namespace
{
const FVector Task18ShooterPosition(0.0, 0.0, 3000.0);
const FVector Task18TargetPosition(800.0, 0.0, 3000.0);

void SendTask18Key(AMiniPlayerController* Controller, FKey Key, EInputEvent Event)
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(
		Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}

UMiniInventoryItemInstance* Task18GetItem(const AMiniPlayerController* Controller, int32 Slot)
{
	const UMiniQuickBarComponent* Bar = Controller ? Controller->GetQuickBar() : nullptr;
	return Bar ? Bar->GetSlotItem(Slot) : nullptr;
}

bool Task18HasAmmo(const UMiniInventoryItemInstance* Item, int32 Magazine, int32 Reserve)
{
	return Item && Item->GetStat(MiniInventoryTags::AmmoInMagazine) == Magazine &&
		Item->GetStat(MiniInventoryTags::ReserveAmmo) == Reserve;
}

bool Task18HasEquipment(const AMiniCharacter* Pawn, UClass* Definition)
{
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	return Equipment && Equipment->GetEquipmentDefinition() == Definition;
}

UMiniAbilitySystemComponent* Task18GetASC(const AMiniCharacter* Pawn)
{
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetMiniAbilitySystemComponent() : nullptr;
}

const UMiniHealthSet* Task18GetHealth(const AMiniCharacter* Pawn)
{
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return State ? State->GetHealthSet() : nullptr;
}

UMiniCombatFeedbackComponent* Task18GetFeedback(const AMiniCharacter* Pawn)
{
	return Pawn ? Pawn->GetCombatFeedbackComponent() : nullptr;
}

bool Task18CueRegistrationReady()
{
	const FString CuePath(TEXT("/MiniShooterCore/GameplayCues"));
	UAbilitySystemGlobals& Globals = UAbilitySystemGlobals::Get();
	if (!Globals.GetGameplayCueNotifyPaths().Contains(CuePath))
	{
		return false;
	}
	UGameplayCueManager* Manager = Globals.GetGameplayCueManager();
	const UGameplayCueSet* CueSet = Manager ? Manager->GetRuntimeCueSet() : nullptr;
	const FGameplayTag ProbeTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("GameplayCue.Mini.AssetProbe")), false);
	if (!CueSet || !ProbeTag.IsValid())
	{
		return false;
	}
	for (const FGameplayCueNotifyData& Cue : CueSet->GameplayCueData)
	{
		if (Cue.GameplayCueTag == ProbeTag &&
			Cue.GameplayCueNotifyObj.ToString().StartsWith(CuePath + TEXT("/")))
		{
			return true;
		}
	}
	return false;
}
}

bool UMiniTask18ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask18"));
#else
	return false;
#endif
}

TStatId UMiniTask18ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask18ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask18ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		bFailed = true;
		const EMiniTask18Phase Phase = GetWorld() && GetWorld()->GetNetMode() == NM_Client
			? ClientPhase : ServerPhase;
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask18Probe FAIL: Phase=%d Reason=%s"),
			static_cast<int32>(Phase), Reason);
	}
}

void UMiniTask18ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || !GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer && ServerPhase != EMiniTask18Phase::Complete)
	{
		TickServer(DeltaTime);
	}
	else if (GetWorld()->GetNetMode() == NM_Client)
	{
		TickClient(DeltaTime);
	}
}

bool UMiniTask18ProbeSubsystem::StartServer()
{
	TArray<AMiniPlayerController*> Remote;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (!It->IsLocalController())
		{
			Remote.Add(*It);
		}
	}
	if (Remote.Num() != 2)
	{
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Controllers[Index] = Remote[Index];
		Pawns[Index] = Cast<AMiniCharacter>(Remote[Index]->GetPawn());
		if (!Pawns[Index].IsValid() || !Task18GetASC(Pawns[Index].Get()) ||
			Task18GetASC(Pawns[Index].Get())->GetAvatarActor() != Pawns[Index].Get() ||
			!Task18GetFeedback(Pawns[Index].Get()) ||
			!Task18HasEquipment(Pawns[Index].Get(), UMiniRifleEquipmentDefinition::StaticClass()) ||
			!Task18HasAmmo(Task18GetItem(Remote[Index], 0), 30, 90))
		{
			return false;
		}
	}
	if (Pawns[0].Get() == Pawns[1].Get())
	{
		Fail(TEXT("the two clients share a Pawn"));
		return false;
	}
	RifleReloadDuration = GetDefault<UMiniRifleEquipmentDefinition>()->GetReloadDuration();
	if (RifleReloadDuration < 0.2f)
	{
		Fail(TEXT("rifle reload duration is invalid"));
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AMiniCharacter* Pawn = Pawns[Index].Get();
		UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement();
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Flying);
		Pawn->SetActorLocation(Index == 0 ? Task18ShooterPosition : Task18TargetPosition,
			false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->ForceNetUpdate();
		FActorSpawnParameters Params;
		Params.Owner = Remote[Index];
		AMiniTask18ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask18ProbeActor>(
			AMiniTask18ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Probe)
		{
			Fail(TEXT("could not create owner-only checkpoint actor"));
			return false;
		}
		Probes[Index] = Probe;
		Probe->InitializeServer(Index + 1, Pawn, Pawns[1 - Index].Get());
	}
	bServerStarted = true;
	StageSeconds = 0.0f;
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask18Probe SERVER_READY: Owners=2 Rifle=30/90 Reload=%.2f"),
		RifleReloadDuration);
	return true;
}

bool UMiniTask18ProbeSubsystem::AllAcknowledged() const
{
	for (const TWeakObjectPtr<AMiniTask18ProbeActor>& Probe : Probes)
	{
		if (!Probe.IsValid() || !Probe->HasAcknowledged(ServerPhase))
		{
			return false;
		}
	}
	return true;
}

void UMiniTask18ProbeSubsystem::Advance(EMiniTask18Phase NewPhase)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask18Probe SERVER_PHASE: %d -> %d"),
		static_cast<int32>(ServerPhase), static_cast<int32>(NewPhase));
	for (const TWeakObjectPtr<AMiniTask18ProbeActor>& Probe : Probes)
	{
		if (Probe.IsValid())
		{
			Probe->SetServerPhase(NewPhase);
		}
	}
	ServerPhase = NewPhase;
	StageSeconds = 0.0f;
}

void UMiniTask18ProbeSubsystem::TickServer(float DeltaTime)
{
	StageSeconds += DeltaTime;
	if (!bServerStarted)
	{
		if (StageSeconds > 90.0f)
		{
			Fail(TEXT("two gameplay-ready clients never appeared"));
		}
		else
		{
			StartServer();
		}
		return;
	}
	if (StageSeconds > 35.0f)
	{
		Fail(TEXT("server phase timed out"));
		return;
	}
	AMiniCharacter* Shooter = Pawns[0].Get();
	AMiniCharacter* Target = Pawns[1].Get();
	UMiniRangedWeaponComponent* Weapon = Shooter ? Shooter->GetRangedWeaponComponent() : nullptr;
	UMiniAbilitySystemComponent* ShooterASC = Task18GetASC(Shooter);
	const UMiniHealthSet* TargetHealth = Task18GetHealth(Target);
	UMiniInventoryItemInstance* Rifle = Task18GetItem(Controllers[0].Get(), 0);
	if (!Shooter || !Target || !Weapon || !ShooterASC || !TargetHealth || !Rifle)
	{
		Fail(TEXT("server lost a player or combat dependency"));
		return;
	}
	switch (ServerPhase)
	{
	case EMiniTask18Phase::Initial:
		if (Task18CueRegistrationReady() && AllAcknowledged())
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe SERVER_CUE_READY: Path=1 AssetProbe=1"));
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask18Probe SERVER_INITIAL: Ready=1"));
			Advance(EMiniTask18Phase::Fire);
		}
		return;
	case EMiniTask18Phase::Fire:
		if (!AllAcknowledged())
		{
			return;
		}
		if (Weapon->GetAcceptedShotCount() != 1 || Weapon->GetLastHitCharacter() != Target ||
			!Task18HasAmmo(Rifle, 29, 90) ||
			!FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f, 0.01f))
		{
			Fail(TEXT("one accepted shot did not yield one hit and 25 damage"));
			return;
		}
		if (!Rifle->SetStat(MiniInventoryTags::AmmoInMagazine, 20))
		{
			Fail(TEXT("could not prepare rifle reload"));
			return;
		}
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask18Probe SERVER_FIRE: Shots=1 Target=75 Rifle=29/90"));
		Advance(EMiniTask18Phase::ReloadStart);
		return;
	case EMiniTask18Phase::ReloadStart:
		bSawServerReload |= ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading);
		if (AllAcknowledged() && bSawServerReload &&
			ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) &&
			Task18HasAmmo(Rifle, 20, 90))
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe SERVER_RELOAD_START: Reloading=1 Rifle=20/90"));
			Advance(EMiniTask18Phase::ReloadCancel);
		}
		return;
	case EMiniTask18Phase::ReloadCancel:
		if (!AllAcknowledged() || StageSeconds < RifleReloadDuration + 0.25f)
		{
			return;
		}
		if (ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
			!Task18HasEquipment(Shooter, UMiniPistolEquipmentDefinition::StaticClass()) ||
			!Task18HasAmmo(Rifle, 20, 90) || Weapon->GetAcceptedShotCount() != 1)
		{
			Fail(TEXT("cancelled reload left a loop, filled ammo or fired again"));
			return;
		}
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask18Probe SERVER_RELOAD_CANCEL: Reloading=0 Rifle=20/90 Pistol=1"));
		{
			UMiniInventoryItemInstance* Pistol = Task18GetItem(Controllers[0].Get(), 1);
			UMiniQuickBarComponent* Bar = Controllers[0]->GetQuickBar();
			if (!Pistol || !Bar || !Pistol->SetStat(MiniInventoryTags::AmmoInMagazine, 10) ||
				!ShooterASC->TryActivateAbilityByClass(UMiniGameplayAbility_Reload::StaticClass()) ||
				!ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
			{
				Fail(TEXT("could not start same-frame pistol reload"));
				return;
			}
			// There can be no ActiveGameplayCues FastArray replication between these
			// calls. The OnActive multicast may still arrive, without a later Removed.
			if (!Bar->SelectSlot(0) ||
				ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
				Pistol->GetStat(MiniInventoryTags::AmmoInMagazine) != 10)
			{
				Fail(TEXT("same-frame reload cancellation failed"));
				return;
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe SERVER_RAPID_RELOAD_CANCEL: SameFrame=1 Reloading=0 PistolMagazine=10 Rifle=1"));
		}
		Advance(EMiniTask18Phase::RapidReloadCancel);
		return;
	case EMiniTask18Phase::RapidReloadCancel:
		if (!AllAcknowledged() || StageSeconds < 0.5f)
		{
			return;
		}
		if (ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
			!Task18HasEquipment(Shooter, UMiniRifleEquipmentDefinition::StaticClass()) ||
			!Task18HasAmmo(Rifle, 20, 90))
		{
			Fail(TEXT("same-frame cancellation left server reload state"));
			return;
		}
		bSawServerReload = false;
		Advance(EMiniTask18Phase::DeathReloadStart);
		return;
	case EMiniTask18Phase::DeathReloadStart:
		bSawServerReload |= ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading);
		if (AllAcknowledged() && bSawServerReload &&
			ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) &&
			Task18HasAmmo(Rifle, 20, 90))
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe SERVER_RELOAD_RECOVERED: Reloading=1 Rifle=20/90"));
			Advance(EMiniTask18Phase::Death);
		}
		return;
	case EMiniTask18Phase::Death:
		if (!bDeathTriggered)
		{
			AMiniGameMode* GameMode = GetWorld()->GetAuthGameMode<AMiniGameMode>();
			if (!GameMode || !GameMode->TryApplyTestDamage(Controllers[1].Get(), Shooter, 150.0f))
			{
				Fail(TEXT("could not kill shooter for death cue check"));
				return;
			}
			bDeathTriggered = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask18Probe SERVER_DEATH_TRIGGERED: Damage=150"));
		}
		if (AllAcknowledged())
		{
			if (!Shooter->GetHealthComponent()->IsDead() ||
				Shooter->GetEquipmentManager()->GetCurrentEquipment() ||
				ShooterASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) ||
				!Task18HasAmmo(Rifle, 20, 90))
			{
				Fail(TEXT("death cue appeared without authoritative death cleanup"));
				return;
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe SERVER_PASS: OwnerPrediction=1 FireEchoSuppressed=1 ObserverFire=1 HitConfirm=1 Impact=1 Damage=1 ReloadStart=1 ReloadCancel=1 RapidCancel=1 ReloadRecovered=1 DeathReloadCancel=1 Death=1"));
			Advance(EMiniTask18Phase::Complete);
		}
		return;
	case EMiniTask18Phase::Complete:
		return;
	}
}

bool UMiniTask18ProbeSubsystem::AimClientAtPeer(AMiniPlayerController* Controller,
	AMiniCharacter* Pawn, AMiniCharacter* Peer) const
{
	UMiniCameraComponent* Camera = Pawn ? Pawn->GetMiniCameraComponent() : nullptr;
	if (!Controller || !Camera || !Peer)
	{
		return false;
	}
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		FMinimalViewInfo View;
		Camera->ResetCamera();
		Camera->GetCameraView(0.0f, View);
		Controller->SetControlRotation(
			(Peer->GetActorLocation() + FVector(0.0, 0.0, 45.0) - View.Location).Rotation());
	}
	Camera->ResetCamera();
	return true;
}

void UMiniTask18ProbeSubsystem::AcknowledgeClient(AMiniTask18ProbeActor* Probe, const TCHAR* Marker)
{
	if (!Probe || bClientAcknowledged)
	{
		return;
	}
	bClientAcknowledged = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask18Probe CLIENT_%s: Owner=%d"),
		Marker, Probe->GetOwnerIndex());
	Probe->ServerAcknowledge(Probe->GetPhase());
}

void UMiniTask18ProbeSubsystem::TickClient(float DeltaTime)
{
	AMiniTask18ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask18ProbeActor> It(GetWorld()); It; ++It)
	{
		Probe = *It;
		break;
	}
	if (!Probe || Probe->GetOwnerIndex() < 1 || Probe->GetOwnerIndex() > 2 ||
		!Probe->GetOwnerPawn() || !Probe->GetPeerPawn())
	{
		return;
	}
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(Probe->GetOwner());
	AMiniCharacter* Pawn = Probe->GetOwnerPawn();
	AMiniCharacter* Peer = Probe->GetPeerPawn();
	const EMiniTask18Phase Phase = Probe->GetPhase();
	if (!Controller || !Controller->IsLocalController() ||
		(Controller->GetPawn() != Pawn && Phase != EMiniTask18Phase::Death))
	{
		return;
	}
	const bool bShooter = Probe->GetOwnerIndex() == 1;
	const bool bMedia = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask18Media"));
	const bool bNoMediaAssets = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask18NoMediaAssets"));
	RifleReloadDuration = GetDefault<UMiniRifleEquipmentDefinition>()->GetReloadDuration();
	AMiniCharacter* Shooter = bShooter ? Pawn : Peer;
	AMiniCharacter* Target = bShooter ? Peer : Pawn;
	UMiniCombatFeedbackComponent* ShooterFeedback = Task18GetFeedback(Shooter);
	UMiniCombatFeedbackComponent* TargetFeedback = Task18GetFeedback(Target);
	if (ClientPhase != Phase)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask18Probe CLIENT_PHASE: Owner=%d %d -> %d"),
			Probe->GetOwnerIndex(), static_cast<int32>(ClientPhase), static_cast<int32>(Phase));
		ClientPhase = Phase;
		ClientPhaseSeconds = 0.0f;
		ClientInputSeconds = 0.0f;
		bClientAimSet = false;
		bClientInputPressed = false;
		bClientInputReleased = false;
		bClientAcknowledged = false;
	}
	ClientPhaseSeconds += DeltaTime;
	if (Phase == EMiniTask18Phase::Complete)
	{
		if (bMedia && bAudioRecordingStarted && !bAudioRecordingFinished && ClientPhaseSeconds >= 0.75f)
		{
			const FString AudioPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("Task18Audio")));
			UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile,
				FString::Printf(TEXT("Task18-Owner%d-Combat"), Probe->GetOwnerIndex()), AudioPath);
			bAudioRecordingFinished = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask18Probe CLIENT_AUDIO_EXPORTED: Owner=%d Path=%s"),
				Probe->GetOwnerIndex(), *AudioPath);
		}
		return;
	}
	if (ClientPhaseSeconds > 35.0f)
	{
		Fail(TEXT("client feedback checkpoint timed out"));
		return;
	}
	if (!ShooterFeedback || !TargetFeedback)
	{
		return;
	}
	if (bNoMediaAssets && (ShooterFeedback->GetSoundPlaybackCount() != 0 ||
		ShooterFeedback->GetMontagePlayCount() != 0 ||
		TargetFeedback->GetSoundPlaybackCount() != 0 || TargetFeedback->GetMontagePlayCount() != 0))
	{
		Fail(TEXT("missing-media run unexpectedly played sound or montage assets"));
		return;
	}
	if (bClientAcknowledged)
	{
		return;
	}
	const bool bInputReady = Pawn->GetHeroComponent() && Pawn->GetHeroComponent()->IsInputActive();
	UMiniInventoryItemInstance* Rifle = Task18GetItem(Controller, 0);
	const UMiniHealthSet* TargetHealth = Task18GetHealth(Target);
	switch (Phase)
	{
	case EMiniTask18Phase::Initial:
		if (Task18CueRegistrationReady() && bInputReady && Rifle &&
			Task18HasEquipment(Pawn, UMiniRifleEquipmentDefinition::StaticClass()) &&
			ShooterFeedback->GetFirePresentationCount() == 0 &&
			TargetFeedback->GetDamagePresentationCount() == 0)
		{
			if (bMedia && !bAudioRecordingStarted)
			{
				// The isolated network test runs above the map. Supply local fill
				// lights so both opposing views can inspect the same animation.
				for (const float Side : { -450.0f, 450.0f })
				{
					APointLight* Fill = GetWorld()->SpawnActor<APointLight>(
						FVector(400.0f, Side, 3300.0f), FRotator::ZeroRotator);
					if (Fill)
					{
						UPointLightComponent* Light = CastChecked<UPointLightComponent>(Fill->GetLightComponent());
						Light->SetMobility(EComponentMobility::Movable);
						Light->SetIntensity(10000.0f);
						Light->SetAttenuationRadius(2500.0f);
						Light->SetCastShadows(false);
					}
				}
				// Automated clients use hidden windows. Keep this test process's
				// game mix audible when Windows gives another app focus.
				FApp::SetUnfocusedVolumeMultiplier(1.0f);
				FApp::SetVolumeMultiplier(1.0f);
				UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 15.0f);
				bAudioRecordingStarted = true;
			}
			if (bMedia && !bShooter)
			{
				AimClientAtPeer(Controller, Pawn, Peer);
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe CLIENT_CUE_READY: Owner=%d Path=1 AssetProbe=1"),
				Probe->GetOwnerIndex());
			AcknowledgeClient(Probe, TEXT("INITIAL"));
		}
		return;
	case EMiniTask18Phase::Fire:
		if (bShooter && bInputReady && !bClientAimSet && ClientPhaseSeconds >= 0.15f)
		{
			if (!AimClientAtPeer(Controller, Pawn, Peer))
			{
				Fail(TEXT("camera unavailable for shot"));
				return;
			}
			bClientAimSet = true;
		}
		if (bShooter && bClientAimSet && !bClientInputPressed && ClientPhaseSeconds >= 0.60f)
		{
			SendTask18Key(Controller, EKeys::LeftMouseButton, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bShooter && bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendTask18Key(Controller, EKeys::LeftMouseButton, IE_Released);
				bClientInputReleased = true;
			}
		}
		if ((!bShooter || bClientInputReleased) && TargetHealth &&
			FMath::IsNearlyEqual(TargetHealth->GetHealth(), 75.0f, 0.01f) &&
			ShooterFeedback->GetFirePresentationCount() == 1 &&
			ShooterFeedback->GetConfirmedFireCueCount() == 1 &&
			ShooterFeedback->GetPredictedFireCount() == (bShooter ? 1U : 0U) &&
			ShooterFeedback->GetSuppressedFireEchoCount() == (bShooter ? 1U : 0U) &&
			ShooterFeedback->GetHitConfirmCount() == (bShooter ? 1U : 0U) &&
			TargetFeedback->GetDamagePresentationCount() == 1 &&
			ShooterFeedback->GetImpactPresentationCount() +
				TargetFeedback->GetImpactPresentationCount() == 1)
		{
			if (bMedia && !bNoMediaAssets &&
				(ShooterFeedback->GetSoundPlaybackCount() < 2 ||
				TargetFeedback->GetSoundPlaybackCount() < 1 || ShooterFeedback->GetMontagePlayCount() != 1))
			{
				Fail(TEXT("rendered fire did not start the gun/impact/damage sounds and fire montage"));
				return;
			}
			if (bMedia && !bNoMediaAssets)
			{
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniTask18Probe CLIENT_MEDIA_FIRE: Owner=%d ShooterSounds=%u TargetSounds=%u Montages=%u"),
					Probe->GetOwnerIndex(), ShooterFeedback->GetSoundPlaybackCount(),
					TargetFeedback->GetSoundPlaybackCount(), ShooterFeedback->GetMontagePlayCount());
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe CLIENT_FIRE_COUNTS: Owner=%d Predicted=%u Presented=%u Confirmed=%u Suppressed=%u Observer=%d Impact=%u Damage=%u HitConfirm=%u"),
				Probe->GetOwnerIndex(), ShooterFeedback->GetPredictedFireCount(),
				ShooterFeedback->GetFirePresentationCount(),
				ShooterFeedback->GetConfirmedFireCueCount(),
				ShooterFeedback->GetSuppressedFireEchoCount(), bShooter ? 0 : 1,
				ShooterFeedback->GetImpactPresentationCount() + TargetFeedback->GetImpactPresentationCount(),
				TargetFeedback->GetDamagePresentationCount(),
				ShooterFeedback->GetHitConfirmCount());
			AcknowledgeClient(Probe, TEXT("FIRE"));
		}
		return;
	case EMiniTask18Phase::ReloadStart:
	case EMiniTask18Phase::DeathReloadStart:
		if (bShooter && bInputReady && Task18HasAmmo(Rifle, 20, 90) && !bClientInputPressed)
		{
			SendTask18Key(Controller, EKeys::R, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bShooter && bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendTask18Key(Controller, EKeys::R, IE_Released);
				bClientInputReleased = true;
			}
		}
		if ((!bShooter || bClientInputReleased) &&
			ShooterFeedback->GetReloadStartCount() == (Phase == EMiniTask18Phase::ReloadStart ? 1U : 2U) &&
			ShooterFeedback->GetReloadStopCount() == (Phase == EMiniTask18Phase::ReloadStart ? 0U : 1U))
		{
			if (bMedia && Phase == EMiniTask18Phase::ReloadStart && ClientPhaseSeconds < 0.5f)
			{
				return;
			}
			if (bMedia && !bNoMediaAssets &&
				(ShooterFeedback->GetSoundPlaybackCount() < ShooterFeedback->GetReloadStartCount() + 2 ||
				ShooterFeedback->GetMontagePlayCount() != ShooterFeedback->GetReloadStartCount() + 1))
			{
				Fail(TEXT("rendered reload did not start its sound and montage"));
				return;
			}
			if (bMedia && Phase == EMiniTask18Phase::ReloadStart)
			{
				const FString Screenshot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
					FPaths::ProjectSavedDir(), TEXT("Screenshots"),
					FString::Printf(TEXT("Task18-Owner%d-Reload.png"), Probe->GetOwnerIndex())));
				FScreenshotRequest::RequestScreenshot(Screenshot, false, false);
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniTask18Probe CLIENT_MEDIA_RELOAD: Owner=%d Sounds=%u Montages=%u Screenshot=%s"),
					Probe->GetOwnerIndex(), ShooterFeedback->GetSoundPlaybackCount(),
					ShooterFeedback->GetMontagePlayCount(), *Screenshot);
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe CLIENT_%s_COUNTS: Owner=%d Start=%u Stop=%u"),
				Phase == EMiniTask18Phase::ReloadStart ? TEXT("RELOAD_START") : TEXT("RELOAD_RECOVERED"),
				Probe->GetOwnerIndex(), ShooterFeedback->GetReloadStartCount(), ShooterFeedback->GetReloadStopCount());
			AcknowledgeClient(Probe,
				Phase == EMiniTask18Phase::ReloadStart ? TEXT("RELOAD_START") : TEXT("RELOAD_RECOVERED"));
		}
		return;
	case EMiniTask18Phase::ReloadCancel:
		if (bShooter && bInputReady && !bClientInputPressed)
		{
			SendTask18Key(Controller, EKeys::Q, IE_Pressed);
			bClientInputPressed = true;
		}
		if (bShooter && bClientInputPressed && !bClientInputReleased)
		{
			ClientInputSeconds += DeltaTime;
			if (ClientInputSeconds >= 0.05f)
			{
				SendTask18Key(Controller, EKeys::Q, IE_Released);
				bClientInputReleased = true;
			}
		}
		if ((!bShooter || bClientInputReleased) &&
			ClientPhaseSeconds >= RifleReloadDuration + 0.25f &&
			Task18HasEquipment(Shooter, UMiniPistolEquipmentDefinition::StaticClass()) &&
			ShooterFeedback->GetReloadStartCount() == 1 &&
			ShooterFeedback->GetReloadStopCount() == 1 &&
			(!bShooter || Task18HasAmmo(Rifle, 20, 90)))
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe CLIENT_RELOAD_CANCEL_COUNTS: Owner=%d Start=1 Stop=1 RifleUnchanged=%d"),
				Probe->GetOwnerIndex(), bShooter ? 1 : -1);
			AcknowledgeClient(Probe, TEXT("RELOAD_CANCEL"));
		}
		return;
	case EMiniTask18Phase::RapidReloadCancel:
		if (ClientPhaseSeconds >= 0.5f &&
			Task18HasEquipment(Shooter, UMiniRifleEquipmentDefinition::StaticClass()))
		{
			if (ShooterFeedback->GetReloadStartCount() != 1 || ShooterFeedback->GetReloadStopCount() != 1)
			{
				Fail(TEXT("OnActive from same-frame cancelled reload created orphan presentation"));
				return;
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe CLIENT_RAPID_RELOAD_CANCEL_COUNTS: Owner=%d Start=1 Stop=1 Orphan=0"),
				Probe->GetOwnerIndex());
			AcknowledgeClient(Probe, TEXT("RAPID_RELOAD_CANCEL"));
		}
		return;
	case EMiniTask18Phase::Death:
		if (Shooter->GetHealthComponent()->IsDead() &&
			ShooterFeedback->GetDeathPresentationCount() == 1 &&
			ShooterFeedback->GetReloadStopCount() == 2)
		{
			if (bMedia && ClientPhaseSeconds < 0.35f)
			{
				return;
			}
			if (bMedia && !bNoMediaAssets && ShooterFeedback->GetSoundPlaybackCount() < 5)
			{
				Fail(TEXT("rendered death did not start its sound"));
				return;
			}
			if (bMedia)
			{
				const FString Screenshot = FPaths::ConvertRelativePathToFull(FPaths::Combine(
					FPaths::ProjectSavedDir(), TEXT("Screenshots"),
					FString::Printf(TEXT("Task18-Owner%d-Death.png"), Probe->GetOwnerIndex())));
				FScreenshotRequest::RequestScreenshot(Screenshot, false, false);
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniTask18Probe CLIENT_MEDIA_DEATH: Owner=%d Sounds=%u Screenshot=%s"),
					Probe->GetOwnerIndex(), ShooterFeedback->GetSoundPlaybackCount(), *Screenshot);
			}
			if (bNoMediaAssets)
			{
				UE_LOG(LogMiniInit, Display,
					TEXT("MiniTask18Probe CLIENT_NO_MEDIA_ASSETS: Owner=%d Sounds=0 Montages=0 Damage=25 Death=1"),
					Probe->GetOwnerIndex());
			}
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask18Probe CLIENT_DEATH_COUNTS: Owner=%d Death=1 ReloadStop=2"),
				Probe->GetOwnerIndex());
			AcknowledgeClient(Probe, TEXT("DEATH"));
		}
		return;
	case EMiniTask18Phase::Complete:
		return;
	}
}
