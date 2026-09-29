#pragma once

#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "MiniInventoryManagerComponent.generated.h"

class UMiniInventoryItemDefinition;
class UMiniInventoryItemInstance;
class UMiniInventoryManagerComponent;
class UActorChannel;
class FOutBunch;
struct FReplicationFlags;

/** One server-owned item stack. The instance carries the mutable item data. */
USTRUCT(BlueprintType)
struct FPS_API FMiniInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMiniInventoryItemInstance> Instance = nullptr;

	UPROPERTY()
	int32 StackCount = 0;
};

/** Delta-replicated private list on a PlayerController. */
USTRUCT()
struct FPS_API FMiniInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FMiniInventoryEntry> Entries;

	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FMiniInventoryEntry, FMiniInventoryList>(Entries, DeltaParms, *this);
	}

	UMiniInventoryManagerComponent* OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FMiniInventoryList> : public TStructOpsTypeTraitsBase2<FMiniInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};

/** Server-authoritative inventory; the owning PlayerController is private to its connection. */
UCLASS(ClassGroup=(Mini), meta=(BlueprintSpawnableComponent))
class FPS_API UMiniInventoryManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMiniInventoryManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch,
		FReplicationFlags* RepFlags) override;

	/** Authority-only. A distinct UObject is created for every added stack. */
	UMiniInventoryItemInstance* AddItem(TSubclassOf<UMiniInventoryItemDefinition> Definition,
		int32 StackCount = 1);
	/** Authority-only. Removes the FastArray reference and destroys the remote subobject. */
	bool RemoveItem(UMiniInventoryItemInstance* Instance);

	const TArray<FMiniInventoryEntry>& GetEntries() const { return InventoryList.Entries; }
	FString GetDebugSnapshot() const;

private:
	UPROPERTY(Replicated)
	FMiniInventoryList InventoryList;
};
