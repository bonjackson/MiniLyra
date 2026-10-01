#pragma once

#include "Components/ActorComponent.h"
#include "Combat/MiniDamageResult.h"
#include "GameplayCueInterface.h"
#include "TimerManager.h"
#include "MiniCombatFeedbackComponent.generated.h"

class UAnimMontage;
class USoundBase;
class UPointLightComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMiniCombatFeedbackEvent,
	FGameplayTag, EventTag, FVector, Location, float, Magnitude);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMiniHitConfirmedEvent,
	int32, ShotSequence, float, AppliedDamage, bool, bKilled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMiniDamageConfirmedEvent,
	int32, ShotSequence, float, AppliedDamage, EMiniDamageTargetKind, TargetKind, bool, bTargetDefeated);

/** Local presentation and UI event bridge. No gameplay decision is made here. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniCombatFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMiniCombatFeedbackComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void PlayPredictedFire(uint32 ShotSequence, bool bPistol, const FVector& MuzzleLocation);
	void HandleCombatCue(FGameplayTag CueTag, EGameplayCueEvent::Type EventType,
		const FGameplayCueParameters& Parameters);
	void NotifyConfirmedHit(uint32 ShotSequence, float AppliedDamage, bool bKilled);
	void NotifyConfirmedDamage(uint32 ShotSequence, float AppliedDamage,
		EMiniDamageTargetKind TargetKind, bool bTargetDefeated);
	/** Replicated death state also restores presentation for late or lost Cue RPCs. */
	void NotifyDeathState();
	void HandleEquipmentChanged();
	void RefreshPendingReload();

	UPROPERTY(BlueprintAssignable, Category = "Mini|Feedback")
	FMiniCombatFeedbackEvent OnCombatFeedback;

	UPROPERTY(BlueprintAssignable, Category = "Mini|Feedback")
	FMiniHitConfirmedEvent OnHitConfirmed;
	/** Explicit target kind; practice target defeat is never a player-kill event. */
	UPROPERTY(BlueprintAssignable, Category = "Mini|Feedback")
	FMiniDamageConfirmedEvent OnDamageConfirmed;

	uint32 GetFirePresentationCount() const { return FirePresentationCount; }
	uint32 GetPredictedFireCount() const { return PredictedFireCount; }
	uint32 GetConfirmedFireCueCount() const { return ConfirmedFireCueCount; }
	uint32 GetSuppressedFireEchoCount() const { return SuppressedFireEchoCount; }
	uint32 GetImpactPresentationCount() const { return ImpactPresentationCount; }
	uint32 GetDamagePresentationCount() const { return DamagePresentationCount; }
	uint32 GetDeathPresentationCount() const { return DeathPresentationCount; }
	uint32 GetReloadStartCount() const { return ReloadStartCount; }
	uint32 GetReloadStopCount() const { return ReloadStopCount; }
	uint32 GetHitConfirmCount() const { return HitConfirmCount; }
	uint32 GetSoundPlaybackCount() const { return SoundPlaybackCount; }
	uint32 GetMontagePlayCount() const { return MontagePlayCount; }

private:
	void PresentFire(bool bPistol, const FVector& Location, bool bPredicted);
	void PresentImpact(const FVector& Location);
	void PresentDamage(const FVector& Location);
	void PresentDeath();
	void StartReload(bool bPistol);
	void StopReload();
	void FlashAt(const FVector& Location, const FLinearColor& Color, float Intensity);
	void PlaySound(TSoftObjectPtr<USoundBase>& Sound, const FVector& Location);
	void PlayWeaponMontage(TSoftObjectPtr<UAnimMontage>& Montage);
	void EmitLocalEvent(FGameplayTag Tag, const FVector& Location, float Magnitude = 0.0f);

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Audio")
	TSoftObjectPtr<USoundBase> RifleFireSound;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Audio")
	TSoftObjectPtr<USoundBase> PistolFireSound;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Audio")
	TSoftObjectPtr<USoundBase> ImpactSound;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Audio")
	TSoftObjectPtr<USoundBase> ReloadSound;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Audio")
	TSoftObjectPtr<USoundBase> DamageSound;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Audio")
	TSoftObjectPtr<USoundBase> DeathSound;

	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Animation")
	TSoftObjectPtr<UAnimMontage> RifleFireMontage;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Animation")
	TSoftObjectPtr<UAnimMontage> PistolFireMontage;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Animation")
	TSoftObjectPtr<UAnimMontage> RifleReloadMontage;
	UPROPERTY(EditDefaultsOnly, Category = "Mini|Feedback|Animation")
	TSoftObjectPtr<UAnimMontage> PistolReloadMontage;

	TArray<uint32> PendingLocalShots;
	TArray<uint32> ReceivedServerShots;
	TArray<TWeakObjectPtr<UPointLightComponent>> ActiveLights;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveReloadMontage;
	bool bReloadVisible = false;
	bool bReloadCueActive = false;
	bool bReloadCuePistol = false;
	TWeakObjectPtr<const UObject> ReloadCueSourceEquipment;
	FTimerHandle PendingReloadTimer;
	uint32 FirePresentationCount = 0;
	uint32 PredictedFireCount = 0;
	uint32 ConfirmedFireCueCount = 0;
	uint32 SuppressedFireEchoCount = 0;
	uint32 ImpactPresentationCount = 0;
	uint32 DamagePresentationCount = 0;
	uint32 DeathPresentationCount = 0;
	uint32 ReloadStartCount = 0;
	uint32 ReloadStopCount = 0;
	uint32 HitConfirmCount = 0;
	uint32 SoundPlaybackCount = 0;
	uint32 MontagePlayCount = 0;
};
