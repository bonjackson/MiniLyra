#pragma once

#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "MiniPracticeSupply.generated.h"

class USphereComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UWidgetComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AMiniPlayerController;

UCLASS()
class FPS_API UMiniPracticeSupplyWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
};

/** A server-owned practice feature. Its overlap supplies existing items only. */
UCLASS()
class FPS_API AMiniPracticeSupply : public AActor
{
	GENERATED_BODY()
public:
	AMiniPracticeSupply();
	bool TryRefill(AMiniPlayerController* Controller);
	USphereComponent* GetSupplyArea() const { return SupplyArea; }
	int32 GetRefillCount() const { return RefillCount; }

	UPROPERTY(EditAnywhere, Category = "Mini|Practice", meta = (ClampMin = "50", ClampMax = "500"))
	float SupplyRadius = 220.0f;
	UPROPERTY(EditAnywhere, Category = "Mini|Practice")
	TSoftObjectPtr<UMaterialInterface> StationMaterial;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	void RefillOverlappingPlayers();
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> SupplyArea;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Pad;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWidgetComponent> Label;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PadMaterial;
	FTimerHandle RefillTimer;
	int32 RefillCount = 0;
};
