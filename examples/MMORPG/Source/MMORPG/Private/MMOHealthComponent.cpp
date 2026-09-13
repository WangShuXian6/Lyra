#include "MMOHealthComponent.h"
#include "MMOGameplay.h"
#include "MMOPawnExtensionComponent.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MMODead, "Status.MMO.Dead");

UMMOHealthComponent::UMMOHealthComponent()
{ SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = false; }
void UMMOHealthComponent::BeginPlay()
{
    Super::BeginPlay();
    PawnExtension = GetOwner()->FindComponentByClass<UMMOPawnExtensionComponent>();
    if (ensure(PawnExtension))
    {
        PawnExtension->OnAbilitySystemInitialized.AddDynamic(this, &ThisClass::BindAbilitySystem);
        PawnExtension->OnAbilitySystemUninitialized.AddDynamic(this, &ThisClass::UnbindAbilitySystem);
        BindAbilitySystem();
    }
}
void UMMOHealthComponent::EndPlay(EEndPlayReason::Type Reason)
{
    UnbindAbilitySystem();
    if (PawnExtension)
    {
        PawnExtension->OnAbilitySystemInitialized.RemoveDynamic(this, &ThisClass::BindAbilitySystem);
        PawnExtension->OnAbilitySystemUninitialized.RemoveDynamic(this, &ThisClass::UnbindAbilitySystem);
    }
    Super::EndPlay(Reason);
}
void UMMOHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UMMOHealthComponent, bIsDead); }
void UMMOHealthComponent::BindAbilitySystem()
{
    UnbindAbilitySystem();
    BoundASC = PawnExtension ? PawnExtension->GetAbilitySystemComponent() : nullptr;
    if (BoundASC.IsValid())
        HealthHandle = BoundASC->GetGameplayAttributeValueChangeDelegate(UMMOAttributes::GetHealthAttribute()).AddUObject(this, &ThisClass::HealthChanged);
}
void UMMOHealthComponent::UnbindAbilitySystem()
{
    if (BoundASC.IsValid()) BoundASC->GetGameplayAttributeValueChangeDelegate(UMMOAttributes::GetHealthAttribute()).Remove(HealthHandle);
    BoundASC.Reset(); HealthHandle.Reset();
}
void UMMOHealthComponent::HealthChanged(const FOnAttributeChangeData& Change)
{
    auto* Character = Cast<AMMOCharacter>(GetOwner());
    if (!Character || !Character->HasAuthority() || bIsDead || Change.NewValue > 0 || !BoundASC.IsValid()
        || BoundASC->GetAvatarActor() != Character) return;
    bIsDead = true;
    BoundASC->AddLooseGameplayTag(TAG_MMODead);
    BoundASC->CancelAllAbilities();
    OnRep_IsDead(); Character->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("MMO_PLAYER_DIED pawn=%s health=%.1f"), *Character->GetName(), Change.NewValue);
    if (auto* GameMode = GetWorld()->GetAuthGameMode<AMMOGameMode>()) GameMode->HandlePlayerDeath(Character);
}
void UMMOHealthComponent::OnRep_IsDead()
{
    auto* Character = Cast<AMMOCharacter>(GetOwner());
    if (!bIsDead || !Character) return;
    Character->GetCharacterMovement()->StopMovementImmediately();
    Character->GetCharacterMovement()->DisableMovement();
    Character->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (Character->GetNetMode() != NM_DedicatedServer && Character->GetMesh()->GetPhysicsAsset())
    {
        Character->GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
        Character->GetMesh()->SetSimulatePhysics(true);
    }
}
UMMOTargetRetaliation::UMMOTargetRetaliation()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo Modifier; Modifier.Attribute = UMMOAttributes::GetHealthAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive; Modifier.ModifierMagnitude = FScalableFloat(-25.f);
    Modifiers.Add(Modifier);
}
