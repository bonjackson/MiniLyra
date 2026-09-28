#include "MiniTask12ProbeSubsystem.h"

#include "MiniTask12ProbeActor.h"
#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Camera/MiniCameraComponent.h"
#include "Camera/MiniCameraMode.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"

namespace
{
const FGameplayAbilitySpec* FindInputSpec(const UMiniAbilitySystemComponent* ASC, FGameplayTag InputTag)
{
	if (ASC)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
			{
				return &Spec;
			}
		}
	}
	return nullptr;
}

bool IsActive(const UMiniAbilitySystemComponent* ASC, FGameplayTag InputTag)
{
	const FGameplayAbilitySpec* Spec = FindInputSpec(ASC, InputTag);
	return Spec && Spec->IsActive();
}

void SendKey(AMiniPlayerController* Controller, const FKey& Key, EInputEvent Event)
{
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(
		Key, Event, Event == IE_Released ? 0.0f : 1.0f));
}

bool IsCameraMode(AMiniCharacter* Pawn, bool bAim)
{
	UMiniCameraComponent* Camera = Pawn ? Pawn->GetMiniCameraComponent() : nullptr;
	const UMiniPawnData* Data = Pawn ? Pawn->GetPawnData() : nullptr;
	if (!Camera || !Data || !Data->DefaultCameraMode || !Data->AimCameraMode)
	{
		return false;
	}
	FMinimalViewInfo View;
	Camera->GetCameraView(0.25f, View);
	return Camera->GetCurrentCameraMode().Get() ==
		(bAim ? Data->AimCameraMode.Get() : Data->DefaultCameraMode.Get());
}

FGameplayTag ReloadTag()
{
	return MiniGameplayTags::State_Reloading;
}

FGameplayTag DeadTag()
{
	return MiniGameplayTags::State_Dead;
}

FGameplayTag InputBlockTag()
{
	return MiniGameplayTags::Gameplay_AbilityInputBlocked;
}
}

bool UMiniTask12ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask12"));
#else
	return false;
#endif
}

TStatId UMiniTask12ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask12ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask12ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask12Probe FAIL: %s Stage=%d"),
			Reason, static_cast<int32>(Stage));
		bFailed = true;
	}
}

void UMiniTask12ProbeSubsystem::SetStage(EStage NewStage)
{
	Stage = NewStage;
	StageSeconds = 0.0f;
}

void UMiniTask12ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || Stage == EStage::Done || !GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer)
	{
		TickServer();
	}
	else if (GetWorld()->GetNetMode() == NM_Client)
	{
		TickClient(DeltaTime);
	}
}

void UMiniTask12ProbeSubsystem::TickServer()
{
	if (bActorSpawned)
	{
		return;
	}
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		AMiniPlayerController* Controller = *It;
		AMiniCharacter* Pawn = Controller && !Controller->IsLocalController()
			? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
		AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
		UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
		if (!Pawn || !ASC || ASC->GetAvatarActor() != Pawn ||
			!FindInputSpec(ASC, MiniGameplayTags::InputTag_Jump) ||
			!FindInputSpec(ASC, MiniGameplayTags::InputTag_Aim) ||
			!FindInputSpec(ASC, MiniGameplayTags::InputTag_Fire))
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.Owner = Controller;
		AMiniTask12ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask12ProbeActor>(
			AMiniTask12ProbeActor::StaticClass(), FTransform::Identity, Params);
		if (!Probe)
		{
			Fail(TEXT("could not spawn the replicated probe actor"));
			return;
		}
		bActorSpawned = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe SERVER_READY: Pawn=%s Actor=%s"),
			*Pawn->GetPathName(), *Probe->GetPathName());
		return;
	}
}

void UMiniTask12ProbeSubsystem::TickClient(float DeltaTime)
{
	StageSeconds += DeltaTime;
	if (StageSeconds > (Stage == EStage::WaitReady ? 60.0f : 15.0f))
	{
		Fail(TEXT("probe stage timed out"));
		return;
	}
	AMiniPlayerController* Controller = nullptr;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsLocalController())
		{
			Controller = *It;
			break;
		}
	}
	if (!Controller)
	{
		return;
	}
	AMiniTask12ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask12ProbeActor> It(GetWorld()); It; ++It)
	{
		if (It->GetOwner() == Controller)
		{
			Probe = *It;
			break;
		}
	}
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(Controller->GetPawn());
	AMiniPlayerState* State = Controller->GetPlayerState<AMiniPlayerState>();
	UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	UMiniHeroComponent* Hero = Pawn ? Pawn->GetHeroComponent() : nullptr;
	if (!Probe || !ASC)
	{
		return;
	}
	const bool bJumpActive = IsActive(ASC, MiniGameplayTags::InputTag_Jump);
	const bool bAimActive = IsActive(ASC, MiniGameplayTags::InputTag_Aim);
	const bool bAiming = ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Aiming);
	switch (Stage)
	{
	case EStage::WaitReady:
		if (Pawn && Hero && Hero->IsInputActive() && ASC->GetAvatarActor() == Pawn &&
			FindInputSpec(ASC, MiniGameplayTags::InputTag_Jump) &&
			FindInputSpec(ASC, MiniGameplayTags::InputTag_Aim) &&
			FindInputSpec(ASC, MiniGameplayTags::InputTag_Fire) && IsCameraMode(Pawn, false))
		{
			OriginalASC = ASC;
			OldPawn = Pawn;
			OldPawnPath = Pawn->GetPathName();
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe CLIENT_READY: Pawn=%s ASC=%s"),
				*OldPawnPath, *ASC->GetPathName());
			SendKey(Controller, EKeys::SpaceBar, IE_Pressed);
			SetStage(EStage::WaitJumpActive);
		}
		break;
	case EStage::WaitJumpActive:
		if (Pawn && bJumpActive &&
			(Pawn->bPressedJump || Pawn->GetCharacterMovement()->IsFalling()))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe JUMP_ACTIVE: Pawn=%s"), *Pawn->GetPathName());
			SendKey(Controller, EKeys::SpaceBar, IE_Released);
			SetStage(EStage::WaitJumpReleased);
		}
		break;
	case EStage::WaitJumpReleased:
		if (!bJumpActive && Pawn && !Pawn->bPressedJump)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe JUMP_RELEASED: Pawn=%s"), *Pawn->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(EStage::WaitAimActive);
		}
		break;
	case EStage::WaitAimActive:
		if (Pawn && bAimActive && bAiming && IsCameraMode(Pawn, true))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe AIM_ACTIVE: Pawn=%s"), *Pawn->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(EStage::WaitAimSingle);
		}
		break;
	case EStage::WaitAimSingle:
		if (StageSeconds > 0.5f)
		{
			if (!bAimActive || ASC->GetTagCount(MiniGameplayTags::State_Aiming) != 1)
			{
				Fail(TEXT("duplicate Aim press created another aiming state"));
				return;
			}
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe AIM_SINGLE: Pawn=%s TagCount=1"),
				*Pawn->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Released);
			SetStage(EStage::WaitAimReleased);
		}
		break;
	case EStage::WaitAimReleased:
		if (Pawn && !bAimActive && !bAiming && IsCameraMode(Pawn, false))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe AIM_RELEASED: Pawn=%s"), *Pawn->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(EStage::WaitAimForReload);
		}
		break;
	case EStage::WaitAimForReload:
		if (bAimActive && bAiming)
		{
			Probe->ServerCancelWithTag(ReloadTag(), MiniGameplayTags::InputTag_Aim);
			SetStage(EStage::WaitReloadCancel);
		}
		break;
	case EStage::WaitReloadCancel:
		if (ASC->HasMatchingGameplayTag(ReloadTag()) && !bAimActive && !bAiming &&
			Pawn && IsCameraMode(Pawn, false))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe RELOAD_CANCEL: Pawn=%s"), *Pawn->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Released);
			Probe->ServerCheckRejected(ReloadTag(), MiniGameplayTags::InputTag_Aim);
			Probe->ServerCheckRejected(ReloadTag(), MiniGameplayTags::InputTag_Fire);
			SetStage(EStage::WaitReloadJumpReady);
		}
		break;
	case EStage::WaitReloadJumpReady:
		if (Pawn && Pawn->CanJump())
		{
			SendKey(Controller, EKeys::SpaceBar, IE_Pressed);
			SetStage(EStage::WaitReloadJump);
		}
		break;
	case EStage::WaitReloadJump:
		if (bJumpActive)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe RELOAD_JUMP_PASS: Pawn=%s"),
				*Pawn->GetPathName());
			SendKey(Controller, EKeys::SpaceBar, IE_Released);
			SetStage(EStage::WaitReloadJumpReleased);
		}
		break;
	case EStage::WaitReloadJumpReleased:
		if (!bJumpActive)
		{
			Probe->ServerClearTag(ReloadTag());
			SetStage(EStage::WaitReloadClear);
		}
		break;
	case EStage::WaitReloadClear:
		if (!ASC->HasMatchingGameplayTag(ReloadTag()) && Pawn && Pawn->CanJump())
		{
			SendKey(Controller, EKeys::SpaceBar, IE_Pressed);
			SetStage(EStage::WaitJumpForDead);
		}
		break;
	case EStage::WaitJumpForDead:
		if (bJumpActive)
		{
			Probe->ServerCancelWithTag(DeadTag(), MiniGameplayTags::InputTag_Jump);
			SetStage(EStage::WaitDeadCancel);
		}
		break;
	case EStage::WaitDeadCancel:
		if (ASC->HasMatchingGameplayTag(DeadTag()) && !bJumpActive && Pawn && !Pawn->bPressedJump)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe DEAD_CANCEL: Pawn=%s"), *Pawn->GetPathName());
			SendKey(Controller, EKeys::SpaceBar, IE_Released);
			Probe->ServerCheckRejected(DeadTag(), MiniGameplayTags::InputTag_Jump);
			Probe->ServerCheckRejected(DeadTag(), MiniGameplayTags::InputTag_Aim);
			Probe->ServerCheckRejected(DeadTag(), MiniGameplayTags::InputTag_Fire);
			Probe->ServerClearTag(DeadTag());
			SetStage(EStage::WaitDeadClear);
		}
		break;
	case EStage::WaitDeadClear:
		if (!ASC->HasMatchingGameplayTag(DeadTag()))
		{
			SendKey(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(EStage::WaitAimForInputBlock);
		}
		break;
	case EStage::WaitAimForInputBlock:
		if (bAimActive && bAiming)
		{
			Probe->ServerCancelWithTag(InputBlockTag(), MiniGameplayTags::InputTag_Aim);
			SetStage(EStage::WaitInputBlockCancel);
		}
		break;
	case EStage::WaitInputBlockCancel:
		if (ASC->HasMatchingGameplayTag(InputBlockTag()) && !bAimActive && !bAiming &&
			Pawn && IsCameraMode(Pawn, false))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe INPUT_CANCEL: Pawn=%s"), *Pawn->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Released);
			Probe->ServerCheckRejected(InputBlockTag(), MiniGameplayTags::InputTag_Jump);
			Probe->ServerCheckRejected(InputBlockTag(), MiniGameplayTags::InputTag_Aim);
			Probe->ServerCheckRejected(InputBlockTag(), MiniGameplayTags::InputTag_Fire);
			Probe->ServerClearTag(InputBlockTag());
			SetStage(EStage::WaitInputBlockClear);
		}
		break;
	case EStage::WaitInputBlockClear:
		if (!ASC->HasMatchingGameplayTag(InputBlockTag()))
		{
			SendKey(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(EStage::WaitAimForRespawn);
		}
		break;
	case EStage::WaitAimForRespawn:
		if (bAimActive && bAiming)
		{
			Probe->ServerRespawnWhileAiming();
			SetStage(EStage::WaitUnpossessed);
		}
		break;
	case EStage::WaitUnpossessed:
		if (!Pawn && !ASC->GetAvatarActor() && !bAimActive && !bAiming)
		{
			SendKey(Controller, EKeys::RightMouseButton, IE_Released);
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe CLIENT_OLD_CLEAN: Pawn=%s"),
				*OldPawnPath);
			SetStage(EStage::WaitNewPawn);
		}
		break;
	case EStage::WaitNewPawn:
		if (Pawn && Pawn != OldPawn.Get() && Hero && Hero->IsInputActive() &&
			ASC == OriginalASC.Get() && ASC->GetAvatarActor() == Pawn && !bAimActive && !bAiming &&
			IsCameraMode(Pawn, false))
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask12Probe NEW_PAWN_CLEAN: OldPawn=%s NewPawn=%s ASC=%s"),
				*OldPawnPath, *Pawn->GetPathName(), *ASC->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Pressed);
			SetStage(EStage::WaitNewAimActive);
		}
		break;
	case EStage::WaitNewAimActive:
		if (Pawn && bAimActive && bAiming && IsCameraMode(Pawn, true))
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask12Probe NEW_AIM_ACTIVE: Pawn=%s"),
				*Pawn->GetPathName());
			SendKey(Controller, EKeys::RightMouseButton, IE_Released);
			SetStage(EStage::WaitNewAimReleased);
		}
		break;
	case EStage::WaitNewAimReleased:
		if (Pawn && !bAimActive && !bAiming && IsCameraMode(Pawn, false))
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask12Probe PASS: Jump=1 Aim=1 Reload=1 Dead=1 InputBlock=1 Respawn=1 Reuse=1"));
			SetStage(EStage::Done);
		}
		break;
	case EStage::Done:
		break;
	}
}
