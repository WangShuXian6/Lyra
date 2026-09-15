#include "MMODocProjectGuard.h"
#include "MMODocToolsLibrary.h"

#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Self.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "MMOBackendClient.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"

namespace
{
FString Json(const TSharedRef<FJsonObject>& Report)
{
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
    return Text;
}

FString Failure(const TSharedRef<FJsonObject>& Report, const FString& Message)
{
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetArrayField(TEXT("errors"), {MakeShared<FJsonValueString>(Message)});
    return Json(Report);
}

UEdGraph* EventGraph(UBlueprint* Blueprint)
{
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
        if (Graph && Graph->GetFName() == TEXT("EventGraph")) return Graph;
    return nullptr;
}

void EmptyGraph(UEdGraph* Graph)
{
    Graph->Modify();
    const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
    for (UEdGraphNode* Node : Nodes)
    {
        if (!Node) continue;
        Node->Modify();
        Graph->RemoveNode(Node);
        Node->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_DoNotDirty);
    }
}

bool Connect(const UEdGraphSchema_K2* Schema, UEdGraphNode* From, FName Output, UEdGraphNode* To, FName Input)
{
    UEdGraphPin* A = From->FindPin(Output, EGPD_Output);
    UEdGraphPin* B = To->FindPin(Input, EGPD_Input);
    return A && B && Schema->TryCreateConnection(A, B);
}

bool SetDefault(const UEdGraphSchema_K2* Schema, UEdGraphNode* Node, FName Name, const TCHAR* Value)
{
    UEdGraphPin* Pin = Node->FindPin(Name, EGPD_Input);
    if (!Pin) return false;
    Schema->TrySetDefaultValue(*Pin, Value);
    return Pin->DefaultValue == Value;
}

TSharedRef<FJsonObject> Compile(UBlueprint* Blueprint)
{
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &Results);
    auto Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors, Warnings;
    for (const auto& Message : Results.Messages)
    {
        if (Message->GetSeverity() == EMessageSeverity::Error)
            Errors.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
        else if (Message->GetSeverity() == EMessageSeverity::Warning)
            Warnings.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
    }
    const bool bUpToDate = Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings;
    Report->SetBoolField(TEXT("passed"), Results.NumErrors == 0 && bUpToDate);
    Report->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Report->SetStringField(TEXT("compileStatus"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(Blueprint->Status.GetValue()));
    Report->SetNumberField(TEXT("errorCount"), Results.NumErrors);
    Report->SetNumberField(TEXT("warningCount"), Results.NumWarnings);
    Report->SetArrayField(TEXT("errors"), Errors);
    Report->SetArrayField(TEXT("warnings"), Warnings);
    Report->SetBoolField(TEXT("compiledThisCall"), true);
    return Report;
}
}

FString UMMODocToolsLibrary::ConfigureBackendTutorial(UBlueprint* Blueprint)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetBoolField(TEXT("saved"), false);
    Report->SetBoolField(TEXT("httpExecuted"), false);
    Report->SetStringField(TEXT("verifiedAt"), FDateTime::UtcNow().ToIso8601());
    Report->SetStringField(TEXT("asset"), GetPathNameSafe(Blueprint));
    Report->SetArrayField(TEXT("errors"), {});

    FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(ProjectDir);
    if (!IsInGameThread() || !IsMMODocProject()
        || !IsValid(Blueprint) || Blueprint->HasAnyFlags(RF_ClassDefaultObject | RF_Transient)
        || Blueprint->GetOutermost()->GetName() != TEXT("/Game/Tutorial/BP_BackendHealth")
        || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
        return Failure(Report, TEXT("Expected the Actor Blueprint /Game/Tutorial/BP_BackendHealth in the LyraDocLabs/MMORPG editor."));

    UFunction* Factory = UMMORequestAction::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UMMORequestAction, RequestMMO));
    UFunction* PrintFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
    if (!Factory || !PrintFunction) return Failure(Report, TEXT("Required reflected backend factory or PrintString function is missing."));

    const FScopedTransaction Transaction(NSLOCTEXT("MMODocTools", "BuildHealth", "Build backend health tutorial graph"));
    Blueprint->Modify();
    UEdGraph* Graph = EventGraph(Blueprint);
    if (!Graph)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
    }
    const auto* Schema = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Schema) return Failure(Report, TEXT("The tutorial graph must use the K2 schema."));
    EmptyGraph(Graph);

    FGraphNodeCreator<UK2Node_Event> EventCreator(*Graph);
    UK2Node_Event* BeginPlay = EventCreator.CreateNode();
    BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
    BeginPlay->bOverrideFunction = true;
    BeginPlay->NodePosX = 0; BeginPlay->NodePosY = 0;
    EventCreator.Finalize();

    FGraphNodeCreator<UK2Node_AsyncAction> RequestCreator(*Graph);
    UK2Node_AsyncAction* Request = RequestCreator.CreateNode();
    Request->InitializeProxyFromFunction(Factory);
    Request->NodePosX = 320; Request->NodePosY = 0;
    RequestCreator.Finalize();

    FGraphNodeCreator<UK2Node_CallFunction> PrintCreator(*Graph);
    UK2Node_CallFunction* Print = PrintCreator.CreateNode();
    Print->SetFromFunction(PrintFunction);
    Print->NodePosX = 800; Print->NodePosY = 120;
    PrintCreator.Finalize();

    FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);
    UK2Node_Self* Self = SelfCreator.CreateNode();
    Self->NodePosX = 0; Self->NodePosY = 270;
    SelfCreator.Finalize();

    const bool bDefaults = SetDefault(Schema, Request, TEXT("Verb"), TEXT("GET"))
        && SetDefault(Schema, Request, TEXT("Route"), TEXT("/health"))
        && SetDefault(Schema, Request, TEXT("JsonBody"), TEXT("{}"));
    const bool bConnections = Connect(Schema, BeginPlay, UEdGraphSchema_K2::PN_Then, Request, UEdGraphSchema_K2::PN_Execute)
        && Connect(Schema, Request, TEXT("Completed"), Print, UEdGraphSchema_K2::PN_Execute)
        && Connect(Schema, Request, TEXT("Json"), Print, TEXT("InString"))
        && Connect(Schema, Self, UEdGraphSchema_K2::PN_Self, Request, TEXT("WorldContextObject"))
        && Connect(Schema, Self, UEdGraphSchema_K2::PN_Self, Print, TEXT("WorldContextObject"));
    if (!bDefaults || !bConnections) return Failure(Report, TEXT("A requested pin/default is absent or the native K2 schema rejected a connection."));

    auto SourceCompilation = Compile(Blueprint);
    Report->SetObjectField(TEXT("sourceCompilation"), SourceCompilation);
    Report->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
    if (!SourceCompilation->GetBoolField(TEXT("passed"))) return Failure(Report, TEXT("The constructed source graph failed native compilation."));

    TSet<UObject*> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes) if (Node) Nodes.Add(Node);
    FString Text;
    FEdGraphUtilities::ExportNodesToText(Nodes, Text);
    const FString EvidenceDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Evidence")));
    const FString TextPath = FPaths::Combine(EvidenceDir, TEXT("backend-health.txt"));
    IFileManager::Get().MakeDirectory(*EvidenceDir, true);
    if (Text.IsEmpty() || !FFileHelper::SaveStringToFile(Text, *TextPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return Failure(Report, TEXT("Unable to export the actual node text to Saved/Evidence/backend-health.txt."));
    Report->SetStringField(TEXT("exportPath"), TextPath);

    const FString TestName = TEXT("BP_BackendHealth_RoundTrip_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    UBlueprint* TestCopy = Cast<UBlueprint>(AssetTools.Get().DuplicateAsset(TestName, TEXT("/Game/Tutorial/Tests"), Blueprint));
    if (!TestCopy) return Failure(Report, TEXT("Unable to create a complete round-trip test Blueprint duplicate."));
    Report->SetStringField(TEXT("testAsset"), TestCopy->GetPathName());
    UEdGraph* TestGraph = EventGraph(TestCopy);
    // Read the file back, so validation exercises the exported artifact rather than only an in-memory string.
    FString ExportedText;
    if (!TestGraph || !FFileHelper::LoadFileToString(ExportedText, *TextPath)
        || !FEdGraphUtilities::CanImportNodesFromText(TestGraph, ExportedText))
        return Failure(Report, TEXT("The saved node text cannot be imported into the complete test Blueprint."));
    TestCopy->Modify();
    EmptyGraph(TestGraph);
    TSet<UEdGraphNode*> Imported;
    FEdGraphUtilities::ImportNodesFromText(TestGraph, ExportedText, Imported);
    for (UEdGraphNode* Node : Imported) Node->CreateNewGuid();
    auto TestCompilation = Compile(TestCopy);
    TestCompilation->SetNumberField(TEXT("importedNodeCount"), Imported.Num());
    TestCompilation->SetNumberField(TEXT("nodeCount"), TestGraph->Nodes.Num());
    const bool bSameCount = Imported.Num() == Nodes.Num() && TestGraph->Nodes.Num() == Graph->Nodes.Num();
    Report->SetObjectField(TEXT("roundTripCompilation"), TestCompilation);
    Report->SetBoolField(TEXT("nodeCountPreserved"), bSameCount);
    Report->SetBoolField(TEXT("passed"), bSameCount && TestCompilation->GetBoolField(TEXT("passed")));
    if (!Report->GetBoolField(TEXT("passed")))
        Report->SetArrayField(TEXT("errors"), {MakeShared<FJsonValueString>(TEXT("Imported graph node count or native compilation did not pass."))});
    Graph->NotifyGraphChanged();
    TestGraph->NotifyGraphChanged();
    const FString ReportPath = FPaths::Combine(EvidenceDir, TEXT("backend-health-roundtrip.json"));
    Report->SetStringField(TEXT("reportPath"), ReportPath);
    const FString ReportText = Json(Report);
    if (!FFileHelper::SaveStringToFile(ReportText, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        return Failure(Report, TEXT("Compilation finished, but the evidence JSON file could not be written."));
    return ReportText;
}
