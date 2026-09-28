#include "MiniTask11AnimAssetLibrary.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/MiniAnimInstance.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "System/MiniLogChannels.h"
#endif

#if WITH_EDITOR
namespace
{
UEdGraph* FindAnimGraph(UAnimBlueprint* Blueprint)
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

template<typename NodeType>
NodeType* MakeNode(UEdGraph* Graph, int32 X, int32 Y)
{
	FGraphNodeCreator<NodeType> Creator(*Graph);
	NodeType* Node = Creator.CreateNode();
	Node->NodePosX = X;
	Node->NodePosY = Y;
	Creator.Finalize();
	return Node;
}

UEdGraphPin* FindPosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction, int32 Index = 0)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && UAnimationGraphSchema::IsLocalSpacePosePin(Pin->PinType))
		{
			if (Index-- == 0)
			{
				return Pin;
			}
		}
	}
	return nullptr;
}

bool Connect(UEdGraph* Graph, UEdGraphPin* Output, UEdGraphPin* Input)
{
	return Output && Input && Graph->GetSchema()->TryCreateConnection(Output, Input);
}

UAnimGraphNode_SequencePlayer* MakeSequence(UEdGraph* Graph, UAnimSequence* Sequence,
	bool bLoop, int32 X, int32 Y)
{
	UAnimGraphNode_SequencePlayer* Node = MakeNode<UAnimGraphNode_SequencePlayer>(Graph, X, Y);
	Node->Node.SetSequence(Sequence);
	Node->Node.SetLoopAnimation(bLoop);
	return Node;
}

UAnimGraphNode_BlendListByBool* MakeBlend(UEdGraph* Graph, int32 X, int32 Y)
{
	// The node constructor creates both poses with the engine's short default blend.
	return MakeNode<UAnimGraphNode_BlendListByBool>(Graph, X, Y);
}

bool WireBlend(UEdGraph* Graph, UAnimGraphNode_BlendListByBool* Blend,
	UEdGraphNode* TrueSource, UEdGraphNode* FalseSource, FName VariableName, int32 X, int32 Y)
{
	UK2Node_VariableGet* GetVariable = MakeNode<UK2Node_VariableGet>(Graph, X, Y);
	GetVariable->VariableReference.SetSelfMember(VariableName);
	GetVariable->ReconstructNode();
	return Connect(Graph, FindPosePin(TrueSource, EGPD_Output), FindPosePin(Blend, EGPD_Input, 0)) &&
		Connect(Graph, FindPosePin(FalseSource, EGPD_Output), FindPosePin(Blend, EGPD_Input, 1)) &&
		Connect(Graph, GetVariable->GetValuePin(), Blend->FindPin(TEXT("bActiveValue")));
}
}
#endif

bool UMiniTask11AnimAssetLibrary::ConfigurePracticeAnim(UAnimBlueprint* AnimBlueprint,
	UAnimSequence* RifleIdle, UAnimSequence* RifleJog,
	UAnimSequence* Jump, UAnimSequence* Fall, UAnimSequence* Land)
{
#if WITH_EDITOR
	if (!AnimBlueprint || !RifleIdle || !RifleJog || !Jump || !Fall || !Land ||
		AnimBlueprint->ParentClass != UMiniAnimInstance::StaticClass() ||
		!AnimBlueprint->TargetSkeleton)
	{
		return false;
	}
	for (const UAnimSequence* Sequence : {RifleIdle, RifleJog, Jump, Fall, Land})
	{
		if (Sequence->GetSkeleton() != AnimBlueprint->TargetSkeleton)
		{
			return false;
		}
	}
	UEdGraph* Graph = FindAnimGraph(AnimBlueprint);
	UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
	if (!Root)
	{
		return false;
	}
	AnimBlueprint->Modify();
	Graph->Modify();
	for (int32 Index = Graph->Nodes.Num() - 1; Index >= 0; --Index)
	{
		if (Graph->Nodes[Index] != Root)
		{
			Graph->RemoveNode(Graph->Nodes[Index]);
		}
	}
	Root->BreakAllNodeLinks();
	UAnimGraphNode_SequencePlayer* IdleNode = MakeSequence(Graph, RifleIdle, true, -1050, 100);
	UAnimGraphNode_SequencePlayer* JogNode = MakeSequence(Graph, RifleJog, true, -1050, -80);
	UAnimGraphNode_SequencePlayer* JumpNode = MakeSequence(Graph, Jump, false, -1050, -300);
	UAnimGraphNode_SequencePlayer* FallNode = MakeSequence(Graph, Fall, true, -1050, -480);
	UAnimGraphNode_SequencePlayer* LandNode = MakeSequence(Graph, Land, false, -650, -700);
	UAnimGraphNode_BlendListByBool* GroundBlend = MakeBlend(Graph, -760, 20);
	UAnimGraphNode_BlendListByBool* AirBlend = MakeBlend(Graph, -760, -380);
	UAnimGraphNode_BlendListByBool* AirGroundBlend = MakeBlend(Graph, -450, -150);
	UAnimGraphNode_BlendListByBool* LandBlend = MakeBlend(Graph, -160, -250);
	const bool bConnected =
		WireBlend(Graph, GroundBlend, JogNode, IdleNode, GET_MEMBER_NAME_CHECKED(UMiniAnimInstance, bIsMoving), -1040, -200) &&
		WireBlend(Graph, AirBlend, JumpNode, FallNode, GET_MEMBER_NAME_CHECKED(UMiniAnimInstance, bIsAscending), -1040, -600) &&
		WireBlend(Graph, AirGroundBlend, AirBlend, GroundBlend, GET_MEMBER_NAME_CHECKED(UMiniAnimInstance, bIsInAir), -750, -570) &&
		WireBlend(Graph, LandBlend, LandNode, AirGroundBlend, GET_MEMBER_NAME_CHECKED(UMiniAnimInstance, bRecentlyLanded), -440, -630) &&
		Connect(Graph, FindPosePin(LandBlend, EGPD_Output), FindPosePin(Root, EGPD_Input));
	if (!bConnected)
	{
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				for (const UEdGraphPin* Pin : Node->Pins)
				{
					UE_LOG(LogMiniInit, Warning, TEXT("MiniAnimGraph PIN: Node=%s Pin=%s Direction=%d"),
						*Node->GetClass()->GetName(), *Pin->PinName.ToString(), static_cast<int32>(Pin->Direction));
				}
			}
		}
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	AnimBlueprint->MarkPackageDirty();
	return AnimBlueprint->GeneratedClass != nullptr && AnimBlueprint->Status == BS_UpToDate;
#else
	return false;
#endif
}

bool UMiniTask11AnimAssetLibrary::VerifyPracticeAnim(const UAnimBlueprint* AnimBlueprint,
	const UAnimSequence* RifleIdle, const UAnimSequence* RifleJog,
	const UAnimSequence* Jump, const UAnimSequence* Fall, const UAnimSequence* Land)
{
#if WITH_EDITOR
	if (!AnimBlueprint || !RifleIdle || !RifleJog || !Jump || !Fall || !Land ||
		AnimBlueprint->ParentClass != UMiniAnimInstance::StaticClass() ||
		!AnimBlueprint->GeneratedClass || AnimBlueprint->Status != BS_UpToDate)
	{
		return false;
	}
	UEdGraph* Graph = FindAnimGraph(const_cast<UAnimBlueprint*>(AnimBlueprint));
	const UAnimGraphNode_Root* Root = Graph ? FBlueprintEditorUtils::GetAnimGraphRoot(Graph) : nullptr;
	if (!Root || !FindPosePin(const_cast<UAnimGraphNode_Root*>(Root), EGPD_Input) ||
		FindPosePin(const_cast<UAnimGraphNode_Root*>(Root), EGPD_Input)->LinkedTo.Num() != 1)
	{
		return false;
	}
	int32 SequenceCount = 0;
	int32 BlendCount = 0;
	int32 VariableCount = 0;
	TSet<const UAnimSequence*> Sequences;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UAnimGraphNode_SequencePlayer* Player = Cast<UAnimGraphNode_SequencePlayer>(Node))
		{
			++SequenceCount;
			Sequences.Add(Cast<UAnimSequence>(Player->Node.GetSequence()));
		}
		else if (Node->IsA<UAnimGraphNode_BlendListByBool>())
		{
			++BlendCount;
		}
		else if (Node->IsA<UK2Node_VariableGet>())
		{
			++VariableCount;
		}
	}
	return SequenceCount == 5 && BlendCount == 4 && VariableCount == 4 &&
		Sequences.Contains(RifleIdle) && Sequences.Contains(RifleJog) && Sequences.Contains(Jump) &&
		Sequences.Contains(Fall) && Sequences.Contains(Land);
#else
	return false;
#endif
}
