#include "MiniPlayerController.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniProbeAbility.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnExtensionComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFeatures/MiniGameFeatureAction_AddInput.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGameState.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Input/MiniPlayerInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

namespace
{
UMiniGameFeatureAction_AddInput* FindTask10Action(const UWorld* World)
{
	const AMiniGameState* GameState = World ? World->GetGameState<AMiniGameState>() : nullptr;
	const UMiniExperienceManagerComponent* Manager = GameState ? GameState->GetExperienceManagerComponent() : nullptr;
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	if (Experience)
	{
		for (UGameFeatureAction* Candidate : Experience->Actions)
		{
			if (Candidate && Candidate->GetFName() == TEXT("MiniTask10_AddInput"))
			{
				return Cast<UMiniGameFeatureAction_AddInput>(Candidate);
			}
		}
	}
	return nullptr;
}

int32 GetMappingRegistrations(const AMiniPlayerController* Controller, const UInputMappingContext* Mapping)
{
	const UMiniPlayerInput* PlayerInput = Controller ? Cast<UMiniPlayerInput>(Controller->PlayerInput) : nullptr;
	return PlayerInput ? PlayerInput->GetMappingRegistrationCount(Mapping) : 0;
}

bool IsFireAbilityActive(const UMiniAbilitySystemComponent* ASC)
{
	if (ASC)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->IsA<UMiniPawnProbeAbility>())
			{
				return Spec.IsActive();
			}
		}
	}
	return false;
}

void SendProbeKey(AMiniPlayerController* Controller, const FKey& Key, EInputEvent Event)
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}
}

void AMiniPlayerController::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bTask10ProbeEnabled = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask10"));
	if (bTask10ProbeEnabled && GetNetMode() == NM_Client && IsLocalController())
	{
		GetWorldTimerManager().SetTimer(Task10ProbeTimer, this, &ThisClass::AdvanceTask10Probe, 0.25f, true);
	}
#endif
}

void AMiniPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(Task10ProbeTimer);
	GetWorldTimerManager().ClearTimer(Task10RespawnTimer);
	Super::EndPlay(EndPlayReason);
}

void AMiniPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (AMiniCharacter* MiniPawn = Cast<AMiniCharacter>(GetPawn()))
	{
		MiniPawn->NotifyInitDependenciesChanged();
	}
}

void AMiniPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	Super::PostProcessInput(DeltaTime, bGamePaused);
	if (!IsLocalController())
	{
		return;
	}
	AMiniPlayerState* MiniPlayerState = GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* ASC = MiniPlayerState ? MiniPlayerState->GetMiniAbilitySystemComponent() : nullptr;
	AMiniCharacter* MiniPawn = Cast<AMiniCharacter>(GetPawn());
	UMiniHeroComponent* Hero = MiniPawn ? MiniPawn->GetHeroComponent() : nullptr;
	if (ASC)
	{
		if (bMiniInputBlocked || !MiniPawn || !Hero || !Hero->IsInputActive() ||
			ASC->GetAvatarActor() != MiniPawn)
		{
			ASC->ClearAbilityInput();
		}
		else
		{
			ASC->ProcessAbilityInput(DeltaTime, bGamePaused);
		}
	}
}

void AMiniPlayerController::SetMiniInputBlocked(bool bBlocked)
{
	if (bMiniInputBlocked == bBlocked)
	{
		return;
	}
	bMiniInputBlocked = bBlocked;
	if (AMiniPlayerState* MiniPlayerState = GetPlayerState<AMiniPlayerState>())
	{
		if (UMiniAbilitySystemComponent* ASC = MiniPlayerState->GetMiniAbilitySystemComponent())
		{
			ASC->ClearAbilityInput();
		}
	}
	if (AMiniCharacter* MiniPawn = Cast<AMiniCharacter>(GetPawn()))
	{
		if (UMiniHeroComponent* Hero = MiniPawn->GetHeroComponent())
		{
			Hero->SetInputSuppressed(bBlocked);
		}
	}
}

void AMiniPlayerController::ServerAdvanceTask10Probe_Implementation(int32 CompletedCycle)
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask10")) || IsLocalController() ||
		CompletedCycle != Task10ServerCompletedCycle + 1 || CompletedCycle > 3)
	{
		return;
	}
	AMiniCharacter* PreviousMiniPawn = Cast<AMiniCharacter>(GetPawn());
	AMiniGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMiniGameMode>() : nullptr;
	if (!PreviousMiniPawn || !GameMode)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniInputProbe RESPAWN_FAIL: Cycle=%d"), CompletedCycle);
		return;
	}
	Task10ServerCompletedCycle = CompletedCycle;
	Task10ServerOldPawn = PreviousMiniPawn;
	UnPossess();
	// Leave the unpossessed Pawn replicated long enough to prove client-side cleanup
	// happens before EndPlay or the replacement Pawn can remove its mapping.
	GetWorldTimerManager().SetTimer(Task10RespawnTimer, this, &ThisClass::FinishTask10ProbeRespawn, 1.25f, false);
	UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe SERVER_UNPOSSESSED: Cycle=%d OldPawn=%s"),
		CompletedCycle, *PreviousMiniPawn->GetPathName());
#endif
}

void AMiniPlayerController::FinishTask10ProbeRespawn()
{
#if !UE_BUILD_SHIPPING
	AMiniCharacter* PreviousMiniPawn = Task10ServerOldPawn.Get();
	AMiniGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AMiniGameMode>() : nullptr;
	if (!PreviousMiniPawn || !GameMode)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniInputProbe RESPAWN_FAIL: Cycle=%d"), Task10ServerCompletedCycle);
		return;
	}
	GameMode->RestartPlayer(this);
	UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe SERVER_RESPAWN: Cycle=%d OldPawn=%s NewPawn=%s"),
		Task10ServerCompletedCycle, *GetPathNameSafe(PreviousMiniPawn), *GetPathNameSafe(GetPawn()));
	PreviousMiniPawn->Destroy();
	Task10ServerOldPawn.Reset();
#endif
}

void AMiniPlayerController::AdvanceTask10Probe()
{
#if !UE_BUILD_SHIPPING
	AMiniCharacter* MiniPawn = Cast<AMiniCharacter>(GetPawn());
	AMiniPlayerState* MiniState = GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* ASC = MiniState ? MiniState->GetMiniAbilitySystemComponent() : nullptr;
	UMiniHeroComponent* Hero = MiniPawn ? MiniPawn->GetHeroComponent() : nullptr;
	UMiniGameFeatureAction_AddInput* Action = FindTask10Action(GetWorld());
	const UInputMappingContext* Mapping = Action ? Action->MappingContext.Get() : nullptr;
	if (Task10ProbeStage == ETask10ProbeStage::WaitUnpossessed)
	{
		const AMiniCharacter* OldMiniPawn = Task10ProbePawn.Get();
		const UMiniHeroComponent* OldHero = OldMiniPawn ? OldMiniPawn->GetHeroComponent() : nullptr;
		if (OldMiniPawn && OldHero && !OldMiniPawn->IsLocallyControlled() &&
			!OldHero->IsInputActive() && OldHero->GetInputBindingCount() == 0 &&
			GetMappingRegistrations(this, Mapping) == 0)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe UNPOSSESSED_CLEAN: Cycle=%d Pawn=%s"),
				Task10ProbeCycle, *OldMiniPawn->GetPathName());
			Task10ProbeStage = ETask10ProbeStage::WaitNextPawn;
		}
		return;
	}
	if (!MiniPawn || !Hero || !ASC || !Mapping)
	{
		return;
	}
	const int32 Registrations = GetMappingRegistrations(this, Mapping);
	const bool bFireActive = IsFireAbilityActive(ASC);
	const bool bHeld = ASC->GetHeldInputCount() > 0;

	switch (Task10ProbeStage)
	{
	case ETask10ProbeStage::WaitReady:
	{
		if (MiniPawn == Task10ProbePawn.Get() || !Hero->IsInputActive() ||
			!Hero->HasReachedInitState(MiniGameplayTags::InitState_GameplayReady) ||
			Registrations != 1 || Hero->GetInputBindingCount() != 16)
		{
			break;
		}
		int32 Simulated = 0;
		for (TActorIterator<AMiniCharacter> It(GetWorld()); It; ++It)
		{
			if (It->GetLocalRole() == ROLE_SimulatedProxy)
			{
				++Simulated;
				const UMiniHeroComponent* SimHero = It->GetHeroComponent();
				if (!SimHero || SimHero->GetInputBindingCount() != 0 || SimHero->OwnsInputMapping() ||
					It->GetPlayerInputComponent())
				{
				UE_LOG(LogMiniInit, Error, TEXT("MiniInputProbe SIMULATED_FAIL: Pawn=%s"), *It->GetPathName());
				Task10ProbeStage = ETask10ProbeStage::Complete;
				return;
				}
			}
		}
		Task10ProbePawn = MiniPawn;
		++Task10ProbeCycle;
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniInputProbe CYCLE_READY: Cycle=%d Pawn=%s Registrations=%d Bindings=%d Simulated=%d"),
			Task10ProbeCycle, *MiniPawn->GetPathName(), Registrations, Hero->GetInputBindingCount(), Simulated);
		if (Task10ProbeCycle == 1)
		{
			Task10ProbeStartLocation = MiniPawn->GetActorLocation();
			SendProbeKey(this, EKeys::W, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitMove;
		}
		else
		{
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitFireActive;
		}
		break;
	}
	case ETask10ProbeStage::WaitMove:
		if (FVector::Dist2D(Task10ProbeStartLocation, MiniPawn->GetActorLocation()) >= 10.0f)
		{
			SendProbeKey(this, EKeys::W, IE_Released);
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe MOVE_PASS: Pawn=%s Distance=%.1f"),
				*MiniPawn->GetPathName(), FVector::Dist2D(Task10ProbeStartLocation, MiniPawn->GetActorLocation()));
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitFireActive;
		}
		break;
	case ETask10ProbeStage::WaitFireActive:
		if (bFireActive && bHeld)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe FIRE_HELD: Cycle=%d Pawn=%s"),
				Task10ProbeCycle, *MiniPawn->GetPathName());
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Released);
			Task10ProbeStage = ETask10ProbeStage::WaitFireEnded;
		}
		break;
	case ETask10ProbeStage::WaitFireEnded:
		if (!bFireActive && !bHeld)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe FIRE_ENDED: Cycle=%d Pawn=%s"),
				Task10ProbeCycle, *MiniPawn->GetPathName());
			if (Task10ProbeCycle <= 3)
			{
				if (Task10ProbeCycle == 3)
				{
					SetMiniInputBlocked(true);
				}
				ServerAdvanceTask10Probe(Task10ProbeCycle);
				Task10ProbeStage = ETask10ProbeStage::WaitUnpossessed;
			}
			else
			{
				SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
				Task10ProbeStage = ETask10ProbeStage::WaitMenuFireActive;
			}
		}
		break;
	case ETask10ProbeStage::WaitNextPawn:
		if (MiniPawn != Task10ProbePawn.Get() && ASC->GetAvatarActor() == MiniPawn &&
			Hero->HasReachedInitState(MiniGameplayTags::InitState_DataInitialized))
		{
			if (Task10ProbeCycle == 3)
			{
				if (Hero->IsInputActive() || Hero->GetInputBindingCount() != 0 || Registrations != 0 ||
					Hero->HasReachedInitState(MiniGameplayTags::InitState_GameplayReady))
				{
					UE_LOG(LogMiniInit, Error, TEXT("MiniInputProbe BLOCKED_RESPAWN_FAIL: Pawn=%s"),
						*MiniPawn->GetPathName());
					Task10ProbeStage = ETask10ProbeStage::Complete;
					return;
				}
				UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe BLOCKED_RESPAWN: Pawn=%s Registrations=0 Bindings=0"),
					*MiniPawn->GetPathName());
				SetMiniInputBlocked(false);
			}
			Task10ProbeStage = ETask10ProbeStage::WaitReady;
		}
		break;
	case ETask10ProbeStage::WaitMenuFireActive:
		if (bFireActive && bHeld)
		{
			// The normal zero hold time clears bPressedJump automatically each tick.
			// Keep it observable here so unbinding must explicitly stop a held jump.
			Task10ProbeOriginalJumpMaxHoldTime = MiniPawn->JumpMaxHoldTime;
			MiniPawn->JumpMaxHoldTime = 5.0f;
			SendProbeKey(this, EKeys::SpaceBar, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitMenuJumpActive;
		}
		break;
	case ETask10ProbeStage::WaitMenuJumpActive:
		if (MiniPawn->bPressedJump)
		{
			SetMiniInputBlocked(true);
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Released);
			Task10ProbeStage = ETask10ProbeStage::WaitMenuBlocked;
		}
		break;
	case ETask10ProbeStage::WaitMenuBlocked:
		if (!bFireActive && !bHeld && !MiniPawn->bPressedJump &&
			Registrations == 0 && Hero->GetInputBindingCount() == 0)
		{
			SendProbeKey(this, EKeys::SpaceBar, IE_Released);
			MiniPawn->JumpMaxHoldTime = Task10ProbeOriginalJumpMaxHoldTime;
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe JUMP_CLEARED: Pawn=%s"), *MiniPawn->GetPathName());
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe MENU_BLOCKED: Pawn=%s"), *MiniPawn->GetPathName());
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitMenuNoFire;
		}
		break;
	case ETask10ProbeStage::WaitMenuNoFire:
		if (!bFireActive && !bHeld && Registrations == 0)
		{
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Released);
			SetMiniInputBlocked(false);
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe MENU_NO_FIRE: Pawn=%s"), *MiniPawn->GetPathName());
			Task10ProbeStage = ETask10ProbeStage::WaitMenuResumed;
		}
		break;
	case ETask10ProbeStage::WaitMenuResumed:
		if (Hero->IsInputActive() && Registrations == 1 && Hero->GetInputBindingCount() == 16)
		{
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitMenuReactivated;
		}
		break;
	case ETask10ProbeStage::WaitMenuReactivated:
		if (bFireActive)
		{
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Released);
			Task10ProbeStage = ETask10ProbeStage::WaitMenuReleased;
		}
		break;
	case ETask10ProbeStage::WaitMenuReleased:
		if (!bFireActive && !bHeld)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe MENU_RESTORED: Pawn=%s"), *MiniPawn->GetPathName());
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitActionFireActive;
		}
		break;
	case ETask10ProbeStage::WaitActionFireActive:
		if (bFireActive && bHeld)
		{
			Action->SetProbeSuspended(GetWorld(), true);
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Released);
			Task10ProbeStage = ETask10ProbeStage::WaitActionSuspended;
		}
		break;
	case ETask10ProbeStage::WaitActionSuspended:
		if (!bFireActive && !bHeld && Registrations == 0 && Hero->GetInputBindingCount() == 0)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe ACTION_BLOCKED: Pawn=%s"), *MiniPawn->GetPathName());
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitActionNoFire;
		}
		break;
	case ETask10ProbeStage::WaitActionNoFire:
		if (!bFireActive && !bHeld && Registrations == 0)
		{
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Released);
			Action->SetProbeSuspended(GetWorld(), false);
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe ACTION_NO_FIRE: Pawn=%s"), *MiniPawn->GetPathName());
			Task10ProbeStage = ETask10ProbeStage::WaitActionResumed;
		}
		break;
	case ETask10ProbeStage::WaitActionResumed:
		if (Hero->IsInputActive() && Registrations == 1 && Hero->GetInputBindingCount() == 16)
		{
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Pressed);
			Task10ProbeStage = ETask10ProbeStage::WaitActionReactivated;
		}
		break;
	case ETask10ProbeStage::WaitActionReactivated:
		if (bFireActive)
		{
			SendProbeKey(this, EKeys::LeftMouseButton, IE_Released);
			Task10ProbeStage = ETask10ProbeStage::WaitActionReleased;
		}
		break;
	case ETask10ProbeStage::WaitActionReleased:
		if (!bFireActive && !bHeld)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniInputProbe PASS: Cycles=4 Respawns=3 MenuGate=1 ActionGate=1"));
			Task10ProbeStage = ETask10ProbeStage::Complete;
			GetWorldTimerManager().ClearTimer(Task10ProbeTimer);
		}
		break;
	case ETask10ProbeStage::Complete:
		break;
	}
#endif
}
