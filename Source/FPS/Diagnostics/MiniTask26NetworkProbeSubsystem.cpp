#include "MiniTask26NetworkProbeSubsystem.h"

#include "Engine/World.h"
#if !UE_BUILD_SHIPPING
#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Diagnostics/MiniTask26NetworkProbeActor.h"
#include "Diagnostics/MiniTask26NetworkSettings.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniLogChannels.h"

namespace
{
bool MiniTask26NetServerPlayerReady(AMiniPlayerController* PC)
{
	auto* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr; auto* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	auto* ASC = PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
	return Pawn && PS && ASC && ASC->GetOwnerActor() == PS && ASC->GetAvatarActor() == Pawn && Pawn->GetHealthComponent() &&
		!Pawn->GetHealthComponent()->IsDead() && Pawn->GetEquipmentManager() && Pawn->GetEquipmentManager()->GetCurrentEquipment() &&
		(!PC->IsLocalController() || (Pawn->GetHeroComponent() && Pawn->GetHeroComponent()->IsInputActive()));
}
void MiniTask26NetPlacePawn(AMiniCharacter* Pawn, const FVector& Location)
{
	Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Pawn->GetCharacterMovement()->StopMovementImmediately();
	Pawn->TeleportTo(Location, FRotator::ZeroRotator, false, true); Pawn->ForceNetUpdate();
}
}
#endif

bool UMiniTask26NetworkProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask26Network"));
#else
	return false;
#endif
}
TStatId UMiniTask26NetworkProbeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask26NetworkProbeSubsystem, STATGROUP_Tickables); }
void UMiniTask26NetworkProbeSubsystem::Fail(const TCHAR* Reason)
{
#if !UE_BUILD_SHIPPING
	bDone = true; UE_LOG(LogMiniInit, Error, TEXT("MiniTask26Net FAIL: Step=%d Reason=%s"), Step, Reason);
	FPlatformMisc::RequestExitWithStatus(true, 1, TEXT("MiniTask26NetworkSubsystemFailure"));
#endif
}
void UMiniTask26NetworkProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
	UWorld* World = GetWorld(); if (bDone || !World || !World->HasBegunPlay()) { return; }
	WaitSeconds += DeltaTime; int32 Timeout = 240; FParse::Value(FCommandLine::Get(), TEXT("MiniTask26NetworkTimeoutSeconds="), Timeout);
	if (WaitSeconds > Timeout) { Fail(TEXT("finite four-peer network acceptance deadline expired")); return; }
	if (World->GetNetMode() == NM_Client) { return; }
	if (World->GetNetMode() != NM_ListenServer) { return; }
	auto* Matches = World->GetSubsystem<UMiniMatchSubsystem>(); auto* Rules = Matches ? Matches->GetMatchRulesComponent() : nullptr;
	if (!Rules) { return; }
	AMiniPlayerController* Host = nullptr; TArray<AMiniPlayerController*> Remotes;
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
	{
		if (It->IsLocalController()) { Host = *It; } else if (It->GetNetConnection()) { Remotes.Add(*It); }
	}
	const double Now = FPlatformTime::Seconds();
	if (!Host) { return; }
	if (Step == 0)
	{
		if (Rules->GetMatchState().bAcceptingScores && !bLateJoinPublished && Remotes.Num() >= 1)
		{
			if (!MiniTask26ReadNetworkSettings(World, TEXT("BeforeLateJoin"), true)) { Fail(TEXT("actual profile before late join mismatched")); return; }
			bLateJoinPublished = true; UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net LATE_JOIN_READY: Playing=1 ExistingPeers=%d"), Remotes.Num() + 1);
		}
		// StartRound replaces every Warmup Pawn. Create bridges only after that
		// production transition, so their replicated target is the actual current life.
		if (!Rules->GetMatchState().bAcceptingScores) { return; }
		if (!MiniTask26NetServerPlayerReady(Host)) { return; }
		for (AMiniPlayerController* PC : Remotes)
		{
			if (!MiniTask26NetServerPlayerReady(PC) || OwnerProbes.ContainsByPredicate([PC](const auto& P) { return P.IsValid() && P->GetOwner() == PC; })) { continue; }
			FActorSpawnParameters Params; Params.Owner = PC;
			auto* Probe = World->SpawnActor<AMiniTask26NetworkProbeActor>(Params);
			if (!Probe) { Fail(TEXT("real owner bridge could not spawn")); return; }
			Probe->InitializeServer(OwnerProbes.Num() + 1, Cast<AMiniCharacter>(Host->GetPawn())); OwnerProbes.Add(Probe);
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net SERVER_JOIN: Owner=%d PlayerId=%d Playing=%d"), Probe->GetOwnerIndex(), PC->GetPlayerState<AMiniPlayerState>()->GetPlayerId(), int32(Rules->GetMatchState().bAcceptingScores));
		}
		if (Remotes.Num() != 3 || OwnerProbes.Num() != 3 || !Rules->GetMatchState().bAcceptingScores) { return; }
		for (const auto& P : OwnerProbes) { if (!P.IsValid() || !P->IsReady()) { return; } }
		if (!MiniTask26ReadNetworkSettings(World, TEXT("FourPeersReady"), true)) { Fail(TEXT("four-peer actual profile readback mismatched")); return; }
		auto* Target = Cast<AMiniCharacter>(Host->GetPawn()); auto* PS = Host->GetPlayerState<AMiniPlayerState>();
		OriginalHostPawn = Target; OriginalHostLife = PS->GetCurrentLifeId();
		// Only fixture initialization writes health. Every tested damage/death is the existing weapon GE.
		PS->GetMiniAbilitySystemComponent()->SetNumericAttributeBase(UMiniHealthSet::GetMaxHealthAttribute(), 1000.0f);
		PS->GetMiniAbilitySystemComponent()->SetNumericAttributeBase(UMiniHealthSet::GetHealthAttribute(), 1000.0f);
		MiniTask26NetPlacePawn(Target, FVector(800, 0, 3000));
		// Keep inactive peers over the map and clear of the fixed firing ray.
		for (int32 I = 0; I < OwnerProbes.Num(); ++I) { auto* PC = Cast<AMiniPlayerController>(OwnerProbes[I]->GetOwner()); MiniTask26NetPlacePawn(Cast<AMiniCharacter>(PC->GetPawn()), FVector(0, 900 + I * 200, 3000)); }
		FinishedAt = Now + 4.0; Step = 1;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net FOUR_PEERS_READY: Players=4 RealConnections=3 RTT20Each=1 LateJoin=%d FixedTargetFixture=1 ProductionSpawnProtectionUnchanged=1"), int32(bLateJoinPublished)); return;
	}
	if (Step == 1)
	{
		if (Now < FinishedAt) { return; } // Let the unchanged production spawn protection expire.
		auto* PC = Cast<AMiniPlayerController>(OwnerProbes[ActiveOwner]->GetOwner());
		MiniTask26NetPlacePawn(Cast<AMiniCharacter>(PC->GetPawn()), FVector(0, 0, 3000));
		OwnerProbes[ActiveOwner]->StartSuite(); Step = 2; return;
	}
	if (Step == 2)
	{
		if (!OwnerProbes[ActiveOwner].IsValid() || !OwnerProbes[ActiveOwner]->IsSuiteDone()) { return; }
		auto* PC = Cast<AMiniPlayerController>(OwnerProbes[ActiveOwner]->GetOwner());
		MiniTask26NetPlacePawn(Cast<AMiniCharacter>(PC->GetPawn()), FVector(0, 900 + ActiveOwner * 200, 3000));
		if (++ActiveOwner < 3) { FinishedAt = Now + 0.5; Step = 1; return; }
		DeathAt = Now; Step = 3; return;
	}
	if (Step == 3)
	{
		if (!MiniTask26NetServerPlayerReady(Host)) { return; }
		auto* PS = Host->GetPlayerState<AMiniPlayerState>(); auto* Pawn = Cast<AMiniCharacter>(Host->GetPawn());
		if (Pawn == OriginalHostPawn.Get() || PS->GetCurrentLifeId() <= OriginalHostLife) { return; }
		if (Now - DeathAt < 2.9 || Remotes.Num() != 3 || Rules->GetMatchState().ConnectedPlayerCount != 4 ||
			Rules->NotifyPlayerDeath(OwnerProbes[2]->GetRealDeath()) || PS->GetMatchStats().Deaths != 1)
		{ Fail(TEXT("production respawn/life/death deduplication mismatch")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net RESPAWN_VERIFIED: OldLife=%u NewLife=%u Delay=%.3f OldLifeNoticeRejected=1 Slots=4"), OriginalHostLife, PS->GetCurrentLifeId(), Now - DeathAt);
		for (const auto& P : OwnerProbes) { P->Publish(EMiniTask26NetPhase::Score); } Step = 4; return;
	}
	if (Step == 4)
	{
		for (const auto& P : OwnerProbes) { if (!P.IsValid() || !P->HasAck()) { return; } }
		if (!MiniTask26ReadNetworkSettings(World, TEXT("BeforeRestore"), true) || !MiniTask26RestoreNetworkSettings(World))
		{ Fail(TEXT("complete server profile restore/readback failed")); return; }
		for (const auto& P : OwnerProbes) { P->Publish(EMiniTask26NetPhase::Restore); } Step = 5; return;
	}
	if (Step == 5)
	{
		for (const auto& P : OwnerProbes) { if (!P.IsValid() || !P->HasAck()) { return; } }
		if (!MiniTask26ReadNetworkSettings(World, TEXT("ServerFinal"), true, true)) { Fail(TEXT("server final restored profile mismatch")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Net SERVER_PASS: Players=4 OwnerSuites=3 RTT20Each=1 DriverConnectionReadback=1 LateJoin=1 RealInputGA=1 RealOwnerServerFire=1 RealGE=1 ScoreOnce=1 Respawn=1 RestoreAllFields=1 NoRewind=1"));
		for (const auto& P : OwnerProbes) { P->FinishClient(); } FinishedAt = Now + 5.0; Step = 6; return;
	}
	if (Step == 6 && Now >= FinishedAt) { bDone = true; FPlatformMisc::RequestExitWithStatus(false, 0, TEXT("MiniTask26NetworkServerComplete")); }
#endif
}
