#include "MiniCharacter.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Camera/MiniCameraComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniPawnData.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Feedback/MiniCombatFeedbackComponent.h"
#include "Weapons/MiniRangedWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerState.h"
#include "Player/MiniPlayerController.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "System/MiniLogChannels.h"

AMiniCharacter::AMiniCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	PawnExtensionComponent = CreateDefaultSubobject<UMiniPawnExtensionComponent>(TEXT("PawnExtension"));
	HeroComponent = CreateDefaultSubobject<UMiniHeroComponent>(TEXT("Hero"));
	CameraComponent = CreateDefaultSubobject<UMiniCameraComponent>(TEXT("MiniCamera"));
	CameraComponent->SetupAttachment(GetRootComponent());
	HealthComponent = CreateDefaultSubobject<UMiniHealthComponent>(TEXT("Health"));
	EquipmentManager = CreateDefaultSubobject<UMiniEquipmentManagerComponent>(TEXT("EquipmentManager"));
	RangedWeaponComponent = CreateDefaultSubobject<UMiniRangedWeaponComponent>(TEXT("RangedWeapon"));
	CombatFeedbackComponent = CreateDefaultSubobject<UMiniCombatFeedbackComponent>(TEXT("CombatFeedback"));
	// Pawn's stock profile ignores Visibility. Weapon traces must hit live capsules.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PracticeRifleMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PracticeRifle"));
	PracticeRifleMesh->SetupAttachment(GetMesh(), TEXT("HandGrip_R"));
	PracticeRifleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
#if !UE_BUILD_SHIPPING
	FString ProbeOrder;
	if (FParse::Value(FCommandLine::Get(), TEXT("MiniProbeInitOrder="), ProbeOrder) &&
		(ProbeOrder == TEXT("DataFirst") || ProbeOrder == TEXT("PlayerStateFirst")))
	{
		bInitOrderProbeEnabled = true;
		bInitProbePawnDataVisible = false;
		bInitProbePlayerStateVisible = false;
	}
#endif
}

void AMiniCharacter::HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag,
	EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters)
{
	if (CombatFeedbackComponent)
	{
		CombatFeedbackComponent->HandleCombatCue(GameplayCueTag, EventType, Parameters);
	}
}

const UMiniPawnData* AMiniCharacter::GetPawnDataForInitialization() const
{
	return bInitProbePawnDataVisible ? PawnData.Get() : nullptr;
}

AMiniPlayerState* AMiniCharacter::GetPlayerStateForInitialization() const
{
	return bInitProbePlayerStateVisible ? GetPlayerState<AMiniPlayerState>() : nullptr;
}

void AMiniCharacter::ReleaseInitProbePawnData()
{
	if (bInitOrderProbeEnabled && !bInitProbePawnDataVisible)
	{
		bInitProbePawnDataVisible = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniInitProbe RELEASE: Dependency=PawnData Pawn=%s"), *GetPathName());
		NotifyInitDependenciesChanged();
	}
}

void AMiniCharacter::ReleaseInitProbePlayerState()
{
	if (bInitOrderProbeEnabled && !bInitProbePlayerStateVisible)
	{
		bInitProbePlayerStateVisible = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniInitProbe RELEASE: Dependency=PlayerState Pawn=%s"), *GetPathName());
		NotifyInitDependenciesChanged();
	}
}

void AMiniCharacter::BeginPlay()
{
	Super::BeginPlay();
	RefreshEquipmentAppearance();
	UE_LOG(LogMiniInit, Display, TEXT("MiniCharacter BeginPlay Role=%d Pawn=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
	NotifyInitDependenciesChanged();
}

void AMiniCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && EquipmentManager)
	{
		EquipmentManager->UnequipItem();
	}
	if (HealthComponent)
	{
		HealthComponent->UninitializeAbilitySystem();
	}
	if (PawnExtensionComponent)
	{
		PawnExtensionComponent->UninitializeAbilitySystem(true);
	}
	Super::EndPlay(EndPlayReason);
}

void AMiniCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (CameraComponent)
	{
		CameraComponent->ResetCamera();
	}
	if (PawnExtensionComponent)
	{
		PawnExtensionComponent->NotifyPawnPossessed();
	}
	NotifyInitDependenciesChanged();
}

void AMiniCharacter::UnPossessed()
{
	if (HasAuthority())
	{
		if (AMiniPlayerController* MiniController = Cast<AMiniPlayerController>(GetController()))
		{
			if (UMiniQuickBarComponent* QuickBar = MiniController->GetQuickBar())
			{
				QuickBar->HandlePawnLost(this);
			}
		}
		if (EquipmentManager)
		{
			EquipmentManager->UnequipItem();
		}
	}
	Super::UnPossessed();
	if (HealthComponent)
	{
		HealthComponent->UninitializeAbilitySystem();
	}
	if (CameraComponent)
	{
		CameraComponent->ResetCamera();
	}
	if (PawnExtensionComponent)
	{
		PawnExtensionComponent->UninitializeAbilitySystem(true);
	}
	if (HeroComponent)
	{
		HeroComponent->NotifyPawnUnpossessed();
	}
	NotifyInitDependenciesChanged();
}

void AMiniCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	if (CameraComponent)
	{
		CameraComponent->ResetCamera();
	}
	if (HeroComponent && HeroComponent->IsInputActive() && !IsLocallyControlled())
	{
		HeroComponent->NotifyPawnUnpossessed();
	}
	if (PawnExtensionComponent)
	{
		PawnExtensionComponent->NotifyPawnPossessed();
	}
	NotifyInitDependenciesChanged();
}

void AMiniCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	NotifyInitDependenciesChanged();
}

void AMiniCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	NotifyInitDependenciesChanged();
}

void AMiniCharacter::NotifyInitDependenciesChanged()
{
	if (PawnExtensionComponent)
	{
		PawnExtensionComponent->CheckDefaultInitialization();
		PawnExtensionComponent->RefreshAbilitySystem();
		PawnExtensionComponent->CheckDefaultInitialization();
	}
	if (HealthComponent)
	{
		AMiniPlayerState* MiniState = GetPlayerState<AMiniPlayerState>();
		UMiniAbilitySystemComponent* ASC = MiniState ? MiniState->GetMiniAbilitySystemComponent() : nullptr;
		if (ASC && ASC->GetAvatarActor() == this)
		{
			HealthComponent->InitializeWithAbilitySystem(ASC);
		}
		else
		{
			HealthComponent->UninitializeAbilitySystem();
		}
	}
	if (HeroComponent)
	{
		HeroComponent->CheckDefaultInitialization();
		HeroComponent->NotifyInputDependenciesChanged();
	}
	if (HasAuthority())
	{
		if (AMiniPlayerController* MiniController = Cast<AMiniPlayerController>(GetController()))
		{
			if (UMiniQuickBarComponent* QuickBar = MiniController->GetQuickBar())
			{
				QuickBar->InitializeForPawn(this);
			}
		}
	}
	if (CombatFeedbackComponent)
	{
		CombatFeedbackComponent->RefreshPendingReload();
	}
}

void AMiniCharacter::RefreshEquipmentAppearance()
{
	if (!PracticeRifleMesh || !EquipmentManager)
	{
		return;
	}
	const TSubclassOf<UMiniEquipmentDefinition> DefinitionClass = EquipmentManager->GetCurrentDefinitionClass();
	const UMiniEquipmentDefinition* Definition = DefinitionClass
		? GetDefault<UMiniEquipmentDefinition>(DefinitionClass) : nullptr;
	UMiniEquipmentInstance* Equipment = EquipmentManager->GetCurrentEquipment();
	if (AppearanceEquipment.Get() != Equipment)
	{
		if (CombatFeedbackComponent)
		{
			CombatFeedbackComponent->HandleEquipmentChanged();
		}
		AppearanceEquipment = Equipment;
	}
	USkeletalMesh* WeaponMesh = Definition ? Definition->GetWeaponMesh() : nullptr;
	if (PracticeRifleMesh->GetSkeletalMeshAsset() != WeaponMesh)
	{
		PracticeRifleMesh->SetSkeletalMesh(WeaponMesh);
	}
	UClass* AnimClass = WeaponMesh && Definition ? Definition->GetWeaponAnimClass() : nullptr;
	if (PracticeRifleMesh->GetAnimClass() != AnimClass)
	{
		PracticeRifleMesh->SetAnimInstanceClass(AnimClass);
	}
	PracticeRifleMesh->SetVisibility(WeaponMesh != nullptr);
	if (CombatFeedbackComponent)
	{
		CombatFeedbackComponent->RefreshPendingReload();
	}
}

void AMiniCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMiniCharacter, PawnData);
}

bool AMiniCharacter::SetPawnData(const UMiniPawnData* InPawnData)
{
	if (!HasAuthority() || !InPawnData)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=AuthorityOrNull Pawn=%s"), *GetPathName());
		return false;
	}
	if (PawnData)
	{
		if (PawnData == InPawnData)
		{
			return true;
		}
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=AlreadySet Pawn=%s Current=%s Requested=%s"),
			*GetPathName(), *GetPathNameSafe(PawnData.Get()), *GetPathNameSafe(InPawnData));
		return false;
	}
	if (HasActorBegunPlay())
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=AfterBeginPlay Pawn=%s"), *GetPathName());
		return false;
	}
	FString Error;
	if (!InPawnData->ValidatePawnData(Error))
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniCharacter PawnDataRejected Reason=%s Pawn=%s"), *Error, *GetPathName());
		return false;
	}

	PawnData = InPawnData;
	ForceNetUpdate();
	NotifyInitDependenciesChanged();
	UE_LOG(LogMiniInit, Display, TEXT("MiniCharacter PawnDataAssigned Role=%d Pawn=%s PawnData=%s BeforeBeginPlay=1"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
	return true;
}

void AMiniCharacter::OnRep_PawnData()
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniCharacter PawnDataReplicated Role=%d Pawn=%s PawnData=%s"),
		static_cast<int32>(GetLocalRole()), *GetPathName(), *GetPathNameSafe(PawnData.Get()));
	NotifyInitDependenciesChanged();
}
