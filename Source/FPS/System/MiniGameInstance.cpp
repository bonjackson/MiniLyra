#include "MiniGameInstance.h"

#include "Components/GameFrameworkComponentManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ModularPawn.h"
#include "Character/MiniPawnData.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniWorldSettings.h"
#include "System/MiniAssetManager.h"
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

	bProbeReceiversRequested = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeReceivers"));
	bProbeExperienceRequested = FParse::Param(FCommandLine::Get(), TEXT("MiniProbeExperience"));
	if (bProbeReceiversRequested || bProbeExperienceRequested)
	{
		UWorld* World = GetWorld();
		if (World && World->IsGameWorld() && !World->HasBegunPlay())
		{
			ProbeWorld = World;
			ProbeWorldBeginPlayHandle = World->OnWorldBeginPlay.AddUObject(this, &ThisClass::HandleProbeWorldBeginPlay);
		}
		else
		{
			RunRequestedProbes();
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
	RunRequestedProbes();
}

void UMiniGameInstance::RunRequestedProbes()
{
	bool bPassed = true;
	if (bProbeReceiversRequested)
	{
		bPassed = RunReceiverProbe() && bPassed;
	}
	if (bProbeExperienceRequested)
	{
		bPassed = RunExperienceProbe() && bPassed;
	}
	FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1, TEXT("MiniProbes"));
}

bool UMiniGameInstance::RunReceiverProbe()
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
		return false;
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTagProbe PASS: all four InitState tags are queryable"));

	UWorld* World = GetWorld();
	UGameFrameworkComponentManager* ComponentManager = GetSubsystem<UGameFrameworkComponentManager>();
	if (!World || !World->IsGameWorld() || !World->HasBegunPlay() || !ComponentManager)
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniReceiverProbe FAIL: no started game world or component manager"));
		return false;
	}

	ReceiverProbeHandle = ComponentManager->AddExtensionHandler(
		TSoftClassPtr<AActor>(AModularPawn::StaticClass()),
		UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(this, &ThisClass::HandleReceiverProbeEvent));
	if (!ReceiverProbeHandle.IsValid())
	{
		UE_LOG(LogMiniInit, Error, TEXT("MiniReceiverProbe FAIL: extension handler could not be registered"));
		return false;
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
	return bPassed;
}

bool UMiniGameInstance::RunExperienceProbe()
{
	UMiniAssetManager* Manager = UMiniAssetManager::GetMiniAssetManager();
	if (!Manager)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: MiniAssetManager is not configured"));
		return false;
	}

	FPrimaryAssetId DefaultId;
	FString Error;
	if (!Manager->TryGetDefaultExperienceId(DefaultId, Error))
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: project default: %s"), *Error);
		return false;
	}

	TArray<FPrimaryAssetId> ScannedIds;
	Manager->GetPrimaryAssetIdList(FMiniPrimaryAssetTypes::Experience, ScannedIds);
	int32 MatchingIds = 0;
	for (const FPrimaryAssetId& Id : ScannedIds)
	{
		MatchingIds += (Id == DefaultId) ? 1 : 0;
	}
	if (MatchingIds != 1)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: expected one scanned entry for %s, found %d"),
			*DefaultId.ToString(), MatchingIds);
		return false;
	}
	const FSoftObjectPath RegisteredPath = Manager->GetPrimaryAssetPath(DefaultId);
	const FSoftObjectPath ExpectedPath(TEXT("/Game/Mini/System/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience"));
	if (RegisteredPath != ExpectedPath)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: ID %s maps to %s, expected %s"),
			*DefaultId.ToString(), *RegisteredPath.ToString(), *ExpectedPath.ToString());
		return false;
	}

	UMiniExperienceDefinition* Experience = Manager->LoadExperienceSynchronously(DefaultId, Error);
	if (!Experience)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: %s"), *Error);
		return false;
	}

	UWorld* World = GetWorld();
	const AMiniWorldSettings* WorldSettings = World ? Cast<AMiniWorldSettings>(World->GetWorldSettings()) : nullptr;
	if (!WorldSettings)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: current map does not use MiniWorldSettings"));
		return false;
	}
	const FPrimaryAssetId MapId = WorldSettings->GetDefaultGameplayExperience(&Error);
	if (MapId != DefaultId)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: map ID '%s' differs from project default '%s': %s"),
			*MapId.ToString(), *DefaultId.ToString(), *Error);
		return false;
	}

	const FPrimaryAssetId UnknownId(FMiniPrimaryAssetTypes::Experience, TEXT("DA_MiniDefinitelyMissing"));
	FString UnknownError;
	const bool bUnknownRejected = !Manager->TryValidateExperienceId(UnknownId, UnknownError)
		&& UnknownError.Contains(TEXT("Unknown Experience ID"));
	UMiniExperienceDefinition* EmptyExperience = NewObject<UMiniExperienceDefinition>(this);
	FString EmptyError;
	const bool bEmptyPawnDataRejected = !EmptyExperience->ValidateDefinition(EmptyError)
		&& EmptyError.Contains(TEXT("DefaultPawnData"));
	UMiniPawnData* EmptyPawnData = NewObject<UMiniPawnData>(this);
	FString EmptyPawnError;
	const bool bEmptyPawnClassRejected = !EmptyPawnData->ValidatePawnData(EmptyPawnError)
		&& EmptyPawnError.Contains(TEXT("PawnClass"));
	UMiniPawnData* WrongClassPawnData = NewObject<UMiniPawnData>(this);
	WrongClassPawnData->PawnClass = APawn::StaticClass();
	FString WrongClassError;
	const bool bWrongPawnClassRejected = !WrongClassPawnData->ValidatePawnData(WrongClassError)
		&& WrongClassError.Contains(TEXT("must derive from MiniCharacter"));
	if (!bUnknownRejected || !bEmptyPawnDataRejected || !bEmptyPawnClassRejected || !bWrongPawnClassRejected)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniExperienceProbe FAIL: negative validation: unknown='%s', empty experience='%s', empty pawn='%s', wrong class='%s'"),
			*UnknownError, *EmptyError, *EmptyPawnError, *WrongClassError);
		return false;
	}

	UE_LOG(LogMiniExperience, Display, TEXT("MiniExperienceProbe PASS: ID=%s Path=%s PawnData=%s MapOverride=%s"),
		*DefaultId.ToString(), *Manager->GetPrimaryAssetPath(DefaultId).ToString(),
		*GetPathNameSafe(Experience->DefaultPawnData.Get()), *MapId.ToString());
	UE_LOG(LogMiniExperience, Display, TEXT("MiniExperienceProbe negative cases PASS: %s; %s; %s; %s"),
		*UnknownError, *EmptyError, *EmptyPawnError, *WrongClassError);
	return true;
}

void UMiniGameInstance::HandleReceiverProbeEvent(AActor* Actor, FName EventName)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniReceiverProbe event %s on %s"), *EventName.ToString(), *GetNameSafe(Actor));
	bProbeReceiverAdded |= EventName == UGameFrameworkComponentManager::NAME_ReceiverAdded;
	bProbeGameActorReady |= EventName == UGameFrameworkComponentManager::NAME_GameActorReady;
	bProbeReceiverRemoved |= EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved;
}
