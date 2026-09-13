#include "LyraDocToolsLibrary.h"

#include "AbilitySystem/Abilities/LyraGameplayAbility.h"
#include "AbilitySystem/LyraAbilitySet.h"
#include "AssetCompilingManager.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "Engine/AssetManagerTypes.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFeatureData.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraWorldSettings.h"
#include "HAL/FileManager.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogLyraDocTools, Log, All);

namespace
{
bool IsTrainingAsset(const UObject* Asset, const TCHAR* RequiredRoot)
{
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(Project);
    const bool bLab = Project.EndsWith(TEXT("/LyraDocLabs/LyraTraining"), ESearchCase::IgnoreCase);
    const bool bAsset = IsValid(Asset) && !Asset->HasAnyFlags(RF_ClassDefaultObject | RF_Transient)
        && Asset->GetOutermost()->GetName().StartsWith(RequiredRoot, ESearchCase::CaseSensitive);
    if (!IsInGameThread() || !bLab || !bAsset)
    {
        UE_LOG(LogLyraDocTools, Error, TEXT("Mutation requires the LyraDocLabs/LyraTraining editor and an asset under %s; received %s"),
            RequiredRoot, *GetPathNameSafe(Asset));
        return false;
    }
    return true;
}

UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& Name)
{
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UEdGraph* Match = nullptr;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph && Graph->GetName() == Name)
        {
            // Ambiguous nested graph names must not silently select another graph.
            if (Match) return nullptr;
            Match = Graph;
        }
    }
    return Match;
}

FString StatusName(const UBlueprint* Blueprint)
{
    return StaticEnum<EBlueprintStatus>()->GetNameStringByValue(static_cast<int64>(Blueprint->Status.GetValue()));
}

TSharedRef<FJsonObject> NewReport(const UObject* Asset, const FString& GraphName)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetStringField(TEXT("asset"), GetPathNameSafe(Asset));
    Report->SetStringField(TEXT("graph"), GraphName);
    Report->SetNumberField(TEXT("nodeCount"), 0);
    Report->SetNumberField(TEXT("errorCount"), 0);
    Report->SetArrayField(TEXT("errors"), {});
    return Report;
}

FString ToJson(const TSharedRef<FJsonObject>& Report)
{
    FString Output;
    const auto Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Report, Writer);
    return Output;
}

FString Fail(const TSharedRef<FJsonObject>& Report, const FString& Error)
{
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetNumberField(TEXT("errorCount"), 1);
    Report->SetArrayField(TEXT("errors"), {MakeShared<FJsonValueString>(Error)});
    UE_LOG(LogLyraDocTools, Error, TEXT("%s"), *Error);
    return ToJson(Report);
}

struct FSignaturePin
{
    FString NodeClass;
    FName Name;
    EEdGraphPinDirection Direction;
    FEdGraphPinType Type;
};

TArray<FSignaturePin> GetFunctionSignature(const UEdGraph* Graph)
{
    TArray<FSignaturePin> Result;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>()))
        {
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin) Result.Add({Node->GetClass()->GetPathName(), Pin->GetFName(), Pin->Direction, Pin->PinType});
            }
        }
    }
    return Result;
}

bool SameSignature(const TArray<FSignaturePin>& Before, const TArray<FSignaturePin>& After)
{
    if (Before.Num() != After.Num()) return false;
    TArray<bool> Used;
    Used.Init(false, After.Num());
    for (const FSignaturePin& Pin : Before)
    {
        bool bFound = false;
        for (int32 Index = 0; Index < After.Num(); ++Index)
        {
            const FSignaturePin& Other = After[Index];
            if (!Used[Index] && Pin.NodeClass == Other.NodeClass && Pin.Name == Other.Name
                && Pin.Direction == Other.Direction && Pin.Type == Other.Type)
            {
                Used[Index] = true;
                bFound = true;
                break;
            }
        }
        if (!bFound) return false;
    }
    return true;
}
}

bool ULyraDocToolsLibrary::ConfigureTrainingWorld(UWorld* World, TSubclassOf<ULyraExperienceDefinition> Experience)
{
    if (!IsTrainingAsset(World, TEXT("/TrainingRange/Maps/")) || World->IsGameWorld() || !Experience
        || !Experience->GetOutermost()->GetName().StartsWith(TEXT("/TrainingRange/Experiences/"))) return false;
    ALyraWorldSettings* Settings = Cast<ALyraWorldSettings>(World->GetWorldSettings());
    FSoftClassProperty* Property = Settings ? FindFProperty<FSoftClassProperty>(Settings->GetClass(), TEXT("DefaultGameplayExperience")) : nullptr;
    if (!Property) return false;
    const FScopedTransaction Transaction(NSLOCTEXT("LyraDocTools", "SetWorldExperience", "Set training map experience"));
    Settings->Modify();
    Settings->PreEditChange(Property);
    *Property->ContainerPtrToValuePtr<TSoftClassPtr<ULyraExperienceDefinition>>(Settings) = Experience.Get();
    FPropertyChangedEvent Event(Property);
    Settings->PostEditChangeProperty(Event);
    World->MarkPackageDirty();
    return true;
}

bool ULyraDocToolsLibrary::BuildTrainingNavigation(UWorld* World)
{
    if (!IsTrainingAsset(World, TEXT("/TrainingRange/Maps/")) || World->IsGameWorld()) return false;
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Navigation)
    {
        UE_LOG(LogLyraDocTools, Error, TEXT("The training World has no navigation system."));
        return false;
    }

    // Editor navigation normally releases AsyncLoadLock after assets finish and
    // at least 16 ticker frames. A synchronous Python commandlet does not tick
    // between loading the map and this call. Finish those prerequisites explicitly.
    FlushAsyncLoading();
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (FAssetCompilingManager::Get().GetNumRemainingAssets() != 0)
    {
        UE_LOG(LogLyraDocTools, Error, TEXT("Training navigation requires all pending asset compilation to finish."));
        return false;
    }
    Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
        UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    const uint8 BlockingLocks = static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor);
    if (Navigation->IsNavigationBuildingLocked(BlockingLocks))
    {
        UE_LOG(LogLyraDocTools, Error, TEXT("Training navigation still has a non-async build lock; refusing to remove it."));
        return false;
    }

    // UNavigationSystemV1::Build waits for every navigation data generator. The
    // BUILDPATHS editor wrapper also opens a progress widget and is not Cmd-safe.
    Navigation->Build();
    World->MarkPackageDirty();
    UE_LOG(LogLyraDocTools, Display, TEXT("Training native navigation build completed; verify ground projections before saving."));
    return true;
}

int32 ULyraDocToolsLibrary::ReplaceAbilityInSet(ULyraAbilitySet* Asset,
    TSubclassOf<ULyraGameplayAbility> Old, TSubclassOf<ULyraGameplayAbility> New)
{
    if (!IsTrainingAsset(Asset, TEXT("/TrainingRange/")) || !Old || !New
        || !New->GetOutermost()->GetName().StartsWith(TEXT("/TrainingRange/"))) return -1;

    FArrayProperty* Grants = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("GrantedGameplayAbilities"));
    FStructProperty* Entry = Grants ? CastField<FStructProperty>(Grants->Inner) : nullptr;
    FClassProperty* Ability = Entry ? FindFProperty<FClassProperty>(Entry->Struct, TEXT("Ability")) : nullptr;
    if (!Ability || !New->IsChildOf(Ability->MetaClass))
    {
        UE_LOG(LogLyraDocTools, Error, TEXT("Lyra AbilitySet reflection schema did not match this engine/project revision."));
        return -1;
    }
    FScriptArrayHelper Values(Grants, Grants->ContainerPtrToValuePtr<void>(Asset));
    int32 Matched = 0;
    bool bChanged = false;
    const FScopedTransaction Transaction(NSLOCTEXT("LyraDocTools", "ReplaceAbility", "Replace training ability grant"));
    for (int32 Index = 0; Index < Values.Num(); ++Index)
    {
        void* Value = Values.GetRawPtr(Index);
        UObject* Current = Ability->GetObjectPropertyValue_InContainer(Value);
        if (Current == Old.Get() || Current == New.Get())
        {
            ++Matched;
            if (Current != New.Get())
            {
                if (!bChanged) { Asset->Modify(); Asset->PreEditChange(Grants); }
                Ability->SetObjectPropertyValue_InContainer(Value, New.Get());
                bChanged = true;
            }
        }
    }
    if (bChanged)
    {
        FPropertyChangedEvent Event(Grants, EPropertyChangeType::ValueSet);
        Asset->PostEditChangeProperty(Event);
        Asset->MarkPackageDirty();
    }
    return Matched;
}

bool ULyraDocToolsLibrary::ConfigureTrainingFeature(UGameFeatureData* Data)
{
    if (!IsTrainingAsset(Data, TEXT("/TrainingRange/"))) return false;
    FDirectoryPath Experiences; Experiences.Path = TEXT("/TrainingRange/Experiences");
    FDirectoryPath Maps; Maps.Path = TEXT("/TrainingRange/Maps");
    FPrimaryAssetTypeInfo ExperienceType(TEXT("LyraExperienceDefinition"), ULyraExperienceDefinition::StaticClass(),
        true, false, TArray<FDirectoryPath>{Experiences}, TArray<FSoftObjectPath>{});
    FPrimaryAssetTypeInfo MapType(TEXT("Map"), UWorld::StaticClass(),
        false, false, TArray<FDirectoryPath>{Maps}, TArray<FSoftObjectPath>{});
    ExperienceType.Rules.CookRule = EPrimaryAssetCookRule::AlwaysCook;
    MapType.Rules.CookRule = EPrimaryAssetCookRule::AlwaysCook;

    const FScopedTransaction Transaction(NSLOCTEXT("LyraDocTools", "ConfigureFeature", "Configure training asset scans"));
    Data->Modify();
    Data->PreEditChange(nullptr);
    TArray<FPrimaryAssetTypeInfo>& Types = Data->GetPrimaryAssetTypesToScan();
    Types.RemoveAll([](const FPrimaryAssetTypeInfo& Type) {
        return Type.PrimaryAssetType == TEXT("LyraExperienceDefinition") || Type.PrimaryAssetType == TEXT("Map");
    });
    Types.Add(MoveTemp(ExperienceType));
    Types.Add(MoveTemp(MapType));
    Data->PostEditChange();
    Data->MarkPackageDirty();
    return true;
}

FString ULyraDocToolsLibrary::RoundTripBlueprint(UBlueprint* TestCopy, const FString& GraphName, const FString& ClipboardPath)
{
    auto Report = NewReport(TestCopy, GraphName);
    Report->SetBoolField(TEXT("compiledThisCall"), false);
    Report->SetStringField(TEXT("clipboardPath"), ClipboardPath);
    if (!IsTrainingAsset(TestCopy, TEXT("/TrainingRange/Tests/")))
        return Fail(Report, TEXT("Expected an explicit test duplicate under /TrainingRange/Tests/ in the LyraTraining lab."));
    if (GraphName != TEXT("EventGraph") && GraphName != TEXT("SelectDirectionalMontage"))
        return Fail(Report, TEXT("This teaching verifier supports EventGraph and SelectDirectionalMontage only."));
    UEdGraph* Graph = FindGraph(TestCopy, GraphName);
    if (!Graph) return Fail(Report, TEXT("The requested graph is missing or its name is ambiguous."));
    FString Text;
    const int64 TextSize = IFileManager::Get().FileSize(*ClipboardPath);
    if (TextSize <= 0 || TextSize > 16 * 1024 * 1024 || !FFileHelper::LoadFileToString(Text, *ClipboardPath))
        return Fail(Report, TEXT("Clipboard file is missing, empty, unreadable, or exceeds 16 MiB."));
    if (!FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
        return Fail(Report, TEXT("The native importer cannot create nodes from this clipboard text."));

    const int32 OriginalCount = Graph->Nodes.Num();
    const auto OriginalSignature = GetFunctionSignature(Graph);
    const FGuid OriginalGraphGuid = Graph->GraphGuid;
    const int32 OriginalMemberCount = TestCopy->NewVariables.Num();
    const FScopedTransaction Transaction(NSLOCTEXT("LyraDocTools", "RoundTrip", "Verify training Blueprint clipboard import"));
    TestCopy->Modify();
    Graph->Modify();
    const TArray<TObjectPtr<UEdGraphNode>> OriginalNodes = Graph->Nodes;
    for (UEdGraphNode* Node : OriginalNodes)
    {
        if (!Node) continue;
        Node->Modify();
        Graph->RemoveNode(Node);
        // Free the old object names so clipboard pin references resolve to the imported nodes.
        // The transaction retains old objects for Undo; no official blueprint is touched.
        Node->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
    }
    TSet<UEdGraphNode*> Imported;
    FEdGraphUtilities::ImportNodesFromText(Graph, Text, Imported);
    for (UEdGraphNode* Node : Imported) Node->CreateNewGuid();
    const bool bSameSignature = SameSignature(OriginalSignature, GetFunctionSignature(Graph));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TestCopy);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(TestCopy, EBlueprintCompileOptions::SkipSave, &Results);
    Graph->NotifyGraphChanged();

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;
    for (const auto& Message : Results.Messages)
    {
        if (Message->GetSeverity() == EMessageSeverity::Error)
            Errors.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
        else if (Message->GetSeverity() == EMessageSeverity::Warning)
            Warnings.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
    }
    if (Imported.Num() != OriginalCount || Graph->Nodes.Num() != OriginalCount)
        Errors.Add(MakeShared<FJsonValueString>(TEXT("A complete graph export must import the same number of nodes as the test duplicate originally contained.")));
    if (!bSameSignature) Errors.Add(MakeShared<FJsonValueString>(TEXT("Imported function parameter/return pin signature differs from the original duplicate.")));
    if (Graph->GraphGuid != OriginalGraphGuid || TestCopy->NewVariables.Num() != OriginalMemberCount)
        Errors.Add(MakeShared<FJsonValueString>(TEXT("Graph identity or Blueprint member definitions changed unexpectedly.")));
    const bool bCompiled = TestCopy->Status == BS_UpToDate || TestCopy->Status == BS_UpToDateWithWarnings;
    Report->SetBoolField(TEXT("passed"), bCompiled && Results.NumErrors == 0 && Errors.IsEmpty());
    Report->SetBoolField(TEXT("compiledThisCall"), true);
    Report->SetStringField(TEXT("compileStatus"), StatusName(TestCopy));
    Report->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
    Report->SetNumberField(TEXT("importedNodeCount"), Imported.Num());
    Report->SetNumberField(TEXT("originalNodeCount"), OriginalCount);
    Report->SetNumberField(TEXT("compilerErrorCount"), Results.NumErrors);
    Report->SetNumberField(TEXT("errorCount"), Errors.Num());
    Report->SetNumberField(TEXT("warningCount"), Results.NumWarnings);
    Report->SetBoolField(TEXT("signaturePreserved"), bSameSignature);
    Report->SetArrayField(TEXT("errors"), Errors);
    Report->SetArrayField(TEXT("warnings"), Warnings);
    Report->SetBoolField(TEXT("saved"), false);
    return ToJson(Report);
}

FString ULyraDocToolsLibrary::ExportBlueprintGraph(UBlueprint* Source, const FString& GraphName, const FString& OutputPath)
{
    auto Report = NewReport(Source, GraphName);
    Report->SetBoolField(TEXT("compiledThisCall"), false);
    if (!IsInGameThread() || !IsValid(Source)) return Fail(Report, TEXT("Export requires a valid Blueprint on the editor game thread."));
    UEdGraph* Graph = FindGraph(Source, GraphName);
    if (!Graph) return Fail(Report, TEXT("The requested graph is missing or its name is ambiguous."));
    if (FPaths::IsRelative(OutputPath) || !FPaths::GetExtension(OutputPath).Equals(TEXT("txt"), ESearchCase::IgnoreCase))
        return Fail(Report, TEXT("Use an absolute .txt output path."));
    TSet<UObject*> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes) if (Node) Nodes.Add(Node);
    FString Text;
    // No compile, Modify, or PrepareForCopying call on the source asset. These tutorial
    // graphs use standard K2 nodes and do not need ownership changes for nested resources.
    FEdGraphUtilities::ExportNodesToText(Nodes, Text);
    if (Text.IsEmpty()) return Fail(Report, TEXT("The graph contains no exportable node text."));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
    if (!FFileHelper::SaveStringToFile(Text, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return Fail(Report, TEXT("Unable to write the native Blueprint text file."));
    Report->SetBoolField(TEXT("passed"), true);
    Report->SetNumberField(TEXT("nodeCount"), Nodes.Num());
    Report->SetStringField(TEXT("compileStatus"), StatusName(Source));
    Report->SetStringField(TEXT("outputPath"), OutputPath);
    Report->SetBoolField(TEXT("saved"), false);
    return ToJson(Report);
}
