#include "MiniTask18AnimAssetLibrary.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "Animation/MiniAnimInstance.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

#if WITH_EDITOR
namespace
{
UEdGraph* Task18AnimFindAnimGraph(UAnimBlueprint* Blueprint)
{
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph") && FBlueprintEditorUtils::GetAnimGraphRoot(Graph))
		{
			return Graph;
		}
	}
	return nullptr;
}

UEdGraphPin* Task18AnimFindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && UAnimationGraphSchema::IsLocalSpacePosePin(Pin->PinType))
		{
			return Pin;
		}
	}
	return nullptr;
}

bool Task18AnimIsDirectlyLinked(const UEdGraphPin* Output, const UEdGraphPin* Input)
{
	return Output && Input && Output->LinkedTo.Num() == 1 && Output->LinkedTo[0] == Input &&
		Input->LinkedTo.Num() == 1 && Input->LinkedTo[0] == Output;
}

template<typename NodeType>
NodeType* Task18AnimMakeNode(UEdGraph* Graph, int32 X, int32 Y)
{
	FGraphNodeCreator<NodeType> Creator(*Graph);
	NodeType* Node = Creator.CreateNode();
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Creator.Finalize();
	return Node;
}

bool Task18AnimCompile(UAnimBlueprint* Blueprint)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Blueprint->MarkPackageDirty();
	return Blueprint->GeneratedClass && Blueprint->Status == BS_UpToDate;
}
}
#endif

bool UMiniTask18AnimAssetLibrary::ConfigureCharacterSlot(UAnimBlueprint* AnimBlueprint, FName SlotName)
{
#if WITH_EDITOR
	if (!AnimBlueprint || !AnimBlueprint->TargetSkeleton ||
		AnimBlueprint->ParentClass != UMiniAnimInstance::StaticClass() || SlotName.IsNone())
	{
		return false;
	}
	if (VerifyCharacterSlot(AnimBlueprint, SlotName))
	{
		return true;
	}
	UEdGraph* Graph = Task18AnimFindAnimGraph(AnimBlueprint);
	UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
	UEdGraphPin* RootInput = Root ? Task18AnimFindPosePin(Root, EGPD_Input) : nullptr;
	if (!RootInput || RootInput->LinkedTo.Num() != 1 ||
		Graph->Nodes.ContainsByPredicate([](const UEdGraphNode* Node) { return Node->IsA<UAnimGraphNode_Slot>(); }))
	{
		return false;
	}
	// Keep the existing Task 11 blend chain intact. Only interpose the slot at its output.
	UEdGraphPin* MovementOutput = RootInput->LinkedTo[0];
	if (!MovementOutput || !MovementOutput->GetOwningNode()->IsA<UAnimGraphNode_BlendListByBool>())
	{
		return false;
	}
	AnimBlueprint->Modify();
	AnimBlueprint->TargetSkeleton->Modify();
	Graph->Modify();
	RootInput->BreakAllPinLinks();
	UAnimGraphNode_Slot* Slot = Task18AnimMakeNode<UAnimGraphNode_Slot>(Graph, Root->NodePosX - 230, Root->NodePosY);
	Slot->Node.SlotName = SlotName;
	if (!Graph->GetSchema()->TryCreateConnection(MovementOutput, Task18AnimFindPosePin(Slot, EGPD_Input)) ||
		!Graph->GetSchema()->TryCreateConnection(Task18AnimFindPosePin(Slot, EGPD_Output), RootInput))
	{
		Graph->RemoveNode(Slot);
		Graph->GetSchema()->TryCreateConnection(MovementOutput, RootInput);
		return false;
	}
	AnimBlueprint->TargetSkeleton->RegisterSlotNode(SlotName);
	AnimBlueprint->TargetSkeleton->MarkPackageDirty();
	return Task18AnimCompile(AnimBlueprint) && VerifyCharacterSlot(AnimBlueprint, SlotName);
#else
	return false;
#endif
}

bool UMiniTask18AnimAssetLibrary::VerifyCharacterSlot(const UAnimBlueprint* AnimBlueprint, FName SlotName)
{
#if WITH_EDITOR
	if (!AnimBlueprint || !AnimBlueprint->TargetSkeleton ||
		AnimBlueprint->ParentClass != UMiniAnimInstance::StaticClass() ||
		!AnimBlueprint->GeneratedClass || AnimBlueprint->Status != BS_UpToDate ||
		SlotName.IsNone() || !AnimBlueprint->TargetSkeleton->ContainsSlotName(SlotName))
	{
		return false;
	}
	UEdGraph* Graph = Task18AnimFindAnimGraph(const_cast<UAnimBlueprint*>(AnimBlueprint));
	UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
	UEdGraphPin* RootInput = Root ? Task18AnimFindPosePin(Root, EGPD_Input) : nullptr;
	if (!RootInput || RootInput->LinkedTo.Num() != 1)
	{
		return false;
	}
	const UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(RootInput->LinkedTo[0]->GetOwningNode());
	if (!Slot || Slot->Node.SlotName != SlotName ||
		!Task18AnimIsDirectlyLinked(Task18AnimFindPosePin(const_cast<UAnimGraphNode_Slot*>(Slot), EGPD_Output), RootInput))
	{
		return false;
	}
	UEdGraphPin* SlotInput = Task18AnimFindPosePin(const_cast<UAnimGraphNode_Slot*>(Slot), EGPD_Input);
	if (!SlotInput || SlotInput->LinkedTo.Num() != 1 ||
		!SlotInput->LinkedTo[0]->GetOwningNode()->IsA<UAnimGraphNode_BlendListByBool>() ||
		!Task18AnimIsDirectlyLinked(SlotInput->LinkedTo[0], SlotInput))
	{
		return false;
	}
	int32 SequenceCount = 0;
	int32 BlendCount = 0;
	int32 SlotCount = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		SequenceCount += Node->IsA<UAnimGraphNode_SequencePlayer>() ? 1 : 0;
		BlendCount += Node->IsA<UAnimGraphNode_BlendListByBool>() ? 1 : 0;
		SlotCount += Node->IsA<UAnimGraphNode_Slot>() ? 1 : 0;
	}
	return SequenceCount == 5 && BlendCount == 4 && SlotCount == 1;
#else
	return false;
#endif
}

bool UMiniTask18AnimAssetLibrary::ConfigureWeaponAnim(UAnimBlueprint* AnimBlueprint, FName SlotName)
{
#if WITH_EDITOR
	if (!AnimBlueprint || !AnimBlueprint->TargetSkeleton ||
		AnimBlueprint->ParentClass != UAnimInstance::StaticClass() || SlotName.IsNone())
	{
		return false;
	}
	if (VerifyWeaponAnim(AnimBlueprint, SlotName))
	{
		return true;
	}
	UEdGraph* Graph = Task18AnimFindAnimGraph(AnimBlueprint);
	UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
	if (!Root || !Graph->Nodes.Contains(Root) ||
		Graph->Nodes.ContainsByPredicate([Root](const UEdGraphNode* Node)
		{
			return Node != Root && !Node->IsA<UAnimGraphNode_LocalRefPose>();
		}))
	{
		return false;
	}
	AnimBlueprint->Modify();
	AnimBlueprint->TargetSkeleton->Modify();
	Graph->Modify();
	for (int32 Index = Graph->Nodes.Num() - 1; Index >= 0; --Index)
	{
		if (Graph->Nodes[Index] != Root)
		{
			Graph->RemoveNode(Graph->Nodes[Index]);
		}
	}
	Root->BreakAllNodeLinks();
	UAnimGraphNode_LocalRefPose* BasePose = Task18AnimMakeNode<UAnimGraphNode_LocalRefPose>(Graph, -460, 0);
	UAnimGraphNode_Slot* Slot = Task18AnimMakeNode<UAnimGraphNode_Slot>(Graph, -230, 0);
	Slot->Node.SlotName = SlotName;
	if (!Graph->GetSchema()->TryCreateConnection(Task18AnimFindPosePin(BasePose, EGPD_Output), Task18AnimFindPosePin(Slot, EGPD_Input)) ||
		!Graph->GetSchema()->TryCreateConnection(Task18AnimFindPosePin(Slot, EGPD_Output), Task18AnimFindPosePin(Root, EGPD_Input)))
	{
		Graph->RemoveNode(Slot);
		Graph->RemoveNode(BasePose);
		return false;
	}
	AnimBlueprint->TargetSkeleton->RegisterSlotNode(SlotName);
	AnimBlueprint->TargetSkeleton->MarkPackageDirty();
	return Task18AnimCompile(AnimBlueprint) && VerifyWeaponAnim(AnimBlueprint, SlotName);
#else
	return false;
#endif
}

bool UMiniTask18AnimAssetLibrary::VerifyWeaponAnim(const UAnimBlueprint* AnimBlueprint, FName SlotName)
{
#if WITH_EDITOR
	if (!AnimBlueprint || !AnimBlueprint->TargetSkeleton ||
		AnimBlueprint->ParentClass != UAnimInstance::StaticClass() ||
		!AnimBlueprint->GeneratedClass || AnimBlueprint->Status != BS_UpToDate ||
		SlotName.IsNone() || !AnimBlueprint->TargetSkeleton->ContainsSlotName(SlotName))
	{
		return false;
	}
	UEdGraph* Graph = Task18AnimFindAnimGraph(const_cast<UAnimBlueprint*>(AnimBlueprint));
	UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
	UEdGraphPin* RootInput = Root ? Task18AnimFindPosePin(Root, EGPD_Input) : nullptr;
	if (!Graph || Graph->Nodes.Num() != 3 || !RootInput || RootInput->LinkedTo.Num() != 1)
	{
		return false;
	}
	const UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(RootInput->LinkedTo[0]->GetOwningNode());
	if (!Slot || Slot->Node.SlotName != SlotName ||
		!Task18AnimIsDirectlyLinked(Task18AnimFindPosePin(const_cast<UAnimGraphNode_Slot*>(Slot), EGPD_Output), RootInput))
	{
		return false;
	}
	UEdGraphPin* SlotInput = Task18AnimFindPosePin(const_cast<UAnimGraphNode_Slot*>(Slot), EGPD_Input);
	return SlotInput && SlotInput->LinkedTo.Num() == 1 &&
		SlotInput->LinkedTo[0]->GetOwningNode()->IsA<UAnimGraphNode_LocalRefPose>() &&
		Task18AnimIsDirectlyLinked(SlotInput->LinkedTo[0], SlotInput);
#else
	return false;
#endif
}

bool UMiniTask18AnimAssetLibrary::ConfigureMontage(UAnimMontage* Montage, UAnimSequence* Sequence, FName SlotName)
{
#if WITH_EDITOR
	if (!Montage || !Sequence || SlotName.IsNone() || !Montage->GetSkeleton() ||
		Montage->GetSkeleton() != Sequence->GetSkeleton() || Montage->SlotAnimTracks.Num() != 1 ||
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1 ||
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() != Sequence)
	{
		return false;
	}
	Montage->Modify();
	USkeleton* Skeleton = Montage->GetSkeleton();
	Skeleton->Modify();
	Skeleton->RegisterSlotNode(SlotName);
	Montage->SlotAnimTracks[0].SlotName = SlotName;
	Montage->CalculateSequenceLength();
	Montage->MarkPackageDirty();
	Skeleton->MarkPackageDirty();
	return VerifyMontage(Montage, Sequence, SlotName);
#else
	return false;
#endif
}

bool UMiniTask18AnimAssetLibrary::VerifyMontage(const UAnimMontage* Montage,
	const UAnimSequence* Sequence, FName SlotName)
{
#if WITH_EDITOR
	if (!Montage || !Sequence || SlotName.IsNone() ||
		Montage->GetSkeleton() != Sequence->GetSkeleton() ||
		!Montage->GetSkeleton() || !Montage->GetSkeleton()->ContainsSlotName(SlotName) ||
		Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].SlotName != SlotName ||
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1 ||
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference() != Sequence ||
		Montage->CompositeSections.IsEmpty() ||
		!FMath::IsNearlyZero(Montage->CompositeSections[0].GetTime(), 0.001f))
	{
		return false;
	}
	return FMath::IsNearlyEqual(Montage->GetPlayLength(), Sequence->GetPlayLength(), 0.01f);
#else
	return false;
#endif
}
