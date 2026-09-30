#include "MiniCombatFeedbackComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerState.h"
#include "Sound/SoundBase.h"
#include "System/MiniGameplayTags.h"
#include "TimerManager.h"

namespace
{
constexpr int32 MaxRememberedShots = 64;

void RememberShot(TArray<uint32>& Shots, uint32 Sequence)
{
	if (!Sequence || Shots.Contains(Sequence))
	{
		return;
	}
	Shots.Add(Sequence);
	if (Shots.Num() > MaxRememberedShots)
	{
		Shots.RemoveAt(0);
	}
}
}

UMiniCombatFeedbackComponent::UMiniCombatFeedbackComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	RifleFireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Game/Audio/SoundWaves/Weapons/sfx_Weapon_AutoRifle_MainLayer_01.sfx_Weapon_AutoRifle_MainLayer_01")));
	PistolFireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Game/Audio/SoundWaves/Weapons/sfx_Weapon_Pistol_MainLayer_nl_01.sfx_Weapon_Pistol_MainLayer_nl_01")));
	ImpactSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Game/Audio/SoundWaves/Weapons/SFX_BulletImpact_01.SFX_BulletImpact_01")));
	ReloadSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Game/Weapons/Rifle/Sounds/Rifle_Load01.Rifle_Load01")));
	DamageSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Game/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_01.Lyra_Plyr_BulletImpact_01")));
	DeathSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Game/Audio/Sounds/Impacts/Lyra_EnemyKilled_01.Lyra_EnemyKilled_01")));
	RifleFireMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Rifle/Animations/AM_MiniRifle_Fire.AM_MiniRifle_Fire")));
	PistolFireMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Pistol/Animations/AM_MiniPistol_Fire.AM_MiniPistol_Fire")));
	RifleReloadMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Rifle/Animations/AM_MiniRifle_Reload.AM_MiniRifle_Reload")));
	PistolReloadMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(
		TEXT("/Game/Mini/Weapons/Pistol/Animations/AM_MiniPistol_Reload.AM_MiniPistol_Reload")));
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask18NoMediaAssets")))
	{
		RifleFireSound.Reset();
		PistolFireSound.Reset();
		ImpactSound.Reset();
		ReloadSound.Reset();
		DamageSound.Reset();
		DeathSound.Reset();
		RifleFireMontage.Reset();
		PistolFireMontage.Reset();
		RifleReloadMontage.Reset();
		PistolReloadMontage.Reset();
	}
#endif
}

void UMiniCombatFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PendingReloadTimer);
	}
	StopReload();
	PendingLocalShots.Reset();
	ReceivedServerShots.Reset();
	for (TWeakObjectPtr<UPointLightComponent>& Light : ActiveLights)
	{
		if (Light.IsValid())
		{
			Light->DestroyComponent();
		}
	}
	ActiveLights.Reset();
	Super::EndPlay(EndPlayReason);
}

void UMiniCombatFeedbackComponent::PlaySound(TSoftObjectPtr<USoundBase>& Sound, const FVector& Location)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (USoundBase* Asset = Sound.LoadSynchronous())
	{
		if (UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Asset, Location))
		{
			SoundPlaybackCount += Audio->IsPlaying() ? 1 : 0;
		}
	}
}

void UMiniCombatFeedbackComponent::FlashAt(const FVector& Location,
	const FLinearColor& Color, float Intensity)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetIntensity(Intensity);
	Light->SetLightColor(Color);
	Light->SetAttenuationRadius(170.0f);
	Light->SetCastShadows(false);
	Light->RegisterComponentWithWorld(World);
	Light->SetWorldLocation(Location);
	const TWeakObjectPtr<UPointLightComponent> WeakLight(Light);
	ActiveLights.Add(WeakLight);
	FTimerHandle ExpireTimer;
	World->GetTimerManager().SetTimer(ExpireTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakLight]()
		{
			if (WeakLight.IsValid())
			{
				WeakLight->DestroyComponent();
			}
			ActiveLights.Remove(WeakLight);
		}), 0.08f, false);
}

void UMiniCombatFeedbackComponent::PlayWeaponMontage(TSoftObjectPtr<UAnimMontage>& Montage)
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	USkeletalMeshComponent* WeaponMesh = Pawn ? Pawn->GetPracticeRifleMesh() : nullptr;
	if (!WeaponMesh || !WeaponMesh->GetSkeletalMeshAsset() ||
		(GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}
	UAnimInstance* Instance = WeaponMesh->GetAnimInstance();
	UAnimMontage* Asset = Montage.LoadSynchronous();
	// Cues and equipment arrive on different actor channels. A delayed Cue must
	// never switch the current weapon to the previous weapon's animation class.
	if (Instance && Asset && Asset->GetSkeleton() == WeaponMesh->GetSkeletalMeshAsset()->GetSkeleton())
	{
		MontagePlayCount += Instance->Montage_Play(Asset) > 0.0f ? 1 : 0;
	}
}

void UMiniCombatFeedbackComponent::EmitLocalEvent(FGameplayTag Tag,
	const FVector& Location, float Magnitude)
{
	OnCombatFeedback.Broadcast(Tag, Location, Magnitude);
}

void UMiniCombatFeedbackComponent::PresentFire(bool bPistol,
	const FVector& Location, bool bPredicted)
{
	++FirePresentationCount;
	PlaySound(bPistol ? PistolFireSound : RifleFireSound, Location);
	PlayWeaponMontage(bPistol ? PistolFireMontage : RifleFireMontage);
	FlashAt(Location, FLinearColor(1.0f, 0.63f, 0.16f), 1800.0f);
	EmitLocalEvent(bPistol ? MiniGameplayTags::GameplayCue_Mini_PistolFire
		: MiniGameplayTags::GameplayCue_Mini_RifleFire, Location, bPredicted ? 1.0f : 0.0f);
}

void UMiniCombatFeedbackComponent::PlayPredictedFire(uint32 ShotSequence,
	bool bPistol, const FVector& MuzzleLocation)
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || !ShotSequence)
	{
		return;
	}
	RememberShot(PendingLocalShots, ShotSequence);
	++PredictedFireCount;
	PresentFire(bPistol, MuzzleLocation, true);
}

void UMiniCombatFeedbackComponent::PresentImpact(const FVector& Location)
{
	++ImpactPresentationCount;
	PlaySound(ImpactSound, Location);
	FlashAt(Location, FLinearColor(1.0f, 0.8f, 0.45f), 900.0f);
	EmitLocalEvent(MiniGameplayTags::GameplayCue_Mini_Impact, Location);
}

void UMiniCombatFeedbackComponent::PresentDamage(const FVector& Location)
{
	++DamagePresentationCount;
	PlaySound(DamageSound, Location);
	FlashAt(Location, FLinearColor(1.0f, 0.08f, 0.08f), 600.0f);
	EmitLocalEvent(MiniGameplayTags::GameplayCue_Mini_Damage, Location);
}

void UMiniCombatFeedbackComponent::PresentDeath()
{
	if (DeathPresentationCount != 0)
	{
		return;
	}
	++DeathPresentationCount;
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (Pawn)
	{
		PlaySound(DeathSound, Pawn->GetActorLocation());
		if (USkeletalMeshComponent* Body = Pawn->GetMesh())
		{
			Body->AddRelativeRotation(FRotator(0.0f, 0.0f, 75.0f));
			Body->AddRelativeLocation(FVector(0.0f, 0.0f, -25.0f));
		}
		EmitLocalEvent(MiniGameplayTags::GameplayCue_Mini_Death, Pawn->GetActorLocation());
	}
	StopReload();
}

void UMiniCombatFeedbackComponent::StartReload(bool bPistol)
{
	if (bReloadVisible)
	{
		return;
	}
	bReloadVisible = true;
	++ReloadStartCount;
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (Pawn)
	{
		PlaySound(ReloadSound, Pawn->GetActorLocation());
		TSoftObjectPtr<UAnimMontage>& Montage = bPistol ? PistolReloadMontage : RifleReloadMontage;
		ActiveReloadMontage = Montage.LoadSynchronous();
		PlayWeaponMontage(Montage);
		EmitLocalEvent(MiniGameplayTags::GameplayCue_Mini_Reload, Pawn->GetActorLocation(), 1.0f);
	}
}

void UMiniCombatFeedbackComponent::StopReload()
{
	if (!bReloadVisible)
	{
		return;
	}
	bReloadVisible = false;
	++ReloadStopCount;
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	USkeletalMeshComponent* WeaponMesh = Pawn ? Pawn->GetPracticeRifleMesh() : nullptr;
	if (WeaponMesh && WeaponMesh->GetAnimInstance() && ActiveReloadMontage)
	{
		WeaponMesh->GetAnimInstance()->Montage_Stop(0.1f, ActiveReloadMontage);
	}
	ActiveReloadMontage = nullptr;
	if (Pawn)
	{
		EmitLocalEvent(MiniGameplayTags::GameplayCue_Mini_Reload, Pawn->GetActorLocation(), 0.0f);
	}
}

void UMiniCombatFeedbackComponent::HandleCombatCue(FGameplayTag CueTag,
	EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters)
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (!Pawn)
	{
		return;
	}
	if (CueTag == MiniGameplayTags::GameplayCue_Mini_Reload)
	{
		if (EventType == EGameplayCueEvent::Removed)
		{
			if (GetWorld())
			{
				GetWorld()->GetTimerManager().ClearTimer(PendingReloadTimer);
			}
			bReloadCueActive = false;
			ReloadCueSourceEquipment.Reset();
			StopReload();
		}
		else if (EventType == EGameplayCueEvent::WhileActive)
		{
			// WhileActive is backed by the replicated active Cue entry. OnActive
			// alone can arrive for a reload cancelled before the first net update.
			bReloadCueActive = true;
			bReloadCuePistol = Parameters.RawMagnitude == 1.0f;
			ReloadCueSourceEquipment = Parameters.SourceObject;
			RefreshPendingReload();
		}
		return;
	}
	if (EventType != EGameplayCueEvent::Executed)
	{
		return;
	}
	if (CueTag == MiniGameplayTags::GameplayCue_Mini_RifleFire ||
		CueTag == MiniGameplayTags::GameplayCue_Mini_PistolFire)
	{
		++ConfirmedFireCueCount;
		const uint32 Sequence = static_cast<uint32>(FMath::Max(0.0f, Parameters.RawMagnitude));
		if (ReceivedServerShots.Contains(Sequence))
		{
			return;
		}
		RememberShot(ReceivedServerShots, Sequence);
		if (Pawn->IsLocallyControlled() && PendingLocalShots.Remove(Sequence) > 0)
		{
			++SuppressedFireEchoCount;
			return;
		}
		PresentFire(CueTag == MiniGameplayTags::GameplayCue_Mini_PistolFire,
			Parameters.Location.IsNearlyZero() ? Pawn->GetActorLocation() : FVector(Parameters.Location), false);
	}
	else if (CueTag == MiniGameplayTags::GameplayCue_Mini_Impact)
	{
		PresentImpact(Parameters.Location);
	}
	else if (CueTag == MiniGameplayTags::GameplayCue_Mini_Damage)
	{
		PresentDamage(Parameters.Location.IsNearlyZero() ? Pawn->GetActorLocation() : FVector(Parameters.Location));
	}
	else if (CueTag == MiniGameplayTags::GameplayCue_Mini_Death)
	{
		PresentDeath();
	}
}

void UMiniCombatFeedbackComponent::NotifyConfirmedHit(uint32 ShotSequence,
	float AppliedDamage, bool bKilled)
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || !ShotSequence || AppliedDamage <= 0.0f)
	{
		return;
	}
	++HitConfirmCount;
	OnHitConfirmed.Broadcast(static_cast<int32>(ShotSequence), AppliedDamage, bKilled);
	EmitLocalEvent(MiniGameplayTags::GameplayCue_Mini_HitConfirmed,
		Pawn->GetActorLocation(), AppliedDamage);
}

void UMiniCombatFeedbackComponent::NotifyDeathState()
{
	PresentDeath();
}

void UMiniCombatFeedbackComponent::HandleEquipmentChanged()
{
	StopReload();
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	USkeletalMeshComponent* WeaponMesh = Pawn ? Pawn->GetPracticeRifleMesh() : nullptr;
	if (WeaponMesh && WeaponMesh->GetAnimInstance())
	{
		WeaponMesh->GetAnimInstance()->Montage_Stop(0.0f);
	}
}

void UMiniCombatFeedbackComponent::RefreshPendingReload()
{
	const AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	const UMiniEquipmentManagerComponent* Manager = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	const UMiniEquipmentInstance* Equipment = Manager ? Manager->GetCurrentEquipment() : nullptr;
	const AMiniPlayerState* State = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	const UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	FGameplayCueParameters CurrentParameters;
	const bool bHasCurrentCue = ASC && ASC->GetAvatarActor() == Pawn &&
		ASC->FindActiveGameplayCueParameters(MiniGameplayTags::GameplayCue_Mini_Reload, CurrentParameters) &&
		Pawn->GetHealthComponent() && !Pawn->GetHealthComponent()->IsDead();
	if (!bHasCurrentCue)
	{
		bReloadCueActive = false;
		ReloadCueSourceEquipment.Reset();
		StopReload();
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(PendingReloadTimer);
		}
		return;
	}
	// A FastArray can receive its entry before SourceObject is mapped; its later
	// PostReplicatedChange does not replay WhileActive. Re-read the stored entry.
	bReloadCueActive = true;
	bReloadCuePistol = CurrentParameters.RawMagnitude == 1.0f;
	ReloadCueSourceEquipment = CurrentParameters.SourceObject;
	if (ReloadCueSourceEquipment.IsValid() && ReloadCueSourceEquipment.Get() == Equipment)
	{
		StartReload(bReloadCuePistol);
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(PendingReloadTimer);
		}
	}
	else if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(PendingReloadTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(PendingReloadTimer, this,
			&ThisClass::RefreshPendingReload, 0.05f, true);
	}
}
