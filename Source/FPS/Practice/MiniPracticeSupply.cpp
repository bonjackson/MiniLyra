#include "MiniPracticeSupply.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MiniPracticeSupply"

void UMiniPracticeSupplyWidget::NativeOnInitialized()
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SupplyLabel"));
	Text->SetText(LOCTEXT("Instructions", "弹药补给\n进入蓝色区域补满两把武器"));
	Text->SetFontSize(20);
	Text->SetJustification(ETextJustify::Center);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.75f, 1.0f)));
	Text->SetShadowColorAndOpacity(FLinearColor::Black);
	Text->SetShadowOffset(FVector2D(1, 1));
	WidgetTree->RootWidget = Text;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Super::NativeOnInitialized();
}

AMiniPracticeSupply::AMiniPracticeSupply()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	SupplyArea = CreateDefaultSubobject<USphereComponent>(TEXT("SupplyArea"));
	SetRootComponent(SupplyArea);
	SupplyArea->InitSphereRadius(SupplyRadius);
	SupplyArea->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SupplyArea->SetCollisionResponseToAllChannels(ECR_Ignore);
	SupplyArea->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SupplyArea->SetGenerateOverlapEvents(true);
	Pad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pad"));
	Pad->SetupAttachment(SupplyArea);
	Pad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pad->SetRelativeScale3D(FVector(3.8f, 3.8f, 0.12f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PadMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (PadMesh.Succeeded()) { Pad->SetStaticMesh(PadMesh.Object); }
	StationMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Mini/Targets/M_MiniPracticeTarget.M_MiniPracticeTarget")));
	Label = CreateDefaultSubobject<UWidgetComponent>(TEXT("Label"));
	Label->SetupAttachment(SupplyArea);
	Label->SetRelativeLocation(FVector(0, 0, 105));
	Label->SetWidgetSpace(EWidgetSpace::Screen);
	Label->SetDrawSize(FVector2D(430, 90));
	Label->SetWidgetClass(UMiniPracticeSupplyWidget::StaticClass());
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMiniPracticeSupply::BeginPlay()
{
	Super::BeginPlay();
	SupplyArea->SetSphereRadius(FMath::Clamp(SupplyRadius, 50.0f, 500.0f));
	if (UMaterialInterface* Material = StationMaterial.LoadSynchronous())
	{
		PadMaterial = UMaterialInstanceDynamic::Create(Material, this);
		PadMaterial->SetVectorParameterValue(TEXT("TargetColor"), FLinearColor(0.02f, 0.4f, 0.85f));
		Pad->SetMaterial(0, PadMaterial);
	}
	if (HasAuthority())
	{
		SupplyArea->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleOverlap);
		RefillOverlappingPlayers();
	}
	else { SupplyArea->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
}

void AMiniPracticeSupply::HandleOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (HasAuthority() && Cast<AMiniCharacter>(OtherActor)) { RefillOverlappingPlayers(); }
}

bool AMiniPracticeSupply::TryRefill(AMiniPlayerController* Controller)
{
	AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	AMiniPlayerState* PS = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
	if (!HasAuthority() || !Controller || !Controller->HasAuthority() || Controller->GetWorld() != GetWorld() ||
		!Pawn || Pawn->GetController() != Controller || !ASC || ASC->GetAvatarActor() != Pawn ||
		!SupplyArea->IsOverlappingActor(Pawn) ||
		FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) > FMath::Square(SupplyArea->GetScaledSphereRadius() + 50.0f) ||
		!Pawn->GetHealthComponent() || Pawn->GetHealthComponent()->IsDead() ||
		ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))
	{
		return false;
	}
	UMiniQuickBarComponent* QuickBar = Controller->GetQuickBar();
	bool bChanged = false;
	for (int32 Slot = 0; QuickBar && Slot < 2; ++Slot)
	{
		UMiniInventoryItemInstance* Item = QuickBar->GetSlotItem(Slot);
		const UMiniInventoryItemDefinition* Definition = Item && Item->GetItemDefinition()
			? GetDefault<UMiniInventoryItemDefinition>(Item->GetItemDefinition()) : nullptr;
		const UMiniInventoryFragment_Equippable* Fragment = Definition
			? Definition->FindFragment<UMiniInventoryFragment_Equippable>() : nullptr;
		UClass* EquipmentClass = Fragment ? Fragment->EquipmentDefinition.LoadSynchronous() : nullptr;
		const UMiniRangedWeaponEquipmentDefinition* Weapon = EquipmentClass
			? Cast<UMiniRangedWeaponEquipmentDefinition>(EquipmentClass->GetDefaultObject()) : nullptr;
		if (!Item || !Definition || !Weapon) { continue; }
		int32 InitialReserve = INDEX_NONE;
		for (const UMiniInventoryItemFragment* ItemFragment : Definition->Fragments)
		{
			if (const UMiniInventoryFragment_InitialStats* Stats = Cast<UMiniInventoryFragment_InitialStats>(ItemFragment))
			{
				for (const FMiniInventoryStat& Stat : Stats->InitialStats)
				{
					if (Stat.Tag == MiniInventoryTags::ReserveAmmo) { InitialReserve = Stat.Count; }
				}
			}
		}
		if (InitialReserve < 0) { continue; }
		const int32 Magazine = Weapon->GetMagazineCapacity();
		if (Item->GetStat(MiniInventoryTags::AmmoInMagazine) < Magazine)
		{
			bChanged |= Item->SetStat(MiniInventoryTags::AmmoInMagazine, Magazine);
		}
		if (Item->GetStat(MiniInventoryTags::ReserveAmmo) < InitialReserve)
		{
			bChanged |= Item->SetStat(MiniInventoryTags::ReserveAmmo, InitialReserve);
		}
	}
	if (bChanged)
	{
		++RefillCount;
		UE_LOG(LogMiniInit, Display, TEXT("MiniPracticeSupply REFILLED: Controller=%s Count=%d"), *GetNameSafe(Controller), RefillCount);
	}
	return bChanged;
}

void AMiniPracticeSupply::RefillOverlappingPlayers()
{
	if (!HasAuthority() || !GetWorld()) { return; }
	TArray<AActor*> Pawns;
	SupplyArea->GetOverlappingActors(Pawns, AMiniCharacter::StaticClass());
	for (AActor* Actor : Pawns)
	{
		AMiniCharacter* Pawn = Cast<AMiniCharacter>(Actor);
		TryRefill(Pawn ? Cast<AMiniPlayerController>(Pawn->GetController()) : nullptr);
	}
	if (Pawns.IsEmpty()) { GetWorldTimerManager().ClearTimer(RefillTimer); }
	else if (!GetWorldTimerManager().IsTimerActive(RefillTimer))
	{
		GetWorldTimerManager().SetTimer(RefillTimer, this, &ThisClass::RefillOverlappingPlayers, 1.0f, true);
	}
}

void AMiniPracticeSupply::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RefillTimer);
	SupplyArea->OnComponentBeginOverlap.RemoveDynamic(this, &ThisClass::HandleOverlap);
	Super::EndPlay(EndPlayReason);
}

#undef LOCTEXT_NAMESPACE
