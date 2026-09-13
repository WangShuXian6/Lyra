#include "MMOGameplay.h"
#include "MMOPawnData.h"
#include "MMOPawnExtensionComponent.h"
#include "MMOExperienceManagerComponent.h"
#include "MMOExperienceDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"

AMMOGameState::AMMOGameState(const FObjectInitializer& Initializer) : Super(Initializer)
{ ExperienceManager = CreateDefaultSubobject<UMMOExperienceManagerComponent>(TEXT("ExperienceManager")); }

bool AMMOPlayerState::SetPawnData(UMMOPawnData* Data)
{
    if (!HasAuthority() || !Data) return false;
    if (PawnDefinition == Data) return true;
    if (PawnDefinition) return false; // Character identity is fixed for this PlayerState's session.
    ASC->InitAbilityActorInfo(this, GetPawn());
    for (const auto& AbilitySet : Data->AbilitySets)
    {
        const auto* Set = AbilitySet.Get();
        if (!Set || !Set->GrantToAbilitySystem(ASC, this, GrantedAbilitySets.AddDefaulted_GetRef()))
        {
            for (auto& Handles : GrantedAbilitySets) Handles.TakeFromAbilitySystem(ASC);
            GrantedAbilitySets.Reset(); return false;
        }
    }
    PawnDefinition = Data; return true;
}
void AMMOPlayerState::EndPlay(EEndPlayReason::Type Reason)
{
    if (HasAuthority()) for (auto& Handles : GrantedAbilitySets) Handles.TakeFromAbilitySystem(ASC);
    GrantedAbilitySets.Reset(); Super::EndPlay(Reason);
}

AMMOGameMode::AMMOGameMode()
{
    PlayerStateClass = AMMOPlayerState::StaticClass(); PlayerControllerClass = AMMOPlayerController::StaticClass();
    DefaultPawnClass = AMMOCharacter::StaticClass(); GameStateClass = AMMOGameState::StaticClass(); bStartPlayersAsSpectators = true;
}
void AMMOGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Manager = UMMOExperienceManagerComponent::FindExperienceManager(GetWorld()))
    {
        Manager->OnExperienceFailed.AddDynamic(this, &ThisClass::ExperienceFailed);
        Manager->CallOrRegister_OnReady(FOnMMOExperienceReady::FDelegate::CreateUObject(this, &ThisClass::ExperienceReady));
        if (!Manager->SetCurrentExperience(DefaultExperience)) ExperienceFailed(TEXT("Could not select the configured MMO Experience"));
    }
    if (GetNetMode() == NM_DedicatedServer) GetWorldTimerManager().SetTimer(SaveTimer, this, &ThisClass::PeriodicSave, 15.f, true);
}
void AMMOGameMode::ExperienceReady(const UMMOExperienceDefinition*)
{
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It) TryStartPlayer(It->Get());
}
void AMMOGameMode::ExperienceFailed(const FString& Reason)
{
    UE_LOG(LogTemp, Error, TEXT("MMO Experience failed: %s"), *Reason);
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        if (auto* PC = Cast<AMMOPlayerController>(It->Get()); PC && !PC->IsLocalController())
        { PC->ClientBackendError(TEXT("experience_unavailable")); PC->Destroy(); }
}
void AMMOGameMode::TryStartPlayer(APlayerController* Controller)
{
    auto* PS = Controller ? Controller->GetPlayerState<AMMOPlayerState>() : nullptr;
    auto* Experience = UMMOExperienceManagerComponent::FindExperienceManager(GetWorld());
    if (GetNetMode() != NM_DedicatedServer || !PS || !PS->bCharacterLoaded || Controller->GetPawn()
        || !Experience || !Experience->IsExperienceReady()) return;
    if (!PS->SetPawnData(Experience->GetPawnData()))
    { CastChecked<AMMOPlayerController>(Controller)->ClientBackendError(TEXT("pawn_data_invalid")); Controller->Destroy(); return; }
    RestartPlayerAtTransform(Controller, FTransform(FRotator::ZeroRotator, PS->SavedPosition));
    if (auto* Pawn = Cast<AMMOCharacter>(Controller->GetPawn()); Pawn && Pawn->PawnExtension->IsPawnReady())
    {
        PS->bSessionReady = true; PS->OnRep_Progress(); PS->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("MMO GameplayReady character %s: Experience + persistence + Pawn InitState"), *PS->CharacterID);
    }
    else
    { CastChecked<AMMOPlayerController>(Controller)->ClientBackendError(TEXT("pawn_initialization_failed")); Controller->Destroy(); }
}
UClass* AMMOGameMode::GetDefaultPawnClassForController_Implementation(AController* Controller)
{
    if (auto* Experience = UMMOExperienceManagerComponent::FindExperienceManager(GetWorld()))
        if (auto* Data = Experience->GetPawnData()) return Data->PawnClass.Get();
    return Super::GetDefaultPawnClassForController_Implementation(Controller);
}
APawn* AMMOGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* Controller, const FTransform& Transform)
{
    auto* PS = Controller ? Controller->GetPlayerState<AMMOPlayerState>() : nullptr;
    UClass* PawnClass = GetDefaultPawnClassForController(Controller);
    if (!PS || !PS->GetPawnData() || !PawnClass || !PawnClass->IsChildOf(AMMOCharacter::StaticClass())) return nullptr;
    auto* Pawn = GetWorld()->SpawnActorDeferred<AMMOCharacter>(PawnClass, Transform, Controller, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
    if (!Pawn) return nullptr;
    Pawn->PawnExtension->SetPawnData(PS->GetPawnData());
    UGameplayStatics::FinishSpawningActor(Pawn, Transform);
    return Pawn;
}
