#include "MMOPawnExtensionComponent.h"
#include "MMOPawnData.h"
#include "MMOFrameworkSubsystem.h"
#include "MMOExperienceManagerComponent.h"
#include "AbilitySystemComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Templates/UnrealTemplate.h"

const FName UMMOPawnExtensionComponent::FeatureName(TEXT("MMOPawnExtension"));
UMMOPawnExtensionComponent::UMMOPawnExtensionComponent(const FObjectInitializer& Initializer) : Super(Initializer)
{ SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = false; }
void UMMOPawnExtensionComponent::OnRegister()
{
    Super::OnRegister();
    if (!ensure(GetPawn<APawn>())) return;
    RegisterInitStateFeature();
}
void UMMOPawnExtensionComponent::BeginPlay()
{
    Super::BeginPlay();
    BindOnActorInitStateChanged(NAME_None, FGameplayTag(), false);
    TryToChangeInitState(MMOInitTags::Spawned);
    CheckDefaultInitialization();
}
void UMMOPawnExtensionComponent::EndPlay(EEndPlayReason::Type Reason)
{ UninitializeAbilitySystem(); UnregisterInitStateFeature(); Super::EndPlay(Reason); }
void UMMOPawnExtensionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMMOPawnExtensionComponent, PawnData); }
bool UMMOPawnExtensionComponent::SetPawnData(UMMOPawnData* InData)
{
    if (!GetOwner()->HasAuthority() || !InData || (PawnData && PawnData != InData)) return false;
    PawnData = InData; GetOwner()->ForceNetUpdate(); CheckDefaultInitialization(); return true;
}
void UMMOPawnExtensionComponent::OnRep_PawnData() { CheckDefaultInitialization(); }
bool UMMOPawnExtensionComponent::IsPawnReady() const
{ return AbilitySystem && AbilitySystem->GetAvatarActor() == GetOwner() && HasReachedInitState(MMOInitTags::GameplayReady); }
bool UMMOPawnExtensionComponent::InitializeAbilitySystem(UAbilitySystemComponent* InASC, AActor* OwnerActor)
{
    if (!InASC || !OwnerActor || !GetPawn<APawn>()) return false;
    if (AbilitySystem == InASC && InASC->GetAvatarActor() == GetOwner()) { CheckDefaultInitialization(); return true; }
    UninitializeAbilitySystem();
    if (AActor* PreviousAvatar = InASC->GetAvatarActor(); PreviousAvatar && PreviousAvatar != GetOwner())
    {
        if (auto* PreviousExtension = PreviousAvatar->FindComponentByClass<UMMOPawnExtensionComponent>()) PreviousExtension->UninitializeAbilitySystem();
    }
    AbilitySystem = InASC;
    InASC->InitAbilityActorInfo(OwnerActor, GetOwner());
    OnAbilitySystemInitialized.Broadcast();
    CheckDefaultInitialization();
    return true;
}
void UMMOPawnExtensionComponent::UninitializeAbilitySystem()
{
    if (!AbilitySystem) return;
    // An old pawn must never clear the new pawn's avatar during overlapping replication/destruction.
    if (AbilitySystem->GetAvatarActor() == GetOwner())
    {
        AbilitySystem->CancelAllAbilities();
        FScopedAbilityListLock AbilityLock(*AbilitySystem);
        for (FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities()) Spec.InputPressed = false;
        AbilitySystem->RemoveAllGameplayCues();
        AbilitySystem->SetAvatarActor(nullptr);
    }
    AbilitySystem = nullptr;
    bReadyAnnounced = false;
    OnAbilitySystemUninitialized.Broadcast();
}
void UMMOPawnExtensionComponent::HandleControllerChanged()
{ if (AbilitySystem && AbilitySystem->GetAvatarActor() == GetOwner()) AbilitySystem->RefreshAbilityActorInfo(); CheckDefaultInitialization(); }
void UMMOPawnExtensionComponent::HandlePlayerStateReplicated() { CheckDefaultInitialization(); }
void UMMOPawnExtensionComponent::SetupPlayerInputComponent() { bInputReady = true; CheckDefaultInitialization(); }
bool UMMOPawnExtensionComponent::CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag Current, FGameplayTag Desired) const
{
    APawn* Pawn = GetPawn<APawn>();
    if (!Current.IsValid()) return Desired == MMOInitTags::Spawned && Pawn;
    if (!Pawn) return false;
    if (Current == MMOInitTags::Spawned && Desired == MMOInitTags::DataAvailable)
    {
        const auto* Experience = UMMOExperienceManagerComponent::FindExperienceManager(GetWorld());
        if (!PawnData || !Experience || !Experience->IsExperienceReady()) return false;
        if ((Pawn->HasAuthority() || Pawn->IsLocallyControlled()) && !Pawn->GetController()) return false;
        return true;
    }
    if (Current == MMOInitTags::DataAvailable && Desired == MMOInitTags::DataInitialized)
    {
        if (!AbilitySystem || AbilitySystem->GetAvatarActor() != Pawn) return false;
        if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()); PC && PC->IsLocalController() && !bInputReady) return false;
        return Manager->HaveAllFeaturesReachedInitState(Pawn, MMOInitTags::DataAvailable);
    }
    return Current == MMOInitTags::DataInitialized && Desired == MMOInitTags::GameplayReady;
}
void UMMOPawnExtensionComponent::HandleChangeInitState(UGameFrameworkComponentManager*, FGameplayTag, FGameplayTag Desired)
{
    // Consumers subscribe before Super::BeginPlay, then bind once both data and InputComponent exist.
    // SetupPlayerInputComponent may set bInputReady here; the outer state chain advances afterward.
    if (Desired == MMOInitTags::DataAvailable) OnDataAvailable.Broadcast();
}
void UMMOPawnExtensionComponent::OnActorInitStateChanged(const FActorInitStateChangedParams& Params)
{ if (Params.FeatureName != FeatureName && Params.FeatureState == MMOInitTags::DataAvailable) CheckDefaultInitialization(); }
void UMMOPawnExtensionComponent::CheckDefaultInitialization()
{
    if (!HasBegunPlay() || bCheckingInitialization) return;
    TGuardValue<bool> Guard(bCheckingInitialization, true);
    CheckDefaultInitializationForImplementers();
    ContinueInitStateChain({MMOInitTags::Spawned, MMOInitTags::DataAvailable, MMOInitTags::DataInitialized, MMOInitTags::GameplayReady});
    if (IsPawnReady() && !bReadyAnnounced) { bReadyAnnounced = true; OnGameplayReady.Broadcast(); }
}
