#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MiniTask18AnimAssetLibrary.generated.h"

class UAnimBlueprint;
class UAnimMontage;
class UAnimSequence;

/** Editor bridge for the small combat montage and slot assets. */
UCLASS()
class FPS_API UMiniTask18AnimAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Add a passthrough slot after Task 11's complete locomotion graph. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Task18")
	static bool ConfigureCharacterSlot(UAnimBlueprint* AnimBlueprint, FName SlotName);

	UFUNCTION(BlueprintCallable, Category = "Mini|Task18")
	static bool VerifyCharacterSlot(const UAnimBlueprint* AnimBlueprint, FName SlotName);

	/** Build a minimal reference-pose -> slot graph for a weapon skeletal mesh. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Task18")
	static bool ConfigureWeaponAnim(UAnimBlueprint* AnimBlueprint, FName SlotName);

	UFUNCTION(BlueprintCallable, Category = "Mini|Task18")
	static bool VerifyWeaponAnim(const UAnimBlueprint* AnimBlueprint, FName SlotName);

	/** The factory supplies the sequence and section; this assigns the declared slot. */
	UFUNCTION(BlueprintCallable, Category = "Mini|Task18")
	static bool ConfigureMontage(UAnimMontage* Montage, UAnimSequence* Sequence, FName SlotName);

	UFUNCTION(BlueprintCallable, Category = "Mini|Task18")
	static bool VerifyMontage(const UAnimMontage* Montage, const UAnimSequence* Sequence, FName SlotName);
};
