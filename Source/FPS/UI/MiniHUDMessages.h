#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "MiniHUDMessages.generated.h"

class AMiniCharacter;
class ULocalPlayer;
class UWorld;
class UMiniHUDViewModel;

namespace MiniHUDTags
{
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(StateChanged);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(HitConfirmed);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(EmptyMagazine);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(HealthSlot);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AmmoSlot);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(CrosshairSlot);
	FPS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MatchSlot);
}

/** Current replicated state of one local player. No network messages are sent here. */
USTRUCT(BlueprintType)
struct FPS_API FMiniHUDSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ULocalPlayer> LocalPlayer = nullptr;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UWorld> World = nullptr;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AMiniCharacter> Pawn = nullptr;
	UPROPERTY(BlueprintReadOnly)
	FGuid ItemId;
	UPROPERTY(BlueprintReadOnly)
	FText WeaponName;
	UPROPERTY(BlueprintReadOnly)
	float Health = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float MaxHealth = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	int32 ActiveSlot = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly)
	int32 MagazineAmmo = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 ReserveAmmo = 0;
	UPROPERTY(BlueprintReadOnly)
	bool bHealthReady = false;
	UPROPERTY(BlueprintReadOnly)
	bool bAmmoReady = false;
	UPROPERTY(BlueprintReadOnly)
	bool bDead = false;
	UPROPERTY(BlueprintReadOnly)
	bool bReloading = false;
	/** Tasks 21/22 will provide replicated match data. Dashes until then. */
	UPROPERTY(BlueprintReadOnly)
	bool bHasMatchData = false;
	UPROPERTY(BlueprintReadOnly)
	int32 Score = 0;
	UPROPERTY(BlueprintReadOnly)
	int32 RemainingSeconds = -1;
	UPROPERTY(BlueprintReadOnly)
	int32 Revision = 0;
};

USTRUCT(BlueprintType)
struct FPS_API FMiniHUDStateMessage
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UMiniHUDViewModel> Source = nullptr;
	UPROPERTY(BlueprintReadOnly)
	FMiniHUDSnapshot Snapshot;
};

/** Raised only after the owner receives the server's confirmed hit RPC. */
USTRUCT(BlueprintType)
struct FPS_API FMiniHUDHitMessage
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UMiniHUDViewModel> Source = nullptr;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ULocalPlayer> LocalPlayer = nullptr;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UWorld> World = nullptr;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AMiniCharacter> Pawn = nullptr;
	UPROPERTY(BlueprintReadOnly)
	int32 ShotSequence = 0;
	UPROPERTY(BlueprintReadOnly)
	float AppliedDamage = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	bool bKilled = false;
};

USTRUCT(BlueprintType)
struct FPS_API FMiniHUDEmptyMessage
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UMiniHUDViewModel> Source = nullptr;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ULocalPlayer> LocalPlayer = nullptr;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UWorld> World = nullptr;
	UPROPERTY(BlueprintReadOnly)
	FGuid ItemId;
};
