#include "MMODocProjectGuard.h"
#include "MMODocToolsLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

FString UMMODocToolsLibrary::ConfigureManaStatus(UWidgetBlueprint* Blueprint)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("date"), FDateTime::UtcNow().ToIso8601());
    Report->SetBoolField(TEXT("passed"), false);
    Report->SetBoolField(TEXT("saved"), false);
    Report->SetBoolField(TEXT("gameplayExecuted"), false);
    auto Encode = [&]() { FString Text; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text)); return Text; };
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()); FPaths::NormalizeFilename(Project);
    if (!IsMMODocProject() || !Blueprint || Blueprint->GetPathName() != TEXT("/Game/UI/WBP_ManaStatus.WBP_ManaStatus"))
    { Report->SetStringField(TEXT("error"), TEXT("Only the isolated lab WBP_ManaStatus may be configured")); return Encode(); }
    UClass* ModelClass = LoadClass<UObject>(nullptr, TEXT("/Script/MMORPG.MMOPlayerViewModel"));
    FProperty* ManaLabel = ModelClass ? FindFProperty<FProperty>(ModelClass, TEXT("ManaLabel")) : nullptr;
    FProperty* ManaFraction = ModelClass ? FindFProperty<FProperty>(ModelClass, TEXT("ManaFraction")) : nullptr;
    if (!ManaLabel || !ManaFraction || !GEditor)
    { Report->SetStringField(TEXT("error"), TEXT("Build the MMORPGEditor ViewModel fields first")); return Encode(); }
    Blueprint->Modify();
    if (Blueprint->WidgetTree) Blueprint->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
    Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional);
    auto* Box = Blueprint->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ManaLayout"));
    Blueprint->WidgetTree->RootWidget = Box;
    auto* Text = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ManaText"));
    Text->bIsVariable = true;
    Text->SetText(FText::FromStringTable(TEXT("/Game/Localization/ST_MMO.ST_MMO"), TEXT("Mana.Preview")));
    Text->SetColorAndOpacity(FSlateColor(FLinearColor(.65f, .88f, 1.f)));
    auto Font = Text->GetFont(); Font.Size = 18; Text->SetFont(Font);
    Box->AddChildToVerticalBox(Text)->SetPadding(FMargin(0, 0, 0, 6));
    auto* Size = Blueprint->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ManaBarSize"));
    Size->SetHeightOverride(14.f); Box->AddChildToVerticalBox(Size);
    auto* Bar = Blueprint->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ManaBar"));
    Bar->bIsVariable = true; Bar->SetPercent(1.f); Bar->SetFillColorAndOpacity(FLinearColor(.08f, .5f, 1.f)); Size->SetContent(Bar);

    auto* Editor = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
    auto* View = Editor->RequestView(Blueprint); View->Modify();
    while (View->GetNumBindings()) View->RemoveBindingAt(0);
    TArray<FGuid> OldModels; for (const auto& Context : View->GetViewModels()) OldModels.Add(Context.GetViewModelId());
    View->RemoveViewModels(OldModels);
    FMVVMBlueprintViewModelContext Context(ModelClass, TEXT("PlayerVM"));
    Context.CreationType = EMVVMBlueprintViewModelContextCreationType::Manual;
    Context.bOptional = true; Context.bCreateSetterFunction = true;
    View->AddViewModel(Context);
    auto Add = [&](FProperty* SourceProperty, UWidget* Widget, const TCHAR* DestinationName)
    {
        auto& Binding = View->AddDefaultBinding();
        Binding.SourcePath.SetViewModelId(Context.GetViewModelId());
        Binding.SourcePath.SetPropertyPath(Blueprint, UE::MVVM::FMVVMConstFieldVariant(SourceProperty));
        Binding.DestinationPath.SetWidgetName(Widget->GetFName());
        Binding.DestinationPath.SetPropertyPath(Blueprint, UE::MVVM::FMVVMConstFieldVariant(FindFProperty<FProperty>(Widget->GetClass(), DestinationName)));
        Binding.BindingType = EMVVMBindingMode::OneWayToDestination;
        Binding.bOverrideExecutionMode = true; Binding.OverrideExecutionMode = EMVVMExecutionMode::Immediate;
    };
    Add(ManaLabel, Text, TEXT("Text")); Add(ManaFraction, Bar, TEXT("Percent"));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FCompilerResultsLog Results; FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &Results);
    Report->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Report->SetStringField(TEXT("viewModelClass"), ModelClass->GetPathName());
    Report->SetStringField(TEXT("viewModelName"), TEXT("PlayerVM"));
    Report->SetStringField(TEXT("creationType"), TEXT("Manual"));
    Report->SetStringField(TEXT("bindingMode"), TEXT("OneWayToDestination / Immediate"));
    Report->SetNumberField(TEXT("bindingCount"), View->GetNumBindings());
    Report->SetArrayField(TEXT("bindings"), {MakeShared<FJsonValueString>(TEXT("PlayerVM.ManaLabel -> ManaText.Text")), MakeShared<FJsonValueString>(TEXT("PlayerVM.ManaFraction -> ManaBar.Percent"))});
    Report->SetNumberField(TEXT("compilerErrors"), Results.NumErrors); Report->SetNumberField(TEXT("compilerWarnings"), Results.NumWarnings);
    Report->SetBoolField(TEXT("passed"), Results.NumErrors == 0 && Blueprint->Status == BS_UpToDate && View->GetNumBindings() == 2);
    const FString ReportPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Evidence/mana-status-bindings.json"));
    Report->SetStringField(TEXT("reportPath"), ReportPath);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true); FFileHelper::SaveStringToFile(Encode(), *ReportPath);
    return Encode();
}
