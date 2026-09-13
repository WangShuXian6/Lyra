#include "MMOGameplay.h"
#include "MMOPawnExtensionComponent.h"
#include "MMOPawnData.h"
#include "MMOInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"

void AMMOCharacter::BeginPlay()
{
    // Subscribe before component BeginPlay can advance DataAvailable.
    PawnExtension->OnDataAvailable.AddDynamic(this, &ThisClass::TryInitializeInput);
    Super::BeginPlay(); TryInitializeInput();
}
void AMMOCharacter::SetupPlayerInputComponent(UInputComponent* Component)
{
    Super::SetupPlayerInputComponent(Component); TryInitializeInput();
}
void AMMOCharacter::TryInitializeInput()
{
    auto* PC = Cast<APlayerController>(Controller);
    if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer() || !InputComponent || InputOwner.IsValid()) return;
    const auto* Data = PawnExtension->GetPawnData();
    const auto* Config = Data ? Data->InputConfig.Get() : nullptr;
    UInputMappingContext* Mapping = Data ? Data->InputMappingContext.Get() : nullptr;
    auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
    if (!Config || !Mapping || !Input) return; // The DataAvailable event retries when the Experience closure is loaded.
    Actions.Reset();
    const TCHAR* InputTags[] {TEXT("InputTag.MMO.Move"), TEXT("InputTag.MMO.Look"), TEXT("InputTag.MMO.Target"), TEXT("InputTag.MMO.Attack"), TEXT("InputTag.MMO.Spell"), TEXT("InputTag.MMO.Inventory"), TEXT("InputTag.MMO.Settings")};
    for (const TCHAR* Tag : InputTags)
    {
        UInputAction* Action = Config->FindInputActionForTag(FGameplayTag::RequestGameplayTag(FName(Tag)));
        if (!ensureMsgf(Action, TEXT("Missing or unloaded input action for %s"), Tag)) return;
        Actions.Add(Action);
    }
    Input->BindAction(Actions[0], ETriggerEvent::Triggered, this, &ThisClass::Move);
    Input->BindAction(Actions[1], ETriggerEvent::Triggered, this, &ThisClass::Look);
    Input->BindAction(Actions[2], ETriggerEvent::Started, this, &ThisClass::SelectTarget);
    Input->BindAction(Actions[3], ETriggerEvent::Started, this, &ThisClass::Attack);
    Input->BindAction(Actions[4], ETriggerEvent::Started, this, &ThisClass::Spell);
    Input->BindAction(Actions[5], ETriggerEvent::Started, this, &ThisClass::OpenInventory);
    Input->BindAction(Actions[6], ETriggerEvent::Started, this, &ThisClass::OpenSettings);
    InputContext = Mapping; InputOwner = PC->GetLocalPlayer();
    InputOwner->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()->AddMappingContext(InputContext, 0);
    PawnExtension->SetupPlayerInputComponent();
    UE_LOG(LogTemp, Display, TEXT("MMO input ready: %s + %s"), *Config->GetPathName(), *Mapping->GetPathName());
}
