#include "MiniHealthComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniDeathGameplayEffect.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "AbilitySystem/MiniSpawnProtectionGameplayEffect.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Feedback/MiniCombatFeedbackComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffectExtension.h"
#include "GameModes/MiniGameMode.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
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
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	if (Pawn && Pawn->HasAuthority() && ASC && ASC->GetAvatarActor() == Pawn)
	{
		if (AMiniPlayerState* PlayerState = Pawn->GetPlayerState<AMiniPlayerState>())
		{
			PlayerState->BeginLifeForPawn(Pawn);
		}
	}
	if (BoundAbilitySystem.Get() == ASC)
	{
		TryApplySpawnProtectionForCurrentLife();
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
		else
		{
			TryApplySpawnProtectionForCurrentLife();
		}
	}
}

void UMiniHealthComponent::UninitializeAbilitySystem()
{
	// A forced unpossess or destruction must not leave this Pawn's infinite death
	// effect on the persistent PlayerState ASC.
	RemoveDeathEffect();
	RemoveSpawnProtectionEffect();
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
	SynchronousDamageContexts.Reset();
}

void UMiniHealthComponent::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (GetOwner() && GetOwner()->HasAuthority() && ChangeData.NewValue <= 0.0f)
	{
		if (ChangeData.OldValue > 0.0f)
		{
			const FMiniPlayerDeathInfo DeathInfo = !SynchronousDamageContexts.IsEmpty()
				? SynchronousDamageContexts.Last()
				: ResolveDamageContext(ChangeData.GEModData ? ChangeData.GEModData->EffectSpec.GetContext() : FGameplayEffectContextHandle());
			StartDeath(&DeathInfo);
		}
		else
		{
			StartDeath();
		}
	}
}

FMiniPlayerDeathInfo UMiniHealthComponent::ResolveDamageContext(const FGameplayEffectContextHandle& Context) const
{
	FMiniPlayerDeathInfo Info;
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	AMiniPlayerState* VictimState = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	Info.VictimPawn = Pawn;
	Info.VictimPlayerState = VictimState;
	Info.VictimLifeId = VictimState ? VictimState->GetCurrentLifeId() : 0;
	AGameStateBase* GameState = Pawn && Pawn->GetWorld() ? Pawn->GetWorld()->GetGameState() : nullptr;
	const UMiniMatchRulesComponent* Match = GameState ? GameState->FindComponentByClass<UMiniMatchRulesComponent>() : nullptr;
	Info.RoundId = Match ? Match->GetRoundId() : 0;

	AMiniPlayerState* InstigatorState = Cast<AMiniPlayerState>(Context.GetInstigator());
	AMiniCharacter* SourcePawn = Cast<AMiniCharacter>(Context.GetEffectCauser());
	if (!SourcePawn)
	{
		SourcePawn = Cast<AMiniCharacter>(Context.GetSourceObject());
	}
	if (!InstigatorState)
	{
		// Resolve only the actor carried by this GE, never the source ASC's current Avatar.
		if (const AMiniCharacter* InstigatorPawn = Cast<AMiniCharacter>(Context.GetInstigator()))
		{
			InstigatorState = InstigatorPawn->GetPlayerState<AMiniPlayerState>();
		}
		if (!InstigatorState)
		{
			const UAbilitySystemComponent* SourceASC = Context.GetOriginalInstigatorAbilitySystemComponent();
			InstigatorState = SourceASC ? Cast<AMiniPlayerState>(SourceASC->GetOwnerActor()) : nullptr;
		}
	}
	Info.InstigatorPlayerState = InstigatorState;
	Info.InstigatorPawn = SourcePawn;
	Info.Cause = !InstigatorState ? EMiniPlayerDeathCause::Environment
		: InstigatorState == VictimState ? EMiniPlayerDeathCause::Suicide : EMiniPlayerDeathCause::Player;
	return Info;
}

void UMiniHealthComponent::BeginDamageContext(const FGameplayEffectContextHandle& Context)
{
	SynchronousDamageContexts.Add(ResolveDamageContext(Context));
}

void UMiniHealthComponent::EndDamageContext()
{
	if (!SynchronousDamageContexts.IsEmpty())
	{
		SynchronousDamageContexts.Pop(EAllowShrinking::No);
	}
}

void UMiniHealthComponent::StartDeath(const FMiniPlayerDeathInfo* DeathInfo)
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
	RemoveSpawnProtectionEffect();
	if (DeathInfo)
	{
		if (AMiniGameMode* GameMode = Pawn->GetWorld()->GetAuthGameMode<AMiniGameMode>())
		{
			GameMode->NotifyPlayerDeath(*DeathInfo);
		}
	}
	FGameplayCueParameters DeathCue;
	DeathCue.Location = Pawn->GetActorLocation();
	DeathCue.Instigator = Pawn;
	DeathCue.EffectCauser = Pawn;
	ASC->ExecuteGameplayCue(MiniGameplayTags::GameplayCue_Mini_Death, DeathCue);
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
	if (UMiniCombatFeedbackComponent* Feedback = Pawn->GetCombatFeedbackComponent())
	{
		Feedback->NotifyDeathState();
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

void UMiniHealthComponent::TryApplySpawnProtectionForCurrentLife()
{
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(GetOwner());
	UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get();
	AMiniPlayerState* PlayerState = Pawn ? Pawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	if (!Pawn || !Pawn->HasAuthority() || bDeathStarted || !ASC || ASC->GetAvatarActor() != Pawn ||
		!PlayerState || PlayerState->GetCurrentLifePawn() != Pawn || PlayerState->GetCurrentLifeId() == 0 ||
		SpawnProtectionGrantedLifeId == PlayerState->GetCurrentLifeId())
	{
		return;
	}
	AGameStateBase* GameState = Pawn->GetWorld()->GetGameState();
	const UMiniMatchRulesComponent* Match = GameState ? GameState->FindComponentByClass<UMiniMatchRulesComponent>() : nullptr;
	if (!Match || !Match->IsFFAConfigured() || !Match->CanRespawnPlayer(Pawn->GetController()))
	{
		return;
	}
	const float Duration = Match->GetSpawnProtectionSeconds();
	if (!FMath::IsFinite(Duration) || Duration <= 0.0f)
	{
		return;
	}
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(
		UMiniSpawnProtectionGameplayEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetDuration(Duration, true);
		SpawnProtectionEffectHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		if (SpawnProtectionEffectHandle.IsValid())
		{
			SpawnProtectionGrantedLifeId = PlayerState->GetCurrentLifeId();
			UE_LOG(LogMiniInit, Display, TEXT("MiniHealth SPAWN_PROTECTED: Pawn=%s Life=%u Duration=%.2f"),
				*Pawn->GetPathName(), SpawnProtectionGrantedLifeId, Duration);
		}
	}
}

void UMiniHealthComponent::RemoveSpawnProtectionEffect()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UMiniAbilitySystemComponent* ASC = BoundAbilitySystem.Get())
		{
			if (SpawnProtectionEffectHandle.IsValid())
			{
				ASC->RemoveActiveGameplayEffect(SpawnProtectionEffectHandle);
				SpawnProtectionEffectHandle.Invalidate();
			}
		}
	}
}
