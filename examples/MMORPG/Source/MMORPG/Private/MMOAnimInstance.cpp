#include "MMOAnimInstance.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"

UMMOAnimInstance::UMMOAnimInstance()
{
    bUseMultiThreadedAnimationUpdate = true;
}

void UMMOAnimInstance::InitializeWithAbilitySystem(UAbilitySystemComponent* AbilitySystem)
{
    check(IsInGameThread());
    if (BoundASC.Get() == AbilitySystem) return;
    GameplayTagPropertyMap.ResetBindings();
    BoundASC = AbilitySystem;
    bSpellOnCooldown = false;
    if (AbilitySystem) GameplayTagPropertyMap.Initialize(this, AbilitySystem);
}

void UMMOAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    GroundSpeed = 0.f;
    bIsFalling = false;
}

void UMMOAnimInstance::NativeUninitializeAnimation()
{
    // Explicitly release this avatar's delegates even when PlayerState survives a respawn.
    GameplayTagPropertyMap.ResetBindings();
    BoundASC.Reset();
    Super::NativeUninitializeAnimation();
}

void UMMOAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    // NativeUpdateAnimation runs on the game thread before parallel graph evaluation.
    // No actor, movement or ASC reads occur in worker-thread Blueprint evaluation.
    check(IsInGameThread());
    const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
    GroundSpeed = Character ? Character->GetVelocity().Size2D() : 0.f;
    bIsFalling = Character && Character->GetCharacterMovement()->IsFalling();
    APlayerState* State = Character ? Character->GetPlayerState() : nullptr;
    // Replicated PlayerState can arrive after the mesh initializes. Rebind only when identity changes.
    UAbilitySystemComponent* ASC = State ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(State) : nullptr;
    if (ASC && ASC->GetAvatarActor() != Character) ASC = nullptr;
    InitializeWithAbilitySystem(ASC);
}
