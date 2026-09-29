#include "MiniTask11ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Animation/MiniAnimInstance.h"
#include "Camera/MiniCameraComponent.h"
#include "Camera/MiniCameraMode.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Character/MiniPawnData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
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

bool UMiniTask11ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) &&
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask11"));
#else
	return false;
#endif
}

TStatId UMiniTask11ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask11ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask11ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (!bFailed)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTask11Probe FAIL: %s"), Reason);
		bFailed = true;
	}
}

void UMiniTask11ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bFailed || bPassed || !GetWorld() || !GetWorld()->HasBegunPlay())
	{
		return;
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer)
	{
		TickServer(DeltaTime);
	}
	else if (GetWorld()->GetNetMode() == NM_Client)
	{
		TickClient(DeltaTime);
	}
}

void UMiniTask11ProbeSubsystem::TickServer(float DeltaTime)
{
	int32 ControllerCount = 0;
	AMiniPlayerController* Host = nullptr;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		++ControllerCount;
		if (It->IsLocalController())
		{
			Host = *It;
		}
	}
	if (ControllerCount < 2 || !Host || bServerDriveDone)
	{
		return;
	}
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(Host->GetPawn());
	if (!Pawn)
	{
		return;
	}
	if (!ServerDrivenPawn.IsValid())
	{
		ServerClientWaitSeconds += DeltaTime;
		// The client must see the initial simulated Pawn before it starts moving.
		if (ServerClientWaitSeconds < 4.0f)
		{
			return;
		}
		ServerDrivenPawn = Pawn;
		ServerDriveStart = Pawn->GetActorLocation();
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe REMOTE_DRIVE_START: Pawn=%s"),
			*Pawn->GetPathName());
	}
	if (Pawn != ServerDrivenPawn.Get())
	{
		Fail(TEXT("server host Pawn changed during remote movement"));
		return;
	}
	if (ServerDriveSeconds < 5.0f)
	{
		Host->SetControlRotation(FRotator(0.0f, 75.0f, 0.0f));
		Pawn->SetActorRotation(FRotator(0.0f, 75.0f, 0.0f));
		// A second movement interval gives headless simulated proxies time to
		// initialize their AnimInstance before we sample its moving state.
		Pawn->AddMovementInput(FVector::ForwardVector, ServerDriveSeconds < 2.5f ? 1.0f : -1.0f);
		ServerDriveSeconds += DeltaTime;
		return;
	}
	const float Distance = FVector::Dist2D(ServerDriveStart, Pawn->GetActorLocation());
	if (Distance < 30.0f)
	{
		Fail(TEXT("server host Pawn did not move"));
		return;
	}
	bServerDriveDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe REMOTE_DRIVE_DONE: Pawn=%s Distance=%.1f Yaw=%.1f"),
		*Pawn->GetPathName(), Distance, Pawn->GetActorRotation().Yaw);
}

void UMiniTask11ProbeSubsystem::TickClient(float DeltaTime)
{
	AMiniPlayerController* Controller = nullptr;
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		if (It->IsLocalController())
		{
			Controller = *It;
			break;
		}
	}
	AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	if (!Pawn)
	{
		return;
	}
	TickCamera(Controller, Pawn);
	TickAnimation(Pawn, DeltaTime);
	TickRemote(Pawn, DeltaTime);
	if (!bFailed && CameraCycle == 4 && bAimPassed && bCollisionPassed &&
		bLocalMovingPassed && bJumpPassed && bLandPassed && bRemotePassed &&
		bRemoteMovingPassed && bRemoteIdlePassed)
	{
		bPassed = true;
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask11Probe PASS: CameraCycles=4 Respawns=3 Aim=1 Collision=1 Remote=1 Anim=1"));
	}
}

void UMiniTask11ProbeSubsystem::TickCamera(AMiniPlayerController* Controller, AMiniCharacter* Pawn)
{
	UMiniHeroComponent* Hero = Pawn->GetHeroComponent();
	UMiniCameraComponent* Camera = Pawn->GetMiniCameraComponent();
	const UMiniPawnData* Data = Pawn->GetPawnData();
	if (!Hero || !Hero->IsInputActive() || !Camera || !Data ||
		!Data->DefaultCameraMode || !Data->AimCameraMode || Controller->GetViewTarget() != Pawn)
	{
		return;
	}
	if (PreviousPawn.Get() != Pawn)
	{
		if (CameraCycle >= 4)
		{
			Fail(TEXT("unexpected fifth local Pawn"));
			return;
		}
		if (CameraCycle > 0 && CameraStage != ECameraStage::Done)
		{
			Fail(TEXT("first Pawn changed before camera checks completed"));
			return;
		}
		FMinimalViewInfo View;
		Camera->GetCameraView(0.1f, View);
		if (!Camera->IsActive() || Camera->GetCurrentCameraMode().Get() != Data->DefaultCameraMode.Get() ||
			!FMath::IsNearlyEqual(View.FOV, Data->DefaultCameraMode.GetDefaultObject()->FieldOfView, 2.0f))
		{
			Fail(TEXT("new Pawn does not own the default camera mode"));
			return;
		}
		const FString NewPath = Pawn->GetPathName();
		if (CameraCycle > 0)
		{
			UE_LOG(LogMiniInit, Display,
				TEXT("MiniTask11Probe RESPAWN_CAMERA: Cycle=%d OldPawn=%s NewPawn=%s"),
				CameraCycle, *PreviousPawnPath, *NewPath);
		}
		PreviousPawn = Pawn;
		PreviousPawnPath = NewPath;
		++CameraCycle;
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask11Probe CAMERA_READY: Cycle=%d Pawn=%s Mode=%s FOV=%.1f"),
			CameraCycle, *NewPath, *GetPathNameSafe(Camera->GetCurrentCameraMode().Get()), View.FOV);
		if (CameraCycle == 1)
		{
			Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Pressed, 1.0f));
			CameraStage = ECameraStage::WaitAim;
		}
		return;
	}
	if (CameraCycle != 1 || CameraStage == ECameraStage::Done)
	{
		return;
	}
	const AMiniPlayerState* State = Pawn->GetPlayerState<AMiniPlayerState>();
	const UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return;
	}
	if (CameraStage == ECameraStage::WaitAim &&
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Aiming))
	{
		FMinimalViewInfo View;
		Camera->GetCameraView(0.25f, View);
		if (Camera->GetCurrentCameraMode().Get() != Data->AimCameraMode.Get() ||
			View.FOV >= Data->DefaultCameraMode.GetDefaultObject()->FieldOfView)
		{
			Fail(TEXT("aim input did not select the aiming camera"));
			return;
		}
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Released, 0.0f));
		CameraStage = ECameraStage::WaitAimRelease;
	}
	else if (CameraStage == ECameraStage::WaitAimRelease &&
		!ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Aiming))
	{
		FMinimalViewInfo View;
		Camera->GetCameraView(0.25f, View);
		if (Camera->GetCurrentCameraMode().Get() != Data->DefaultCameraMode.Get())
		{
			Fail(TEXT("releasing aim did not restore the default camera"));
			return;
		}
		bAimPassed = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe AIM_PASS: Pawn=%s FOV=%.1f"),
			*Pawn->GetPathName(), View.FOV);
		CameraStage = ECameraStage::CheckCollision;
	}
	else if (CameraStage == ECameraStage::CheckCollision)
	{
		FMinimalViewInfo ClearView;
		Camera->GetCameraView(0.25f, ClearView);
		const FVector Pivot = Pawn->GetActorLocation();
		const FVector Ideal = Camera->GetLastIdealLocation();
		const FVector BlockerLocation = FMath::Lerp(Pivot, Ideal, 0.45f);
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Obstacle = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform(BlockerLocation), SpawnParameters);
		if (!Obstacle)
		{
			Fail(TEXT("could not spawn camera collision obstacle"));
			return;
		}
		CollisionObstacle = Obstacle;
		USphereComponent* Sphere = NewObject<USphereComponent>(Obstacle, TEXT("Task11CameraBlocker"));
		Obstacle->AddInstanceComponent(Sphere);
		Obstacle->SetRootComponent(Sphere);
		Sphere->InitSphereRadius(48.0f);
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		Sphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		Sphere->RegisterComponent();
		Obstacle->SetActorLocation(BlockerLocation);
		FMinimalViewInfo BlockedView;
		Camera->GetCameraView(0.25f, BlockedView);
		const float ClearDistance = FVector::Dist(Pivot, ClearView.Location);
		const float BlockedDistance = FVector::Dist(Pivot, BlockedView.Location);
		const bool bBlocked = Camera->WasCollisionBlocked();
		Obstacle->Destroy();
		CollisionObstacle.Reset();
		FMinimalViewInfo RecoveredView;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Camera->GetCameraView(0.25f, RecoveredView);
		}
		const float RecoveredDistance = FVector::Dist(Pivot, RecoveredView.Location);
		if (!bBlocked || BlockedDistance >= ClearDistance - 10.0f ||
			RecoveredDistance <= BlockedDistance + 10.0f)
		{
			Fail(TEXT("camera collision or recovery failed"));
			return;
		}
		bCollisionPassed = true;
		CameraStage = ECameraStage::Done;
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask11Probe COLLISION_PASS: Pawn=%s Clear=%.1f Blocked=%.1f Recovered=%.1f"),
			*Pawn->GetPathName(), ClearDistance, BlockedDistance, RecoveredDistance);
	}
}

void UMiniTask11ProbeSubsystem::TickAnimation(AMiniCharacter* LocalPawn, float DeltaTime)
{
	UMiniAnimInstance* Anim = LocalPawn->GetMesh()
		? Cast<UMiniAnimInstance>(LocalPawn->GetMesh()->GetAnimInstance()) : nullptr;
	if (!Anim)
	{
		return;
	}
	// NullRHI does not guarantee a rendered mesh update. Run the same native update
	// explicitly so this checks the live Pawn movement values in a headless process.
	Anim->NativeUpdateAnimation(DeltaTime);
	if (!bLocalMovingPassed && Anim->bIsMoving && Anim->GroundSpeed > 8.0f)
	{
		bLocalMovingPassed = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe ANIM_LOCAL_MOVE: Pawn=%s Speed=%.1f"),
			*LocalPawn->GetPathName(), Anim->GroundSpeed);
	}
	if (!bJumpPassed && Anim->bIsInAir && Anim->bIsAscending)
	{
		bJumpPassed = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe ANIM_JUMP: Pawn=%s"), *LocalPawn->GetPathName());
	}
	if (bJumpPassed && !bLandPassed && Anim->bRecentlyLanded)
	{
		bLandPassed = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe ANIM_LAND: Pawn=%s"), *LocalPawn->GetPathName());
	}
}

void UMiniTask11ProbeSubsystem::TickRemote(AMiniCharacter* LocalPawn, float DeltaTime)
{
	AMiniCharacter* Remote = RemotePawn.Get();
	if (!Remote)
	{
		for (TActorIterator<AMiniCharacter> It(GetWorld()); It; ++It)
		{
			if (*It != LocalPawn && It->GetLocalRole() == ROLE_SimulatedProxy)
			{
				Remote = *It;
				RemotePawn = Remote;
				RemoteStartLocation = Remote->GetActorLocation();
				RemoteStartYaw = Remote->GetActorRotation().Yaw;
				break;
			}
		}
	}
	if (!Remote)
	{
		return;
	}
	const float Distance = FVector::Dist2D(RemoteStartLocation, Remote->GetActorLocation());
	const float YawChange = FMath::Abs(FMath::FindDeltaAngleDegrees(RemoteStartYaw, Remote->GetActorRotation().Yaw));
	if (!bRemotePassed && Distance >= 30.0f && YawChange >= 20.0f)
	{
		bRemotePassed = true;
		UE_LOG(LogMiniInit, Display,
			TEXT("MiniTask11Probe REMOTE_PASS: Pawn=%s Distance=%.1f YawChange=%.1f Role=%d"),
			*Remote->GetPathName(), Distance, YawChange, static_cast<int32>(Remote->GetLocalRole()));
	}
	UMiniAnimInstance* Anim = Remote->GetMesh()
		? Cast<UMiniAnimInstance>(Remote->GetMesh()->GetAnimInstance()) : nullptr;
	if (!Anim)
	{
		return;
	}
	Anim->NativeUpdateAnimation(DeltaTime);
	if (!bRemoteMovingPassed && Anim->bIsMoving && Anim->GroundSpeed > 8.0f)
	{
		bRemoteMovingPassed = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe ANIM_REMOTE_MOVE: Pawn=%s Speed=%.1f"),
			*Remote->GetPathName(), Anim->GroundSpeed);
	}
	if (bRemoteMovingPassed && !bRemoteIdlePassed && !Anim->bIsMoving && Anim->GroundSpeed <= 8.0f)
	{
		bRemoteIdlePassed = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask11Probe ANIM_REMOTE_IDLE: Pawn=%s"),
			*Remote->GetPathName());
	}
}
