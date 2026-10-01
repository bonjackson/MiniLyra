#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "MiniPracticeTarget.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UWidgetComponent;
class UMiniAbilitySystemComponent;
class UMiniHealthSet;
class UMiniPracticeTargetDefinition;
class UMiniPracticeTargetStatusWidget;
struct FOnAttributeChangeData;

/** One replicated presentation snapshot avoids health/enable replication-order flicker. */
USTRUCT(BlueprintType)
struct FPS_API FMiniPracticeTargetState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float Health = 0.0f;
	UPROPERTY(BlueprintReadOnly) float MaxHealth = 100.0f;
	UPROPERTY(BlueprintReadOnly) bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly) int32 DisableCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 ResetCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 Revision = 0;
};

/** Static replicated Actor target: its own ASC is both owner and avatar. */
UCLASS(Blueprintable)
class FPS_API AMiniPracticeTarget : public AActor, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AMiniPracticeTarget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UMiniAbilitySystemComponent* GetMiniAbilitySystemComponent() const { return AbilitySystemComponent; }
	const UMiniHealthSet* GetHealthSet() const { return HealthSet; }
	const FMiniPracticeTargetState& GetTargetState() const { return TargetState; }
	bool IsTargetEnabled() const { return TargetState.bEnabled && TargetState.Health > 0.0f; }
	/** Checked by the GameMode immediately before applying the ordinary damage GE. */
	bool CanReceiveDamage() const;
	UMiniPracticeTargetStatusWidget* GetStatusWidget() const;

	/** Configure on a Blueprint class or before FinishSpawningActor. Null uses native defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_TargetDefinition, Category = "Mini|Target")
	TObjectPtr<UMiniPracticeTargetDefinition> TargetDefinition;

private:
	void HandleHealthChanged(const FOnAttributeChangeData& ChangeData);
	void HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData);
	void PublishTargetState();
	void ResetTarget();
	void RefreshPresentation();
	void InitializeVisualMaterial();
	const UMiniPracticeTargetDefinition* GetDefinition() const;
	UFUNCTION()
	void OnRep_TargetState();
	UFUNCTION()
	void OnRep_TargetDefinition();

	UPROPERTY(VisibleAnywhere, Category = "Mini|Target") TObjectPtr<UBoxComponent> HitBox;
	UPROPERTY(VisibleAnywhere, Category = "Mini|Target") TObjectPtr<UStaticMeshComponent> BoardMesh;
	UPROPERTY(VisibleAnywhere, Category = "Mini|Target") TObjectPtr<UWidgetComponent> StatusLabel;
	UPROPERTY(VisibleAnywhere, Category = "Mini|Target") TObjectPtr<UMiniAbilitySystemComponent> AbilitySystemComponent;
	UPROPERTY(VisibleAnywhere, Category = "Mini|Target") TObjectPtr<UMiniHealthSet> HealthSet;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StateMaterial;
	UPROPERTY(ReplicatedUsing = OnRep_TargetState) FMiniPracticeTargetState TargetState;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;
	FTimerHandle ResetTimerHandle;
	bool bEndingPlay = false;
};
