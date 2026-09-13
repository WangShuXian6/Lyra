#include "MMODocToolsLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "UObject/UnrealType.h"

namespace
{
const FLinearColor Ink(.035f, .052f, .09f, 1.f);
const FLinearColor Paper(.90f, .94f, 1.f, 1.f);
const FLinearColor Muted(.62f, .73f, .87f, 1.f);
const FLinearColor Accent(.08f, .36f, .65f, 1.f);
bool ReadDesignerStrings(TMap<FString,FString>& Strings,FString& Error)
{
    FString Text; TSharedPtr<FJsonObject> Document;
    if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir()/TEXT("ui-designer-strings.json")))
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Document))
    { Error=TEXT("Missing project ui-designer-strings.json; sync the original teaching manifest first."); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Entries;
    if (!Document->TryGetArrayField(TEXT("entries"),Entries)) { Error=TEXT("String manifest entries missing"); return false; }
    for (const auto& Entry:*Entries)
    {
        const auto Object=Entry->AsObject();
        Strings.Add(Object->GetStringField(TEXT("key")),Object->GetStringField(TEXT("source")));
    }
    return true;
}

// This is an asset authoring utility in an Editor-only module. The packaged game
// instantiates these saved WidgetBlueprints; it never executes this tree builder.
struct FDesigner
{
    UWidgetBlueprint* Blueprint;
    UWidgetTree* Tree;
    TMap<FString, FString> Strings;
    FString Error;
    explicit FDesigner(UWidgetBlueprint* In) : Blueprint(In), Tree(In->WidgetTree) {}

    bool ReadStrings()
    {
        if (!ReadDesignerStrings(Strings,Error)) return false;
        if (!LoadObject<UStringTable>(nullptr,TEXT("/Game/Localization/ST_MMO.ST_MMO")))
        { Error=TEXT("Create, populate and save /Game/Localization/ST_MMO before the WidgetBlueprints."); return false; }
        return true;
    }
    FText Text(const TCHAR* Key)
    {
        const FString* Source = Strings.Find(Key);
        if (!Source) { Error = FString::Printf(TEXT("Missing localization key: %s"), Key); return FText::GetEmpty(); }
        return FText::FromStringTable(TEXT("/Game/Localization/ST_MMO.ST_MMO"),Key);
    }
    template<typename T> T* Widget(const TCHAR* Name)
    {
        auto* Result = Tree->ConstructWidget<T>(T::StaticClass(), FName(Name));
        Result->bIsVariable = true;
        return Result;
    }
    UTextBlock* Label(const TCHAR* Name, const TCHAR* Key, int32 FontSize = 18, bool bMuted = false)
    {
        auto* Result = Widget<UTextBlock>(Name);
        Result->SetText(Text(Key));
        auto Font = Result->GetFont(); Font.Size = FontSize; Result->SetFont(Font);
        Result->SetColorAndOpacity(FSlateColor(bMuted ? Muted : Paper));
        Result->SetAutoWrapText(true);
        return Result;
    }
    void Add(UVerticalBox* Box, UWidget* Child, float Bottom = 12.f)
    {
        auto* Slot = Box->AddChildToVerticalBox(Child);
        Slot->SetPadding(FMargin(0, 0, 0, Bottom)); Slot->SetHorizontalAlignment(HAlign_Fill);
    }
    USizeBox* Height(UWidget* Child, const TCHAR* Name, float Minimum)
    {
        auto* Size = Widget<USizeBox>(Name); Size->SetMinDesiredHeight(Minimum); Size->SetContent(Child); return Size;
    }
    UHorizontalBox* Row(UVerticalBox* Box, const TCHAR* Name)
    {
        auto* Result = Widget<UHorizontalBox>(Name); Add(Box, Result); return Result;
    }
    void Across(UHorizontalBox* Box, UWidget* Child, bool bLast = false)
    {
        auto* Slot = Box->AddChildToHorizontalBox(Child);
        Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Slot->SetPadding(FMargin(0, 0, bLast ? 0 : 10, 0));
        Slot->SetVerticalAlignment(VAlign_Fill);
    }
    UWidget* Button(const TCHAR* Name, const TCHAR* Key, bool bPrimary = false)
    {
        auto* Result = Widget<UButton>(Name);
        if (Blueprint->GetName() == TEXT("WBP_PlayerHUD"))
        {
            // Author the editable Designer setting before any Slate instance is
            // built. HUD clicks remain available, while A/Tab belong to gameplay.
            if (auto* Focusable = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable")))
                Focusable->SetPropertyValue_InContainer(Result, false);
            else Error = TEXT("UE Button.IsFocusable reflection property is unavailable");
        }
        auto Style = Result->GetStyle();
        Style.SetNormal(FSlateRoundedBoxBrush(bPrimary ? Accent : FLinearColor(.12f,.19f,.29f,1), 5.f));
        Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.18f,.42f,.66f,1), 5.f));
        Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.05f,.25f,.48f,1), 5.f));
        Style.NormalPadding = FMargin(14, 8); Style.PressedPadding = FMargin(14, 9, 14, 7);
        Result->SetStyle(Style);
        const FString LabelName = FString(Name) + TEXT("Label");
        auto* TextBlock = Label(*LabelName, Key); TextBlock->SetJustification(ETextJustify::Center); Result->SetContent(TextBlock);
        // Give auto-wrapped labels the available button width when culture changes.
        // Text justification remains centered; narrow HUD buttons can still wrap.
        if (auto* ContentSlot = Cast<UButtonSlot>(TextBlock->Slot)) ContentSlot->SetHorizontalAlignment(HAlign_Fill);
        const FString SizeName = FString(Name) + TEXT("Size");
        return Height(Result, *SizeName, 46.f);
    }
    void Input(UVerticalBox* Box, const TCHAR* Name, const TCHAR* LabelKey, const TCHAR* HintKey, bool bPassword = false)
    {
        const FString LabelName = FString(Name) + TEXT("Label"); Add(Box, Label(*LabelName, LabelKey), 5);
        auto* Input = Widget<UEditableTextBox>(Name); Input->SetHintText(Text(HintKey)); Input->SetIsPassword(bPassword);
        auto Style = Input->GetWidgetStyle(); Style.TextStyle.Font.Size = 18;
        Style.ForegroundColor = FSlateColor(Ink); Style.FocusedForegroundColor = FSlateColor(Ink);
        Style.BackgroundImageNormal = FSlateRoundedBoxBrush(Paper, 4.f);
        Style.BackgroundImageHovered = FSlateRoundedBoxBrush(FLinearColor::White, 4.f);
        Style.BackgroundImageFocused = FSlateRoundedBoxBrush(FLinearColor::White, 4.f);
        Style.BackgroundImageReadOnly = FSlateRoundedBoxBrush(FLinearColor(.68f,.72f,.79f,1), 4.f);
        Style.Padding = FMargin(12, 8); Input->SetWidgetStyle(Style); Input->SetForegroundColor(Ink);
        const FString SizeName = FString(Name) + TEXT("Size"); Add(Box, Height(Input, *SizeName, 44));
    }
    UVerticalBox* Frame(float Width, const TCHAR* BodyName, bool bCentered = true, bool bModal = false)
    {
        auto* Root = Widget<UOverlay>(TEXT("ScreenLayout")); Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible); Tree->RootWidget = Root;
        if (bModal)
        {
            auto* Scrim = Widget<UBorder>(TEXT("ModalScrim")); Scrim->SetBrushColor(FLinearColor(0, 0, 0, .62f));
            Scrim->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* Slot = Root->AddChildToOverlay(Scrim); Slot->SetHorizontalAlignment(HAlign_Fill); Slot->SetVerticalAlignment(VAlign_Fill);
        }
        auto* Size = Widget<USizeBox>(TEXT("PanelWidth")); Size->SetWidthOverride(Width);
        auto* Slot = Root->AddChildToOverlay(Size); Slot->SetPadding(FMargin(24));
        Slot->SetHorizontalAlignment(bCentered ? HAlign_Center : HAlign_Left); Slot->SetVerticalAlignment(bCentered ? VAlign_Center : VAlign_Top);
        auto* Panel = Widget<UBorder>(TEXT("PanelBackground")); Panel->SetBrushColor(Ink); Panel->SetPadding(FMargin(24)); Size->SetContent(Panel);
        auto* Body = Widget<UVerticalBox>(BodyName); Panel->SetContent(Body); return Body;
    }
    void Bar(UVerticalBox* Box, const TCHAR* Name, const FLinearColor& Color)
    {
        auto* Value = Widget<UProgressBar>(Name); Value->SetPercent(1.f); Value->SetFillColorAndOpacity(Color);
        const FString SizeName = FString(Name) + TEXT("Size"); Add(Box, Height(Value, *SizeName, 14));
    }
};

bool BindViewModels(UWidgetBlueprint* Blueprint, const TArray<TPair<FString,FString>>& Bindings, FString& Error)
{
    if (!GEditor) { Error = TEXT("Editor subsystem unavailable"); return false; }
    auto* Editor = GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>();
    auto* View = Editor->RequestView(Blueprint); View->Modify();
    while (View->GetNumBindings()) View->RemoveBindingAt(0);
    TArray<FGuid> Old; for (const auto& Context : View->GetViewModels()) Old.Add(Context.GetViewModelId()); View->RemoveViewModels(Old);
    if (Bindings.IsEmpty()) return true;
    UClass* Model = LoadClass<UObject>(nullptr, TEXT("/Script/MMORPG.MMOPlayerViewModel"));
    if (!Model) { Error = TEXT("Build MMOPlayerViewModel first"); return false; }
    FMVVMBlueprintViewModelContext Context(Model, TEXT("PlayerVM"));
    Context.CreationType = EMVVMBlueprintViewModelContextCreationType::Manual;
    Context.bOptional = true; Context.bCreateSetterFunction = true; View->AddViewModel(Context);
    for (const auto& Pair : Bindings)
    {
        FString WidgetName, PropertyName; Pair.Key.Split(TEXT("."), &WidgetName, &PropertyName);
        UWidget* Widget = Blueprint->WidgetTree->FindWidget(FName(WidgetName));
        FProperty* Source = FindFProperty<FProperty>(Model, FName(Pair.Value));
        FProperty* Destination = Widget ? FindFProperty<FProperty>(Widget->GetClass(), FName(PropertyName)) : nullptr;
        if (!Source || !Destination) { Error = TEXT("Missing MVVM property: ") + Pair.Key + TEXT(" <- ") + Pair.Value; return false; }
        auto& Binding = View->AddDefaultBinding();
        Binding.SourcePath.SetViewModelId(Context.GetViewModelId());
        Binding.SourcePath.SetPropertyPath(Blueprint, UE::MVVM::FMVVMConstFieldVariant(Source));
        Binding.DestinationPath.SetWidgetName(Widget->GetFName());
        Binding.DestinationPath.SetPropertyPath(Blueprint, UE::MVVM::FMVVMConstFieldVariant(Destination));
        Binding.BindingType = EMVVMBindingMode::OneWayToDestination;
        Binding.bOverrideExecutionMode = true; Binding.OverrideExecutionMode = EMVVMExecutionMode::Immediate;
    }
    return true;
}
}

FString UMMODocToolsLibrary::ConfigureDesignerStrings(UStringTable* StringTable)
{
    auto Report=MakeShared<FJsonObject>(); Report->SetBoolField(TEXT("passed"),false); Report->SetBoolField(TEXT("saved"),false);
    auto Encode=[&]() { FString Text; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text)); return Text; };
    TMap<FString,FString> Strings; FString Error;
    if (FPaths::GetCleanFilename(FPaths::GetProjectFilePath())!=TEXT("MMORPG.uproject") || !StringTable
        || StringTable->GetPathName()!=TEXT("/Game/Localization/ST_MMO.ST_MMO") || !ReadDesignerStrings(Strings,Error))
    { Report->SetStringField(TEXT("error"),Error.IsEmpty()?TEXT("Expected the original MMORPG ST_MMO asset"):Error); return Encode(); }
    StringTable->Modify(); auto Table=StringTable->GetMutableStringTable(); Table->SetNamespace(TEXT("MMOUI"));
    for (const auto& Entry:Strings) Table->SetSourceString(Entry.Key,Entry.Value,TEXT("MMORPG Designer fixed UI text; edit the matching reviewed manifest entry."));
    StringTable->MarkPackageDirty(); Report->SetBoolField(TEXT("passed"),true);
    Report->SetStringField(TEXT("asset"),StringTable->GetPathName()); Report->SetStringField(TEXT("namespace"),TEXT("MMOUI"));
    Report->SetStringField(TEXT("nativeCulture"),TEXT("en")); Report->SetNumberField(TEXT("entries"),Strings.Num());
    return Encode();
}

FString UMMODocToolsLibrary::ConfigureDesignerWidget(UWidgetBlueprint* Blueprint)
{
    auto Report = MakeShared<FJsonObject>(); Report->SetStringField(TEXT("date"), FDateTime::UtcNow().ToIso8601());
    Report->SetBoolField(TEXT("passed"), false); Report->SetBoolField(TEXT("saved"), false); Report->SetBoolField(TEXT("gameplayExecuted"), false);
    auto Encode = [&]() { FString Text; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text)); return Text; };
    const TMap<FString,FString> Parents = {
        {TEXT("WBP_MMOLayout"), TEXT("/Script/MMORPG.MMORootLayout")}, {TEXT("WBP_Login"), TEXT("/Script/MMORPG.MMOLoginPanel")},
        {TEXT("WBP_ManaStatus"), TEXT("/Script/UMG.UserWidget")}, {TEXT("WBP_PlayerHUD"), TEXT("/Script/MMORPG.MMOHUDPanel")},
        {TEXT("WBP_Inventory"), TEXT("/Script/MMORPG.MMOInventoryPanel")}, {TEXT("WBP_Settings"), TEXT("/Script/MMORPG.MMOSettingsPanel")},
        {TEXT("WBP_Confirm"), TEXT("/Script/MMORPG.MMOConfirmPanel")}
    };
    const FString Name = Blueprint ? Blueprint->GetName() : FString(); const FString* Parent = Parents.Find(Name);
    if (FPaths::GetCleanFilename(FPaths::GetProjectFilePath()) != TEXT("MMORPG.uproject") || !Parent || !Blueprint->ParentClass
        || Blueprint->ParentClass->GetPathName() != *Parent || Blueprint->GetPathName() != FString::Printf(TEXT("/Game/UI/%s.%s"), *Name, *Name))
    { Report->SetStringField(TEXT("error"), TEXT("Only the agreed MMORPG /Game/UI WidgetBlueprint and native parent may be authored")); return Encode(); }
    FDesigner D(Blueprint); if (!D.ReadStrings()) { Report->SetStringField(TEXT("error"), D.Error); return Encode(); }
    Blueprint->Modify();
    if (Blueprint->WidgetTree) Blueprint->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
    Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, TEXT("WidgetTree"), RF_Transactional); D.Tree = Blueprint->WidgetTree;
    TArray<TPair<FString,FString>> Bindings;
    if (Name == TEXT("WBP_MMOLayout"))
    {
        auto* Root = D.Widget<UOverlay>(TEXT("LayerLayout")); D.Tree->RootWidget = Root;
        Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        for (const TCHAR* Layer : {TEXT("GameLayer"),TEXT("MenuLayer"),TEXT("ModalLayer")})
        {
            auto* Stack = D.Widget<UCommonActivatableWidgetStack>(Layer); Stack->SetTransitionDuration(.16f);
            auto* Slot = Root->AddChildToOverlay(Stack); Slot->SetHorizontalAlignment(HAlign_Fill); Slot->SetVerticalAlignment(VAlign_Fill);
        }
    }
    else if (Name == TEXT("WBP_Login"))
    {
        auto* Form = D.Frame(680, TEXT("Form")); D.Add(Form,D.Label(TEXT("Title"),TEXT("Login.Title"),30),18);
        D.Add(Form,D.Label(TEXT("AccountTitle"),TEXT("Account.Title"),22),12);
        D.Input(Form,TEXT("Username"),TEXT("Account.Username"),TEXT("Account.UsernameHint"));
        D.Input(Form,TEXT("Password"),TEXT("Account.Password"),TEXT("Account.PasswordHint"),true);
        auto* Account = D.Row(Form,TEXT("AccountActions")); D.Across(Account,D.Button(TEXT("LoginButton"),TEXT("Action.SignIn"),true)); D.Across(Account,D.Button(TEXT("RegisterButton"),TEXT("Action.Register")),true);
        D.Add(Form,D.Label(TEXT("CharacterTitle"),TEXT("Character.Title"),22),12);
        D.Input(Form,TEXT("CharacterName"),TEXT("Character.Name"),TEXT("Character.NameHint"));
        D.Add(Form,D.Button(TEXT("CreateButton"),TEXT("Action.CreateCharacter")));
        D.Add(Form,D.Label(TEXT("CharacterListLabel"),TEXT("Character.Select")),5);
        auto* List = D.Widget<UComboBoxString>(TEXT("CharacterList")); List->SetMaxListHeight(240); D.Add(Form,D.Height(List,TEXT("CharacterListSize"),44));
        auto* Actions = D.Row(Form,TEXT("CharacterActions")); D.Across(Actions,D.Button(TEXT("RefreshButton"),TEXT("Action.Refresh"))); D.Across(Actions,D.Button(TEXT("JoinButton"),TEXT("Action.Enter"),true),true);
        D.Add(Form,D.Label(TEXT("Message"),TEXT("Login.Status"),17,true),0);
    }
    else if (Name == TEXT("WBP_ManaStatus"))
    {
        auto* Box = D.Widget<UVerticalBox>(TEXT("ManaLayout")); D.Tree->RootWidget = Box;
        D.Add(Box,D.Label(TEXT("ManaText"),TEXT("Mana.Preview")),6); D.Bar(Box,TEXT("ManaBar"),FLinearColor(.08f,.5f,1.f));
        Bindings={{TEXT("ManaText.Text"),TEXT("ManaLabel")},{TEXT("ManaBar.Percent"),TEXT("ManaFraction")}};
    }
    else if (Name == TEXT("WBP_PlayerHUD"))
    {
        auto* Box = D.Frame(540,TEXT("HUDLayout"),false); D.Add(Box,D.Label(TEXT("Title"),TEXT("HUD.Title"),26));
        D.Add(Box,D.Label(TEXT("Values"),TEXT("HUD.StatusPreview")));
        UClass* ManaClass = LoadClass<UUserWidget>(nullptr,TEXT("/Game/UI/WBP_ManaStatus.WBP_ManaStatus_C"));
        if (!ManaClass) D.Error=TEXT("Create and compile WBP_ManaStatus before WBP_PlayerHUD");
        else { auto* Mana = D.Tree->ConstructWidget<UUserWidget>(ManaClass,TEXT("ManaStatus")); Mana->bIsVariable=true; D.Add(Box,Mana); }
        D.Add(Box,D.Label(TEXT("TargetHeading"),TEXT("Target.Title"),22)); D.Add(Box,D.Label(TEXT("TargetText"),TEXT("Target.Preview")));
        D.Bar(Box,TEXT("TargetHealthBar"),FLinearColor(.9f,.25f,.2f)); D.Add(Box,D.Label(TEXT("CooldownText"),TEXT("Cooldown.Preview"),17,true));
        auto* Abilities = D.Row(Box,TEXT("AbilityActions")); D.Across(Abilities,D.Button(TEXT("TargetButton"),TEXT("Action.Target"))); D.Across(Abilities,D.Button(TEXT("AttackButton"),TEXT("Action.Attack"))); D.Across(Abilities,D.Button(TEXT("SpellButton"),TEXT("Action.Spell"),true),true);
        auto* Menus = D.Row(Box,TEXT("MenuActions")); D.Across(Menus,D.Button(TEXT("InventoryButton"),TEXT("Action.Inventory"))); D.Across(Menus,D.Button(TEXT("SettingsButton"),TEXT("Action.Settings")),true);
        D.Add(Box,D.Label(TEXT("InputHintText"),TEXT("HUD.Controls"),15,true),0);
        Bindings={{TEXT("Values.Text"),TEXT("StatusText")},{TEXT("TargetText.Text"),TEXT("TargetLabel")},{TEXT("TargetHealthBar.Percent"),TEXT("TargetFraction")},{TEXT("CooldownText.Text"),TEXT("CooldownLabel")},{TEXT("InputHintText.Text"),TEXT("InputHint")}};
    }
    else if (Name == TEXT("WBP_Inventory"))
    {
        auto* Box = D.Frame(600,TEXT("InventoryLayout")); D.Add(Box,D.Label(TEXT("Title"),TEXT("Inventory.Title"),28),20);
        D.Add(Box,D.Label(TEXT("Message"),TEXT("Inventory.Empty")),24);
        D.Add(Box,D.Button(TEXT("PotionButton"),TEXT("Action.Potion"),true)); D.Add(Box,D.Button(TEXT("CloseButton"),TEXT("Action.Back")),0);
        Bindings={{TEXT("Message.Text"),TEXT("InventoryLabel")}};
    }
    else if (Name == TEXT("WBP_Settings"))
    {
        auto* Box = D.Frame(600,TEXT("SettingsLayout")); D.Add(Box,D.Label(TEXT("Title"),TEXT("Settings.Title"),28),20);
        D.Add(Box,D.Label(TEXT("QualityHeading"),TEXT("Settings.Quality"),22)); auto* Quality=D.Row(Box,TEXT("QualityActions"));
        D.Across(Quality,D.Button(TEXT("LowQualityButton"),TEXT("Quality.Low"))); D.Across(Quality,D.Button(TEXT("MediumQualityButton"),TEXT("Quality.Medium"),true)); D.Across(Quality,D.Button(TEXT("HighQualityButton"),TEXT("Quality.High")),true);
        D.Add(Box,D.Label(TEXT("LanguageHeading"),TEXT("Settings.Language"),22)); auto* Languages=D.Row(Box,TEXT("LanguageActions"));
        D.Across(Languages,D.Button(TEXT("ChineseButton"),TEXT("Language.Chinese"))); D.Across(Languages,D.Button(TEXT("EnglishButton"),TEXT("Language.English")),true);
        D.Add(Box,D.Button(TEXT("LogoutButton"),TEXT("Action.Logout"))); D.Add(Box,D.Button(TEXT("CloseButton"),TEXT("Action.Back")),0);
    }
    else if (Name == TEXT("WBP_Confirm"))
    {
        auto* Box=D.Frame(600,TEXT("ConfirmLayout"),true,true); D.Add(Box,D.Label(TEXT("Title"),TEXT("Confirm.Title"),28),20);
        D.Add(Box,D.Label(TEXT("Explanation"),TEXT("Confirm.Explanation"),18,true),24);
        D.Add(Box,D.Button(TEXT("ConfirmButton"),TEXT("Confirm.Accept"))); D.Add(Box,D.Button(TEXT("CancelButton"),TEXT("Confirm.Cancel"),true),0);
    }
    if (!D.Error.IsEmpty() || !BindViewModels(Blueprint,Bindings,D.Error)) { Report->SetStringField(TEXT("error"),D.Error); return Encode(); }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FCompilerResultsLog Results; FKismetEditorUtilities::CompileBlueprint(Blueprint,EBlueprintCompileOptions::SkipSave,&Results);
    TArray<UWidget*> Widgets; Blueprint->WidgetTree->GetAllWidgets(Widgets);
    TArray<TSharedPtr<FJsonValue>> WidgetRows,BindingRows;
    int32 CanvasCount=0;
    for (UWidget* Widget:Widgets)
    {
        auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),Widget->GetName()); Row->SetStringField(TEXT("class"),Widget->GetClass()->GetPathName()); Row->SetBoolField(TEXT("isVariable"),Widget->bIsVariable);
        if (const auto* Button=Cast<UButton>(Widget)) Row->SetBoolField(TEXT("isFocusable"),Button->GetIsFocusable());
        WidgetRows.Add(MakeShared<FJsonValueObject>(Row)); if (Widget->GetClass()->GetName()==TEXT("CanvasPanel")) ++CanvasCount;
    }
    for (const auto& Pair:Bindings) BindingRows.Add(MakeShared<FJsonValueString>(TEXT("PlayerVM.")+Pair.Value+TEXT(" -> ")+Pair.Key));
    Report->SetStringField(TEXT("asset"),Blueprint->GetPathName()); Report->SetStringField(TEXT("parent"),*Parent);
    Report->SetStringField(TEXT("layoutOrigin"),TEXT("Saved WidgetBlueprint Designer tree; Editor-only authoring utility"));
    Report->SetStringField(TEXT("localizationNamespace"),TEXT("MMOUI")); Report->SetStringField(TEXT("nativeCulture"),TEXT("en"));
    Report->SetStringField(TEXT("stringTable"),TEXT("/Game/Localization/ST_MMO.ST_MMO"));
    Report->SetArrayField(TEXT("widgets"),WidgetRows); Report->SetArrayField(TEXT("bindings"),BindingRows);
    Report->SetNumberField(TEXT("widgetCount"),Widgets.Num()); Report->SetNumberField(TEXT("canvasCount"),CanvasCount); Report->SetNumberField(TEXT("bindingCount"),Bindings.Num());
    Report->SetStringField(TEXT("bindingMode"),TEXT("Manual PlayerVM / OneWayToDestination / Immediate"));
    if (!Bindings.IsEmpty())
    {
        Report->SetStringField(TEXT("viewModelClass"),TEXT("/Script/MMORPG.MMOPlayerViewModel"));
        Report->SetStringField(TEXT("viewModelName"),TEXT("PlayerVM")); Report->SetStringField(TEXT("creationType"),TEXT("Manual"));
    }
    Report->SetNumberField(TEXT("compilerErrors"),Results.NumErrors); Report->SetNumberField(TEXT("compilerWarnings"),Results.NumWarnings);
    Report->SetBoolField(TEXT("passed"),Results.NumErrors==0&&Blueprint->Status==BS_UpToDate&&CanvasCount==0);
    const FString ReportPath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Evidence")/(Name+TEXT("-designer.json")));
    Report->SetStringField(TEXT("reportPath"),ReportPath); IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath),true); FFileHelper::SaveStringToFile(Encode(),*ReportPath);
    return Encode();
}
