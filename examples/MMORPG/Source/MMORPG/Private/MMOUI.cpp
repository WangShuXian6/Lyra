#include "MMOUI.h"
#include "MMOGameplay.h"
#include "MMOBackendClient.h"
#include "CommonLocalPlayer.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Input/UIActionBindingHandle.h"
#include "TimerManager.h"
#include "Serialization/JsonSerializer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "CommonInputSettings.h"
#include "CommonInputSubsystem.h"
#include "ICommonInputModule.h"
#include "MVVMSubsystem.h"
#include "View/MVVMView.h"
#include "Components/ProgressBar.h"
#include "Kismet/KismetInternationalizationLibrary.h"

#define LOCTEXT_NAMESPACE "MMOUI"

static FGameplayTag Layer(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name)); }
UMMOInputData::UMMOInputData()
{
    EnhancedInputBackAction = CreateDefaultSubobject<UInputAction>(TEXT("UIBackAction"));
    EnhancedInputBackAction->ValueType = EInputActionValueType::Boolean;
}
static FText ErrorText(int32 HTTPCode, const TSharedPtr<FJsonObject>& Json)
{
    const TSharedPtr<FJsonObject>* Error = nullptr; FString Code;
    if (Json && Json->TryGetObjectField(TEXT("error"), Error)) (*Error)->TryGetStringField(TEXT("code"), Code);
    if (Code == TEXT("username_taken")) return LOCTEXT("UsernameTaken", "That account name is already registered.");
    if (Code == TEXT("name_taken")) return LOCTEXT("CharacterNameTaken", "That character name is already in use.");
    if (Code == TEXT("already_online")) return LOCTEXT("AlreadyOnline", "This account already has an active character. Exit that session first.");
    if (Code == TEXT("character_limit")) return LOCTEXT("CharacterLimit", "This account has reached the character limit.");
    if (Code == TEXT("invalid_ticket")) return LOCTEXT("InvalidTicket", "The join ticket expired or was already used. Request a new ticket.");
    if (HTTPCode == 400) return LOCTEXT("InvalidInput", "Check the account name, password (12 or more characters), and character name.");
    if (HTTPCode == 401) return LOCTEXT("Unauthorized", "The account or password is incorrect, or your session expired. Sign in again.");
    if (HTTPCode == 403) return LOCTEXT("Forbidden", "You do not have permission to perform this operation.");
    if (HTTPCode == 404) return LOCTEXT("CharacterUnavailable", "This character or service resource is unavailable.");
    if (HTTPCode == 0 || HTTPCode >= 500) return LOCTEXT("BackendUnavailable", "The backend is unavailable. Check that the service and database are running.");
    return FText::Format(LOCTEXT("BackendRequestFailed", "Request failed (HTTP {0}). Please try again."), FText::AsNumber(HTTPCode));
}
static void AssignViewModel(UUserWidget* Widget, UMMOPlayerViewModel* Model)
{
    if (Widget) if (auto* View = UMVVMSubsystem::GetViewFromUserWidget(Widget); View && View->IsConstructed())
        ensure(View->SetViewModel(TEXT("PlayerVM"), TScriptInterface<INotifyFieldValueChanged>(Model)));
}
void UMMOPlayerViewModel::BindPlayer(AMMOPlayerState* State, AMMOCharacter* Pawn)
{
    if (Player.Get() == State && Character.Get() == Pawn) return;
    Unbind(); Player = State; Character = Pawn;
    if (State)
    {
        HealthHandle = State->ASC->GetGameplayAttributeValueChangeDelegate(UMMOAttributes::GetHealthAttribute()).AddWeakLambda(this, [this](const FOnAttributeChangeData&) { RefreshStatus(); });
        ManaHandle = State->ASC->GetGameplayAttributeValueChangeDelegate(UMMOAttributes::GetManaAttribute()).AddWeakLambda(this, [this](const FOnAttributeChangeData&) { RefreshMana(); });
        ProgressHandle = State->OnProgressChanged.AddUObject(this, &ThisClass::RefreshProgress);
        EffectAddedHandle = State->ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddWeakLambda(this,
            [this](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { RefreshCooldown(); });
        EffectRemovedHandle = State->ASC->OnAnyGameplayEffectRemovedDelegate().AddWeakLambda(this,
            [this](const FActiveGameplayEffect&) { RefreshCooldown(); });
    }
    if (Pawn) TargetHandle = Pawn->OnTargetChanged.AddUObject(this, &ThisClass::BindTarget);
    BindTarget(); Refresh();
}
void UMMOPlayerViewModel::Unbind()
{
    if (auto* State = Player.Get())
    {
        State->ASC->GetGameplayAttributeValueChangeDelegate(UMMOAttributes::GetHealthAttribute()).Remove(HealthHandle);
        State->ASC->GetGameplayAttributeValueChangeDelegate(UMMOAttributes::GetManaAttribute()).Remove(ManaHandle);
        State->ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(EffectAddedHandle);
        State->ASC->OnAnyGameplayEffectRemovedDelegate().Remove(EffectRemovedHandle);
        State->OnProgressChanged.Remove(ProgressHandle); State->GetWorldTimerManager().ClearTimer(CooldownTimer);
    }
    if (auto* Pawn = Character.Get()) Pawn->OnTargetChanged.Remove(TargetHandle);
    if (auto* OldTarget = Target.Get()) OldTarget->OnHealthChanged.Remove(TargetHealthHandle);
    Player.Reset(); Character.Reset(); Target.Reset();
}
void UMMOPlayerViewModel::BeginDestroy() { Unbind(); Super::BeginDestroy(); }
void UMMOPlayerViewModel::BindTarget()
{
    if (auto* OldTarget = Target.Get()) OldTarget->OnHealthChanged.Remove(TargetHealthHandle);
    Target = Character.IsValid() ? Character->SelectedTarget.Get() : nullptr;
    if (auto* NewTarget = Target.Get()) TargetHealthHandle = NewTarget->OnHealthChanged.AddUObject(this, &ThisClass::RefreshTarget);
    RefreshTarget();
}
void UMMOPlayerViewModel::Refresh()
{
    RefreshStatus(); RefreshMana(); RefreshInventory(); RefreshTarget(); RefreshCooldown(); RefreshInputHint();
}
void UMMOPlayerViewModel::SetInputMethod(ECommonInputType Type) { bUsingGamepad = Type == ECommonInputType::Gamepad; RefreshInputHint(); }
void UMMOPlayerViewModel::RefreshInputHint()
{
    UE_MVVM_SET_PROPERTY_VALUE(InputHint, bUsingGamepad
        ? LOCTEXT("GamepadInputHint", "Left stick: move · Right stick: look\nY: target · A: attack · X: arcane bolt\nView: inventory · Menu: settings")
        : LOCTEXT("KeyboardInputHint", "WASD: move · Hold right mouse: look\nTab: target · 1: attack · 2: arcane bolt\nI: inventory · P: settings"));
}
void UMMOPlayerViewModel::RefreshProgress() { RefreshStatus(); RefreshInventory(); }
void UMMOPlayerViewModel::RefreshStatus()
{
    auto* State = Player.Get();
    UE_MVVM_SET_PROPERTY_VALUE(Health, State ? State->Attributes->GetHealth() : 0.f);
    const FText NewStatus = State && State->bSessionReady
        ? (Health <= 0 ? LOCTEXT("PlayerDead", "You died. Respawning at the checkpoint…") : FText::Format(LOCTEXT("PlayerStatusValue", "{0} · Level {1} · XP {2}\nHealth {3} / 100"), FText::AsCultureInvariant(State->GetPlayerName()), FText::AsNumber(State->Level), FText::AsNumber(State->XP), FText::AsNumber(FMath::RoundToInt(Health))))
        : LOCTEXT("WaitingForCharacter", "Waiting for the server to load your character…");
    UE_MVVM_SET_PROPERTY_VALUE(Status, NewStatus);
    UE_MVVM_SET_PROPERTY_VALUE(StatusText, NewStatus);
}
void UMMOPlayerViewModel::RefreshMana()
{
    auto* State = Player.Get();
    UE_MVVM_SET_PROPERTY_VALUE(Mana, State ? State->Attributes->GetMana() : 0.f);
    UE_MVVM_SET_PROPERTY_VALUE(ManaFraction, FMath::Clamp(Mana / 100.f, 0.f, 1.f));
    UE_MVVM_SET_PROPERTY_VALUE(ManaLabel, FText::Format(LOCTEXT("ManaValue", "Mana {0} / 100"), FText::AsNumber(FMath::RoundToInt(Mana))));
}
void UMMOPlayerViewModel::RefreshTarget()
{
    UE_MVVM_SET_PROPERTY_VALUE(TargetHealth, Target.IsValid() ? Target->Health : 0.f);
    UE_MVVM_SET_PROPERTY_VALUE(TargetFraction, FMath::Clamp(TargetHealth / 100.f, 0.f, 1.f));
    UE_MVVM_SET_PROPERTY_VALUE(TargetLabel, Target.IsValid()
        ? FText::Format(LOCTEXT("TargetHealthValue", "Training target · Health {0} / 100"), FText::AsNumber(FMath::RoundToInt(TargetHealth)))
        : LOCTEXT("NoTarget", "No target selected"));
}
void UMMOPlayerViewModel::RefreshCooldown()
{
    auto* State = Player.Get();
    float Remaining = 0;
    if (State)
    {
        FGameplayTagContainer Tags; Tags.AddTag(Layer(TEXT("Cooldown.MMO.Spell")));
        for (float Time : State->ASC->GetActiveEffectsTimeRemaining(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(Tags))) Remaining = FMath::Max(Remaining, Time);
    }
    UE_MVVM_SET_PROPERTY_VALUE(Cooldown, FMath::RoundToFloat(Remaining * 10) / 10);
    FNumberFormattingOptions CooldownFormat; CooldownFormat.MinimumFractionalDigits = 1; CooldownFormat.MaximumFractionalDigits = 1;
    UE_MVVM_SET_PROPERTY_VALUE(CooldownLabel, FText::Format(LOCTEXT("SpellCooldownValue", "Arcane bolt: {0}s cooldown · costs 20 mana"), FText::AsNumber(Cooldown, &CooldownFormat)));
    if (State)
    {
        auto& Timers = State->GetWorldTimerManager();
        if (Remaining <= 0) Timers.ClearTimer(CooldownTimer);
        else if (!Timers.IsTimerActive(CooldownTimer)) Timers.SetTimer(CooldownTimer, this, &ThisClass::RefreshCooldown, .1f, true);
    }
}
void UMMOPlayerViewModel::RefreshInventory()
{
    auto* State = Player.Get();
    TArray<FText> Items;
    if (State) for (const auto& Item : State->Inventory) if (Item.Quantity > 0)
        Items.Add(FText::Format(LOCTEXT("InventoryItemQuantity", "{0} × {1}"), Item.ItemId == TEXT("potion") ? LOCTEXT("ManaPotion", "Mana potion") : FText::AsCultureInvariant(Item.ItemId), FText::AsNumber(Item.Quantity)));
    const FText NewInventory = Items.IsEmpty() ? LOCTEXT("InventoryEmpty", "Your inventory is empty. Defeat a training target to earn a potion.") : FText::Join(FText::FromString(TEXT("\n")), Items);
    UE_MVVM_SET_PROPERTY_VALUE(InventoryText, NewInventory);
    UE_MVVM_SET_PROPERTY_VALUE(InventoryLabel, NewInventory);
}

UMMORootLayout::UMMORootLayout(const FObjectInitializer& Initializer) : Super(Initializer)
{
    LoginClass = FSoftObjectPath(TEXT("/Game/UI/WBP_Login.WBP_Login_C"));
    HUDClass = FSoftObjectPath(TEXT("/Game/UI/WBP_PlayerHUD.WBP_PlayerHUD_C"));
    InventoryClass = FSoftObjectPath(TEXT("/Game/UI/WBP_Inventory.WBP_Inventory_C"));
    SettingsClass = FSoftObjectPath(TEXT("/Game/UI/WBP_Settings.WBP_Settings_C"));
    ConfirmClass = FSoftObjectPath(TEXT("/Game/UI/WBP_Confirm.WBP_Confirm_C"));
}
void UMMORootLayout::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if (ensure(GameLayer && MenuLayer && ModalLayer))
    {
        RegisterLayer(Layer(TEXT("UI.Layer.Game")), GameLayer);
        RegisterLayer(Layer(TEXT("UI.Layer.Menu")), MenuLayer);
        RegisterLayer(Layer(TEXT("UI.Layer.Modal")), ModalLayer);
    }
}
UMMOInventoryPanel* UMMORootLayout::ShowInventory() { return PushWidgetToLayerStack<UMMOInventoryPanel>(Layer(TEXT("UI.Layer.Menu")), InventoryClass.LoadSynchronous()); }
UMMOSettingsPanel* UMMORootLayout::ShowSettings() { return PushWidgetToLayerStack<UMMOSettingsPanel>(Layer(TEXT("UI.Layer.Menu")), SettingsClass.LoadSynchronous()); }
UMMOConfirmPanel* UMMORootLayout::ShowConfirm() { return PushWidgetToLayerStack<UMMOConfirmPanel>(Layer(TEXT("UI.Layer.Modal")), ConfirmClass.LoadSynchronous()); }

void UMMORootLayout::NativeConstruct()
{
    Super::NativeConstruct();
    ViewModel = NewObject<UMMOPlayerViewModel>(this);
    InputPlayer = GetOwningLocalPlayer();
    if (InputPlayer.IsValid()) if (auto* CommonInput = InputPlayer->GetSubsystem<UCommonInputSubsystem>())
    {
        InputMethodHandle = CommonInput->OnInputMethodChangedNative.AddUObject(ViewModel, &UMMOPlayerViewModel::SetInputMethod);
        ViewModel->SetInputMethod(CommonInput->GetCurrentInputType());
    }
    if (InputPlayer.IsValid()) if (auto* EnhancedInput = InputPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
    {
        UIInputContext = NewObject<UInputMappingContext>(this);
        UInputAction* BackAction = ICommonInputModule::GetSettings().GetEnhancedInputBackAction();
        if (BackAction)
        {
            UIInputContext->MapKey(BackAction, EKeys::Escape);
            UIInputContext->MapKey(BackAction, EKeys::Gamepad_FaceButton_Right);
            EnhancedInput->AddMappingContext(UIInputContext, 100);
        }
    }
    if (auto* LP = Cast<UCommonLocalPlayer>(GetOwningLocalPlayer()))
    {
        StateHandle = LP->OnPlayerStateSet.AddWeakLambda(this, [this](UCommonLocalPlayer*, APlayerState*) { RefreshScreens(); });
        PawnHandle = LP->OnPlayerPawnSet.AddWeakLambda(this, [this](UCommonLocalPlayer*, APawn*) { RefreshScreens(); });
    }
    // An actual player-scoped UIExtension contract; HUD registers the matching point.
    if (auto* ExtensionSystem = GetWorld()->GetSubsystem<UUIExtensionSubsystem>())
        StatusExtension = ExtensionSystem->RegisterExtensionAsData(Layer(TEXT("UI.Slot.Status")), GetOwningLocalPlayer(), ViewModel, 0);
    RefreshScreens();
}
void UMMORootLayout::RefreshScreens()
{
    auto* PC = GetOwningPlayer(); if (!PC) return;
    auto* State = PC->GetPlayerState<AMMOPlayerState>();
    if (ObservedState.Get() != State)
    {
        if (auto* Old = ObservedState.Get()) Old->OnProgressChanged.Remove(ProgressHandle);
        ObservedState = State;
        if (State) ProgressHandle = State->OnProgressChanged.AddUObject(this, &ThisClass::RefreshScreens);
    }
    ViewModel->BindPlayer(State, Cast<AMMOCharacter>(PC->GetPawn()));
    if (State && State->bSessionReady)
    {
        if (Login) { FindAndRemoveWidgetFromLayer(Login); Login = nullptr; }
        if (!HUD) HUD = PushWidgetToLayerStack<UMMOHUDPanel>(Layer(TEXT("UI.Layer.Game")), HUDClass.LoadSynchronous());
        HUD->SetViewModel(ViewModel);
    }
    else
    {
        if (HUD)
        {
            for (const TCHAR* Name : { TEXT("UI.Layer.Modal"), TEXT("UI.Layer.Menu"), TEXT("UI.Layer.Game") })
                if (auto* Stack = GetLayerWidget(Layer(Name))) Stack->ClearWidgets();
            HUD = nullptr; Login = nullptr;
        }
        if (!Login) Login = PushWidgetToLayerStack<UMMOLoginPanel>(Layer(TEXT("UI.Layer.Menu")), LoginClass.LoadSynchronous());
    }
}
void UMMORootLayout::NativeDestruct()
{
    if (InputPlayer.IsValid()) if (auto* CommonInput = InputPlayer->GetSubsystem<UCommonInputSubsystem>()) CommonInput->OnInputMethodChangedNative.Remove(InputMethodHandle);
    if (InputPlayer.IsValid()) if (auto* EnhancedInput = InputPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        if (UIInputContext) EnhancedInput->RemoveMappingContext(UIInputContext);
    UIInputContext = nullptr; InputPlayer.Reset();
    if (ViewModel) ViewModel->Unbind();
    StatusExtension.Unregister();
    if (auto* Old = ObservedState.Get()) Old->OnProgressChanged.Remove(ProgressHandle);
    if (auto* LP = Cast<UCommonLocalPlayer>(GetOwningLocalPlayer())) { LP->OnPlayerStateSet.Remove(StateHandle); LP->OnPlayerPawnSet.Remove(PawnHandle); }
    // CommonGame may reuse this root when a new PlayerController is assigned.
    // Remove gameplay, menu and modal widgets before its next NativeConstruct.
    for (const TCHAR* Name : { TEXT("UI.Layer.Modal"), TEXT("UI.Layer.Menu"), TEXT("UI.Layer.Game") })
        if (auto* Stack = GetLayerWidget(Layer(Name))) Stack->ClearWidgets();
    HUD = nullptr; Login = nullptr; ViewModel = nullptr; ObservedState.Reset();
    Super::NativeDestruct();
}

UMMOPanel::UMMOPanel(const FObjectInitializer& Initializer) : Super(Initializer)
{
    bIsBackHandler = true; bAutoRestoreFocus = true;
}
TOptional<FUIInputConfig> UMMOPanel::GetDesiredInputConfig() const { return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false); }
UWidget* UMMOPanel::NativeGetDesiredFocusTarget() const { return FirstFocus; }
FReply UMMOPanel::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
    if (bIsBackHandler && (KeyEvent.GetKey() == EKeys::Escape || KeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Right))
    { DeactivateWidget(); return FReply::Handled(); }
    return Super::NativeOnKeyDown(Geometry, KeyEvent);
}
void UMMOLoginPanel::NativeConstruct()
{
    bIsBackHandler = false; FirstFocus = Username; Super::NativeConstruct();
    if (auto* GI = GetGameInstance<UMMOGameInstance>(); GI && !GI->PendingLoginMessage.IsEmpty())
    { SetBusy(false, GI->PendingLoginMessage); GI->PendingLoginMessage = FText::GetEmpty(); }
    LoginButton->OnClicked.AddUniqueDynamic(this, &ThisClass::LoginClicked);
    RegisterButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RegisterClicked);
    RefreshButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RefreshClicked);
    CreateButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CreateClicked);
    JoinButton->OnClicked.AddUniqueDynamic(this, &ThisClass::JoinClicked);
}
void UMMOLoginPanel::NativeDestruct()
{
    ++RequestGeneration; bRequestPending = false;
    LoginButton->OnClicked.RemoveDynamic(this, &ThisClass::LoginClicked);
    RegisterButton->OnClicked.RemoveDynamic(this, &ThisClass::RegisterClicked);
    RefreshButton->OnClicked.RemoveDynamic(this, &ThisClass::RefreshClicked);
    CreateButton->OnClicked.RemoveDynamic(this, &ThisClass::CreateClicked);
    JoinButton->OnClicked.RemoveDynamic(this, &ThisClass::JoinClicked);
    Password->SetText(FText::GetEmpty()); Super::NativeDestruct();
}

void UMMOLoginPanel::SetBusy(bool bBusy, const FText& Status) { bRequestPending = bBusy; if (Message) Message->SetText(Status); }
void UMMOLoginPanel::LoginClicked() { Authenticate(false); }
void UMMOLoginPanel::RegisterClicked() { Authenticate(true); }
void UMMOLoginPanel::Authenticate(bool bRegister)
{
    if (bRequestPending) return;
    auto* Backend = GetGameInstance()->GetSubsystem<UMMOBackendClient>(); auto Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("username"), Username->GetText().ToString()); Body->SetStringField(TEXT("password"), Password->GetText().ToString());
    SetBusy(true, bRegister ? LOCTEXT("Registering", "Creating account…") : LOCTEXT("SigningIn", "Signing in…"));
    Backend->Request(TEXT("POST"), bRegister ? TEXT("/v1/auth/register") : TEXT("/v1/auth/login"), Body,
        [WeakThis = TWeakObjectPtr<UMMOLoginPanel>(this), Generation = RequestGeneration, bRegister](bool OK, int32 Code, TSharedPtr<FJsonObject> Json)
    {
        if (auto* Self = WeakThis.Get(); Self && Self->RequestGeneration == Generation)
        {
            Self->SetBusy(false, OK ? (bRegister ? LOCTEXT("AccountCreated", "Account created. Please sign in.") : LOCTEXT("SignedIn", "Signed in.")) : ErrorText(Code, Json));
            if (OK && !bRegister) { Self->GetGameInstance()->GetSubsystem<UMMOBackendClient>()->SetSessionToken(Json->GetStringField(TEXT("token"))); Self->Password->SetText(FText::GetEmpty()); Self->RefreshClicked(); }
        }
    });
}
void UMMOLoginPanel::RefreshClicked()
{
    if (bRequestPending) return; SetBusy(true, LOCTEXT("LoadingCharacters", "Loading characters…"));
    GetGameInstance()->GetSubsystem<UMMOBackendClient>()->Request(TEXT("GET"), TEXT("/v1/characters"), nullptr, [WeakThis = TWeakObjectPtr<UMMOLoginPanel>(this), Generation = RequestGeneration](bool OK, int32 Code, TSharedPtr<FJsonObject> Json)
    {
        if (auto* Self = WeakThis.Get(); Self && Self->RequestGeneration == Generation)
        {
            Self->SetBusy(false, OK ? LOCTEXT("ChooseCharacter", "Choose a character to enter the zone.") : ErrorText(Code, Json)); if (!OK) return;
            Self->CharacterIDs.Empty(); Self->CharacterList->ClearOptions();
            for (const auto& Value : Json->GetArrayField(TEXT("characters")))
            { auto Character = Value->AsObject(); Self->CharacterIDs.Add(Character->GetStringField(TEXT("id"))); Self->CharacterList->AddOption(Character->GetStringField(TEXT("name"))); }
            if (Self->CharacterIDs.Num()) Self->CharacterList->SetSelectedIndex(0);
        }
    });
}
void UMMOLoginPanel::CreateClicked()
{
    if (bRequestPending) return; SetBusy(true, LOCTEXT("CreatingCharacter", "Creating character…")); auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("name"), CharacterName->GetText().ToString());
    GetGameInstance()->GetSubsystem<UMMOBackendClient>()->Request(TEXT("POST"), TEXT("/v1/characters"), Body, [WeakThis = TWeakObjectPtr<UMMOLoginPanel>(this), Generation = RequestGeneration](bool OK, int32 Code, TSharedPtr<FJsonObject> Json)
    { if (auto* Self = WeakThis.Get(); Self && Self->RequestGeneration == Generation) { Self->SetBusy(false, OK ? LOCTEXT("CharacterCreated", "Character created.") : ErrorText(Code, Json)); if (OK) Self->RefreshClicked(); } });
}
void UMMOLoginPanel::JoinClicked()
{
    const int32 Index = CharacterList->GetSelectedIndex(); if (bRequestPending || !CharacterIDs.IsValidIndex(Index)) return;
    SetBusy(true, LOCTEXT("RequestingTicket", "Requesting a one-time join ticket…")); auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("characterId"), CharacterIDs[Index]); Body->SetStringField(TEXT("serverId"), TEXT("local-1"));
    GetGameInstance()->GetSubsystem<UMMOBackendClient>()->Request(TEXT("POST"), TEXT("/v1/join-ticket"), Body, [WeakThis = TWeakObjectPtr<UMMOLoginPanel>(this), Generation = RequestGeneration](bool OK, int32 Code, TSharedPtr<FJsonObject> Json)
    {
        if (auto* Self = WeakThis.Get(); Self && Self->RequestGeneration == Generation)
        {
            Self->SetBusy(false, OK ? LOCTEXT("ConnectingServer", "Connecting to the game server…") : ErrorText(Code, Json));
            if (OK) Self->GetOwningPlayer()->ClientTravel(Json->GetStringField(TEXT("address")) + TEXT("?Ticket=") + Json->GetStringField(TEXT("ticket")), TRAVEL_Absolute);
        }
    });
}

TOptional<FUIInputConfig> UMMOHUDPanel::GetDesiredInputConfig() const { return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::NoCapture, false); }
void UMMOHUDPanel::NativeConstruct()
{
    bIsBackHandler = false; bAutoRestoreFocus = false; FirstFocus = nullptr; Super::NativeConstruct();
    TargetButton->OnClicked.AddUniqueDynamic(this, &ThisClass::TargetClicked);
    AttackButton->OnClicked.AddUniqueDynamic(this, &ThisClass::AttackClicked);
    SpellButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SpellClicked);
    InventoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::InventoryClicked);
    SettingsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SettingsClicked);
    if (auto* ExtensionSystem = GetWorld()->GetSubsystem<UUIExtensionSubsystem>())
        ExtensionPoint = ExtensionSystem->RegisterExtensionPointForContext(Layer(TEXT("UI.Slot.Status")), GetOwningLocalPlayer(), EUIExtensionPointMatch::ExactMatch,
        {UMMOPlayerViewModel::StaticClass()}, FExtendExtensionPointDelegate::CreateWeakLambda(this, [this](EUIExtensionAction Action, const FUIExtensionRequest& Request)
        { SetViewModel(Action == EUIExtensionAction::Added ? Cast<UMMOPlayerViewModel>(Request.Data) : nullptr); }));
}
void UMMOHUDPanel::SetViewModel(UMMOPlayerViewModel* Model)
{
    VM = Model; AssignViewModel(this, VM); AssignViewModel(ManaStatus, VM);
}
bool UMMOHUDPanel::ValidateManaBinding() const
{
    if (!ManaStatus || !VM) return false;
    const auto* Label = Cast<UTextBlock>(ManaStatus->GetWidgetFromName(TEXT("ManaText")));
    const auto* Bar = Cast<UProgressBar>(ManaStatus->GetWidgetFromName(TEXT("ManaBar")));
    return Label && Bar && Values && Values->GetText().EqualTo(VM->StatusText)
        && Label->GetText().EqualTo(VM->ManaLabel) && FMath::IsNearlyEqual(Bar->GetPercent(), VM->ManaFraction);
}
void UMMOHUDPanel::NativeDestruct()
{
    ExtensionPoint.Unregister(); SetViewModel(nullptr);
    TargetButton->OnClicked.RemoveDynamic(this, &ThisClass::TargetClicked);
    AttackButton->OnClicked.RemoveDynamic(this, &ThisClass::AttackClicked);
    SpellButton->OnClicked.RemoveDynamic(this, &ThisClass::SpellClicked);
    InventoryButton->OnClicked.RemoveDynamic(this, &ThisClass::InventoryClicked);
    SettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::SettingsClicked);
    Super::NativeDestruct();
}
void UMMOHUDPanel::TargetClicked() { if (auto* Pawn = Cast<AMMOCharacter>(GetOwningPlayerPawn())) Pawn->SelectTarget(); }
void UMMOHUDPanel::AttackClicked() { if (auto* Pawn = Cast<AMMOCharacter>(GetOwningPlayerPawn())) Pawn->Attack(); }
void UMMOHUDPanel::SpellClicked() { if (auto* Pawn = Cast<AMMOCharacter>(GetOwningPlayerPawn())) Pawn->Spell(); }
void UMMOHUDPanel::InventoryClicked() { if (auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(GetOwningPlayer()))) Root->ShowInventory(); }
void UMMOHUDPanel::SettingsClicked() { if (auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(GetOwningPlayer()))) Root->ShowSettings(); }
void UMMOInventoryPanel::NativeConstruct()
{
    FirstFocus = PotionButton; Super::NativeConstruct();
    PotionButton->OnClicked.AddUniqueDynamic(this, &ThisClass::PotionClicked); CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseClicked);
    if (auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(GetOwningPlayer()))) VM = Root->ViewModel;
    AssignViewModel(this, VM);
}
void UMMOInventoryPanel::NativeDestruct()
{
    AssignViewModel(this, nullptr); VM = nullptr;
    PotionButton->OnClicked.RemoveDynamic(this, &ThisClass::PotionClicked); CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::CloseClicked);
    Super::NativeDestruct();
}
void UMMOInventoryPanel::PotionClicked() { if (auto* Pawn = Cast<AMMOCharacter>(GetOwningPlayerPawn())) Pawn->ServerUsePotion(); }
void UMMOInventoryPanel::CloseClicked() { DeactivateWidget(); }
void UMMOSettingsPanel::NativeConstruct()
{
    FirstFocus = MediumQualityButton; Super::NativeConstruct();
    LowQualityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::LowQualityClicked);
    MediumQualityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::MediumQualityClicked);
    HighQualityButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HighQualityClicked);
    LogoutButton->OnClicked.AddUniqueDynamic(this, &ThisClass::LogoutClicked);
    CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseClicked);
    ChineseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ChineseClicked);
    EnglishButton->OnClicked.AddUniqueDynamic(this, &ThisClass::EnglishClicked);
}
void UMMOSettingsPanel::NativeDestruct()
{
    LowQualityButton->OnClicked.RemoveDynamic(this, &ThisClass::LowQualityClicked);
    MediumQualityButton->OnClicked.RemoveDynamic(this, &ThisClass::MediumQualityClicked);
    HighQualityButton->OnClicked.RemoveDynamic(this, &ThisClass::HighQualityClicked);
    LogoutButton->OnClicked.RemoveDynamic(this, &ThisClass::LogoutClicked);
    CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::CloseClicked);
    ChineseButton->OnClicked.RemoveDynamic(this, &ThisClass::ChineseClicked);
    EnglishButton->OnClicked.RemoveDynamic(this, &ThisClass::EnglishClicked);
    Super::NativeDestruct();
}
void UMMOSettingsPanel::ChangeQuality(int32 Level)
{
    if (auto* Settings = UGameUserSettings::GetGameUserSettings())
    {
        Settings->SetOverallScalabilityLevel(Level);
        // Quality controls do not own window size or fullscreen mode.
        Settings->ApplyNonResolutionSettings();
        if (!GIsEditor) Settings->SaveSettings();
    }
}
void UMMOSettingsPanel::LowQualityClicked() { ChangeQuality(0); }
void UMMOSettingsPanel::MediumQualityClicked() { ChangeQuality(1); }
void UMMOSettingsPanel::HighQualityClicked() { ChangeQuality(2); }
void UMMOSettingsPanel::ChangeCulture(const FString& Culture)
{
    if (UKismetInternationalizationLibrary::SetCurrentCulture(Culture, true))
        if (auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(GetOwningPlayer())))
            if (Root->ViewModel) Root->ViewModel->Refresh();
}
void UMMOSettingsPanel::ChineseClicked() { ChangeCulture(TEXT("zh-Hans")); }
void UMMOSettingsPanel::EnglishClicked() { ChangeCulture(TEXT("en")); }
void UMMOSettingsPanel::LogoutClicked() { if (auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(GetOwningPlayer()))) Root->ShowConfirm(); }
void UMMOSettingsPanel::CloseClicked() { DeactivateWidget(); }
void UMMOConfirmPanel::NativeConstruct()
{
    bIsModal = true; FirstFocus = CancelButton; Super::NativeConstruct();
    ConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ConfirmClicked); CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CancelClicked);
}
void UMMOConfirmPanel::NativeDestruct()
{
    ConfirmButton->OnClicked.RemoveDynamic(this, &ThisClass::ConfirmClicked); CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::CancelClicked);
    Super::NativeDestruct();
}

void UMMOConfirmPanel::ConfirmClicked()
{
    auto* Backend = GetGameInstance()->GetSubsystem<UMMOBackendClient>();
    Backend->Request(TEXT("POST"), TEXT("/v1/auth/logout"), MakeShared<FJsonObject>(), [](bool, int32, TSharedPtr<FJsonObject>) {});
    Backend->SetSessionToken(TEXT("")); UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")));
}
void UMMOConfirmPanel::CancelClicked() { DeactivateWidget(); }


#undef LOCTEXT_NAMESPACE
