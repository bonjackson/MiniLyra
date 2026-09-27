#include "MiniGameState.h"

#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "System/MiniLogChannels.h"
#include "TimerManager.h"

namespace
{
int32 GFeatureProbeCyclesCompleted = 0;

const TCHAR* GetMiniNetModeName(const ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone: return TEXT("Standalone");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	case NM_ListenServer: return TEXT("ListenServer");
	case NM_Client: return TEXT("Client");
	default: return TEXT("Unknown");
	}
}
}

AMiniGameState::AMiniGameState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ExperienceManagerComponent = CreateDefaultSubobject<UMiniExperienceManagerComponent>(TEXT("ExperienceManagerComponent"));
}

void AMiniGameState::BeginPlay()
{
	Super::BeginPlay();

	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeExperienceFlow")))
	{
		return;
	}
	if (!ExperienceManagerComponent)
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFlowProbe FAIL: ExperienceManagerComponent is missing"));
		return;
	}

	ExperienceManagerComponent->CallOrRegister_OnExperienceLoaded(
		FOnMiniExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::HandleFlowProbeLoaded));
	ExperienceManagerComponent->CallOrRegister_OnExperienceFailed(
		FOnMiniExperienceFailed::FDelegate::CreateUObject(this, &ThisClass::HandleFlowProbeFailed));
}

void AMiniGameState::HandleFlowProbeLoaded(const UMiniExperienceDefinition* Experience)
{
	bFlowProbeLateSubscriberCalled = false;
	ExperienceManagerComponent->CallOrRegister_OnExperienceLoaded(
		FOnMiniExperienceLoaded::FDelegate::CreateWeakLambda(this,
			[this](const UMiniExperienceDefinition* LateExperience)
			{
				bFlowProbeLateSubscriberCalled = LateExperience != nullptr;
			}));

	const TCHAR* NetModeName = GetMiniNetModeName(GetNetMode());
	if (Experience && bFlowProbeLateSubscriberCalled)
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniFlowProbe PASS: NetMode=%s ID=%s LateSubscriber=1"),
			NetModeName, *ExperienceManagerComponent->GetCurrentExperienceId().ToString());

		// A standalone, opt-in probe repeatedly tears down and recreates the
		// world in one process. This exercises action removal and plugin release
		// without changing the normal game flow or racing a remote client.
		int32 RequestedCycles = 0;
		if (GetNetMode() == NM_Standalone &&
			FParse::Value(FCommandLine::Get(), TEXT("MiniProbeFeatureCycles="), RequestedCycles) &&
			RequestedCycles > 0 && GFeatureProbeCyclesCompleted < RequestedCycles)
		{
			const int32 Cycle = ++GFeatureProbeCyclesCompleted;
			const bool bFinalCycle = Cycle == RequestedCycles;
			UE_LOG(LogMiniExperience, Display, TEXT("MiniFeatureCycle LOADED: Index=%d Total=%d"), Cycle, RequestedCycles);
			FTimerHandle TravelTimer;
			GetWorldTimerManager().SetTimer(TravelTimer,
				FTimerDelegate::CreateWeakLambda(this, [this, bFinalCycle, Cycle]()
				{
					UE_LOG(LogMiniExperience, Display, TEXT("MiniFeatureCycle TRAVEL: Index=%d Final=%d"), Cycle, bFinalCycle);
					if (bFinalCycle)
					{
						UGameplayStatics::OpenLevel(this, TEXT("/Engine/Maps/Entry"), true, TEXT("game=/Script/Engine.GameModeBase"));
					}
					else
					{
						UGameplayStatics::OpenLevel(this, TEXT("/Game/Mini/Maps/L_MiniPractice"));
					}
				}), 0.25f, false);
		}
	}
	else
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFlowProbe FAIL: NetMode=%s ID=%s LateSubscriber=%d Experience=%s"),
			NetModeName, *ExperienceManagerComponent->GetCurrentExperienceId().ToString(),
			bFlowProbeLateSubscriberCalled, *GetNameSafe(Experience));
	}
}

void AMiniGameState::HandleFlowProbeFailed(const FString& Reason)
{
	const TCHAR* NetModeName = GetMiniNetModeName(GetNetMode());
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeInvalidExperience")))
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniFlowProbe FAIL_EXPECTED: NetMode=%s Reason=%s"), NetModeName, *Reason);
	}
	else
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniFlowProbe FAIL: NetMode=%s Reason=%s"), NetModeName, *Reason);
	}
}
