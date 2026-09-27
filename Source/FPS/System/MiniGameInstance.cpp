#include "MiniGameInstance.h"

#include "Components/GameFrameworkComponentManager.h"
#include "Engine/World.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ModularPawn.h"
#include "MiniGameplayTags.h"
#include "MiniLogChannels.h"

UMiniGameInstance::UMiniGameInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMiniGameInstance::Init()
{
	Super::Init();

	UGameFrameworkComponentManager* ComponentManager = GetSubsystem<UGameFrameworkComponentManager>();
	if (!ensureMsgf(ComponentManager, TEXT("Mini initialization requires ModularGameplay.")))
	{
		return;
	}

	ComponentManager->RegisterInitState(MiniGameplayTags::InitState_Spawned, false, FGameplayTag());
	ComponentManager->RegisterInitState(MiniGameplayTags::InitState_DataAvailable, false, MiniGameplayTags::InitState_Spawned);
	ComponentManager->RegisterInitState(MiniGameplayTags::InitState_DataInitialized, false, MiniGameplayTags::InitState_DataAvailable);
	ComponentManager->RegisterInitState(MiniGameplayTags::InitState_GameplayReady, false, MiniGameplayTags::InitState_DataInitialized);

	const bool bValidStateOrder =
		ComponentManager->IsInitStateAfterOrEqual(MiniGameplayTags::InitState_DataAvailable, MiniGameplayTags::InitState_Spawned) &&
		ComponentManager->IsInitStateAfterOrEqual(MiniGameplayTags::InitState_DataInitialized, MiniGameplayTags::InitState_DataAvailable) &&
		ComponentManager->IsInitStateAfterOrEqual(MiniGameplayTags::InitState_GameplayReady, MiniGameplayTags::InitState_DataInitialized);

	if (ensureMsgf(bValidStateOrder, TEXT("Mini initialization state order is invalid.")))
	{
		UE_LOG(LogMiniInit, Display, TEXT("Registered InitState.Spawned -> DataAvailable -> DataInitialized -> GameplayReady"));
	}
}

void UMiniGameInstance::OnStart()
{
	Super::OnStart();

	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeReceivers")))
	{
		UWorld* World = GetWorld();
		if (World && World->IsGameWorld() && !World->HasBegunPlay())
		{
			ProbeWorld = World;
			ProbeWorldBeginPlayHandle = World->OnWorldBeginPlay.AddUObject(this, &ThisClass::HandleProbeWorldBeginPlay);
		}
		else
		{
			RunReceiverProbe();
		}
	}
}

void UMiniGameInstance::Shutdown()
{
	if (UWorld* World = ProbeWorld.Get())
	{
		World->OnWorldBeginPlay.Remove(ProbeWorldBeginPlayHandle);
	}
	ProbeWorldBeginPlayHandle.Reset();
	ProbeWorld.Reset();
	ReceiverProbeHandle.Reset();
	Super::Shutdown();
}

void UMiniGameInstance::HandleProbeWorldBeginPlay()
{
	if (UWorld* World = ProbeWorld.Get())
	{
		World->OnWorldBeginPlay.Remove(ProbeWorldBeginPlayHandle);
	}
	ProbeWorldBeginPlayHandle.Reset();
	ProbeWorld.Reset();
	RunReceiverProbe();
}

void UMiniGameInstance::RunReceiverProbe()
{
	const UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
	const bool bTagsValid =
		TagManager.RequestGameplayTag(FName(TEXT("InitState.Spawned")), false) == MiniGameplayTags::InitState_Spawned &&
		TagManager.RequestGameplayTag(FName(TEXT("InitState.DataAvailable")), false) == MiniGameplayTags::InitState_DataAvailable &&
		TagManager.RequestGameplayTag(FName(TEXT("InitState.DataInitialized")), false) == MiniGameplayTags::InitState_DataInitialized &&
		TagManager.RequestGameplayTag(FName(TEXT("InitState.GameplayReady")), false) == MiniGameplayTags::InitState_GameplayReady;
	if (!bTagsValid)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniTagProbe FAIL: a native InitState tag is missing or mismatched"));
		FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("MiniTagProbe"));
		return;
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTagProbe PASS: all four InitState tags are queryable"));

	UWorld* World = GetWorld();
	UGameFrameworkComponentManager* ComponentManager = GetSubsystem<UGameFrameworkComponentManager>();
	if (!World || !World->IsGameWorld() || !World->HasBegunPlay() || !ComponentManager)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniReceiverProbe FAIL: no started game world or component manager"));
		FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("MiniReceiverProbe"));
		return;
	}

	ReceiverProbeHandle = ComponentManager->AddExtensionHandler(
		TSoftClassPtr<AActor>(AModularPawn::StaticClass()),
		UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(this, &ThisClass::HandleReceiverProbeEvent));
	if (!ReceiverProbeHandle.IsValid())
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniReceiverProbe FAIL: extension handler could not be registered"));
		FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("MiniReceiverProbe"));
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AModularPawn* ProbePawn = World->SpawnActor<AModularPawn>(FVector(0, 0, 100000), FRotator::ZeroRotator, SpawnParameters);
	if (ProbePawn)
	{
		ProbePawn->Destroy();
	}

	const bool bPassed = ProbePawn && bProbeReceiverAdded && bProbeGameActorReady && bProbeReceiverRemoved;
	if (bPassed)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniReceiverProbe PASS: ReceiverAdded=%d GameActorReady=%d ReceiverRemoved=%d"),
			bProbeReceiverAdded, bProbeGameActorReady, bProbeReceiverRemoved);
	}
	else
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniReceiverProbe FAIL: ReceiverAdded=%d GameActorReady=%d ReceiverRemoved=%d"),
			bProbeReceiverAdded, bProbeGameActorReady, bProbeReceiverRemoved);
	}
	ReceiverProbeHandle.Reset();
	FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1, TEXT("MiniReceiverProbe"));
}

void UMiniGameInstance::HandleReceiverProbeEvent(AActor* Actor, FName EventName)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniReceiverProbe event %s on %s"), *EventName.ToString(), *GetNameSafe(Actor));
	bProbeReceiverAdded |= EventName == UGameFrameworkComponentManager::NAME_ReceiverAdded;
	bProbeGameActorReady |= EventName == UGameFrameworkComponentManager::NAME_GameActorReady;
	bProbeReceiverRemoved |= EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved;
}
