#include "MMOGameplay.h"
#include "MMOAbilitySystemComponent.h"
#include "MMOUI.h"
#include "MMOHealthComponent.h"
#include "MMOPawnExtensionComponent.h"
#include "MMOExperienceManagerComponent.h"
#include "MMOBackendClient.h"
#if WITH_SERVER_CODE
#include "MMOBackendServer.h"
#endif
#include "Net/UnrealNetwork.h"
#include "NativeGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ModularGameState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "GameFeaturesSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "ImageUtils.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SpellCooldown, "Cooldown.MMO.Spell");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AttackCooldown, "Cooldown.MMO.Attack");

UMMOAttributes::UMMOAttributes() { InitHealth(100); InitMana(100); }
void UMMOAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UMMOAttributes, Health, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UMMOAttributes, Mana, COND_None, REPNOTIFY_Always);
}
void UMMOAttributes::PreAttributeChange(const FGameplayAttribute&, float& Value) { Value = FMath::Clamp(Value, 0.f, 100.f); }
void UMMOAttributes::OnRep_Health(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMMOAttributes, Health, Old); }
void UMMOAttributes::OnRep_Mana(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UMMOAttributes, Mana, Old); }

AMMOPlayerState::AMMOPlayerState(const FObjectInitializer& Initializer) : Super(Initializer)
{
    ASC = CreateDefaultSubobject<UMMOAbilitySystemComponent>(TEXT("ASC")); ASC->SetIsReplicated(true);
    ASC->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    Attributes = CreateDefaultSubobject<UMMOAttributes>(TEXT("Attributes")); SetNetUpdateFrequency(30);
}
void AMMOPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AMMOPlayerState, Inventory, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AMMOPlayerState, XP, COND_OwnerOnly);
    DOREPLIFETIME(AMMOPlayerState, Level); DOREPLIFETIME(AMMOPlayerState, bSessionReady);
}
void AMMOPlayerState::OnRep_Progress() { OnProgressChanged.Broadcast(); }
void AMMOPlayerState::LoadSnapshot(const TSharedPtr<FJsonObject>& Character)
{
    check(HasAuthority());
    CharacterID = Character->GetStringField(TEXT("id")); SetPlayerName(Character->GetStringField(TEXT("name")));
    const auto State = Character->GetObjectField(TEXT("state"));
    Level = State->GetIntegerField(TEXT("level")); XP = State->GetIntegerField(TEXT("xp"));
    const float SavedHealth = FMath::Clamp(float(State->GetNumberField(TEXT("health"))), 0.f, 100.f);
    ASC->SetNumericAttributeBase(UMMOAttributes::GetHealthAttribute(), SavedHealth > 0 ? SavedHealth : 100.f);
    ASC->SetNumericAttributeBase(UMMOAttributes::GetManaAttribute(), FMath::Clamp(float(State->GetNumberField(TEXT("mana"))), 0.f, 100.f));
    const auto Position = State->GetObjectField(TEXT("position"));
    SavedPosition = FVector(Position->GetNumberField(TEXT("x")), Position->GetNumberField(TEXT("y")), Position->GetNumberField(TEXT("z")));
    if (SavedHealth <= 0) SavedPosition = FVector(0, 0, 150);
    Inventory.Reset();
    for (const auto& Value : State->GetArrayField(TEXT("inventory")))
    { FMMOItem Item; Item.ItemId = Value->AsObject()->GetStringField(TEXT("itemId")); Item.Quantity = Value->AsObject()->GetIntegerField(TEXT("quantity")); Inventory.Add(Item); }
    bCharacterLoaded = true; OnRep_Progress(); ForceNetUpdate();
}
TSharedPtr<FJsonObject> AMMOPlayerState::MakeSnapshot() const
{
    auto State = MakeShared<FJsonObject>(); State->SetNumberField(TEXT("level"), Level); State->SetNumberField(TEXT("xp"), XP);
    State->SetNumberField(TEXT("health"), Attributes->GetHealth()); State->SetNumberField(TEXT("mana"), Attributes->GetMana());
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FMMOItem& Item : Inventory) { if (Item.Quantity <= 0) continue; auto Object = MakeShared<FJsonObject>(); Object->SetStringField(TEXT("itemId"), Item.ItemId); Object->SetNumberField(TEXT("quantity"), Item.Quantity); Items.Add(MakeShared<FJsonValueObject>(Object)); }
    State->SetArrayField(TEXT("inventory"), Items);
    const FVector Location = GetPawn() ? GetPawn()->GetActorLocation() : SavedPosition;
    auto Position = MakeShared<FJsonObject>(); Position->SetNumberField(TEXT("x"), Location.X); Position->SetNumberField(TEXT("y"), Location.Y); Position->SetNumberField(TEXT("z"), FMath::Max(Location.Z, 100.));
    State->SetObjectField(TEXT("position"), Position); return State;
}
void AMMOPlayerState::Reward()
{
    if (!HasAuthority()) return;
    XP += 25; Level = 1 + XP / 100;
    auto* Item = Inventory.FindByPredicate([](const FMMOItem& I) { return I.ItemId == TEXT("potion"); });
    if (Item) Item->Quantity++; else { FMMOItem NewItem; NewItem.ItemId = TEXT("potion"); NewItem.Quantity = 1; Inventory.Add(NewItem); }
    OnRep_Progress(); ForceNetUpdate();
}

AMMOTarget::AMMOTarget()
{
    bReplicates = true; Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Target")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Mesh->SetStaticMesh(Shape.Object); Mesh->SetRelativeScale3D(FVector(.7, .7, 1.7)); Mesh->SetCollisionProfileName(TEXT("BlockAll"));
}
void AMMOTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMMOTarget, Health); }
void AMMOTarget::OnRep_Health() { OnHealthChanged.Broadcast(); }
void AMMOTarget::Hit(float Damage, AMMOPlayerState* Attacker)
{
    if (!HasAuthority() || Health <= 0) return;
    Health = FMath::Max(0.f, Health - Damage); OnRep_Health(); ForceNetUpdate();
    if (Health == 0)
    {
        Attacker->Reward();
        GetWorldTimerManager().SetTimer(RespawnTimer, FTimerDelegate::CreateWeakLambda(this, [this] { Health = 100; OnRep_Health(); ForceNetUpdate(); }), 5.f, false);
    }
    else
    {
        // Resolve after the attacking ability finishes; death may cancel other active abilities.
        GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
            [this, Player = TWeakObjectPtr<AMMOPlayerState>(Attacker)]
        {
            if (!Player.IsValid() || !Player->bSessionReady || Player->Attributes->GetHealth() <= 0 || Health <= 0) return;
            auto Context = Player->ASC->MakeEffectContext(); Context.AddInstigator(this, this);
            const auto Spec = Player->ASC->MakeOutgoingSpec(UMMOTargetRetaliation::StaticClass(), 1.f, Context);
            if (Spec.IsValid()) Player->ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
        }));
    }
}
UMMOSpellCost::UMMOSpellCost()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo Modifier; Modifier.Attribute = UMMOAttributes::GetManaAttribute(); Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FScalableFloat(-20.f); Modifiers.Add(Modifier);
}
UMMOSpellCooldown::UMMOSpellCooldown()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration; DurationMagnitude = FScalableFloat(3.f);
    FInheritedTagContainer Tags; Tags.AddTag(TAG_SpellCooldown);
    auto* TagComponent = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags")); GEComponents.Add(TagComponent); TagComponent->SetAndApplyTargetTagChanges(Tags);
}
UMMOAttackCooldown::UMMOAttackCooldown()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration; DurationMagnitude = FScalableFloat(.65f);
    FInheritedTagContainer Tags; Tags.AddTag(TAG_AttackCooldown);
    auto* TagComponent = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags")); GEComponents.Add(TagComponent); TagComponent->SetAndApplyTargetTagChanges(Tags);
}
UMMOAttackAbility::UMMOAttackAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    CooldownGameplayEffectClass = UMMOAttackCooldown::StaticClass();
    ActivationBlockedTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Status.MMO.Dead")));
}
UMMOSpellAbility::UMMOSpellAbility()
{
    Damage = 40; Range = 1200;
    CostGameplayEffectClass = UMMOSpellCost::StaticClass(); CooldownGameplayEffectClass = UMMOSpellCooldown::StaticClass();
}
void UMMOAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info, const FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
    auto* Pawn = Cast<AMMOCharacter>(Info->AvatarActor.Get());
    auto* State = Cast<AMMOPlayerState>(Info->OwnerActor.Get());
    const bool bTargetValid = Pawn && State && State->bSessionReady && State->Attributes->GetHealth() > 0
        && Pawn->HealthComponent && !Pawn->HealthComponent->IsDead() && Pawn->PawnExtension->IsPawnReady()
        && State->ASC == Info->AbilitySystemComponent.Get() && State->ASC->GetAvatarActor() == Pawn
        && IsValid(Pawn->SelectedTarget) && Pawn->SelectedTarget->Health > 0
        && FVector::DistSquared(Pawn->GetActorLocation(), Pawn->SelectedTarget->GetActorLocation()) <= FMath::Square(Range);
    if (bTargetValid)
    {
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(MMOLineOfSight), false, Pawn);
        Pawn->GetWorld()->LineTraceSingleByChannel(Hit, Pawn->GetActorLocation(), Pawn->SelectedTarget->GetActorLocation(), ECC_Visibility, Params);
        if ((!Hit.bBlockingHit || Hit.GetActor() == Pawn->SelectedTarget) && CommitAbility(Handle, Info, Activation))
        { Pawn->SelectedTarget->Hit(Damage, State); Pawn->MulticastAttackCue(Pawn->SelectedTarget->GetActorLocation(), Damage > 20); }
    }
    EndAbility(Handle, Info, Activation, true, false);
}

AMMOCharacter::AMMOCharacter(const FObjectInitializer& Initializer) : Super(Initializer)
{
    bReplicates = true; bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true; GetCharacterMovement()->MaxWalkSpeed = 500;
    PawnExtension = CreateDefaultSubobject<UMMOPawnExtensionComponent>(TEXT("PawnExtension"));
    HealthComponent = CreateDefaultSubobject<UMMOHealthComponent>(TEXT("HealthComponent"));
    auto* Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm")); Arm->SetupAttachment(GetRootComponent());
    Arm->TargetArmLength = 650; Arm->bUsePawnControlRotation = true;
    auto* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(Arm);
}
UAbilitySystemComponent* AMMOCharacter::GetAbilitySystemComponent() const { return PawnExtension ? PawnExtension->GetAbilitySystemComponent() : nullptr; }
void AMMOCharacter::InitializeAbilitySystem()
{
    if (auto* PS = GetPlayerState<AMMOPlayerState>())
    {
        PawnExtension->InitializeAbilitySystem(PS->ASC, PS);
    }
    else UninitializeAbilitySystem();
}
void AMMOCharacter::PossessedBy(AController* NewController) { Super::PossessedBy(NewController); InitializeAbilitySystem(); PawnExtension->HandleControllerChanged(); }
void AMMOCharacter::UnPossessed() { UninitializeAbilitySystem(); Super::UnPossessed(); }
void AMMOCharacter::OnRep_PlayerState() { Super::OnRep_PlayerState(); InitializeAbilitySystem(); PawnExtension->HandlePlayerStateReplicated(); }
void AMMOCharacter::OnRep_Controller() { Super::OnRep_Controller(); PawnExtension->HandleControllerChanged(); }
void AMMOCharacter::UninitializeAbilitySystem()
{
    PawnExtension->UninitializeAbilitySystem();
    if (auto* LocalPlayer = InputOwner.Get())
        if (auto* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Subsystem->RemoveMappingContext(InputContext);
    InputOwner.Reset();
}
void AMMOCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    UninitializeAbilitySystem();
    Super::EndPlay(Reason);
}
void AMMOCharacter::Move(const FInputActionValue& Value)
{
    if (!CanProcessGameplayInput()) return;
    const FVector2D Axis = Value.Get<FVector2D>(); const FRotator Yaw(0, GetControlRotation().Yaw, 0);
    AddMovementInput(Yaw.Vector(), Axis.Y); AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}
void AMMOCharacter::Look(const FInputActionValue& Value)
{
    if (!CanProcessGameplayInput()) return;
    // UI keeps the cursor available. Hold right mouse to rotate; controller stick is also supported.
    const auto* PC = Cast<APlayerController>(Controller);
    if (PC && (PC->IsInputKeyDown(EKeys::RightMouseButton) || PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX) != 0 || PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY) != 0))
    { auto Axis = Value.Get<FVector2D>(); AddControllerYawInput(Axis.X); AddControllerPitchInput(-Axis.Y); }
}
void AMMOCharacter::SelectTarget()
{
    if (!CanProcessGameplayInput()) return;
    AMMOTarget* Best = nullptr; float Distance = FMath::Square(1500.f);
    for (TActorIterator<AMMOTarget> It(GetWorld()); It; ++It)
    { float D = FVector::DistSquared(GetActorLocation(), It->GetActorLocation()); if (It->Health > 0 && D < Distance) { Best = *It; Distance = D; } }
    ServerSelectTarget(Best);
}
void AMMOCharacter::ServerSelectTarget_Implementation(AMMOTarget* Target)
{
    if (Target && (!IsValid(Target) || FVector::DistSquared(GetActorLocation(), Target->GetActorLocation()) > FMath::Square(1500.f))) return;
    SelectedTarget = Target; OnRep_Target();
}
void AMMOCharacter::OnRep_Target() { OnTargetChanged.Broadcast(); }
bool AMMOCharacter::CanProcessGameplayInput() const
{
    if (!PawnExtension->IsPawnReady() || HealthComponent->IsDead()) return false;
    auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(Cast<APlayerController>(Controller)));
    if (!Root) return false;
    for (const TCHAR* Tag : {TEXT("UI.Layer.Menu"), TEXT("UI.Layer.Modal")})
        if (auto* Stack = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(FName(Tag))); Stack && Stack->GetActiveWidget()) return false;
    return true;
}
void AMMOCharacter::Attack() { if (CanProcessGameplayInput()) ServerActivate(FGameplayTag::RequestGameplayTag(TEXT("InputTag.MMO.Attack"))); }
void AMMOCharacter::Spell() { if (CanProcessGameplayInput()) ServerActivate(FGameplayTag::RequestGameplayTag(TEXT("InputTag.MMO.Spell"))); }
void AMMOCharacter::OpenInventory()
{
    if (auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(Cast<APlayerController>(Controller))))
    {
        for (const TCHAR* Name : {TEXT("UI.Layer.Menu"), TEXT("UI.Layer.Modal")})
            if (auto* Stack = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(FName(Name))); Stack && Stack->GetActiveWidget()) return;
        Root->ShowInventory();
    }
}
void AMMOCharacter::OpenSettings()
{
    if (auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(Cast<APlayerController>(Controller))))
    {
        for (const TCHAR* Name : {TEXT("UI.Layer.Menu"), TEXT("UI.Layer.Modal")})
            if (auto* Stack = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(FName(Name))); Stack && Stack->GetActiveWidget()) return;
        Root->ShowSettings();
    }
}
void AMMOCharacter::ServerActivate_Implementation(FGameplayTag InputTag)
{
    const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.MMO.Attack"));
    const FGameplayTag SpellTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.MMO.Spell"));
    if (InputTag != AttackTag && InputTag != SpellTag) return;
    auto* PS = GetPlayerState<AMMOPlayerState>();
    auto* TaggedASC = PS ? Cast<UMMOAbilitySystemComponent>(PS->ASC.Get()) : nullptr;
    if (!HasAuthority() || !PS || !PS->bSessionReady || !TaggedASC || !PawnExtension->IsPawnReady()
        || HealthComponent->IsDead() || PS->Attributes->GetHealth() <= 0 || PS->GetPawn() != this
        || TaggedASC->GetOwnerActor() != PS || TaggedASC->GetAvatarActor() != this
        || PawnExtension->GetAbilitySystemComponent() != TaggedASC) return;
#if !UE_BUILD_SHIPPING
    const int32 MatchingSpecs = TaggedASC->GetGrantedAbilitiesForInputTag(InputTag).Num();
#endif
    [[maybe_unused]] const bool bActivated = TaggedASC->TryActivateAbilitiesByInputTag(InputTag);
#if !UE_BUILD_SHIPPING
    UE_LOG(LogTemp, Display, TEXT("MMO_INPUT_DISPATCH tag=%s activated=%d specCount=%d authority=%d"),
        *InputTag.ToString(), bActivated, MatchingSpecs, TaggedASC->IsOwnerActorAuthoritative());
#endif
}
void AMMOCharacter::ServerUsePotion_Implementation()
{
    auto* PS = GetPlayerState<AMMOPlayerState>(); if (!PS || !PS->bSessionReady || HealthComponent->IsDead() || PS->Attributes->GetHealth() <= 0 || PS->Attributes->GetMana() >= 100) return;
    for (auto& Item : PS->Inventory) if (Item.ItemId == TEXT("potion") && Item.Quantity > 0)
    { --Item.Quantity; PS->ASC->SetNumericAttributeBase(UMMOAttributes::GetManaAttribute(), FMath::Min(100.f, PS->Attributes->GetMana() + 40)); PS->OnRep_Progress(); PS->ForceNetUpdate(); break; }
}
void AMMOCharacter::MulticastAttackCue_Implementation(FVector End, bool bSpell)
{
    if (GetNetMode() == NM_DedicatedServer) return;
    DrawDebugLine(GetWorld(), GetActorLocation(), End, bSpell ? FColor::Cyan : FColor::Yellow, false, .4f, 0, 5.f);
    DrawDebugSphere(GetWorld(), End, bSpell ? 75 : 35, 12, bSpell ? FColor::Cyan : FColor::Yellow, false, .4f);
}
void AMMOCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME_CONDITION(AMMOCharacter, SelectedTarget, COND_OwnerOnly); }

void AMMOPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController()) { bShowMouseCursor = true; SetControlRotation(FRotator(-30, 0, 0)); }
#if !UE_BUILD_SHIPPING
    if (IsLocalController() && FParse::Param(FCommandLine::Get(), TEXT("MMOLocalizationProbe")))
    {
        GetWorldTimerManager().SetTimer(SmokeTimer, FTimerDelegate::CreateWeakLambda(this,
            [this] { RecordCultureState(TEXT("fresh-process")); ConsoleCommand(TEXT("quit")); }), 3.f, false);
        return;
    }
    FString SmokeUser;
    if (IsLocalController() && FParse::Value(FCommandLine::Get(), TEXT("MMOSmokeUser="), SmokeUser))
    {
        auto* GI = CastChecked<UMMOGameInstance>(GetGameInstance());
        if (!GI->bSmokeLoginStarted)
        {
            GI->bSmokeLoginStarted = true;
            if (FParse::Param(FCommandLine::Get(), TEXT("MMOShowcase")))
            {
                FTimerHandle CaptureTimer, LoginTimer;
                GetWorldTimerManager().SetTimer(CaptureTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { CaptureShowcase(TEXT("login")); }), 2.f, false);
                GetWorldTimerManager().SetTimer(LoginTimer, FTimerDelegate::CreateWeakLambda(this, [this, SmokeUser]() { SmokeLogin(SmokeUser); }), 5.f, false);
            }
            else SmokeLogin(SmokeUser);
        }
        GetWorldTimerManager().SetTimer(SmokeTimer, this, &ThisClass::SmokeTick, .1f, true);
    }
#endif
}
void AMMOPlayerController::SmokeLogin(const FString& Username)
{
#if !UE_BUILD_SHIPPING
    auto* Backend = GetGameInstance()->GetSubsystem<UMMOBackendClient>();
    auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("username"), Username);
    Body->SetStringField(TEXT("password"), FPlatformMisc::GetEnvironmentVariable(TEXT("MMO_TEST_PASSWORD")));
    Backend->Request(TEXT("POST"), TEXT("/v1/auth/login"), Body, [WeakThis = TWeakObjectPtr<AMMOPlayerController>(this)](bool OK, int32, TSharedPtr<FJsonObject> Json)
    {
        auto* Self = WeakThis.Get(); if (!Self) return;
        if (!OK) { UE_LOG(LogTemp, Error, TEXT("MMO_SMOKE_ERROR login")); Self->ConsoleCommand(TEXT("quit")); return; }
        auto* Client = Self->GetGameInstance()->GetSubsystem<UMMOBackendClient>(); Client->SetSessionToken(Json->GetStringField(TEXT("token")));
        Client->Request(TEXT("GET"), TEXT("/v1/characters"), nullptr, [WeakThis](bool Listed, int32, TSharedPtr<FJsonObject> List)
        {
            auto* PC = WeakThis.Get(); if (!PC) return;
            if (!Listed || List->GetArrayField(TEXT("characters")).IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("MMO_SMOKE_ERROR character_list")); PC->ConsoleCommand(TEXT("quit")); return; }
            auto TicketBody = MakeShared<FJsonObject>(); TicketBody->SetStringField(TEXT("characterId"), List->GetArrayField(TEXT("characters"))[0]->AsObject()->GetStringField(TEXT("id")));
            TicketBody->SetStringField(TEXT("serverId"), TEXT("local-1"));
            PC->GetGameInstance()->GetSubsystem<UMMOBackendClient>()->Request(TEXT("POST"), TEXT("/v1/join-ticket"), TicketBody, [WeakThis](bool Joined, int32, TSharedPtr<FJsonObject> Ticket)
            {
                if (auto* Controller = WeakThis.Get())
                {
                    if (!Joined) { UE_LOG(LogTemp, Error, TEXT("MMO_SMOKE_ERROR join_ticket")); Controller->ConsoleCommand(TEXT("quit")); return; }
                    Controller->ClientTravel(Ticket->GetStringField(TEXT("address")) + TEXT("?Ticket=") + Ticket->GetStringField(TEXT("ticket")), TRAVEL_Absolute);
                }
            });
        });
    });
#endif
}
void AMMOPlayerController::SmokeTick()
{
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("MMOShowcase"))) { ShowcaseTick(); return; }
    auto* TestGameInstance = CastChecked<UMMOGameInstance>(GetGameInstance());
    if (TestGameInstance->bSmokeReturnExpected)
    {
        if (SmokeStart < 0) SmokeStart = GetWorld()->GetTimeSeconds();
        auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(this));
        auto* MenuStack = Root ? Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Menu"))) : nullptr;
        auto* ModalStack = Root ? Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Modal"))) : nullptr;
        auto* GameStack = Root ? Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Game"))) : nullptr;
        const bool bReturned = MenuStack && ModalStack && GameStack && MenuStack->GetActiveWidget()
            && MenuStack->GetActiveWidget()->IsA<UMMOLoginPanel>() && ModalStack->GetWidgetList().IsEmpty() && GameStack->GetWidgetList().IsEmpty();
        if (bReturned || GetWorld()->GetTimeSeconds() - SmokeStart > 5)
        {
            FString TestRole; FParse::Value(FCommandLine::Get(), TEXT("MMOSmokeRole="), TestRole);
            UE_LOG(LogTemp, Display, TEXT("MMO_SMOKE_RESULT {\"role\":\"%s\",\"uiReturn\":true,\"passed\":%s,\"tagDrivenInput\":%s}"), *TestRole,
                bReturned && TestGameInstance->bSmokeInputTagsVerifiedBeforeReturn ? TEXT("true") : TEXT("false"),
                TestGameInstance->bSmokeInputTagsVerifiedBeforeReturn ? TEXT("true") : TEXT("false"));
            GetWorldTimerManager().ClearTimer(SmokeTimer); ConsoleCommand(TEXT("quit"));
        }
        return;
    }
    auto* State = GetPlayerState<AMMOPlayerState>(); auto* SmokeCharacter = Cast<AMMOCharacter>(GetPawn());
    if (!State || !State->bSessionReady || !SmokeCharacter) return;
    if (SmokeStart < 0) { SmokeStart = GetWorld()->GetTimeSeconds(); UE_LOG(LogTemp, Display, TEXT("MMO_SMOKE admitted")); }
    const double Elapsed = GetWorld()->GetTimeSeconds() - SmokeStart;
    if (FParse::Param(FCommandLine::Get(), TEXT("MMOSmokeDeath")) || FParse::Param(FCommandLine::Get(), TEXT("MMOSmokeDeadLogout"))) { DeathSmokeTick(State, SmokeCharacter, Elapsed); return; }
    FString TestRole; FParse::Value(FCommandLine::Get(), TEXT("MMOSmokeRole="), TestRole);
    const bool bAttacker = TestRole == TEXT("A");
    const bool bUIReturn = FParse::Param(FCommandLine::Get(), TEXT("MMOSmokeUIReturn"));
    const bool bDeadRestore = FParse::Param(FCommandLine::Get(), TEXT("MMOSmokeDeadRestore"));
    const bool bKeepPotion = FParse::Param(FCommandLine::Get(), TEXT("MMOSmokeKeepPotion"));
    const bool bRestore = FParse::Param(FCommandLine::Get(), TEXT("MMOSmokeRestore")) || bUIReturn || bDeadRestore;
    if (!bRestore)
    {
        if (Elapsed >= 1 && SmokeStep == 0) { SmokeCharacter->SelectTarget(); ++SmokeStep; }
        if (bAttacker)
        {
            if (Elapsed >= 3 && SmokeStep == 1) { SmokeCharacter->Spell(); ++SmokeStep; }
            if (Elapsed >= 3.3 && SmokeStep == 2) { SmokeCharacter->Spell(); ++SmokeStep; }
            if (Elapsed >= 4.5 && SmokeStep == 3) { bSmokeCooldownPassed = FMath::IsNearlyEqual(State->Attributes->GetMana(), 80.f); ++SmokeStep; }
            if (Elapsed >= 6.5 && SmokeStep == 4) { SmokeCharacter->Spell(); ++SmokeStep; }
            if (Elapsed >= 10 && SmokeStep == 5) { SmokeCharacter->Spell(); ++SmokeStep; }
            if (Elapsed >= 12 && SmokeStep == 6) { if (!bKeepPotion) SmokeCharacter->ServerUsePotion(); ++SmokeStep; }
            if (Elapsed >= 13 && Elapsed <= 15) SmokeCharacter->AddMovementInput(FVector::ForwardVector, 1.f);
        }
        else
        {
            if (SmokeCharacter->SelectedTarget && SmokeCharacter->SelectedTarget->Health < 100) bSmokeSawDamage = true;
            for (TActorIterator<AMMOCharacter> It(GetWorld()); It; ++It)
            {
                if (*It == SmokeCharacter) continue;
                if (!SmokeOther.IsValid()) { SmokeOther = *It; SmokeOtherStart = It->GetActorLocation(); }
                if (SmokeOther.Get() == *It && FVector::Dist(It->GetActorLocation(), SmokeOtherStart) > 5) bSmokeSawOtherMovement = true;
            }
        }
    }
    if (Elapsed >= (bRestore ? 4 : 18))
    {
        GetWorldTimerManager().ClearTimer(SmokeTimer);
        if (bUIReturn)
        {
            auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(this));
            if (Root)
            {
                auto* Settings = Root->ShowSettings();
                Settings->ProcessEvent(Settings->FindFunctionChecked(TEXT("LogoutClicked")), nullptr);
                auto* ModalStack = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Modal")));
                if (auto* Confirm = ModalStack ? Cast<UMMOConfirmPanel>(ModalStack->GetActiveWidget()) : nullptr)
                {
                    TestGameInstance->bSmokeInputTagsVerifiedBeforeReturn = RecordAbilityInputState(TEXT("before-ui-return"));
                    TestGameInstance->bSmokeReturnExpected = true;
                    Confirm->ProcessEvent(Confirm->FindFunctionChecked(TEXT("ConfirmClicked")), nullptr);
                    return;
                }
            }
            UE_LOG(LogTemp, Error, TEXT("MMO_SMOKE_ERROR ui_layers_unavailable")); ConsoleCommand(TEXT("quit")); return;
        }
        const int32 ExpectedXP = bAttacker ? 25 : 0; const float ExpectedMana = bAttacker ? (bKeepPotion ? 40.f : 80.f) : 100.f;
        auto* UI = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(this));
        auto* GameLayer = UI ? UI->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Game"))) : nullptr;
        auto* ActualHUD = GameLayer ? Cast<UMMOHUDPanel>(GameLayer->GetActiveWidget()) : nullptr;
        const bool bMVVMBinding = ActualHUD && ActualHUD->ValidateManaBinding();
        int32 PotionCount = 0; for (const auto& Item : State->Inventory) if (Item.ItemId == TEXT("potion")) PotionCount += Item.Quantity;
        const bool bPositionRestored = bDeadRestore ? SmokeCharacter->GetActorLocation().Size2D() < 100.f : (!bRestore || !bAttacker || SmokeCharacter->GetActorLocation().X > 5.f);
        const bool bHealthRestored = !bDeadRestore || FMath::IsNearlyEqual(State->Attributes->GetHealth(), 100.f);
        const bool bPassed = State->XP == ExpectedXP && FMath::IsNearlyEqual(State->Attributes->GetMana(), ExpectedMana) && PotionCount == (bKeepPotion && bAttacker ? 1 : 0) && bPositionRestored && bHealthRestored
            && bMVVMBinding && (bRestore || (bAttacker ? bSmokeCooldownPassed : bSmokeSawDamage && bSmokeSawOtherMovement));
        auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("role"), TestRole); Result->SetBoolField(TEXT("restore"), bRestore); Result->SetBoolField(TEXT("passed"), bPassed);
        Result->SetNumberField(TEXT("xp"), State->XP); Result->SetNumberField(TEXT("mana"), State->Attributes->GetMana()); Result->SetNumberField(TEXT("potions"), PotionCount);
        Result->SetNumberField(TEXT("health"), State->Attributes->GetHealth()); Result->SetBoolField(TEXT("deadCheckpointRestore"), bDeadRestore);
        Result->SetBoolField(TEXT("cooldown"), bSmokeCooldownPassed); Result->SetBoolField(TEXT("sawDamage"), bSmokeSawDamage); Result->SetBoolField(TEXT("sawOtherMovement"), bSmokeSawOtherMovement);
        Result->SetNumberField(TEXT("positionX"), SmokeCharacter->GetActorLocation().X);
        Result->SetBoolField(TEXT("umgMVVMBinding"), bMVVMBinding);
        const bool bTaggedInput = RecordAbilityInputState(TEXT("gameplay-result"));
        Result->SetBoolField(TEXT("tagDrivenInput"), bTaggedInput);
        Result->SetBoolField(TEXT("passed"), bPassed && bTaggedInput);
        FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
        UE_LOG(LogTemp, Display, TEXT("MMO_SMOKE_RESULT %s"), *Text);
        ConsoleCommand(TEXT("quit"));
    }
#endif
}
void AMMOPlayerController::CaptureShowcase(const FString& Stage)
{
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("MMONoAutoCapture"))) return;
    FString TestRole; FParse::Value(FCommandLine::Get(), TEXT("MMOSmokeRole="), TestRole);
    FString CaptureDirectory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("MMOCaptureDir="), CaptureDirectory)) CaptureDirectory = FPaths::ProjectSavedDir() / TEXT("Screenshots/MMOShowcase");
    IFileManager::Get().MakeDirectory(*CaptureDirectory, true);
    const FString Filename = CaptureDirectory / FString::Printf(TEXT("mmo-%s-%s.png"), *TestRole, *Stage);
    auto* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    const auto ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
    TArray<FColor> Pixels; FIntVector Size(0, 0, 0);
    // Capture from a game timer, outside ProcessScreenShots/Slate's current draw pass.
    // The SViewport includes its UMG game layers and excludes native window chrome.
    const bool bCaptured = ViewportWidget.IsValid() && FSlateApplication::IsInitialized()
        && FSlateApplication::Get().TakeScreenshot(ViewportWidget.ToSharedRef(), Pixels, Size);
    const bool bSaved = bCaptured && FImageUtils::SaveImageByExtension(*Filename, FImageView(Pixels.GetData(), Size.X, Size.Y));
    UE_LOG(LogTemp, Display, TEXT("MMO_SHOWCASE_CAPTURE saved=%d width=%d height=%d %s"), bSaved, Size.X, Size.Y, *Filename);
#endif
}
void AMMOPlayerController::ShowcaseTick()
{
#if !UE_BUILD_SHIPPING
    auto* State = GetPlayerState<AMMOPlayerState>(); auto* ShowcaseCharacter = Cast<AMMOCharacter>(GetPawn());
    if (!State || !State->bSessionReady || !ShowcaseCharacter) return;
    if (SmokeStart < 0)
    {
        SmokeStart = GetWorld()->GetTimeSeconds();
        // Optional original teaching graph, generated by the editor-only MMODocTools module.
        // Its BeginPlay executes the actual Blueprint HTTP node and prints the health response.
        UClass* HealthDemo = LoadClass<AActor>(nullptr, TEXT("/Game/Tutorial/BP_BackendHealth.BP_BackendHealth_C"));
        AActor* HealthActor = HealthDemo ? GetWorld()->SpawnActor<AActor>(HealthDemo) : nullptr;
        UE_LOG(LogTemp, Display, TEXT("MMO_SHOWCASE_HEALTH_GRAPH spawned=%d class=%s"),
            IsValid(HealthActor), *GetPathNameSafe(HealthDemo));
    }
    const double Elapsed = GetWorld()->GetTimeSeconds() - SmokeStart;
    FString TestRole; FParse::Value(FCommandLine::Get(), TEXT("MMOSmokeRole="), TestRole);
    auto* Root = Cast<UMMORootLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(this)); if (!Root) return;
    auto* Menu = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Menu")));
    auto* Modal = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Modal")));
    if (TestRole == TEXT("A") && Elapsed >= 24.5 && Elapsed < 26 && !bShowcaseMovementStarted)
    { bShowcaseMovementStarted = true; ShowcaseMoveTick(); }
    if (TestRole == TEXT("A") && Elapsed >= 25 && LocomotionSamples == 0)
    { RecordLocomotionState(TEXT("moving-first")); ++LocomotionSamples; }
    if (TestRole == TEXT("A") && Elapsed >= 25.5 && LocomotionSamples == 1)
    { RecordLocomotionState(TEXT("moving-second")); ++LocomotionSamples; }
    if (Elapsed >= 1 && SmokeStep == 0) { ShowcaseCharacter->SelectTarget(); ++SmokeStep; }
    if (Elapsed >= 3 && SmokeStep == 1) { CaptureShowcase(TEXT("hud")); ++SmokeStep; }
    if (Elapsed >= 4 && SmokeStep == 2) { if (TestRole == TEXT("A")) ShowcaseCharacter->Spell(); ++SmokeStep; }
    if (Elapsed >= 4.4 && SmokeStep == 3) { CaptureShowcase(TEXT("skill")); ++SmokeStep; }
    if (Elapsed >= 7.2 && SmokeStep == 4) { if (TestRole == TEXT("A")) ShowcaseCharacter->Spell(); ++SmokeStep; }
    if (Elapsed >= 10.5 && SmokeStep == 5) { if (TestRole == TEXT("A")) ShowcaseCharacter->Spell(); ++SmokeStep; }
    if (Elapsed >= 12 && SmokeStep == 6) { Root->ShowInventory(); ++SmokeStep; }
    if (Elapsed >= 14 && SmokeStep == 7) { CaptureShowcase(TEXT("inventory")); ++SmokeStep; }
    if (Elapsed >= 15 && SmokeStep == 8) { if (Menu && Menu->GetActiveWidget()) Menu->GetActiveWidget()->DeactivateWidget(); ++SmokeStep; }
    if (Elapsed >= 16 && SmokeStep == 9)
    {
        int32 BeforeWidth = 0, BeforeHeight = 0; GetViewportSize(BeforeWidth, BeforeHeight);
        auto* Settings = Root->ShowSettings();
        if (Settings) Settings->ProcessEvent(Settings->FindFunctionChecked(TEXT("MediumQualityClicked")), nullptr);
        int32 AfterWidth = 0, AfterHeight = 0; GetViewportSize(AfterWidth, AfterHeight);
        UE_LOG(LogTemp, Display, TEXT("MMO_SHOWCASE_QUALITY_RESULT {\"buttonApplied\":%s,\"beforeWidth\":%d,\"beforeHeight\":%d,\"afterWidth\":%d,\"afterHeight\":%d}"), Settings ? TEXT("true") : TEXT("false"), BeforeWidth, BeforeHeight, AfterWidth, AfterHeight);
        ++SmokeStep;
    }
    if (Elapsed >= 18 && SmokeStep == 10) { CaptureShowcase(TEXT("settings")); ++SmokeStep; }
    if (Elapsed >= 19 && SmokeStep == 11)
    {
        if (auto* Settings = Menu ? Cast<UMMOSettingsPanel>(Menu->GetActiveWidget()) : nullptr) Settings->ProcessEvent(Settings->FindFunctionChecked(TEXT("LogoutClicked")), nullptr);
        ++SmokeStep;
    }
    if (Elapsed >= 21 && SmokeStep == 12) { CaptureShowcase(TEXT("modal")); ++SmokeStep; }
    if (Elapsed >= 22 && SmokeStep == 13)
    {
        if (auto* Confirm = Modal ? Cast<UMMOConfirmPanel>(Modal->GetActiveWidget()) : nullptr) Confirm->ProcessEvent(Confirm->FindFunctionChecked(TEXT("CancelClicked")), nullptr);
        ++SmokeStep;
    }
    if (Elapsed >= 24 && SmokeStep == 14)
    {
        CaptureShowcase(TEXT("returned-settings"));
        auto* Settings = Menu ? Cast<UMMOSettingsPanel>(Menu->GetActiveWidget()) : nullptr;
        const bool bFocus = Settings && Settings->GetDesiredFocusTarget() && Settings->GetDesiredFocusTarget()->HasAnyUserFocus();
        UE_LOG(LogTemp, Display, TEXT("MMO_SHOWCASE_PROGRAMMATIC_FOCUS role=%s focused=%d"), *TestRole, bFocus);
        if (Settings) Settings->DeactivateWidget(); ++SmokeStep;
    }
    if (Elapsed >= 26 && SmokeStep == 15)
    {
        CaptureShowcase(TEXT("final-hud")); ++SmokeStep;
        auto* GameLayer = Root->GetLayerWidget(FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.Game")));
        auto* ActualHUD = GameLayer ? Cast<UMMOHUDPanel>(GameLayer->GetActiveWidget()) : nullptr;
        UE_LOG(LogTemp, Display, TEXT("MMO_MVVM_BINDING role=%s passed=%d mana=%.1f"), *TestRole, ActualHUD && ActualHUD->ValidateManaBinding(), State->Attributes->GetMana());
    }
    if (Elapsed >= 27 && SmokeStep == 16)
    {
        if (auto* Settings = Root->ShowSettings())
            Settings->ProcessEvent(Settings->FindFunctionChecked(TestRole == TEXT("A") ? TEXT("ChineseClicked") : TEXT("EnglishClicked")), nullptr);
        ++SmokeStep;
    }
    if (Elapsed >= 28 && SmokeStep == 17)
    { CaptureShowcase(TEXT("language-first-settings")); RecordCultureState(TEXT("first-switch")); ++SmokeStep; }
    if (Elapsed >= 29 && SmokeStep == 18)
    {
        if (auto* Settings = Menu ? Cast<UMMOSettingsPanel>(Menu->GetActiveWidget()) : nullptr)
            Settings->ProcessEvent(Settings->FindFunctionChecked(TestRole == TEXT("A") ? TEXT("EnglishClicked") : TEXT("ChineseClicked")), nullptr);
        ++SmokeStep;
    }
    if (Elapsed >= 30 && SmokeStep == 19)
    { CaptureShowcase(TEXT("language-second-settings")); RecordCultureState(TEXT("second-switch")); ++SmokeStep; }
    if (Elapsed >= 31 && SmokeStep == 20)
    {
        if (auto* Settings = Menu ? Cast<UMMOSettingsPanel>(Menu->GetActiveWidget()) : nullptr)
        {
            Settings->ProcessEvent(Settings->FindFunctionChecked(TestRole == TEXT("A") ? TEXT("ChineseClicked") : TEXT("EnglishClicked")), nullptr);
            Settings->DeactivateWidget();
        }
        ++SmokeStep;
    }
    if (Elapsed >= 32 && SmokeStep == 21)
    {
        CaptureShowcase(TEXT("language-final-hud")); RecordCultureState(TEXT("final-hud")); GetWorldTimerManager().ClearTimer(SmokeTimer);
        RecordAbilityInputState(TEXT("showcase-complete"));
        UE_LOG(LogTemp, Display, TEXT("MMO_SHOWCASE_READY role=%s"), *TestRole);
        if (FParse::Param(FCommandLine::Get(), TEXT("MMOShowcaseAutoQuit"))) ConsoleCommand(TEXT("quit"));
    }
#endif
}
void AMMOPlayerController::OnPossess(APawn* InPawn) { Super::OnPossess(InPawn); }
void AMMOPlayerController::OnUnPossess()
{
    if (HasAuthority()) if (auto* State = GetPlayerState<AMMOPlayerState>(); State && GetPawn()) State->SavedPosition = GetPawn()->GetActorLocation();
    Super::OnUnPossess();
}
void AMMOPlayerController::OnRep_PlayerState() { Super::OnRep_PlayerState(); }
void AMMOPlayerController::ClientBackendError_Implementation(const FString& Message)
{
    UE_LOG(LogTemp, Error, TEXT("MMO session: %s"), *Message);
    FText Display = NSLOCTEXT("MMOUI", "Session.ConnectionFailed", "Connection failed. Please try joining again.");
    if (Message == TEXT("experience_unavailable")) Display = NSLOCTEXT("MMOUI", "Session.ExperienceUnavailable", "The game world is unavailable. Please try again later.");
    else if (Message == TEXT("pawn_data_invalid") || Message == TEXT("pawn_initialization_failed")) Display = NSLOCTEXT("MMOUI", "Session.CharacterInitializationFailed", "Your character could not be initialized. Please try joining again.");
    else if (Message == TEXT("server_backend_unconfigured")) Display = NSLOCTEXT("MMOUI", "Session.ServerUnavailable", "The game server is unavailable. Please try again later.");
    else if (Message == TEXT("ticket_rejected")) Display = NSLOCTEXT("MMOUI", "Session.TicketRejected", "Your entry ticket was rejected or expired. Select your character and join again.");
    else if (Message == TEXT("persistence_session_lost")) Display = NSLOCTEXT("MMOUI", "Session.PersistenceLost", "Your character session has ended. Join again to reload your saved progress.");
    if (auto* GI = GetGameInstance<UMMOGameInstance>()) GI->PendingLoginMessage = Display;
    UGameplayStatics::OpenLevel(this, TEXT("/Engine/Maps/Entry"));
}

struct FMMOSaveRecord
{
    TWeakObjectPtr<AMMOPlayerState> Player;
    FString CharacterID, Lease;
    int32 Version = 0;
    bool bInFlight = false, bFinal = false;
    TSharedPtr<FJsonObject> LastSnapshot;
    TSharedPtr<FJsonObject> PendingBody;
    bool bPendingFinal = false;
    int32 RetryCount = 0;
    FTimerHandle RetryTimer;
};
void AMMOGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& Error)
{
    Super::PreLogin(Options, Address, UniqueId, Error);
    if (GetNetMode() == NM_DedicatedServer && UGameplayStatics::ParseOption(Options, TEXT("Ticket")).IsEmpty()) Error = TEXT("An authenticated join ticket is required.");
    if (const auto* Experience = UMMOExperienceManagerComponent::FindExperienceManager(GetWorld()); Experience && Experience->LoadState == EMMOExperienceLoadState::Failed) Error = TEXT("Game Experience failed to load.");
}
FString AMMOGameMode::InitNewPlayer(APlayerController* NewPlayer, const FUniqueNetIdRepl& Id, const FString& Options, const FString& Portal)
{
    FString Error = Super::InitNewPlayer(NewPlayer, Id, Options, Portal);
    CastChecked<AMMOPlayerController>(NewPlayer)->PendingTicket = UGameplayStatics::ParseOption(Options, TEXT("Ticket")); return Error;
}
void AMMOGameMode::HandleStartingNewPlayer_Implementation(APlayerController*) { /* Admission completes asynchronously in PostLogin. */ }
void AMMOGameMode::PostLogin(APlayerController* Player)
{
    Super::PostLogin(Player);
#if WITH_SERVER_CODE
    if (GetNetMode() != NM_DedicatedServer) return; // Standalone process is the login front end.
    auto* Backend = GetGameInstance()->GetSubsystem<UMMOBackendServer>();
    auto* PC = CastChecked<AMMOPlayerController>(Player);
    if (!Backend || !Backend->IsConfigured()) { PC->ClientBackendError(TEXT("server_backend_unconfigured")); PC->Destroy(); return; }
    auto Body = MakeShared<FJsonObject>(); Body->SetStringField(TEXT("ticket"), PC->PendingTicket); PC->PendingTicket.Empty();
    Backend->Request(TEXT("/v1/server/consume-ticket"), Body, [WeakThis = TWeakObjectPtr<AMMOGameMode>(this), WeakPC = TWeakObjectPtr<AMMOPlayerController>(PC), WeakBackend = TWeakObjectPtr<UMMOBackendServer>(Backend)](bool OK, int32, TSharedPtr<FJsonObject> Json)
    {
        auto* Self = WeakThis.Get(); auto* Controller = WeakPC.Get();
        if (!Self || !Controller)
        {
            if (OK) if (auto* B = WeakBackend.Get())
            {
                auto Release = MakeShared<FJsonObject>(); Release->SetStringField(TEXT("characterId"), Json->GetObjectField(TEXT("character"))->GetStringField(TEXT("id")));
                Release->SetStringField(TEXT("leaseToken"), Json->GetStringField(TEXT("leaseToken")));
                B->Request(TEXT("/v1/server/release"), Release, [](bool, int32, TSharedPtr<FJsonObject>) {});
            }
            return;
        }
        if (!OK) { Controller->ClientBackendError(TEXT("ticket_rejected")); Controller->Destroy(); return; }
        auto* PS = Controller->GetPlayerState<AMMOPlayerState>(); if (!PS) { Controller->Destroy(); return; }
        const auto Character = Json->GetObjectField(TEXT("character")); PS->LoadSnapshot(Character);
        auto Record = MakeShared<FMMOSaveRecord>(); Record->Player = PS; Record->CharacterID = PS->CharacterID;
        Record->Lease = Json->GetStringField(TEXT("leaseToken")); Record->Version = Character->GetIntegerField(TEXT("version"));
        Self->Records.Add(PS, Record);
        Self->TryStartPlayer(Controller);
        UE_LOG(LogTemp, Display, TEXT("MMO admitted character %s at version %d"), *Record->CharacterID, Record->Version);
    });
#endif
}
void AMMOGameMode::PeriodicSave()
{
    for (auto& Pair : Records) if (Pair.Key.IsValid()) Save(Pair.Value, false);
}
void AMMOGameMode::Save(const TSharedPtr<FMMOSaveRecord>& Record, bool bFinal)
{
#if WITH_SERVER_CODE
    auto* Backend = GetGameInstance()->GetSubsystem<UMMOBackendServer>(); if (!Backend) return;
    Record->bFinal |= bFinal;
    if (auto* PS = Record->Player.Get()) Record->LastSnapshot = PS->MakeSnapshot();
    if (Record->bInFlight || !Record->LastSnapshot) return;
    Record->bInFlight = true;
    if (!Record->PendingBody)
    {
        Record->PendingBody = MakeShared<FJsonObject>(); Record->PendingBody->SetStringField(TEXT("characterId"), Record->CharacterID);
        Record->PendingBody->SetStringField(TEXT("leaseToken"), Record->Lease); Record->PendingBody->SetNumberField(TEXT("expectedVersion"), Record->Version);
        Record->PendingBody->SetStringField(TEXT("requestId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)); Record->PendingBody->SetObjectField(TEXT("state"), Record->LastSnapshot);
        Record->bPendingFinal = Record->bFinal;
    }
    // A lost response must retry exactly the same idempotency key, version and snapshot.
    const auto Body = Record->PendingBody;
    const bool bSavingFinal = Record->bPendingFinal;
    Backend->Request(TEXT("/v1/server/save"), Body, [WeakThis = TWeakObjectPtr<AMMOGameMode>(this), WeakBackend = TWeakObjectPtr<UMMOBackendServer>(Backend), Record, bSavingFinal](bool OK, int32 Code, TSharedPtr<FJsonObject> Json)
    {
        Record->bInFlight = false;
        if (!OK)
        {
            UE_LOG(LogTemp, Error, TEXT("MMO save failed (%d), character %s; session is not released as saved"), Code, *Record->CharacterID);
            if (auto* PS = Record->Player.Get())
            {
                // Stop mutations on loss of the lease or a version conflict. Transport errors retry on the next timer.
                if (Code == 401 || Code == 403 || Code == 409) { PS->bSessionReady = false; if (auto* PC = Cast<AMMOPlayerController>(PS->GetOwner())) { PC->ClientBackendError(TEXT("persistence_session_lost")); PC->Destroy(); } }
            }
            if ((Code == 0 || Code >= 500) && Record->bFinal && ++Record->RetryCount <= 10)
                if (auto* Self = WeakThis.Get()) Self->GetWorldTimerManager().SetTimer(Record->RetryTimer,
                    FTimerDelegate::CreateWeakLambda(Self, [Self, Record] { Self->Save(Record, true); }), 2.f, false);
            return;
        }
        Record->Version = Json->GetIntegerField(TEXT("version"));
        Record->PendingBody.Reset(); Record->RetryCount = 0;
        UE_LOG(LogTemp, Display, TEXT("MMO saved character %s at version %d%s"), *Record->CharacterID, Record->Version, bSavingFinal ? TEXT(" (final)") : TEXT(""));
        if (bSavingFinal)
        {
            if (auto* B = WeakBackend.Get())
            {
                auto Release = MakeShared<FJsonObject>(); Release->SetStringField(TEXT("characterId"), Record->CharacterID); Release->SetStringField(TEXT("leaseToken"), Record->Lease);
                B->Request(TEXT("/v1/server/release"), Release, [CharacterID = Record->CharacterID](bool Released, int32, TSharedPtr<FJsonObject>)
                {
                    if (Released) { UE_LOG(LogTemp, Display, TEXT("MMO released character %s"), *CharacterID); }
                    else { UE_LOG(LogTemp, Warning, TEXT("MMO release failed; lease will expire")); }
                });
            }
        }
        else if (Record->bFinal) { if (auto* Self = WeakThis.Get()) Self->Save(Record, true); }
    });
#endif
}
void AMMOGameMode::Logout(AController* Exiting)
{
    if (auto* PC = Cast<APlayerController>(Exiting))
        if (FTimerHandle* Timer = RespawnTimers.Find(PC)) { GetWorldTimerManager().ClearTimer(*Timer); RespawnTimers.Remove(PC); }
    if (auto* PS = Exiting->GetPlayerState<AMMOPlayerState>())
    { if (auto* Record = Records.Find(PS)) { Save(*Record, true); Records.Remove(PS); } }
    Super::Logout(Exiting);
}
void AMMOGameMode::EndPlay(EEndPlayReason::Type Reason)
{
    for (auto& Pair : RespawnTimers) GetWorldTimerManager().ClearTimer(Pair.Value);
    RespawnTimers.Reset();
    GetWorldTimerManager().ClearTimer(SaveTimer);
    for (auto& Pair : Records) Save(Pair.Value, true);
    Super::EndPlay(Reason);
}
UMMOGameInstance::UMMOGameInstance(const FObjectInitializer& Initializer) : Super(Initializer) {}
void UMMOGameInstance::Init() { Super::Init(); }
