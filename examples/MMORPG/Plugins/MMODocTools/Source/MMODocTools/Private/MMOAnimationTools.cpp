#include "MMODocProjectGuard.h"
#include "MMOAnimationTools.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/MemberReference.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Factories/AnimBlueprintFactory.h"
#include "IAssetTools.h"
#include "HAL/FileManager.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace MMOAnimationBuilder
{
FString Encode(const TSharedRef<FJsonObject>& Report)
{
    FString Output;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Output));
    return Output;
}

UEdGraph* FindGraph(UAnimBlueprint* Blueprint, FName Name)
{
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs) if (Graph && Graph->GetFName() == Name) return Graph;
    return nullptr;
}

UAnimGraphNode_Root* ClearGraph(UEdGraph* Graph)
{
    if (!Graph) return nullptr;
    Graph->Modify();
    UAnimGraphNode_Root* Root = nullptr;
    const auto Nodes = Graph->Nodes;
    for (UEdGraphNode* Node : Nodes)
    {
        Node->Modify();
        if (auto* Candidate = Cast<UAnimGraphNode_Root>(Node))
        {
            Root = Candidate;
            for (UEdGraphPin* Pin : Root->Pins) Pin->BreakAllPinLinks();
            Root->NodePosX = 650;
            Root->NodePosY = 50;
        }
        else
        {
            Graph->RemoveNode(Node);
            Node->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
        }
    }
    return Root;
}

bool Connect(UEdGraphNode* Source, FName Output, UEdGraphNode* Target, FName Input)
{
    UEdGraphPin* A = Source ? Source->FindPin(Output, EGPD_Output) : nullptr;
    UEdGraphPin* B = Target ? Target->FindPin(Input, EGPD_Input) : nullptr;
    return A && B && Source->GetGraph()->GetSchema()->TryCreateConnection(A, B);
}

template <typename NodeType>
NodeType* AddNode(UEdGraph* Graph, int32 X, int32 Y)
{
    FGraphNodeCreator<NodeType> Creator(*Graph);
    NodeType* Node = Creator.CreateNode();
    Node->NodePosX = X;
    Node->NodePosY = Y;
    Creator.Finalize();
    return Node;
}

UK2Node_VariableGet* AddVariable(UEdGraph* Graph, UClass* /*Parent*/, FName Name, int32 X, int32 Y)
{
    FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
    auto* Node = Creator.CreateNode();
    // The animation instance itself inherits these native properties. An external
    // member reference would require an explicit object target and disable the self fast path.
    Node->VariableReference.SetSelfMember(Name);
    Node->NodePosX = X; Node->NodePosY = Y;
    Creator.Finalize();
    return Node;
}

bool ReferencePose(UEdGraph* Graph)
{
    auto* Root = ClearGraph(Graph);
    return Root && Connect(AddNode<UAnimGraphNode_LocalRefPose>(Graph, 300, 50), TEXT("Pose"), Root, TEXT("Result"));
}

TSharedRef<FJsonObject> Compile(UAnimBlueprint* Blueprint)
{
    FCompilerResultsLog Log;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Result->SetNumberField(TEXT("errors"), Log.NumErrors);
    Result->SetNumberField(TEXT("warnings"), Log.NumWarnings);
    TArray<TSharedPtr<FJsonValue>> Messages;
    for (const auto& Message : Log.Messages) Messages.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
    Result->SetArrayField(TEXT("messages"), Messages);
    Result->SetBoolField(TEXT("passed"), Log.NumErrors == 0 && Blueprint->Status == BS_UpToDate);
    return Result;
}

bool Save(UObject* Asset)
{
    UPackage* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

UAnimBlueprint* GetOrCreate(const TCHAR* Name, UClass* Parent, USkeleton* Skeleton, USkeletalMesh* Mesh, bool bInterface)
{
    const FString Directory(TEXT("/Game/Characters/MMO"));
    const FString ObjectPath = Directory / Name + FString(TEXT(".")) + Name;
    if (auto* Existing = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath)) return Existing;
    UAnimBlueprintFactory* Factory = bInterface ? NewObject<UAnimLayerInterfaceFactory>() : NewObject<UAnimBlueprintFactory>();
    if (!bInterface)
    {
        Factory->ParentClass = Parent;
        Factory->TargetSkeleton = Skeleton;
        Factory->PreviewSkeletalMesh = Mesh;
    }
    return Cast<UAnimBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().CreateAsset(
        Name, Directory, UAnimBlueprint::StaticClass(), Factory));
}

bool Implement(UAnimBlueprint* Blueprint, UAnimBlueprint* Interface)
{
    for (const auto& Existing : Blueprint->ImplementedInterfaces)
        if (Existing.Interface == Interface->GeneratedClass) return true;
    return FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(Interface->GeneratedClass));
}

bool BuildLocomotion(UEdGraph* Graph, UClass* Parent, UBlendSpace* BlendSpace, UAnimSequence* Fall)
{
    auto* Root = ClearGraph(Graph);
    if (!Root) return false;
    FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> WalkCreator(*Graph);
    auto* Walk = WalkCreator.CreateNode();
    Walk->Node.SetBlendSpace(BlendSpace);
    Walk->NodePosX = -280; Walk->NodePosY = 200;
    WalkCreator.Finalize();
    FGraphNodeCreator<UAnimGraphNode_SequencePlayer> FallCreator(*Graph);
    auto* Falling = FallCreator.CreateNode();
    Falling->Node.SetSequence(Fall);
    Falling->Node.SetLoopAnimation(true);
    Falling->NodePosX = -280; Falling->NodePosY = -100;
    FallCreator.Finalize();
    auto* Blend = AddNode<UAnimGraphNode_BlendListByBool>(Graph, 170, 50);
    auto* Speed = AddVariable(Graph, Parent, TEXT("GroundSpeed"), -600, 250);
    auto* IsFalling = AddVariable(Graph, Parent, TEXT("bIsFalling"), -100, -270);
    // Bool pose index 0 is True; index 1 is False (see the native node's CustomizePinData).
    // UE mannequin's 2D blend space uses X=Direction and Y=Speed.
    return Connect(Speed, TEXT("GroundSpeed"), Walk, TEXT("Y"))
        && Connect(Falling, TEXT("Pose"), Blend, TEXT("BlendPose_0"))
        && Connect(Walk, TEXT("Pose"), Blend, TEXT("BlendPose_1"))
        && Connect(IsFalling, TEXT("bIsFalling"), Blend, TEXT("bActiveValue"))
        && Connect(Blend, TEXT("Pose"), Root, TEXT("Result"));
}

TSharedRef<FJsonObject> RoundTrip(UAnimBlueprint* Blueprint, FName GraphName, const TCHAR* OutputName)
{
    auto Result = MakeShared<FJsonObject>();
    UEdGraph* OriginalGraph = FindGraph(Blueprint, GraphName);
    if (!OriginalGraph) { Result->SetBoolField(TEXT("passed"), false); return Result; }
    TSet<UObject*> Selection;
    FGuid OutputNodeGuid;
    FName OutputPinName;
    for (UEdGraphNode* Node : OriginalGraph->Nodes)
    {
        if (Node->CanDuplicateNode()) Selection.Add(Node);
        if (auto* Root = Cast<UAnimGraphNode_Root>(Node))
        {
            if (UEdGraphPin* ResultPin = Root->FindPin(TEXT("Result")); ResultPin && ResultPin->LinkedTo.Num() == 1)
            {
                OutputNodeGuid = ResultPin->LinkedTo[0]->GetOwningNode()->NodeGuid;
                OutputPinName = ResultPin->LinkedTo[0]->PinName;
            }
        }
    }
    FString Text;
    FEdGraphUtilities::ExportNodesToText(Selection, Text);
    const FString Filename = FPaths::ProjectSavedDir() / TEXT("TutorialBlueprints") / OutputName;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    if (!FFileHelper::SaveStringToFile(Text, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Result->SetBoolField(TEXT("passed"), false);
        Result->SetStringField(TEXT("error"), TEXT("Could not save the newly exported native animation node text."));
        return Result;
    }
    auto& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    const FString CopyName = FString(TEXT("RoundTrip_")) + Blueprint->GetName() + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Copy = Cast<UAnimBlueprint>(Assets.DuplicateAsset(CopyName, TEXT("/Game/Tutorial/Tests"), Blueprint));
    UEdGraph* CopyGraph = Copy ? FindGraph(Copy, GraphName) : nullptr;
    if (!CopyGraph) { Result->SetBoolField(TEXT("passed"), false); return Result; }
    // Output Pose is a schema-owned, non-copyable node. Preserve it exactly as the
    // editor does when a learner pastes nodes, then reconnect the pasted pose output.
    UAnimGraphNode_Root* CopyRoot = ClearGraph(CopyGraph);
    if (!CopyRoot || !OutputNodeGuid.IsValid()) { Result->SetBoolField(TEXT("passed"), false); return Result; }
    TSet<UEdGraphNode*> Imported;
    FEdGraphUtilities::ImportNodesFromText(CopyGraph, Text, Imported);
    bool Reconnected = false;
    for (UEdGraphNode* Node : Imported)
        if (Node->NodeGuid == OutputNodeGuid) Reconnected = Connect(Node, OutputPinName, CopyRoot, TEXT("Result"));
    if (!Reconnected)
    {
        Result->SetBoolField(TEXT("passed"), false);
        Result->SetStringField(TEXT("error"), TEXT("Could not reconnect the pasted output to the existing Output Pose."));
        return Result;
    }
    const auto CompileResult = Compile(Copy);
    Result->SetStringField(TEXT("source"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("graph"), GraphName.ToString());
    Result->SetStringField(TEXT("nativeText"), Filename);
    Result->SetNumberField(TEXT("totalGraphNodes"), OriginalGraph->Nodes.Num());
    Result->SetNumberField(TEXT("sourceNodes"), Selection.Num());
    Result->SetNumberField(TEXT("importedNodes"), Imported.Num());
    Result->SetBoolField(TEXT("preservedOutputPoseNode"), true);
    Result->SetBoolField(TEXT("reconnectedOutputPose"), Reconnected);
    Result->SetObjectField(TEXT("compile"), CompileResult);
    Result->SetBoolField(TEXT("passed"), CompileResult->GetBoolField(TEXT("passed")) && Imported.Num() == Selection.Num());
    // This validation duplicate is intentionally not saved or shipped.
    Copy->GetOutermost()->SetDirtyFlag(false);
    return Result;
}
}

FString UMMOAnimationTools::PrepareCharacterAnimation()
{
    using namespace MMOAnimationBuilder;
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetStringField(TEXT("checkedAt"), FDateTime::UtcNow().ToIso8601());
    if (!IsMMODocProject())
    {
        Report->SetStringField(TEXT("error"), TEXT("Open the original MMORPG lab or a registered stage 3 lesson project."));
        return Encode(Report);
    }
    UClass* RuntimeClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Script/MMORPG.MMOAnimInstance"));
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    auto* BlendSpace = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run"));
    auto* Fall = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"));
    if (!RuntimeClass || !Mesh || !BlendSpace || !Fall)
    {
        Report->SetStringField(TEXT("error"), TEXT("Build MMOAnimInstance and copy the UE template character resources before generation."));
        return Encode(Report);
    }
    FScopedTransaction Transaction(NSLOCTEXT("MMODocTools", "PrepareAnimation", "Prepare MMO animation layers"));
    auto* Interface = GetOrCreate(TEXT("ALI_MMOCharacter"), RuntimeClass, Mesh->GetSkeleton(), Mesh, true);
    if (!Interface) return Encode(Report);
    if (!FindGraph(Interface, TEXT("Locomotion")))
    {
        auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Interface, TEXT("Locomotion"), UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());
        FBlueprintEditorUtils::AddDomainSpecificGraph(Interface, Graph);
    }
    TArray<TSharedPtr<FJsonValue>> Compiles;
    auto InterfaceCompile = Compile(Interface);
    Compiles.Add(MakeShared<FJsonValueObject>(InterfaceCompile));
    Report->SetArrayField(TEXT("compiles"), Compiles);
    if (!InterfaceCompile->GetBoolField(TEXT("passed")) || !Save(Interface)) return Encode(Report);
    auto* Unarmed = GetOrCreate(TEXT("ABP_MMOUnarmed"), RuntimeClass, Mesh->GetSkeleton(), Mesh, false);
    auto* Main = GetOrCreate(TEXT("ABP_MMOCharacter"), RuntimeClass, Mesh->GetSkeleton(), Mesh, false);
    if (!Unarmed || !Main || !Implement(Unarmed, Interface) || !Implement(Main, Interface)) return Encode(Report);
    if (!BuildLocomotion(FindGraph(Unarmed, TEXT("Locomotion")), RuntimeClass, BlendSpace, Fall)
        || !ReferencePose(FindGraph(Unarmed, TEXT("AnimGraph")))
        || !BuildLocomotion(FindGraph(Main, TEXT("Locomotion")), RuntimeClass, BlendSpace, Fall))
    {
        Report->SetStringField(TEXT("error"), TEXT("Animation graph pin connection failed; inspect native node pins."));
        return Encode(Report);
    }
    auto UnarmedCompile = Compile(Unarmed);
    Compiles.Add(MakeShared<FJsonValueObject>(UnarmedCompile));
    Report->SetArrayField(TEXT("compiles"), Compiles);
    if (!UnarmedCompile->GetBoolField(TEXT("passed")) || !Save(Unarmed)) return Encode(Report);
    auto* MainGraph = FindGraph(Main, TEXT("AnimGraph"));
    auto* Root = ClearGraph(MainGraph);
    if (!Root) return Encode(Report);
    FGraphNodeCreator<UAnimGraphNode_LinkedAnimLayer> LayerCreator(*MainGraph);
    auto* Layer = LayerCreator.CreateNode();
    Layer->Node.Interface = Interface->GeneratedClass;
    Layer->Node.InstanceClass = Unarmed->GeneratedClass;
    Layer->Node.Layer = TEXT("Locomotion");
    // SetLayerName is public but not DLL-exported by this 5.8 MinimalAPI class.
    // Apply the same reflected member-reference update without patching the engine.
    auto* ReferenceProperty = FindFProperty<FStructProperty>(Layer->GetClass(), TEXT("FunctionReference"));
    if (!ReferenceProperty || ReferenceProperty->Struct != FMemberReference::StaticStruct())
    {
        Report->SetStringField(TEXT("error"), TEXT("Linked layer FunctionReference schema is unavailable."));
        return Encode(Report);
    }
    FGuid FunctionGuid;
    FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(Interface->GeneratedClass, TEXT("Locomotion"), FunctionGuid);
    ReferenceProperty->ContainerPtrToValuePtr<FMemberReference>(Layer)->SetExternalMember(TEXT("Locomotion"), Interface->GeneratedClass, FunctionGuid);
    Layer->InterfaceGuid = FBlueprintEditorUtils::FindInterfaceGraphGuid(TEXT("Locomotion"), Interface->GeneratedClass);
    Layer->NodePosX = -300; Layer->NodePosY = 50;
    LayerCreator.Finalize();
    auto* Slot = AddNode<UAnimGraphNode_Slot>(MainGraph, 170, 50);
    Slot->Node.SlotName = TEXT("DefaultSlot");
    if (!Connect(Layer, TEXT("Pose"), Slot, TEXT("Source")) || !Connect(Slot, TEXT("Pose"), Root, TEXT("Result")))
    {
        Report->SetStringField(TEXT("error"), TEXT("Main animation graph pin connection failed."));
        return Encode(Report);
    }
    auto MainCompile = Compile(Main);
    Compiles.Add(MakeShared<FJsonValueObject>(MainCompile));
    Report->SetArrayField(TEXT("compiles"), Compiles);
    if (!MainCompile->GetBoolField(TEXT("passed")) || !Save(Main)) return Encode(Report);
    const auto MainRoundTrip = RoundTrip(Main, TEXT("AnimGraph"), TEXT("mmo-character-animgraph.txt"));
    const auto LayerRoundTrip = RoundTrip(Unarmed, TEXT("Locomotion"), TEXT("mmo-unarmed-locomotion.txt"));
    Report->SetArrayField(TEXT("roundTrips"), {MakeShared<FJsonValueObject>(MainRoundTrip), MakeShared<FJsonValueObject>(LayerRoundTrip)});
    Report->SetBoolField(TEXT("passed"), MainRoundTrip->GetBoolField(TEXT("passed")) && LayerRoundTrip->GetBoolField(TEXT("passed")));
    return Encode(Report);
}

FString UMMOAnimationTools::ConfigureCharacter(UBlueprint* Blueprint)
{
    using namespace MMOAnimationBuilder;
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()).Replace(TEXT("\\"), TEXT("/"));
    if (!IsMMODocProject() || !Blueprint
        || Blueprint->GetPathName() != TEXT("/Game/Tutorial/BP_MMOCharacter.BP_MMOCharacter"))
    {
        Report->SetStringField(TEXT("error"), TEXT("Expected the original BP_MMOCharacter inside the isolated MMORPG lab."));
        return Encode(Report);
    }
    auto* MeshAsset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    auto* AnimClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Characters/MMO/ABP_MMOCharacter.ABP_MMOCharacter_C"));
    if (!MeshAsset || !AnimClass || !Blueprint->GeneratedClass) return Encode(Report);
    auto* Defaults = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
    if (!Defaults || !Defaults->GetMesh()) return Encode(Report);
    FScopedTransaction Transaction(NSLOCTEXT("MMODocTools", "ConfigureManny", "Configure MMO Manny character defaults"));
    Blueprint->Modify(); Defaults->Modify();
    USkeletalMeshComponent* MeshComponent = Defaults->GetMesh();
    MeshComponent->Modify();
    MeshComponent->SetSkeletalMesh(MeshAsset);
    MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    MeshComponent->SetAnimInstanceClass(AnimClass);
    MeshComponent->SetRelativeLocation(FVector(0, 0, -Defaults->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
    MeshComponent->SetRelativeRotation(FRotator(0, -90, 0));
    MeshComponent->SetCollisionProfileName(TEXT("CharacterMesh"));
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    Defaults = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
    MeshComponent = Defaults ? Defaults->GetMesh() : nullptr;
    const bool Retained = MeshComponent && MeshComponent->GetSkeletalMeshAsset() == MeshAsset && MeshComponent->GetAnimClass() == AnimClass
        && MeshComponent->GetAnimationMode() == EAnimationMode::AnimationBlueprint;
    Report->SetNumberField(TEXT("errors"), Log.NumErrors);
    Report->SetNumberField(TEXT("warnings"), Log.NumWarnings);
    Report->SetBoolField(TEXT("defaultsRetainedAfterCompile"), Retained);
    Report->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Report->SetStringField(TEXT("mesh"), MeshAsset->GetPathName());
    Report->SetStringField(TEXT("animClass"), AnimClass->GetPathName());
    if (MeshComponent)
    {
        Report->SetStringField(TEXT("relativeLocation"), MeshComponent->GetRelativeLocation().ToString());
        Report->SetStringField(TEXT("relativeRotation"), MeshComponent->GetRelativeRotation().ToString());
    }
    Report->SetBoolField(TEXT("passed"), Log.NumErrors == 0 && Retained && Save(Blueprint));
    return Encode(Report);
}

FString UMMOAnimationTools::ValidateCharacterAnimation()
{
    using namespace MMOAnimationBuilder;
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetBoolField(TEXT("saved"), false);
    Report->SetBoolField(TEXT("sourceGraphsReconstructed"), false);
    Report->SetStringField(TEXT("checkedAt"), FDateTime::UtcNow().ToIso8601());
    const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()).Replace(TEXT("\\"), TEXT("/"));
    if (!IsMMODocProject())
    {
        Report->SetStringField(TEXT("error"), TEXT("Validate existing animation only inside the isolated MMORPG lab."));
        return Encode(Report);
    }
    auto* Interface = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/MMO/ALI_MMOCharacter.ALI_MMOCharacter"));
    auto* Main = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/MMO/ABP_MMOCharacter.ABP_MMOCharacter"));
    auto* Unarmed = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/MMO/ABP_MMOUnarmed.ABP_MMOUnarmed"));
    if (!Interface || !Main || !Unarmed)
    {
        Report->SetStringField(TEXT("error"), TEXT("The three existing animation assets are required; validation never creates source assets."));
        return Encode(Report);
    }
    const TArray<UAnimBlueprint*> Sources { Interface, Unarmed, Main };
    auto CollectGuids = [&Sources]()
    {
        TArray<FString> Guids;
        for (UAnimBlueprint* Source : Sources)
        {
            TArray<UEdGraph*> Graphs;
            Source->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs)
            {
                const FString Prefix = Source->GetPathName() + TEXT(":") + Graph->GetName();
                Guids.Add(Prefix + TEXT(":graph:") + Graph->GraphGuid.ToString());
                for (UEdGraphNode* Node : Graph->Nodes) if (Node) Guids.Add(Prefix + TEXT(":node:") + Node->NodeGuid.ToString());
            }
        }
        Guids.Sort();
        return Guids;
    };
    const TArray<FString> BeforeGuids = CollectGuids();
    TArray<TSharedPtr<FJsonValue>> Compiles;
    for (UAnimBlueprint* Source : Sources)
    {
        const auto Result = Compile(Source);
        Compiles.Add(MakeShared<FJsonValueObject>(Result));
        Report->SetArrayField(TEXT("compiles"), Compiles);
        if (!Result->GetBoolField(TEXT("passed")) || Result->GetIntegerField(TEXT("warnings")) != 0)
            return Encode(Report);
    }
    int32 CorrectSpeedBindings = 0;
    for (UAnimBlueprint* Source : { Main, Unarmed })
    {
        UEdGraph* Graph = FindGraph(Source, TEXT("Locomotion"));
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            auto* Player = Cast<UAnimGraphNode_BlendSpacePlayer>(Node);
            UEdGraphPin* SpeedPin = Player ? Player->FindPin(TEXT("Y"), EGPD_Input) : nullptr;
            if (!SpeedPin || SpeedPin->LinkedTo.Num() != 1) continue;
            auto* Variable = Cast<UK2Node_VariableGet>(SpeedPin->LinkedTo[0]->GetOwningNode());
            if (Variable && Variable->VariableReference.GetMemberName() == TEXT("GroundSpeed")) ++CorrectSpeedBindings;
        }
    }
    Report->SetStringField(TEXT("speedAxis"), TEXT("Y"));
    Report->SetNumberField(TEXT("groundSpeedBindings"), CorrectSpeedBindings);
    if (CorrectSpeedBindings != 2)
    {
        Report->SetStringField(TEXT("error"), TEXT("Both existing Locomotion graphs must connect GroundSpeed to the template BlendSpace Y/Speed input."));
        return Encode(Report);
    }
    const auto MainRoundTrip = RoundTrip(Main, TEXT("AnimGraph"), TEXT("mmo-character-animgraph.txt"));
    const auto LayerRoundTrip = RoundTrip(Unarmed, TEXT("Locomotion"), TEXT("mmo-unarmed-locomotion.txt"));
    Report->SetArrayField(TEXT("roundTrips"), {MakeShared<FJsonValueObject>(MainRoundTrip), MakeShared<FJsonValueObject>(LayerRoundTrip)});
    const bool bGuidsPreserved = BeforeGuids == CollectGuids();
    Report->SetBoolField(TEXT("originalNodeGuidsPreserved"), bGuidsPreserved);
    for (const auto& Result : { MainRoundTrip, LayerRoundTrip })
    {
        if (!Result->GetBoolField(TEXT("passed")) || Result->GetObjectField(TEXT("compile"))->GetIntegerField(TEXT("warnings")) != 0)
            return Encode(Report);
    }
    if (!bGuidsPreserved) return Encode(Report);
    const bool bSaved = Save(Interface) && Save(Unarmed) && Save(Main);
    Report->SetBoolField(TEXT("saved"), bSaved);
    Report->SetBoolField(TEXT("passed"), bSaved);
    return Encode(Report);
}
