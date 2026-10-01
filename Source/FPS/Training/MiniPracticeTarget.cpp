#include "MiniPracticeTarget.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "System/MiniLogChannels.h"
#include "Training/MiniPracticeTargetDefinition.h"
#include "Training/MiniPracticeTargetStatusWidget.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MiniPracticeTarget"

AMiniPracticeTarget::AMiniPracticeTarget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(30.0f);
	PrimaryActorTick.bCanEverTick = false;
	HitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HitBox"));
	SetRootComponent(HitBox);
	HitBox->SetBoxExtent(FVector(12.0f, 65.0f, 75.0f));
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HitBox->SetCollisionObjectType(ECC_WorldDynamic);
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	HitBox->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	HitBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	HitBox->SetGenerateOverlapEvents(false);
	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	BoardMesh->SetupAttachment(HitBox);
	BoardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoardMesh->SetRelativeScale3D(FVector(0.24f, 1.30f, 1.50f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) { BoardMesh->SetStaticMesh(Cube.Object); }
	StatusLabel = CreateDefaultSubobject<UWidgetComponent>(TEXT("StatusLabel"));
	StatusLabel->SetupAttachment(HitBox);
	StatusLabel->SetRelativeLocation(FVector(-20.0f, 0.0f, 100.0f));
	StatusLabel->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	StatusLabel->SetRelativeScale3D(FVector(0.25f));
	StatusLabel->SetWidgetSpace(EWidgetSpace::World);
	StatusLabel->SetWidgetClass(UMiniPracticeTargetStatusWidget::StaticClass());
	StatusLabel->SetDrawSize(FVector2D(500.0f, 180.0f));
	StatusLabel->SetPivot(FVector2D(0.5f, 0.5f));
	StatusLabel->SetTwoSided(true);
	StatusLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AbilitySystemComponent = CreateDefaultSubobject<UMiniAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	// There is no autonomous owning player for a static world Actor. Attributes
	// replicate to everyone; no player-oriented Mixed effect ownership is needed.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	HealthSet = CreateDefaultSubobject<UMiniHealthSet>(TEXT("HealthSet"));
}

void AMiniPracticeTarget::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (AbilitySystemComponent && HealthSet)
	{
		AbilitySystemComponent->AddAttributeSetSubobject(HealthSet.Get());
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

const UMiniPracticeTargetDefinition* AMiniPracticeTarget::GetDefinition() const
{
	return TargetDefinition ? TargetDefinition.Get() : GetDefault<UMiniPracticeTargetDefinition>();
}

UAbilitySystemComponent* AMiniPracticeTarget::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AMiniPracticeTarget::BeginPlay()
{
	Super::BeginPlay();
	const UMiniPracticeTargetDefinition* Definition = GetDefinition();
	if (GetNetMode() != NM_DedicatedServer)
	{
		StatusLabel->InitWidget();
		InitializeVisualMaterial();
	}
	if (HasAuthority() && AbilitySystemComponent && HealthSet)
	{
		FString Error;
		if (!Definition->ValidateDefinition(Error))
		{
			UE_LOG(LogMiniInit, Error, TEXT("MiniPracticeTarget INVALID_DEFINITION: Target=%s Reason=%s"), *GetPathName(), *Error);
			TargetState.bEnabled = false;
			AbilitySystemComponent->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), 0.0f);
			PublishTargetState();
			return;
		}
		HealthChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UMiniHealthSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
		MaxHealthChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UMiniHealthSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::HandleMaxHealthChanged);
		TargetState.bEnabled = true;
		AbilitySystemComponent->SetNumericAttributeBase(UMiniHealthSet::GetMaxHealthAttribute(), Definition->MaxHealth);
		AbilitySystemComponent->SetNumericAttributeBase(UMiniHealthSet::GetIncomingDamageAttribute(), 0.0f);
		AbilitySystemComponent->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), Definition->MaxHealth);
		PublishTargetState();
		UE_LOG(LogMiniInit, Display, TEXT("MiniPracticeTarget READY: Target=%s Health=%.1f ResetDelay=%.2f OwnerIsSelf=1 AvatarIsSelf=1"),
			*GetPathName(), TargetState.Health, Definition->ResetDelay);
	}
	RefreshPresentation();
}

bool AMiniPracticeTarget::CanReceiveDamage() const
{
	return HasAuthority() && !bEndingPlay && HasActorBegunPlay() && IsTargetEnabled() &&
		AbilitySystemComponent && HealthSet && AbilitySystemComponent->GetOwnerActor() == this &&
		AbilitySystemComponent->GetAvatarActor() == this &&
		AbilitySystemComponent->GetSet<UMiniHealthSet>() == HealthSet.Get() && HealthSet->GetHealth() > 0.0f;
}

void AMiniPracticeTarget::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (!HasAuthority() || bEndingPlay) { return; }
	if (TargetState.bEnabled && ChangeData.NewValue <= 0.0f)
	{
		TargetState.bEnabled = false;
		++TargetState.DisableCount;
		// Keep the board blocking Visibility/Pawn during reset. Later accepted
		// shots consume ammo and impact the board, but cannot repeatedly damage it.
		GetWorldTimerManager().SetTimer(ResetTimerHandle, this, &ThisClass::ResetTarget,
			GetDefinition()->ResetDelay, false);
		UE_LOG(LogMiniInit, Display, TEXT("MiniPracticeTarget DISABLED: Target=%s Count=%d PlayerDeath=0 ResetDelay=%.2f"),
			*GetPathName(), TargetState.DisableCount, GetDefinition()->ResetDelay);
	}
	PublishTargetState();
}

void AMiniPracticeTarget::HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (HasAuthority() && !bEndingPlay) { PublishTargetState(); }
}

void AMiniPracticeTarget::PublishTargetState()
{
	if (!HasAuthority() || bEndingPlay || !HealthSet) { return; }
	TargetState.Health = HealthSet->GetHealth();
	TargetState.MaxHealth = HealthSet->GetMaxHealth();
	++TargetState.Revision;
	ForceNetUpdate();
	RefreshPresentation();
}

void AMiniPracticeTarget::ResetTarget()
{
	ResetTimerHandle.Invalidate();
	if (!HasAuthority() || bEndingPlay || !AbilitySystemComponent || !HealthSet || TargetState.bEnabled) { return; }
	FString Error;
	if (!GetDefinition()->ValidateDefinition(Error)) { return; }
	TargetState.bEnabled = true;
	++TargetState.ResetCount;
	AbilitySystemComponent->SetNumericAttributeBase(UMiniHealthSet::GetIncomingDamageAttribute(), 0.0f);
	AbilitySystemComponent->SetNumericAttributeBase(UMiniHealthSet::GetMaxHealthAttribute(), GetDefinition()->MaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), GetDefinition()->MaxHealth);
	PublishTargetState();
	UE_LOG(LogMiniInit, Display, TEXT("MiniPracticeTarget RESET: Target=%s Count=%d Health=%.1f Enabled=1"),
		*GetPathName(), TargetState.ResetCount, TargetState.Health);
}

void AMiniPracticeTarget::RefreshPresentation()
{
	if (bEndingPlay || GetNetMode() == NM_DedicatedServer || !StatusLabel) { return; }
	const UMiniPracticeTargetDefinition* Definition = GetDefinition();
	const FLinearColor Color = TargetState.bEnabled ? Definition->ActiveColor : Definition->DisabledColor;
	if (StateMaterial) { StateMaterial->SetVectorParameterValue(TEXT("TargetColor"), Color); }
	if (UMiniPracticeTargetStatusWidget* Widget = GetStatusWidget())
	{
		Widget->SetTargetState(Definition->DisplayName, TargetState, Definition->ResetDelay, Color);
	}
}

UMiniPracticeTargetStatusWidget* AMiniPracticeTarget::GetStatusWidget() const
{
	return StatusLabel ? Cast<UMiniPracticeTargetStatusWidget>(StatusLabel->GetUserWidgetObject()) : nullptr;
}

void AMiniPracticeTarget::OnRep_TargetState()
{
	RefreshPresentation();
}

void AMiniPracticeTarget::InitializeVisualMaterial()
{
	if (bEndingPlay || GetNetMode() == NM_DedicatedServer || !BoardMesh || GetDefinition()->BoardMaterial.IsNull()) { return; }
	if (UMaterialInterface* Material = GetDefinition()->BoardMaterial.LoadSynchronous())
	{
		StateMaterial = BoardMesh->CreateDynamicMaterialInstance(0, Material);
	}
}

void AMiniPracticeTarget::OnRep_TargetDefinition()
{
	if (HasActorBegunPlay()) { InitializeVisualMaterial(); }
	RefreshPresentation();
}

void AMiniPracticeTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, TargetState);
	DOREPLIFETIME(ThisClass, TargetDefinition);
}

void AMiniPracticeTarget::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ResetTimerHandle); }
	ResetTimerHandle.Invalidate();
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UMiniHealthSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UMiniHealthSet::GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
		AbilitySystemComponent->ClearActorInfo();
	}
	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	StateMaterial = nullptr;
	if (StatusLabel) { StatusLabel->SetWidget(nullptr); }
	// Board/WidgetComponent are default subobjects owned and destroyed by this Actor.
	Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
