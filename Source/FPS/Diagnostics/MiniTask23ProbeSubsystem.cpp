#include "MiniTask23ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGameState.h"
#include "GameModes/MiniWorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "Training/MiniPracticeTarget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
AMiniPlayerController* Task23LocalController(UWorld* World)
{
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
	{
		if (It->IsLocalController()) { return *It; }
	}
	return nullptr;
}

AMiniPlayerState* Task23PlayerState(AMiniPlayerController* PC)
{
	return PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
}

AMiniCharacter* Task23Pawn(AMiniPlayerController* PC)
{
	return PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
}

UMiniAbilitySystemComponent* Task23ASC(AMiniPlayerController* PC)
{
	const AMiniPlayerState* PS = Task23PlayerState(PC);
	return PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
}

bool Task23Ready(AMiniPlayerController* PC)
{
	AMiniCharacter* Pawn = Task23Pawn(PC);
	UMiniAbilitySystemComponent* ASC = Task23ASC(PC);
	return Pawn && ASC && ASC->GetOwnerActor() == Task23PlayerState(PC) && ASC->GetAvatarActor() == Pawn &&
		Pawn->GetHealthComponent() && !Pawn->GetHealthComponent()->IsDead() &&
		Pawn->GetHeroComponent() && (!PC->IsLocalController() || Pawn->GetHeroComponent()->IsInputActive()) &&
		Pawn->GetEquipmentManager() && Pawn->GetEquipmentManager()->GetCurrentEquipment();
}

UMiniHUDViewModel* Task23HUD(AMiniPlayerController* PC)
{
	UMiniPrimaryGameLayout* Root = PC && PC->GetLocalPlayer()
		? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	if (!Layer) { return nullptr; }
	for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
	{
		if (UMiniHUDLayout* Layout = Cast<UMiniHUDLayout>(Widget)) { return Layout->GetViewModel(); }
	}
	return nullptr;
}

bool Task23SameStats(const FMiniPlayerMatchStats& A, const FMiniPlayerMatchStats& B)
{
	return A.RoundId == B.RoundId && A.Kills == B.Kills && A.Deaths == B.Deaths && A.Revision == B.Revision;
}

FCollisionQueryParams Task23GeometryQuery(UWorld* World)
{
	FCollisionQueryParams Query(SCENE_QUERY_STAT(MiniTask23Geometry), false);
	for (TActorIterator<APawn> It(World); It; ++It) { Query.AddIgnoredActor(*It); }
	return Query;
}
}

bool UMiniTask23ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask23="), Mode);
#else
	return false;
#endif
}

void UMiniTask23ProbeSubsystem::Deinitialize()
{
	RemoveSpawnBlockers();
	Super::Deinitialize();
}

TStatId UMiniTask23ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask23ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask23ProbeSubsystem::Fail(const TCHAR* Reason)
{
	bDone = true;
	RemoveSpawnBlockers();
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask23Probe FAIL: Reason=%s Step=%d CompletedRounds=%d"), Reason, ServerStep, CompletedRounds);
}

void UMiniTask23ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDone || !GetWorld() || !GetWorld()->HasBegunPlay()) { return; }
	FString Mode;
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask23="), Mode);
	WaitSeconds += DeltaTime;
	int32 Timeout = 240;
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask23TimeoutSeconds="), Timeout);
	if (WaitSeconds > Timeout) { Fail(TEXT("map/network/round acceptance timed out")); return; }
	if (Mode == TEXT("Practice")) { TickPractice(); }
	else if (Mode == TEXT("Arena"))
	{
		if (GetWorld()->GetNetMode() == NM_Client) { TickClient(); }
		else if (GetWorld()->GetNetMode() == NM_Standalone) { return; } // Client connection's temporary entry world.
		else { TickServer(); }
	}
	else { Fail(TEXT("unknown Task23 probe mode")); }
}

bool UMiniTask23ProbeSubsystem::VerifyMap(bool bArena, bool bRoutes)
{
	UWorld* World = GetWorld();
	AMiniGameState* GS = World->GetGameState<AMiniGameState>();
	const UMiniExperienceManagerComponent* Manager = GS ? GS->GetExperienceManagerComponent() : nullptr;
	const AMiniWorldSettings* Settings = Cast<AMiniWorldSettings>(World->GetWorldSettings());
	if (!Manager || !Manager->IsExperienceLoaded() || !Settings) { return false; }
	const FString ExpectedName = bArena ? TEXT("DA_MiniArenaExperience") : TEXT("DA_MiniPracticeExperience");
	if (Manager->GetCurrentExperienceId().PrimaryAssetName != FName(*ExpectedName) ||
		Settings->DefaultGameplayExperience.ToSoftObjectPath().GetAssetName() != ExpectedName ||
		!World->GetMapName().EndsWith(bArena ? TEXT("L_MiniArena") : TEXT("L_MiniPractice")))
	{ Fail(TEXT("map default selected the wrong production Experience")); return false; }
	if (!bArena) { return true; }
	if (!FMath::IsNearlyEqual(Settings->KillZ, -650.0f) || !Settings->bEnableWorldBoundsChecks)
	{ Fail(TEXT("arena KillZ/world bounds configuration missing")); return false; }
	const AMiniCharacter* LocalPawn = Task23Pawn(Task23LocalController(World));
	const UCapsuleComponent* StartCapsule = LocalPawn ? LocalPawn->GetCapsuleComponent() : nullptr;
	if (!StartCapsule) { return false; }
	const FCollisionShape StartShape = FCollisionShape::MakeCapsule(StartCapsule->GetScaledCapsuleRadius(), StartCapsule->GetScaledCapsuleHalfHeight());
	int32 Starts = 0, Covers = 0, Floors = 0, Hazards = 0;
	const FCollisionQueryParams Query = Task23GeometryQuery(World);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		Covers += It->ActorHasTag(TEXT("MiniArenaCover")) ? 1 : 0;
		Floors += It->ActorHasTag(TEXT("MiniArenaFloor")) ? 1 : 0;
		Hazards += It->ActorHasTag(TEXT("MiniArenaHazard")) ? 1 : 0;
		if (const APlayerStart* Start = Cast<APlayerStart>(*It))
		{
			++Starts;
			FHitResult Hit;
			if (!Start->ActorHasTag(TEXT("MiniArenaSpawn")) || !World->LineTraceSingleByChannel(Hit,
				Start->GetActorLocation() + FVector(0, 0, 20), Start->GetActorLocation() - FVector(0, 0, 300), ECC_Pawn, Query) ||
				!Hit.GetActor() || !Hit.GetActor()->ActorHasTag(TEXT("MiniArenaFloor")) || Hit.ImpactNormal.Z < 0.9f)
			{ Fail(TEXT("arena start has no safe colliding floor")); return false; }
			if (World->OverlapBlockingTestByChannel(Start->GetActorLocation(), Start->GetActorQuat(), ECC_Pawn, StartShape, Query))
			{
				Fail(*FString::Printf(TEXT("actual player capsule does not fit arena start %s"), *Start->GetName()));
				return false;
			}
		}
	}
	if (Starts != 8 || Covers < 6 || Floors < 2 || Hazards < 1)
	{ Fail(TEXT("independent arena geometry/start/hazard set missing")); return false; }
	FHitResult Pit;
	if (World->LineTraceSingleByChannel(Pit, FVector(1700, 1325, 200), FVector(1700, 1325, -600), ECC_Pawn, Query))
	{ Fail(TEXT("marked fall pit accidentally has an invisible colliding floor")); return false; }
	if (bRoutes)
	{
		const AMiniCharacter* Pawn = Task23Pawn(Task23LocalController(World));
		const UCapsuleComponent* Capsule = Pawn ? Pawn->GetCapsuleComponent() : nullptr;
		if (!Capsule) { return false; }
		const float Height = Capsule->GetScaledCapsuleHalfHeight() + 5.0f;
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
		const TArray<TArray<FVector2D>> Routes = {
			{FVector2D(-1720, -800), FVector2D(1720, -800)},
			{FVector2D(-1720, 800), FVector2D(1250, 800)},
			{FVector2D(-1720, 0), FVector2D(-1720, -800), FVector2D(-350, -800), FVector2D(-350, 330),
				FVector2D(350, 330), FVector2D(350, -800), FVector2D(1720, -800), FVector2D(1720, 0)}};
		for (int32 RouteIndex = 0; RouteIndex < Routes.Num(); ++RouteIndex)
		{
			for (int32 Segment = 1; Segment < Routes[RouteIndex].Num(); ++Segment)
			{
				const FVector2D From = Routes[RouteIndex][Segment - 1], To = Routes[RouteIndex][Segment];
				FHitResult Hit;
				if (World->SweepSingleByChannel(Hit, FVector(From.X, From.Y, Height), FVector(To.X, To.Y, Height),
					FQuat::Identity, ECC_Pawn, Shape, Query))
				{
					Fail(*FString::Printf(TEXT("arena capsule route %d segment %d blocked by %s"), RouteIndex, Segment, *GetNameSafe(Hit.GetActor())));
					return false;
				}
				const int32 Samples = FMath::CeilToInt(FVector2D::Distance(From, To) / 150.0f);
				for (int32 Sample = 0; Sample <= Samples; ++Sample)
				{
					const FVector2D Point = FMath::Lerp(From, To, static_cast<double>(Sample) / Samples);
					if (!World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, 200), FVector(Point.X, Point.Y, -300), ECC_Pawn, Query) ||
						!Hit.GetActor() || !Hit.GetActor()->ActorHasTag(TEXT("MiniArenaFloor")) || Hit.ImpactNormal.Z < 0.9f)
					{ Fail(TEXT("arena walkable route contains an unmarked gap or obstruction")); return false; }
				}
			}
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_MAP_PASS: Starts=8 StartCapsules=8 Ground=1 Routes=3 CapsuleSweeps=1 PitOpen=1 Covers=%d Hazards=%d DefaultArena=1"), Covers, Hazards);
	}
	return true;
}

bool UMiniTask23ProbeSubsystem::VerifyPlayerGround(AMiniPlayerController* PC) const
{
	const AMiniCharacter* Pawn = Task23Pawn(PC);
	if (!Task23Ready(PC) || !Pawn->GetCharacterMovement()->IsMovingOnGround()) { return false; }
	FHitResult Hit;
	const FVector Location = Pawn->GetActorLocation();
	return GetWorld()->LineTraceSingleByChannel(Hit, Location, Location - FVector(0, 0, 300), ECC_Pawn,
		Task23GeometryQuery(GetWorld())) && Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("MiniArenaFloor"));
}

void UMiniTask23ProbeSubsystem::DiscoverOwners()
{
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		AMiniPlayerController* PC = *It;
		if (PC->IsLocalController() || !Task23PlayerState(PC) || !PC->GetNetConnection()) { continue; }
		if (OwnerProbes.ContainsByPredicate([PC](const TWeakObjectPtr<AMiniTask23ProbeActor>& Probe)
			{ return Probe.IsValid() && Probe->GetOwner() == PC; })) { continue; }
		FActorSpawnParameters Params;
		Params.Owner = PC;
		AMiniTask23ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask23ProbeActor>(Params);
		if (!Probe) { Fail(TEXT("owner probe spawn failed")); return; }
		Probe->InitializeServer(OwnerProbes.Num() + 1);
		OwnerProbes.Add(Probe);
		PublishedName = NAME_None;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_JOIN: Owner=%d PlayerId=%d"), Probe->GetOwnerIndex(), Task23PlayerState(PC)->GetPlayerId());
	}
}

bool UMiniTask23ProbeSubsystem::Checkpoint(FName Name, bool bLivePawn)
{
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>();
	if (!Phases || !Matches || OwnerProbes.Num() != 3) { return false; }
	const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
	const FMiniMatchState Match = Matches->GetCurrentMatchState();
	if (PublishedName != Name || !MiniTask23SamePhase(Phase, PublishedPhase) || !MiniTask23SameMatch(Match, PublishedMatch))
	{
		PublishedName = Name;
		PublishedPhase = Phase;
		PublishedMatch = Match;
		++CheckpointSerial;
		for (const TWeakObjectPtr<AMiniTask23ProbeActor>& Probe : OwnerProbes)
		{
			if (Probe.IsValid()) { Probe->SetCheckpoint(CheckpointSerial, Name, Phase, Match, bLivePawn); }
		}
	}
	for (const TWeakObjectPtr<AMiniTask23ProbeActor>& Probe : OwnerProbes)
	{
		if (!Probe.IsValid() || !IsValid(Probe->GetOwner()) || !Probe->HasAcknowledged()) { return false; }
	}
	AMiniPlayerController* Host = Task23LocalController(GetWorld());
	UMiniHUDViewModel* VM = Task23HUD(Host);
	return VM && VM->IsRunning() && VM->GetSnapshot().bHasMatchData &&
		VM->GetSnapshot().PhaseTag == Phase.PhaseTag && MiniTask23SameMatch(VM->GetSnapshot().MatchState, Match) &&
		(!bLivePawn || (Task23Ready(Host) && VM->GetSnapshot().Pawn == Task23Pawn(Host)));
}

bool UMiniTask23ProbeSubsystem::BeginFall(AMiniPlayerController* PC)
{
	if (!Task23Ready(PC)) { return false; }
	AMiniCharacter* Pawn = Task23Pawn(PC);
	AMiniPlayerState* PS = Task23PlayerState(PC);
	VictimController = PC;
	SavedPawn = Pawn;
	SavedLifeId = PS->GetCurrentLifeId();
	SavedStats = PS->GetMatchStats();
	SavedHealth = PS->GetHealthSet()->GetHealth();
	SavedEquipment = Pawn->GetEquipmentManager()->GetCurrentEquipment();
	SavedHostKills = Task23PlayerState(Task23LocalController(GetWorld()))->GetMatchStats().Kills;
	bFallProtected = Task23ASC(PC)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected);
	ReplacementPawn.Reset();
	StepStartedAt = GetWorld()->GetTimeSeconds();
	DeathAt = StepStartedAt;
	// Let CharacterMovement::CheckStillInWorld call the actual FellOutOfWorld override.
	// The diagnostic does not call Character::FellOutOfWorld or any recovery handler.
	Pawn->GetCharacterMovement()->StopMovementImmediately();
	if (!Pawn->TeleportTo(FVector(1700, 1325, GetWorld()->GetWorldSettings()->KillZ - 200.0f), Pawn->GetActorRotation(), false, true))
	{ Fail(TEXT("authority fall teleport failed")); return false; }
	Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Pawn->ForceNetUpdate();
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_FALL_TELEPORT: PlayerId=%d Life=%u BelowKillZ=1 Protected=%d ActualMovementPath=1"),
		PS->GetPlayerId(), SavedLifeId, bFallProtected ? 1 : 0);
	return true;
}

bool UMiniTask23ProbeSubsystem::AwaitSafeRecovery()
{
	AMiniPlayerController* PC = VictimController.Get();
	AMiniPlayerState* PS = Task23PlayerState(PC);
	AMiniCharacter* Pawn = Task23Pawn(PC);
	if (!PS || !Pawn || Pawn != SavedPawn.Get() || Pawn->GetHealthComponent()->IsDead() ||
		PS->GetCurrentLifeId() != SavedLifeId || !Task23SameStats(PS->GetMatchStats(), SavedStats) ||
		!FMath::IsNearlyEqual(PS->GetHealthSet()->GetHealth(), SavedHealth, 0.01f) ||
		Pawn->GetEquipmentManager()->GetCurrentEquipment() != SavedEquipment.Get())
	{ Fail(TEXT("gated fall destroyed/replaced a Pawn or changed life, health, scores or equipment")); return false; }
	if (GetWorld()->GetTimeSeconds() - StepStartedAt > 4.0)
	{ Fail(TEXT("gated fall did not recover onto a safe start")); return false; }
	if (!VerifyPlayerGround(PC)) { return false; }
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	if (GM->GetPendingOutOfWorldRecoveryCount() || GM->GetPendingRespawnCount())
	{ Fail(TEXT("safe recovery left pending recovery or respawn work")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_SAFE_FALL: PlayerId=%d SamePawn=1 SameLife=1 Health=1 Scores=1 Equipment=1 Ground=1 Pending=0 Protected=%d"),
		PS->GetPlayerId(), bFallProtected ? 1 : 0);
	return true;
}

bool UMiniTask23ProbeSubsystem::AwaitFallDeath()
{
	AMiniPlayerController* PC = VictimController.Get();
	AMiniPlayerState* PS = Task23PlayerState(PC);
	AMiniCharacter* Pawn = Task23Pawn(PC);
	if (!PS || !Pawn || Pawn != SavedPawn.Get())
	{ Fail(TEXT("FellOutOfWorld bypassed persistent Pawn death lifecycle")); return false; }
	if (!Pawn->GetHealthComponent()->IsDead())
	{
		if (GetWorld()->GetTimeSeconds() - DeathAt > 2.0) { Fail(TEXT("unprotected real fall did not apply environmental death")); }
		return false;
	}
	const FMiniPlayerMatchStats& Stats = PS->GetMatchStats();
	if (PS->GetCurrentLifeId() != SavedLifeId || Stats.RoundId != SavedStats.RoundId || Stats.Kills != SavedStats.Kills ||
		Stats.Deaths != SavedStats.Deaths + 1 || PS->GetHealthSet()->GetHealth() > 0.01f ||
		Task23PlayerState(Task23LocalController(GetWorld()))->GetMatchStats().Kills != SavedHostKills ||
		GetWorld()->GetAuthGameMode<AMiniGameMode>()->GetPendingRespawnCount() != 1)
	{ Fail(TEXT("environment fall did not count exactly one death, zero kills and a delayed respawn")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_ENVIRONMENT_DEATH: PlayerId=%d Life=%u Deaths=%d KillsUnchanged=1 RealGE=1 PawnRetained=1"),
		PS->GetPlayerId(), SavedLifeId, Stats.Deaths);
	return true;
}

bool UMiniTask23ProbeSubsystem::AwaitRespawn()
{
	AMiniPlayerController* PC = VictimController.Get();
	if (!Task23Ready(PC) || Task23Pawn(PC) == SavedPawn.Get())
	{
		if (GetWorld()->GetTimeSeconds() - DeathAt > 8.0) { Fail(TEXT("new live Avatar did not appear after death")); }
		return false;
	}
	if (!ReplacementPawn.IsValid())
	{
		AMiniPlayerState* PS = Task23PlayerState(PC);
		AMiniCharacter* Pawn = Task23Pawn(PC);
		SpawnObservedAt = GetWorld()->GetTimeSeconds();
		if (SpawnObservedAt - DeathAt < 2.9 || PS->GetCurrentLifeId() != SavedLifeId + 1 ||
			PS->GetCurrentLifePawn() != Pawn || !FMath::IsNearlyEqual(PS->GetHealthSet()->GetHealth(), 100.0f, 0.01f) ||
			!Task23ASC(PC)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
		{ Fail(TEXT("respawn lost delay, single life increment, health or protection")); return false; }
		ReplacementPawn = Pawn;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_RESPAWN: PlayerId=%d Life=%u Delay=%.3f NewAvatar=1 Protection=1"),
			PS->GetPlayerId(), PS->GetCurrentLifeId(), SpawnObservedAt - DeathAt);
	}
	return VerifyPlayerGround(PC);
}

bool UMiniTask23ProbeSubsystem::ProtectionExpired()
{
	AMiniPlayerController* PC = VictimController.Get();
	if (!Task23Ready(PC) || Task23Pawn(PC) != ReplacementPawn.Get())
	{ Fail(TEXT("respawn Avatar changed unexpectedly while awaiting protection expiry")); return false; }
	const double Elapsed = GetWorld()->GetTimeSeconds() - SpawnObservedAt;
	if (Task23ASC(PC)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
	{
		if (Elapsed > 2.5) { Fail(TEXT("fall recovery or repeated initialization extended spawn protection")); }
		return false;
	}
	if (Elapsed < 1.7) { Fail(TEXT("spawn protection ended before the configured duration")); return false; }
	return true;
}

bool UMiniTask23ProbeSubsystem::KillByGE(AMiniPlayerController* PC)
{
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	AMiniPlayerController* Host = Task23LocalController(GetWorld());
	if (!Task23Ready(PC) || !Task23Ready(Host) || Task23ASC(PC)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return false; }
	VictimController = PC;
	SavedPawn = Task23Pawn(PC);
	SavedLifeId = Task23PlayerState(PC)->GetCurrentLifeId();
	SavedStats = Task23PlayerState(PC)->GetMatchStats();
	SavedHostKills = Task23PlayerState(Host)->GetMatchStats().Kills;
	DeathAt = GetWorld()->GetTimeSeconds();
	ReplacementPawn.Reset();
	if (!GM->TryApplyTestDamage(Host, SavedPawn.Get(), 150.0f) || !SavedPawn->GetHealthComponent()->IsDead() ||
		Task23PlayerState(PC)->GetMatchStats().Deaths != SavedStats.Deaths + 1 ||
		Task23PlayerState(Host)->GetMatchStats().Kills != SavedHostKills + 1)
	{ Fail(TEXT("real combat GE did not submit one kill and death")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_COMBAT_GE: Round=%d Kill=%d Victim=%d Life=%u"),
		SavedStats.RoundId, SavedHostKills + 1, Task23PlayerState(PC)->GetPlayerId(), SavedLifeId);
	return true;
}

void UMiniTask23ProbeSubsystem::MakeSpawnBlockers()
{
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		AActor* Blocker = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), It->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!Blocker) { Fail(TEXT("spawn blocking fixture failed")); return; }
		UBoxComponent* Box = NewObject<UBoxComponent>(Blocker);
		Blocker->SetRootComponent(Box);
		Blocker->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(110, 110, 180));
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Blocker->SetActorLocation(It->GetActorLocation());
		SpawnBlockers.Add(Blocker);
	}
	if (SpawnBlockers.Num() != 8) { Fail(TEXT("did not block all eight actual PlayerStarts")); }
}

void UMiniTask23ProbeSubsystem::RemoveSpawnBlockers()
{
	for (const TWeakObjectPtr<AActor>& Blocker : SpawnBlockers)
	{
		if (Blocker.IsValid()) { Blocker->Destroy(); }
	}
	SpawnBlockers.Reset();
}

void UMiniTask23ProbeSubsystem::TickServer()
{
	UWorld* World = GetWorld();
	if (World->GetNetMode() != NM_ListenServer) { Fail(TEXT("Arena mode requires a real Listen Server")); return; }
	AMiniGameState* GS = World->GetGameState<AMiniGameState>();
	AMiniGameMode* GM = World->GetAuthGameMode<AMiniGameMode>();
	AMiniPlayerController* Host = Task23LocalController(World);
	UMiniArenaRulesComponent* Arena = GS ? GS->FindComponentByClass<UMiniArenaRulesComponent>() : nullptr;
	UMiniMatchRulesComponent* Rules = GS ? GS->FindComponentByClass<UMiniMatchRulesComponent>() : nullptr;
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
	if (!GS || !GM || !Host || !Arena || !Rules || !Phases || !Matches) { return; }
	const double Now = World->GetTimeSeconds();
	const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
	const FMiniMatchState Match = Matches->GetCurrentMatchState();
	DiscoverOwners();
	if (bDone) { return; }
	if (ServerStep == 0)
	{
		if (!Task23Ready(Host) || !VerifyPlayerGround(Host)) { return; }
		if (!bMapVerified)
		{
			if (!VerifyMap(true, true)) { return; }
			bMapVerified = true;
		}
		if (Match.ConnectedPlayerCount != 1 || Match.RoundId != 0 || Match.bAcceptingScores ||
			Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Warmup || Phase.PhaseEndTimeServer != 0.0)
		{ Fail(TEXT("single map-default arena player did not remain in Warmup")); return; }
		if (!BeginFall(Host)) { return; }
		ServerStep = 1;
		return;
	}
	if (ServerStep == 1)
	{
		if (!AwaitSafeRecovery()) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_WARMUP_FALL_PASS: ActualKillZ=1 SameLife=1 NoDeath=1"));
		MakeSpawnBlockers();
		if (bDone || !BeginFall(Host)) { return; }
		ServerStep = 2;
		return;
	}
	if (ServerStep == 2)
	{
		if (Now - StepStartedAt < (bBlockedCleanupVerified ? 0.6 : 1.2)) { return; }
		if (GM->GetPendingOutOfWorldRecoveryCount() != 1 || GM->GetPendingRespawnCount() ||
			Task23Pawn(Host) != SavedPawn.Get() || Task23PlayerState(Host)->GetCurrentLifeId() != SavedLifeId ||
			!Task23SameStats(Task23PlayerState(Host)->GetMatchStats(), SavedStats) || SavedPawn->GetHealthComponent()->IsDead() ||
			!FMath::IsNearlyEqual(Task23PlayerState(Host)->GetHealthSet()->GetHealth(), SavedHealth, 0.01f) ||
			SavedPawn->GetEquipmentManager()->GetCurrentEquipment() != SavedEquipment.Get() ||
			SavedPawn->GetActorLocation().Z >= World->GetWorldSettings()->KillZ ||
			SavedPawn->GetCharacterMovement()->MovementMode != MOVE_None)
		{ Fail(TEXT("fully blocked fall failed to retain one pending same-life recovery")); return; }
		if (!bBlockedCleanupVerified)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_BLOCKED_FALL_PASS: StartsBlocked=8 SamePawn=1 SameLife=1 Health=1 Scores=1 Equipment=1 Pending=1"));
			// Exercise the real shared cleanup entry used by match finalization,
			// round replacement and rules shutdown. This is not an Action/logout test.
			GM->CancelPendingRespawnsForMatch();
			if (GM->GetPendingOutOfWorldRecoveryCount() || GM->GetPendingRespawnCount() ||
				Task23Pawn(Host) != SavedPawn.Get() || Task23PlayerState(Host)->GetCurrentLifeId() != SavedLifeId ||
				!Task23SameStats(Task23PlayerState(Host)->GetMatchStats(), SavedStats) ||
				!FMath::IsNearlyEqual(Task23PlayerState(Host)->GetHealthSet()->GetHealth(), SavedHealth, 0.01f) ||
				SavedPawn->GetEquipmentManager()->GetCurrentEquipment() != SavedEquipment.Get() ||
				SavedPawn->GetCharacterMovement()->MovementMode == MOVE_None)
			{ Fail(TEXT("shared match cleanup retained recovery or changed the live Pawn")); return; }
			bBlockedCleanupVerified = true;
			StepStartedAt = Now;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_BLOCKED_CLEANUP_PASS: SharedProductionEntry=1 PendingRecovery=0 PendingRespawn=0 SameLife=1 Health=1 Scores=1 Equipment=1 OwnedMovementPauseReleased=1"));
			return;
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_RECOVERY_REQUEUED_PASS: ActualNextMovementTick=1 StartsStillBlocked=8 NewRecovery=1 SameLife=1 DuplicateRecovery=0"));
		RemoveSpawnBlockers();
		StepStartedAt = Now;
		ServerStep = 3;
		return;
	}
	if (ServerStep == 3)
	{
		if (!AwaitSafeRecovery()) { return; }
		ServerStep = 4;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_JOIN_READY: BlockersReleased=1 SamePawnRecovered=1 ClientsMayJoin=3"));
		return;
	}
	if (ServerStep == 4)
	{
		if (OwnerProbes.Num() != 3 || Match.ConnectedPlayerCount != 4 || Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing) { return; }
		Participants.Reset();
		Participants.Add(Host);
		for (const TWeakObjectPtr<AMiniTask23ProbeActor>& Probe : OwnerProbes) { Participants.Add(Cast<AMiniPlayerController>(Probe->GetOwner())); }
		for (const TWeakObjectPtr<AMiniPlayerController>& PC : Participants) { if (!VerifyPlayerGround(PC.Get())) { return; } }
		if (Match.RoundId != 1 || !Match.bAcceptingScores || Match.bHasResult || Match.ScoreLimit != 10 ||
			FMath::Abs(Phase.PhaseEndTimeServer - Phase.PhaseStartTimeServer - 300.0) > 0.1)
		{ Fail(TEXT("four-player arena used nonproduction match/phase configuration")); return; }
		if (!Checkpoint(TEXT("ArenaReady"))) { return; }
		InitialRound = Match.RoundId;
		if (Task23ASC(Participants[1].Get())->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
		if (!BeginFall(Participants[1].Get())) { return; }
		ServerStep = 5;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_FOUR_PLAYING_PASS: Players=4 Clients=3 Ground=1 MapDefaultArena=1 Deadline=300 HUD=1"));
		return;
	}
	if (ServerStep == 5)
	{
		if (!AwaitFallDeath()) { return; }
		ServerStep = 6;
		return;
	}
	if (ServerStep == 6)
	{
		if (!AwaitRespawn()) { return; }
		AMiniPlayerController* PC = VictimController.Get();
		Task23Pawn(PC)->GetHealthComponent()->InitializeWithAbilitySystem(Task23ASC(PC));
		const TWeakObjectPtr<AMiniCharacter> SpawnedPawn = ReplacementPawn;
		if (!BeginFall(PC)) { return; }
		ReplacementPawn = SpawnedPawn;
		ServerStep = 7;
		return;
	}
	if (ServerStep == 7)
	{
		if (!AwaitSafeRecovery()) { return; }
		if (!bFallProtected) { Fail(TEXT("protected fall fixture missed the actual protection GE")); return; }
		ServerStep = 8;
		return;
	}
	if (ServerStep == 8)
	{
		if (!ProtectionExpired()) { return; }
		EnvironmentDeaths = 1;
		ServerStep = 9;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_PROTECTED_FALL_PASS: SameLife=1 RealGEExpired=1 DuplicateInitNoRefresh=1"));
		return;
	}
	if (ServerStep == 9)
	{
		if (EnvironmentDeaths == 4)
		{
			for (const TWeakObjectPtr<AMiniPlayerController>& PC : Participants)
			{
				const FMiniPlayerMatchStats& Stats = Task23PlayerState(PC.Get())->GetMatchStats();
				if (Stats.Kills != 0 || Stats.Deaths != 1 || !VerifyPlayerGround(PC.Get()))
				{ Fail(TEXT("four fall deaths failed to restore every participant with zero kills")); return; }
			}
			if (!Checkpoint(TEXT("EnvironmentDeaths"))) { return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_ALL_FALLS_PASS: Players=4 EnvironmentDeaths=4 PlayerKills=0 Respawn=3 Ground=1"));
			ServerStep = 12;
			return;
		}
		AMiniPlayerController* NextVictim = Participants[(EnvironmentDeaths + 1) % 4].Get();
		if (Task23ASC(NextVictim)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
		if (!BeginFall(NextVictim)) { return; }
		ServerStep = 10;
		return;
	}
	if (ServerStep == 10)
	{
		if (!AwaitFallDeath()) { return; }
		ServerStep = 11;
		return;
	}
	if (ServerStep == 11)
	{
		if (!AwaitRespawn() || !ProtectionExpired()) { return; }
		++EnvironmentDeaths;
		ServerStep = 9;
		return;
	}
	if (ServerStep == 12)
	{
		if (Match.RoundId != InitialRound + CompletedRounds || Match.bHasResult || !Match.bAcceptingScores)
		{ Fail(TEXT("continuous scoring left the expected open round")); return; }
		if (!KillByGE(Participants[1 + CombatKills % 3].Get())) { return; }
		++CombatKills;
		ServerStep = CombatKills == 10 ? 14 : 13;
		return;
	}
	if (ServerStep == 13)
	{
		if (!AwaitRespawn() || !ProtectionExpired()) { return; }
		ServerStep = 12;
		return;
	}
	if (ServerStep == 14)
	{
		if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_PostMatch) { return; }
		int32 TotalKills = 0, TotalDeaths = 0;
		for (const FMiniMatchPlayerRow& Row : Match.ResultRows) { TotalKills += Row.Kills; TotalDeaths += Row.Deaths; }
		if (!Match.bHasResult || Match.bAcceptingScores || Match.EndReason != EMiniMatchEndReason::ScoreLimit || Match.bIsDraw ||
			Match.WinnerPlayerIds.Num() != 1 || Match.WinnerPlayerIds[0] != Task23PlayerState(Host)->GetPlayerId() ||
			Match.ResultRows.Num() != 4 || TotalKills != 10 || TotalDeaths != (CompletedRounds == 0 ? 14 : 10) ||
			GM->GetPendingRespawnCount() || GM->GetPendingOutOfWorldRecoveryCount())
		{ Fail(TEXT("ten real kills did not freeze the four-player result with correct environmental totals")); return; }
		PreviousRoundLives.Reset();
		for (const TWeakObjectPtr<AMiniPlayerController>& PC : Participants) { PreviousRoundLives.Add(Task23PlayerState(PC.Get())->GetCurrentLifeId()); }
		PublishedMatch = Match;
		if (!BeginFall(Host)) { return; }
		ServerStep = 15;
		return;
	}
	if (ServerStep == 15)
	{
		if (!MiniTask23SameMatch(PublishedMatch, Match) || Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_PostMatch)
		{ Fail(TEXT("PostMatch fall altered the frozen result or missed its window")); return; }
		if (!AwaitSafeRecovery()) { return; }
		++CompletedRounds;
		ServerStep = 16;
		return;
	}
	if (ServerStep == 16)
	{
		if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_PostMatch)
		{ Fail(TEXT("clients did not acknowledge the real five-second result window")); return; }
		const FName Name = CompletedRounds == 1 ? FName(TEXT("Result1")) : FName(TEXT("Result2"));
		if (!Checkpoint(Name, false)) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_RESULT_PASS: CompletedRound=%d Players=4 RealKills=10 Clients=3 Frozen=1 PostFallSameLife=1 PendingRespawn=0"), CompletedRounds);
		ServerStep = 17;
		return;
	}
	if (ServerStep == 17)
	{
		if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing || Match.RoundId != InitialRound + CompletedRounds) { return; }
		if (Match.bHasResult || !Match.bAcceptingScores || Match.ConnectedPlayerCount != 4 || GM->GetPendingOutOfWorldRecoveryCount())
		{ Fail(TEXT("automatic next round retained result or recovery work")); return; }
		for (int32 Index = 0; Index < Participants.Num(); ++Index)
		{
			AMiniPlayerController* PC = Participants[Index].Get();
			if (!VerifyPlayerGround(PC)) { return; }
			const AMiniPlayerState* PS = Task23PlayerState(PC);
			if (PS->GetCurrentLifeId() != PreviousRoundLives[Index] + 1 || PS->GetMatchStats().RoundId != Match.RoundId ||
				PS->GetMatchStats().Kills || PS->GetMatchStats().Deaths)
			{ Fail(TEXT("next round did not create exactly one new life and clear every score")); return; }
		}
		if (!Checkpoint(CompletedRounds == 1 ? FName(TEXT("Round2")) : FName(TEXT("Round3")))) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe SERVER_NEXT_ROUND_PASS: Round=%d Players=4 ScoresCleared=1 NewLives=4 Ground=1 Clients=3"), Match.RoundId);
		if (CompletedRounds == 2)
		{
			StepStartedAt = Now;
			ServerStep = 18;
			return;
		}
		CombatKills = 0;
		ServerStep = 12;
	}
	if (ServerStep == 18)
	{
		// Allow the optional client overview's new camera frame and screenshot to finish.
		if (Now - StepStartedAt < 5.0) { return; }
		for (const TWeakObjectPtr<AMiniPlayerController>& PC : Participants)
		{
			if (!VerifyPlayerGround(PC.Get()) || Task23PlayerState(PC.Get())->GetMatchStats().Kills ||
				Task23PlayerState(PC.Get())->GetMatchStats().Deaths)
			{ Fail(TEXT("third-round players became stuck or acquired unexpected scores during the final observation")); return; }
		}
		bDone = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe ARENA_PASS: MapDefaultArena=1 Starts=8 Routes=3 Listen=1 Clients=3 CompletedRounds=2 RealGEKills=20 EnvironmentDeaths=4 WarmupFall=1 ProtectedFall=1 PostFall=2 BlockedAndReleased=1 SharedRecoveryCleanup=1 SameLifeRecovery=1 Respawn3=1 ProtectionNoRefresh=1 HUD=1"));
	}
}

bool UMiniTask23ProbeSubsystem::RequestMedia(AMiniTask23ProbeActor* Probe)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask23Media")) ||
		(Probe->GetCheckpointName() != TEXT("ArenaReady") && Probe->GetCheckpointName() != TEXT("Result1") && Probe->GetCheckpointName() != TEXT("Result2"))) { return true; }
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return false; }
#endif
	FString Directory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask23MediaOutput="), Directory)) { Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("Task23-Owner%d-%s.png"), Probe->GetOwnerIndex(), *Probe->GetCheckpointName().ToString()));
	FScreenshotRequest::RequestScreenshot(Path, true, false);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe CLIENT_SCREENSHOT: Path=%s"), *Path);
	return true;
}

void UMiniTask23ProbeSubsystem::BeginOverview()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask23Media"))) { bDone = true; return; }
	const FVector CameraLocation(-2900, -2400, 3200);
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CameraLocation,
		(FVector::ZeroVector - CameraLocation).Rotation(), Params);
	if (!Camera) { Fail(TEXT("diagnostic overview camera spawn failed")); return; }
	Camera->GetCameraComponent()->SetFieldOfView(70.0f);
	Task23LocalController(GetWorld())->SetViewTarget(Camera);
	OverviewStartedAt = GetWorld()->GetTimeSeconds();
	bOverviewPending = true;
}

void UMiniTask23ProbeSubsystem::LogClientWait(const TCHAR* Reason, AMiniTask23ProbeActor* Probe)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - ClientWaitLoggedAt < 5.0) { return; }
	ClientWaitLoggedAt = Now;
	AMiniPlayerController* PC = Task23LocalController(GetWorld());
	AMiniPlayerState* PS = Task23PlayerState(PC);
	AMiniCharacter* Pawn = Task23Pawn(PC);
	UMiniAbilitySystemComponent* ASC = Task23ASC(PC);
	UMiniHeroComponent* Hero = Pawn ? Pawn->GetHeroComponent() : nullptr;
	UMiniHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
	UMiniHUDViewModel* VM = Task23HUD(PC);
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>();
	const FMiniGamePhaseState Phase = Phases ? Phases->GetCurrentPhaseState() : FMiniGamePhaseState();
	const FMiniMatchState Match = Matches ? Matches->GetCurrentMatchState() : FMiniMatchState();
	UE_LOG(LogMiniInit, Display,
		TEXT("MiniTask23Probe CLIENT_WAIT: Reason=%s Owner=%d Checkpoint=%s Serial=%d Pawn=%s Expected=%s Avatar=%s Life=%u/%u Phase=%d Match=%d Round=%d/%d Ready=%d HeroInput=%d Bindings=%d DataInit=%d InputComponent=%s PCBlocked=%d Dead=%d DeadTag=%d Health=%.1f Equipment=%s HUDPawn=%s HUDHealth=%d HUDAmmo=%d MovingOnGround=%d Z=%.1f"),
		Reason, Probe ? Probe->GetOwnerIndex() : 0, Probe ? *Probe->GetCheckpointName().ToString() : TEXT("None"),
		Probe ? Probe->GetCheckpointSerial() : 0, *GetNameSafe(Pawn), *GetNameSafe(Probe ? Probe->GetExpectedPawn() : nullptr),
		*GetNameSafe(ASC ? ASC->GetAvatarActor() : nullptr), PS ? PS->GetCurrentLifeId() : 0, Probe ? Probe->GetExpectedLifeId() : 0,
		Probe && MiniTask23SamePhase(Phase, Probe->GetExpectedPhase()) ? 1 : 0,
		Probe && MiniTask23SameMatch(Match, Probe->GetExpectedMatch()) ? 1 : 0, Match.RoundId, PS ? PS->GetMatchStats().RoundId : -1,
		Task23Ready(PC) ? 1 : 0, Hero && Hero->IsInputActive() ? 1 : 0, Hero ? Hero->GetInputBindingCount() : -1,
		Hero && Hero->HasReachedInitState(MiniGameplayTags::InitState_DataInitialized) ? 1 : 0,
		*GetNameSafe(Pawn ? Pawn->GetPlayerInputComponent() : nullptr), PC && PC->IsMiniInputBlocked() ? 1 : 0,
		Health && Health->IsDead() ? 1 : 0, ASC ? ASC->GetTagCount(MiniGameplayTags::State_Dead) : -1,
		PS && PS->GetHealthSet() ? PS->GetHealthSet()->GetHealth() : -1.0f,
		*GetNameSafe(Pawn && Pawn->GetEquipmentManager() ? Pawn->GetEquipmentManager()->GetCurrentEquipment() : nullptr),
		*GetNameSafe(VM ? VM->GetSnapshot().Pawn.Get() : nullptr), VM && VM->GetSnapshot().bHealthReady ? 1 : 0,
		VM && VM->GetSnapshot().bAmmoReady ? 1 : 0, Pawn && Pawn->GetCharacterMovement()->IsMovingOnGround() ? 1 : 0,
		Pawn ? Pawn->GetActorLocation().Z : 0.0f);
}

void UMiniTask23ProbeSubsystem::TickClient()
{
	UWorld* World = GetWorld();
	if (bOverviewPending)
	{
		if (World->GetTimeSeconds() - OverviewStartedAt < 1.0) { return; }
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
		{
			if (World->GetTimeSeconds() - OverviewStartedAt > 3.5) { Fail(TEXT("overview shaders did not finish inside final observation")); }
			return;
		}
#endif
		FString Directory;
		if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask23MediaOutput="), Directory)) { Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, TEXT("Task23-ArenaOverview.png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe CLIENT_OVERVIEW_SCREENSHOT: Path=%s ActualArenaMap=1"), *Path);
		bOverviewPending = false;
		bDone = true;
		return;
	}
	AMiniPlayerController* PC = Task23LocalController(World);
	AMiniPlayerState* PS = Task23PlayerState(PC);
	AMiniGameState* GS = World->GetGameState<AMiniGameState>();
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
	UMiniHUDViewModel* VM = Task23HUD(PC);
	AMiniTask23ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask23ProbeActor> It(World); It; ++It)
	{
		if (It->GetOwner() == PC && It->GetOwnerIndex() > 0) { Probe = *It; break; }
	}
	if (!PC || !PS || !GS || !Phases || !Matches || !VM || !VM->IsRunning() || !Probe ||
		Probe->GetCheckpointSerial() <= 0)
	{ LogClientWait(TEXT("OwnerSources"), Probe); return; }
	if (Probe->GetCheckpointSerial() == ClientLastSerial) { return; }
	if (!bClientMapVerified)
	{
		if (!VerifyMap(true, false)) { LogClientWait(TEXT("MapLoad"), Probe); return; }
		bClientMapVerified = true;
	}
	const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
	const FMiniMatchState Match = Matches->GetCurrentMatchState();
	const FMiniHUDSnapshot& Snapshot = VM->GetSnapshot();
	if (!GS->FindComponentByClass<UMiniArenaRulesComponent>() || !GS->FindComponentByClass<UMiniMatchRulesComponent>() ||
		!MiniTask23SamePhase(Phase, Probe->GetExpectedPhase()) || !MiniTask23SameMatch(Match, Probe->GetExpectedMatch()) ||
		!Snapshot.bHasMatchData || !MiniTask23SameMatch(Snapshot.MatchState, Match) || Snapshot.PhaseTag != Phase.PhaseTag ||
		PS->GetCurrentLifeId() != Probe->GetExpectedLifeId() || PC->GetPawn() != Probe->GetExpectedPawn())
	{ LogClientWait(TEXT("ReplicatedState"), Probe); return; }
	const FMiniMatchPlayerRow* Row = Match.Rows.FindByPredicate([PS](const FMiniMatchPlayerRow& Entry) { return Entry.PlayerId == PS->GetPlayerId(); });
	const FMiniPlayerMatchStats Stats = PS->GetMatchStats();
	if (!Row || Stats.RoundId != Match.RoundId || Stats.Kills != Row->Kills || Stats.Deaths != Row->Deaths ||
		Snapshot.Score != Row->Kills || Snapshot.Deaths != Row->Deaths)
	{ LogClientWait(TEXT("Scores"), Probe); return; }
	if (Probe->RequiresLivePawn() && (!Task23Ready(PC) || Snapshot.Pawn != Task23Pawn(PC) ||
		!Snapshot.bHealthReady || !Snapshot.bAmmoReady || !VerifyPlayerGround(PC)))
	{ LogClientWait(TEXT("LivePawnInputGroundHUD"), Probe); return; }
	const int32 Remaining = FMath::CeilToInt(FMath::Max(0.0, Phase.PhaseEndTimeServer - GS->GetServerWorldTimeSeconds()));
	if (!VM->IsPhaseCountdownRunning() || FMath::Abs(Snapshot.PhaseRemainingSeconds - Remaining) > 1)
	{ LogClientWait(TEXT("Countdown"), Probe); return; }
	if (!RequestMedia(Probe)) { LogClientWait(TEXT("ShaderQuiet"), Probe); return; }
	ClientLastSerial = Probe->GetCheckpointSerial();
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe CLIENT_CHECKPOINT: Owner=%d Checkpoint=%s Round=%d Life=%u MapDefaultArena=1 HUD=1"),
		Probe->GetOwnerIndex(), *Probe->GetCheckpointName().ToString(), Match.RoundId, PS->GetCurrentLifeId());
	Probe->ServerAcknowledge(ClientLastSerial, Phase, Match, Stats, PS->GetCurrentLifeId(), Task23Pawn(PC), GS->GetServerWorldTimeSeconds(), true, true);
	if (Probe->GetCheckpointName() == TEXT("Round3")) { BeginOverview(); }
}

void UMiniTask23ProbeSubsystem::TickPractice()
{
	if (GetWorld()->GetNetMode() != NM_Standalone) { Fail(TEXT("Practice mode expects standalone")); return; }
	AMiniPlayerController* PC = Task23LocalController(GetWorld());
	AMiniGameState* GS = GetWorld()->GetGameState<AMiniGameState>();
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	UMiniHUDViewModel* VM = Task23HUD(PC);
	UMiniMatchSubsystem* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>();
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	if ((ServerStep == 0 && !Task23Ready(PC)) || !Task23PlayerState(PC) || !GS || !GM || !VM || !VM->IsRunning() ||
		!Matches || !Phases || !VerifyMap(false, false)) { return; }
	int32 Targets = 0;
	for (TActorIterator<AMiniPracticeTarget> It(GetWorld()); It; ++It) { Targets += It->IsTargetEnabled() ? 1 : 0; }
	if (Targets < 1) { return; }
	if (GS->FindComponentByClass<UMiniArenaRulesComponent>() || GS->FindComponentByClass<UMiniMatchRulesComponent>() ||
		Matches->HasMatchContext() || Phases->HasArenaContext() || VM->GetSnapshot().bHasMatchData || VM->GetSnapshot().bHasPhaseData ||
		Task23ASC(PC)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
	{ Fail(TEXT("training map acquired Arena rules, HUD or spawn protection")); return; }
	AMiniPlayerState* PS = Task23PlayerState(PC);
	if (ServerStep == 0)
	{
		const float Health = PS->GetHealthSet()->GetHealth();
		if (!GM->TryApplyEnvironmentDamage(Task23Pawn(PC), 25.0f) || !FMath::IsNearlyEqual(PS->GetHealthSet()->GetHealth(), Health - 25.0f, 0.01f) ||
			PS->GetMatchStats().Kills || PS->GetMatchStats().Deaths)
		{ Fail(TEXT("training damage chain changed or acquired competitive scoring")); return; }
		if (!BeginFall(PC)) { return; }
		ServerStep = 1;
		return;
	}
	if (ServerStep == 1)
	{
		// A training fall has a real death and delayed respawn but no FFA statistics.
		AMiniCharacter* Pawn = Task23Pawn(PC);
		if (!Pawn || Pawn != SavedPawn.Get()) { Fail(TEXT("training FellOutOfWorld bypassed the Pawn lifecycle")); return; }
		if (!Pawn->GetHealthComponent()->IsDead())
		{
			if (GetWorld()->GetTimeSeconds() - DeathAt > 2.0) { Fail(TEXT("training real fall did not apply environmental death")); }
			return;
		}
		if (PS->GetCurrentLifeId() != SavedLifeId || !Task23SameStats(PS->GetMatchStats(), SavedStats) ||
			PS->GetHealthSet()->GetHealth() > 0.01f || GM->GetPendingRespawnCount() != 1)
		{ Fail(TEXT("training fall changed competitive statistics or lost delayed respawn")); return; }
		ServerStep = 2;
		return;
	}
	if (ServerStep == 2)
	{
		if (!Task23Ready(PC) || Task23Pawn(PC) == SavedPawn.Get())
		{
			if (GetWorld()->GetTimeSeconds() - DeathAt > 8.0) { Fail(TEXT("training real fall did not restore a new live Avatar")); }
			return;
		}
		if (!Task23Pawn(PC)->GetCharacterMovement()->IsMovingOnGround() || VM->GetSnapshot().Pawn != Task23Pawn(PC) || !VM->GetSnapshot().bHealthReady) { return; }
		if (GetWorld()->GetTimeSeconds() - DeathAt < 2.9 || PS->GetCurrentLifeId() != SavedLifeId + 1 ||
			!FMath::IsNearlyEqual(PS->GetHealthSet()->GetHealth(), 100.0f, 0.01f) || !Task23SameStats(PS->GetMatchStats(), SavedStats) ||
			GM->GetPendingRespawnCount() || GM->GetPendingOutOfWorldRecoveryCount())
		{ Fail(TEXT("training fall did not preserve no-score mode, three-second delay and single new life")); return; }
		bDone = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask23Probe PRACTICE_PASS: MapDefaultPractice=1 TrainingTargets=%d RealGE=1 RealFall=1 Respawn3=1 Ground=1 ArenaRules=0 MatchSource=0 PhaseSource=0 FFAHUD=0 Protection=0"), Targets);
	}
}
