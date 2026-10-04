#include "MiniTask26NetworkProbeActor.h"

#include "Net/UnrealNetwork.h"

#if !UE_BUILD_SHIPPING
#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniDamageGameplayEffect.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Camera/MiniCameraComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Diagnostics/MiniTask26NetworkSettings.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "GameplayEffect.h"
#include "AbilitySystem/MiniGameplayAbility_RangedFire.h"
#include "AbilitySystem/MiniGameplayAbility_Reload.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "Weapons/MiniRangedWeaponComponent.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace
{
AMiniPlayerController* MiniTask26NetPC(const AActor* Actor) { return Cast<AMiniPlayerController>(Actor->GetOwner()); }
AMiniCharacter* MiniTask26NetPawn(const AActor* Actor)
{
	const auto* PC = MiniTask26NetPC(Actor); return PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
}
AMiniPlayerState* MiniTask26NetPS(const AActor* Actor)
{
	const auto* PC = MiniTask26NetPC(Actor); return PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
}
UMiniEquipmentInstance* MiniTask26NetEquipment(const AActor* Actor)
{
	const auto* Pawn = MiniTask26NetPawn(Actor); return Pawn && Pawn->GetEquipmentManager() ? Pawn->GetEquipmentManager()->GetCurrentEquipment() : nullptr;
}
UMiniInventoryItemInstance* MiniTask26NetItem(const AActor* Actor)
{
	const auto* Equipment = MiniTask26NetEquipment(Actor); return Equipment ? Equipment->GetSourceItem() : nullptr;
}
const UMiniRangedWeaponEquipmentDefinition* MiniTask26NetDefinition(const AActor* Actor)
{
	const auto* Equipment = MiniTask26NetEquipment(Actor);
	return Equipment ? Cast<UMiniRangedWeaponEquipmentDefinition>(Equipment->GetEquipmentDefinition().GetDefaultObject()) : nullptr;
}
UMiniHUDViewModel* MiniTask26NetHUD(AMiniPlayerController* PC)
{
	auto* Root = PC && PC->GetLocalPlayer() ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	auto* Layer = Root ? Root->GetLayerWidget(UMiniPrimaryGameLayout::GetGameLayerTag()) : nullptr;
	if (Layer) { for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
	{
		if (auto* HUD = Cast<UMiniHUDLayout>(Widget); HUD && HUD->IsActivated() && HUD->GetExtensionWidgetCount() == 4) { return HUD->GetViewModel(); }
	} }
	return nullptr;
}
void MiniTask26NetKey(AMiniPlayerController* PC, FKey Key, EInputEvent Event)
{
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}
bool MiniTask26NetOwnerSourceReady(const AActor* Actor)
{
	const auto* PS = MiniTask26NetPS(Actor); const auto* Pawn = MiniTask26NetPawn(Actor);
	const auto* ASC = PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
	const auto* Equipment = MiniTask26NetEquipment(Actor);
	if (!ASC || ASC->GetOwnerActor() != PS || ASC->GetAvatarActor() != Pawn || !Equipment) { return false; }
	int32 Fire = 0, Reload = 0;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.SourceObject.Get() == Equipment && Spec.Ability)
		{
			Fire += Spec.Ability->IsA<UMiniGameplayAbility_RangedFire>() && Spec.GetDynamicSpecSourceTags().HasTagExact(MiniGameplayTags::InputTag_Fire);
			Reload += Spec.Ability->IsA<UMiniGameplayAbility_Reload>() && Spec.GetDynamicSpecSourceTags().HasTagExact(MiniGameplayTags::InputTag_Reload);
		}
	}
	return Fire == 1 && Reload == 1;
}
bool MiniTask26NetOptIn() { return FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask26Network")); }
}
#endif

AMiniTask26NetworkProbeActor::AMiniTask26NetworkProbeActor()
{
#if !UE_BUILD_SHIPPING
	bReplicates = true; bOnlyRelevantToOwner = true; PrimaryActorTick.bCanEverTick = true;
#endif
}
void AMiniTask26NetworkProbeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, OwnerIndex); DOREPLIFETIME(ThisClass, TargetPawn);
	DOREPLIFETIME(ThisClass, Phase); DOREPLIFETIME(ThisClass, CommandSerial);
}
void AMiniTask26NetworkProbeActor::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
	if (auto* ASC = ObservedTargetASC.Get()) { ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(DamageHandle); }
#endif
	Super::EndPlay(Reason);
}
void AMiniTask26NetworkProbeActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
	if (!MiniTask26NetOptIn() || bFailed || OwnerIndex < 1) { return; }
	if (HasAuthority()) { TickServer(DeltaTime); } else { TickClient(DeltaTime); }
#endif
}
void AMiniTask26NetworkProbeActor::InitializeServer(int32 Index, AMiniCharacter* Target)
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || !MiniTask26NetOptIn() || !Target || Index < 1 || Index > 3) { return; }
	OwnerIndex = Index; TargetPawn = Target; StepAt = FPlatformTime::Seconds();
	auto* PS = Target->GetPlayerState<AMiniPlayerState>(); auto* ASC = PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
	if (!ASC) { Fail(TEXT("target ASC unavailable")); return; }
	ObservedTargetASC = ASC;
	DamageHandle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this, &ThisClass::OnDamageGE);
	ForceNetUpdate();
#endif
}
void AMiniTask26NetworkProbeActor::OnDamageGE(UAbilitySystemComponent* SourceASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
{
#if !UE_BUILD_SHIPPING
	if (HasAuthority() && Spec.Def && Spec.Def->IsA<UMiniDamageGameplayEffect>() &&
		Spec.GetContext().GetOriginalInstigator() == MiniTask26NetPS(this))
	{
		++DamageGECount;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net REAL_DAMAGE_GE: Owner=%d Count=%d Source=%s Effect=%s ServerObserved=1"),
			OwnerIndex, DamageGECount, *GetNameSafe(SourceASC), *Spec.Def->GetName());
	}
#endif
}
void AMiniTask26NetworkProbeActor::Fail(const TCHAR* Reason)
{
#if !UE_BUILD_SHIPPING
	if (bFailed) { return; } bFailed = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask26Net FAIL: Owner=%d Phase=%d Serial=%d Reason=%s"), OwnerIndex, int32(Phase), CommandSerial, Reason);
	FPlatformMisc::RequestExitWithStatus(true, 1, TEXT("MiniTask26NetworkFailure"));
#endif
}
void AMiniTask26NetworkProbeActor::Publish(EMiniTask26NetPhase NewPhase)
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority() || !MiniTask26NetOptIn()) { return; }
	auto* Pawn = MiniTask26NetPawn(this); auto* Weapon = Pawn ? Pawn->GetRangedWeaponComponent() : nullptr;
	auto* Item = MiniTask26NetItem(this); auto* TargetPS = TargetPawn ? TargetPawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	if (NewPhase == EMiniTask26NetPhase::Empty && (!Item || !Item->SetStat(MiniInventoryTags::AmmoInMagazine, 0))) { Fail(TEXT("empty fixture failed")); return; }
	if (NewPhase == EMiniTask26NetPhase::Switch) { OldEquipment = MiniTask26NetEquipment(this); OldItemId = OldEquipment ? OldEquipment->GetSourceItemId() : FGuid(); }
	if (NewPhase == EMiniTask26NetPhase::Lethal)
	{
		auto* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>(); auto* Rules = Matches ? Matches->GetMatchRulesComponent() : nullptr;
		if (!TargetPS || !Rules || !Rules->GetMatchState().bAcceptingScores) { Fail(TEXT("lethal command outside production scoring window")); return; }
		// Fixture health only; lethal damage, death and score must still originate in ServerFire's real GE.
		TargetPS->GetMiniAbilitySystemComponent()->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), 5.0f);
		RealDeath.VictimPawn = TargetPawn; RealDeath.VictimPlayerState = TargetPS;
		RealDeath.InstigatorPawn = Pawn; RealDeath.InstigatorPlayerState = MiniTask26NetPS(this);
		RealDeath.RoundId = Rules->GetRoundId(); RealDeath.VictimLifeId = TargetPS->GetCurrentLifeId(); RealDeath.Cause = EMiniPlayerDeathCause::Player;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net LETHAL_FIXTURE: Health=5 DamageAmountClientSupplied=0 Round=%d Life=%u"), RealDeath.RoundId, RealDeath.VictimLifeId);
	}
	BaseShots = Weapon ? Weapon->GetAcceptedShotCount() : 0; BaseSequence = Weapon ? Weapon->GetLastProcessedSequence() : 0;
	BaseAmmo = Item ? Item->GetStat(MiniInventoryTags::AmmoInMagazine) : 0;
	BaseReserve = Item ? Item->GetStat(MiniInventoryTags::ReserveAmmo) : 0;
	BaseHealth = TargetPS && TargetPS->GetHealthSet() ? TargetPS->GetHealthSet()->GetHealth() : 0;
	BaseGE = DamageGECount; bSawReload = false; Phase = NewPhase; ++CommandSerial; StepAt = FPlatformTime::Seconds(); ForceNetUpdate();
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net COMMAND: Owner=%d Phase=%d Serial=%d Shots=%u Ammo=%d GE=%d"), OwnerIndex, int32(Phase), CommandSerial, BaseShots, BaseAmmo, BaseGE);
#endif
}
void AMiniTask26NetworkProbeActor::StartSuite() { Publish(EMiniTask26NetPhase::Input); }
bool AMiniTask26NetworkProbeActor::ValidateResult(int32 ShotDelta, int32 GEDelta, int32 AmmoDelta, float Damage, uint32 Sequence, int32 Reason)
{
#if !UE_BUILD_SHIPPING
	auto* Pawn = MiniTask26NetPawn(this); auto* Weapon = Pawn ? Pawn->GetRangedWeaponComponent() : nullptr;
	auto* Item = MiniTask26NetItem(this); auto* PS = TargetPawn ? TargetPawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	if (!Weapon || !Item || !PS || !PS->GetHealthSet()) { return false; }
	if (Weapon->GetLastProcessedSequence() != Sequence || int32(Weapon->GetLastFireRejectionReason()) != Reason || !HasAck()) { return false; }
	if (Weapon->GetAcceptedShotCount() != BaseShots + ShotDelta || DamageGECount != BaseGE + GEDelta ||
		Item->GetStat(MiniInventoryTags::AmmoInMagazine) != BaseAmmo + AmmoDelta ||
		!FMath::IsNearlyEqual(PS->GetHealthSet()->GetHealth(), BaseHealth - Damage, 0.01f))
	{ Fail(TEXT("real RPC accepted/rejected shot, ammo, GE and damage deltas disagree")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net RPC_VERIFIED: Owner=%d Phase=%d Sequence=%u ShotDelta=%d AmmoDelta=%d GEDelta=%d Damage=%.1f Rejection=%d RealOwnerRPC=1"),
		OwnerIndex, int32(Phase), Sequence, ShotDelta, AmmoDelta, GEDelta, Damage, Reason);
	return true;
#else
	return false;
#endif
}

void AMiniTask26NetworkProbeActor::TickServer(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (Phase == EMiniTask26NetPhase::Idle || Phase == EMiniTask26NetPhase::SuiteDone || Phase == EMiniTask26NetPhase::Score || Phase == EMiniTask26NetPhase::Restore || Phase == EMiniTask26NetPhase::Complete) { return; }
	if (FPlatformTime::Seconds() - StepAt > 35.0) { Fail(TEXT("real client RPC stage timed out")); return; }
	if (!MiniTask26ReadNetworkSettings(GetWorld(), TEXT("Combat"), false)) { Fail(TEXT("Driver/connection profile changed during combat")); return; }
	auto* Definition = MiniTask26NetDefinition(this); auto* Item = MiniTask26NetItem(this); auto* PS = MiniTask26NetPS(this);
	if (!Definition || !Item || !PS) { return; } const float Damage = Definition->GetFireDamage();
	const int32 None = int32(EMiniFireRejectionReason::None);
	switch (Phase)
	{
	case EMiniTask26NetPhase::Input:
		if (ValidateResult(1, 1, -1, Damage, 1, None)) { Publish(EMiniTask26NetPhase::Replay); } break;
	case EMiniTask26NetPhase::Replay:
		if (ValidateResult(0, 0, 0, 0, 1, int32(EMiniFireRejectionReason::InvalidSequence))) { Publish(EMiniTask26NetPhase::BadView); } break;
	case EMiniTask26NetPhase::BadView:
		if (ValidateResult(0, 0, 0, 0, 2, int32(EMiniFireRejectionReason::InvalidView))) { Publish(EMiniTask26NetPhase::Recover); } break;
	case EMiniTask26NetPhase::Recover:
		if (ValidateResult(1, 1, -1, Damage, 3, None)) { Publish(EMiniTask26NetPhase::Rate); } break;
	case EMiniTask26NetPhase::Rate:
		if (ValidateResult(1, 1, -1, Damage, 5, int32(EMiniFireRejectionReason::FireRate))) { Publish(EMiniTask26NetPhase::RateRecover); } break;
	case EMiniTask26NetPhase::RateRecover:
		if (ValidateResult(1, 1, -1, Damage, 6, None)) { Publish(EMiniTask26NetPhase::Empty); } break;
	case EMiniTask26NetPhase::Empty:
		if (ValidateResult(0, 0, 0, 0, 7, int32(EMiniFireRejectionReason::EmptyMagazine))) { Publish(EMiniTask26NetPhase::Reload); } break;
	case EMiniTask26NetPhase::Reload:
	{
		bSawReload |= PS->GetMiniAbilitySystemComponent()->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading);
		if (HasAck() && bSawReload && !PS->GetMiniAbilitySystemComponent()->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading) &&
			Item->GetStat(MiniInventoryTags::AmmoInMagazine) == Definition->GetMagazineCapacity() &&
			Item->GetStat(MiniInventoryTags::ReserveAmmo) == BaseReserve - Definition->GetMagazineCapacity())
		{
			if (DamageGECount != BaseGE || MiniTask26NetPawn(this)->GetRangedWeaponComponent()->GetAcceptedShotCount() != BaseShots) { Fail(TEXT("reload changed combat counters")); return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net RELOAD_VERIFIED: Owner=%d RealEnhancedInput=1 ReloadTagObserved=1 Magazine=%d Reserve=%d"), OwnerIndex, Item->GetStat(MiniInventoryTags::AmmoInMagazine), Item->GetStat(MiniInventoryTags::ReserveAmmo));
			Publish(EMiniTask26NetPhase::ReloadRecover);
		} break;
	}
	case EMiniTask26NetPhase::ReloadRecover:
		if (ValidateResult(1, 1, -1, Damage, 8, None)) { Publish(EMiniTask26NetPhase::Switch); } break;
	case EMiniTask26NetPhase::Switch:
	{
		auto* Current = MiniTask26NetEquipment(this); auto* PC = MiniTask26NetPC(this);
		if (HasAck() && PC->GetQuickBar()->GetActiveSlotIndex() == 1 && Current && Current != OldEquipment &&
			Current->GetSourceItemId() != OldItemId && OldEquipment && OldEquipment->GetGrantedHandles().AbilityHandles.IsEmpty())
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net SWITCH_VERIFIED: Owner=%d RealEnhancedInput=1 OldSourceRevoked=1 OldGuid=%s NewGuid=%s"), OwnerIndex, *OldItemId.ToString(), *Current->GetSourceItemId().ToString());
			Publish(EMiniTask26NetPhase::StaleEquipment);
		} break;
	}
	case EMiniTask26NetPhase::StaleEquipment:
		if (ValidateResult(0, 0, 0, 0, BaseSequence, int32(EMiniFireRejectionReason::WrongEquipment)))
		{
			const auto* Weapon = MiniTask26NetPawn(this)->GetRangedWeaponComponent();
			if (Weapon->LastProcessedSequenceByItem.FindRef(MiniTask26NetEquipment(this)->GetSourceItemId()) != 0) { Fail(TEXT("old equipment request consumed new equipment sequence")); return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net OLD_EQUIPMENT_REJECTED: Owner=%d RealOwnerRPC=1 NewItemSequence=0 Unconsumed=1"), OwnerIndex);
			Publish(EMiniTask26NetPhase::SwitchRecover);
		} break;
	case EMiniTask26NetPhase::SwitchRecover:
		if (ValidateResult(1, 1, -1, Damage, 10, None))
		{
			if (OwnerIndex == 3) { Publish(EMiniTask26NetPhase::Lethal); }
			else { bSuiteDone = true; Publish(EMiniTask26NetPhase::SuiteDone); UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net OWNER_SUITE_PASS: Owner=%d InputGA=1 Replay=1 InvalidView=1 Rate=1 Empty=1 Reload=1 OldEquipment=1 Recovery=1 RealGE=1"), OwnerIndex); }
		} break;
	case EMiniTask26NetPhase::Lethal:
		if (ValidateResult(1, 1, -1, BaseHealth, 11, None))
		{
			auto* Rules = GetWorld()->GetSubsystem<UMiniMatchSubsystem>()->GetMatchRulesComponent(); auto* Victim = RealDeath.VictimPlayerState.Get();
			FMiniPlayerDeathInfo OldRound = RealDeath; --OldRound.RoundId;
			if (!Victim || PS->GetMatchStats().Kills != 1 || Victim->GetMatchStats().Deaths != 1 ||
				Rules->NotifyPlayerDeath(RealDeath) || Rules->NotifyPlayerDeath(OldRound) || PS->GetMatchStats().Kills != 1 || Victim->GetMatchStats().Deaths != 1)
			{ Fail(TEXT("real lethal GE did not score once or duplicate/old-round was accepted")); return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net REAL_GE_SCORE: Owner=3 Kills=1 Deaths=1 DuplicateRejected=1 OldRoundRejected=1 ClientReportedKillRPC=0"));
			bSuiteDone = true; Publish(EMiniTask26NetPhase::SuiteDone);
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net OWNER_SUITE_PASS: Owner=3 InputGA=1 Replay=1 InvalidView=1 Rate=1 Empty=1 Reload=1 OldEquipment=1 Recovery=1 RealGE=1"));
		} break;
	default: break;
	}
#endif
}

bool AMiniTask26NetworkProbeActor::Aim(FVector& Origin, FVector& Direction) const
{
#if !UE_BUILD_SHIPPING
	const auto* Pawn = MiniTask26NetPawn(this); auto* Camera = Pawn ? Pawn->GetMiniCameraComponent() : nullptr;
	if (!Camera || !TargetPawn) { return false; }
	FMinimalViewInfo View; Camera->GetCameraView(0.0f, View); Origin = View.Location;
	Direction = (TargetPawn->GetActorLocation() - Origin).GetSafeNormal(); return !Direction.IsNearlyZero();
#else
	return false;
#endif
}
void AMiniTask26NetworkProbeActor::SendRaw(uint32 Sequence, bool bBadView, bool bOldEquipment)
{
#if !UE_BUILD_SHIPPING
	auto* Pawn = MiniTask26NetPawn(this); auto* Weapon = Pawn ? Pawn->GetRangedWeaponComponent() : nullptr;
	auto* Equipment = bOldEquipment ? OldEquipment.Get() : MiniTask26NetEquipment(this); FVector Origin, Direction;
	if (!Pawn || Pawn->HasAuthority() || !Pawn->IsLocallyControlled() || !Weapon || !Equipment || !Aim(Origin, Direction)) { Fail(TEXT("owning client production RPC source unavailable")); return; }
	if (bBadView) { Origin += FVector(5000, 0, 0); }
	// The friend is Development-only. No alternate gameplay RPC or direct server TryFire is introduced.
	Weapon->ServerFire(Origin, Direction, Sequence, Equipment->GetSourceItemId(), Equipment);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net CLIENT_SEND: Owner=%d Phase=%d Sequence=%u ProductionServerFire=1 BadView=%d OldEquipment=%d"), OwnerIndex, int32(Phase), Sequence, int32(bBadView), int32(bOldEquipment));
#endif
}

void AMiniTask26NetworkProbeActor::ServerEcho_Implementation(int32 Sample, double SentAt)
{
#if !UE_BUILD_SHIPPING
	if (MiniTask26NetOptIn() && HasAuthority() && Sample >= 0 && Sample < 20 && FMath::IsFinite(SentAt)) { ClientEcho(Sample, SentAt); }
#endif
}
void AMiniTask26NetworkProbeActor::ClientEcho_Implementation(int32 Sample, double SentAt)
{
#if !UE_BUILD_SHIPPING
	if (!MiniTask26NetOptIn() || HasAuthority() || Sample != OutstandingEcho || SentAt != EchoSentAt) { return; }
	const double Milliseconds = (FPlatformTime::Seconds() - EchoSentAt) * 1000.0;
	if (!FMath::IsFinite(Milliseconds) || Milliseconds < 0) { Fail(TEXT("invalid same-clock RTT sample")); return; }
	RTTSamples.Add(Milliseconds); OutstandingEcho = INDEX_NONE;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net RTT_SAMPLE: Owner=%d Sample=%d Ms=%.3f SameClientClock=1"), OwnerIndex, Sample, Milliseconds);
	if (RTTSamples.Num() == 20)
	{
		TArray<double> Sorted = RTTSamples; Sorted.Sort(); const double Median = (Sorted[9] + Sorted[10]) * 0.5;
		const double P95 = Sorted[18]; int32 Lag = 0; FParse::Value(FCommandLine::Get(), TEXT("MiniTask26ExpectedLag="), Lag);
		if (Lag == 50 && Median < 80.0) { Fail(TEXT("weak profile did not produce an observed RTT consistent with 50ms each direction")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net RTT_SUMMARY: Owner=%d Samples=20 MedianMs=%.3f P95Ms=%.3f ConfigLag=%d ActualMeasured=1"), OwnerIndex, Median, P95, Lag);
		ServerEchoDone(20, Median, P95);
	}
#endif
}
void AMiniTask26NetworkProbeActor::ServerEchoDone_Implementation(int32 Samples, double Median, double P95)
{
#if !UE_BUILD_SHIPPING
	int32 Lag = 0; FParse::Value(FCommandLine::Get(), TEXT("MiniTask26ExpectedLag="), Lag);
	if (!MiniTask26NetOptIn() || !HasAuthority() || bEchoAck || Samples != 20 || !FMath::IsFinite(Median) ||
		!FMath::IsFinite(P95) || Median < 0 || P95 < Median || (Lag == 50 && Median < 80.0)) { return; }
	// These are diagnostic latency observations only; they cannot select damage or scoring.
	bEchoAck = true; UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net RTT_ACK: Owner=%d Samples=20 MedianMs=%.3f P95Ms=%.3f"), OwnerIndex, Median, P95);
#endif
}
void AMiniTask26NetworkProbeActor::ServerReady_Implementation()
{
#if !UE_BUILD_SHIPPING
	auto* PC = MiniTask26NetPC(this); auto* Pawn = MiniTask26NetPawn(this); auto* PS = MiniTask26NetPS(this);
	if (!MiniTask26NetOptIn() || !HasAuthority() || bReadyAck || !PC || PC->IsLocalController() || !PC->GetNetConnection() ||
		!Pawn || !PS || !PS->GetMiniAbilitySystemComponent() || PS->GetMiniAbilitySystemComponent()->GetAvatarActor() != Pawn || !MiniTask26NetEquipment(this)) { return; }
	if (!MiniTask26ReadNetworkSettings(GetWorld(), TEXT("OwnerReady"), true)) { Fail(TEXT("server owner-ready actual profile mismatch")); return; }
	bReadyAck = true; UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net OWNER_READY_ACK: Owner=%d RealOwningClient=1 Avatar=1 Equipment=1"), OwnerIndex);
#endif
}
void AMiniTask26NetworkProbeActor::ServerAcknowledge_Implementation(int32 Serial, int32 Kills, int32 Deaths, int32 Round, bool bMutationRejected)
{
#if !UE_BUILD_SHIPPING
	if (!MiniTask26NetOptIn() || !HasAuthority() || Serial <= 0 || Serial != CommandSerial || AckSerial == Serial) { return; }
	if (Phase == EMiniTask26NetPhase::Score)
	{
		auto* PS = MiniTask26NetPS(this); const auto* Stats = PS ? &PS->GetMatchStats() : nullptr;
		if (!Stats || Stats->Kills != Kills || Stats->Deaths != Deaths || Stats->RoundId != Round || !bMutationRejected) { Fail(TEXT("client scoreboard/HUD/mutation checkpoint mismatch")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net SCORE_REPLICATED_ACK: Owner=%d Kills=%d Deaths=%d Round=%d HUD=1 ClientMutationsRejected=1"), OwnerIndex, Kills, Deaths, Round);
	}
	AckSerial = Serial;
#endif
}
void AMiniTask26NetworkProbeActor::TickClient(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	auto* PC = MiniTask26NetPC(this); auto* Pawn = MiniTask26NetPawn(this); auto* PS = MiniTask26NetPS(this);
	auto* Equipment = MiniTask26NetEquipment(this); auto* HUD = MiniTask26NetHUD(PC);
	if (!PC || !PC->IsLocalController() || !Pawn || !PS || !Equipment || !Pawn->GetHeroComponent() ||
		!Pawn->GetHeroComponent()->IsInputActive() || !HUD || !HUD->IsRunning() || !MiniTask26NetOwnerSourceReady(this)) { return; }
	// Match the authority fixture on the autonomous proxy. MOVE_None only on
	// authority does not stop client prediction; flying also preserves view updates.
	Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Pawn->GetCharacterMovement()->StopMovementImmediately();
	const bool bRestored = Phase == EMiniTask26NetPhase::Restore || Phase == EMiniTask26NetPhase::Complete;
	if (!bRestored && !MiniTask26ReadNetworkSettings(GetWorld(), TEXT("OwningClient"), !bReadbackPrinted)) { Fail(TEXT("client actual Driver/connection profile mismatch")); return; }
	bReadbackPrinted = true;
	if (!bReadySent) { bReadySent = true; ServerReady(); UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net CLIENT_READY: Owner=%d Input=1 HUDWidgets=4 ActualEquipment=1"), OwnerIndex); }
	if (RTTSamples.Num() < 20 && OutstandingEcho == INDEX_NONE)
	{
		OutstandingEcho = RTTSamples.Num(); EchoSentAt = FPlatformTime::Seconds(); ServerEcho(OutstandingEcho, EchoSentAt);
	}
	if (OutstandingEcho != INDEX_NONE && FPlatformTime::Seconds() - EchoSentAt > 10.0) { Fail(TEXT("finite RTT echo deadline expired")); return; }
	if (ClientSerial != CommandSerial)
	{
		ClientSerial = CommandSerial; ClientStepAt = FPlatformTime::Seconds();
		bClientActed = bInputPressed = bInputReleased = false;
	}
	if (Phase == EMiniTask26NetPhase::Idle || Phase == EMiniTask26NetPhase::SuiteDone || Phase == EMiniTask26NetPhase::Complete) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - ClientStepAt > 35.0 && !bClientActed) { Fail(TEXT("client production input/RPC command deadline expired")); return; }
	if (bClientActed) { return; }
	if (Phase == EMiniTask26NetPhase::Restore)
	{
		if (!MiniTask26RestoreNetworkSettings(GetWorld())) { Fail(TEXT("client complete profile restore/readback failed")); return; }
		bClientActed = true; ServerAcknowledge(CommandSerial, 0, 0, 0, true); return;
	}
	if (Phase == EMiniTask26NetPhase::Score)
	{
		auto* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>(); auto* Rules = Matches ? Matches->GetMatchRulesComponent() : nullptr;
		const auto& Stats = PS->GetMatchStats(); const auto& Snapshot = HUD->GetSnapshot();
		if (!Rules || !Matches->GetCurrentMatchState().bAcceptingScores || Stats.Kills != (OwnerIndex == 3 ? 1 : 0) || Stats.Deaths != 0 ||
			!Snapshot.bHasMatchData || Snapshot.Score != Stats.Kills || Snapshot.Deaths != Stats.Deaths ||
			!Snapshot.bAmmoReady || Snapshot.ItemId != Equipment->GetSourceItemId() || !Equipment->GetSourceItem() ||
			Snapshot.MagazineAmmo != Equipment->GetSourceItem()->GetStat(MiniInventoryTags::AmmoInMagazine) ||
			Snapshot.ReserveAmmo != Equipment->GetSourceItem()->GetStat(MiniInventoryTags::ReserveAmmo) || Snapshot.ActiveSlot != 1) { return; }
		const auto Before = Stats; FMiniPlayerDeathInfo EmptyDeath;
		const bool KillAccepted = PS->RecordMatchKill(Stats.RoundId); const bool DeathAccepted = PS->RecordMatchDeath(Stats.RoundId);
		const bool ResetAccepted = PS->ResetMatchStats(Stats.RoundId); const bool NotifyAccepted = Rules->NotifyPlayerDeath(EmptyDeath);
		if (KillAccepted || DeathAccepted || ResetAccepted || NotifyAccepted || Stats.Kills != Before.Kills || Stats.Deaths != Before.Deaths || Stats.Revision != Before.Revision)
		{ Fail(TEXT("client could mutate production match stats/rules")); return; }
		bClientActed = true; ServerAcknowledge(CommandSerial, Stats.Kills, Stats.Deaths, Stats.RoundId, true); return;
	}
	if (!OldEquipment) { OldEquipment = Equipment; OldItemId = Equipment->GetSourceItemId(); }
	if (Phase != EMiniTask26NetPhase::Reload && Phase != EMiniTask26NetPhase::Switch)
	{
		FVector Origin, Direction; if (!Aim(Origin, Direction)) { return; }
		PC->SetControlRotation(Direction.Rotation());
		if (FVector::Dist(Pawn->GetActorLocation(), FVector(0, 0, 3000)) > 30.0 || !TargetPawn ||
			FVector::Dist(TargetPawn->GetActorLocation(), FVector(800, 0, 3000)) > 30.0) { return; }
	}
	if (Now - ClientStepAt < 0.75) { return; } // Real camera/control rotation/replication settle, no validation relaxation.
	if (Phase == EMiniTask26NetPhase::Input || Phase == EMiniTask26NetPhase::Reload || Phase == EMiniTask26NetPhase::Switch)
	{
		const FKey Key = Phase == EMiniTask26NetPhase::Input ? EKeys::LeftMouseButton : Phase == EMiniTask26NetPhase::Reload ? EKeys::R : EKeys::Q;
		if (!bInputPressed) { MiniTask26NetKey(PC, Key, IE_Pressed); bInputPressed = true; InputPressedAt = Now; return; }
		if (!bInputReleased && Now - InputPressedAt >= 0.05)
		{
			MiniTask26NetKey(PC, Key, IE_Released); bInputReleased = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net CLIENT_INPUT: Owner=%d Phase=%d Key=%s RealEnhancedInput=1"), OwnerIndex, int32(Phase), *Key.ToString());
		}
		if (!bInputReleased) { return; }
		if (Phase == EMiniTask26NetPhase::Input && Pawn->GetRangedWeaponComponent()->NextLocalSequence != 1) { return; }
		if (Phase == EMiniTask26NetPhase::Switch && PC->GetQuickBar()->GetActiveSlotIndex() != 1) { return; }
	}
	else
	{
		switch (Phase)
		{
		case EMiniTask26NetPhase::Replay: SendRaw(1); break;
		case EMiniTask26NetPhase::BadView: SendRaw(2, true); break;
		case EMiniTask26NetPhase::Recover: SendRaw(3); break;
		case EMiniTask26NetPhase::Rate: SendRaw(4); SendRaw(5); break;
		case EMiniTask26NetPhase::RateRecover: SendRaw(6); break;
		case EMiniTask26NetPhase::Empty: SendRaw(7); break;
		case EMiniTask26NetPhase::ReloadRecover: SendRaw(8); break;
		case EMiniTask26NetPhase::StaleEquipment: SendRaw(9, false, true); break;
		case EMiniTask26NetPhase::SwitchRecover: SendRaw(10); break;
		case EMiniTask26NetPhase::Lethal: SendRaw(11); break;
		default: return;
		}
	}
	bClientActed = true; ServerAcknowledge(CommandSerial, 0, 0, 0, true);
#endif
}
void AMiniTask26NetworkProbeActor::FinishClient() { ClientFinish(); }
void AMiniTask26NetworkProbeActor::ClientFinish_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (!MiniTask26NetOptIn() || HasAuthority()) { return; }
	if (RTTSamples.Num() != 20 || !MiniTask26ReadNetworkSettings(GetWorld(), TEXT("ClientFinal"), true, true)) { Fail(TEXT("final client RTT/profile recovery evidence missing")); return; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net CLIENT_PASS: Owner=%d RTT20=1 RealInput=1 RealOwnerRPC=1 ScoreHUD=1 RestoredDriverAndConnection=1"), OwnerIndex);
	FPlatformMisc::RequestExitWithStatus(false, 0, TEXT("MiniTask26NetworkClientComplete"));
#endif
}
