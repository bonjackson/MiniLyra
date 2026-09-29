#include "MiniHealthComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniDeathGameplayEffect.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameModes/MiniGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

UMiniHealthComponent::UMiniHealthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

void UMiniHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeAbilitySystem();
	Super::EndPlay(EndPlayReason);
}

void UMiniHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMiniHealthComponent, bDeathStarted);
}

void UMiniHealthComponent::InitializeWithAbilitySystem(UMiniAbilitySystemComponent* ASC)
{
	if (BoundAbilitySystem.Get() == ASC)
	{
		return;
	}
	UninitializeAbilitySystem();
	if (!ASC || !ASC->GetSet<UMiniHealthSet>())
	{
		return;
	}
	BoundAbilitySystem = ASC;
	HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
		UMiniHealthSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const UMiniHealthSet* HealthSet = ASC->GetSet<UMiniHealthSet>();
		if (HealthSet && HealthSet->GetHealth() <= 0.0f)
		{
			StartDeath();
		}
	}
}

void UMiniHealthComponent::UninitializeAbilitySystem()
{
	// A forced unpossess or destruction must not leave this Pawn's infinite death
	// effect on the persistent PlayerState ASC.
	RemoveDeathEffect();
	if (UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
	{
		if (HealthChangedHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(
				UMiniHealthSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		}
	}
	HealthChangedHandle.Reset();
	BoundAbilitySystem.Reset();
}

void UMiniHealthComponent::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (GetOwner() && GetOwner()->HasAuthority() && ChangeData.NewValue <= 0.0f)
	{
		StartDeath();
	}
}

void UMiniHealthComponent::StartDeath()
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get();
	if (bDeathStarted || !Pawn || !Pawn->HasAuthority() || !ASC || ASC->GetAvatarActor() != Pawn ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead))
	{
		return;
	}
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UMiniDeathGameplayEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return;
	}
	DeathEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	if (!DeathEffectHandle.IsValid() || !ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead))
	{
		return;
	}
	bDeathStarted = true;
	// Revoke this life's weapon grants while the PlayerState ASC still has this Pawn as Avatar.
	if (AMiniPlayerController* Controller = Cast<AMiniPlayerController>(Pawn->GetController()))
	{
		if (UMiniQuickBarComponent* QuickBar = Controller->GetQuickBar())
		{
			QuickBar->HandlePawnLost(Pawn);
		}
	}
	if (UMiniEquipmentManagerComponent* Equipment = Pawn->GetEquipmentManager())
	{
		Equipment->UnequipItem();
	}
	Pawn->ForceNetUpdate();
	ApplyDeathPresentation();
	UE_LOG(LogMiniInit, Display, TEXT("MiniHealth DEATH_STARTED: Pawn=%s"), *Pawn->GetPathName());
	if (AMiniGameMode* GameMode = Pawn->GetWorld()->GetAuthGameMode<AMiniGameMode>())
	{
		GameMode->ScheduleRespawn(Pawn);
	}
}

void UMiniHealthComponent::ApplyDeathPresentation()
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (!Pawn || !bDeathStarted)
	{
		return;
	}
	Pawn->StopJumping();
	if (UCharacterMovementComponent* Movement = Pawn->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Pawn->SetActorEnableCollision(false);
	if (UMiniHeroComponent* Hero = Pawn->GetHeroComponent())
	{
		Hero->SetInputSuppressed(true);
	}
}

void UMiniHealthComponent::OnRep_DeathStarted()
{
	ApplyDeathPresentation();
}

void UMiniHealthComponent::RemoveDeathEffect()
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get();
	if (!Pawn || !Pawn->HasAuthority() || !ASC)
	{
		return;
	}
	if (DeathEffectHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(DeathEffectHandle);
		DeathEffectHandle.Invalidate();
	}
}
