#include "MMOAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"

TArray<FMMOGrantedAbilityInput> UMMOAbilitySystemComponent::GetGrantedAbilitiesForInputTag(const FGameplayTag& InputTag)
{
    TArray<FMMOGrantedAbilityInput> Matches;
    if (!InputTag.IsValid()) return Matches;
    ABILITYLIST_SCOPE_LOCK();
    for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
    {
        if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag)) continue;
        auto& Match = Matches.AddDefaulted_GetRef();
        Match.Handle = Spec.Handle;
        Match.InputID = Spec.InputID;
        Match.AbilityClassPath = Spec.Ability->GetClass()->GetPathName();
    }
    return Matches;
}

bool UMMOAbilitySystemComponent::TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag)
{
    if (!IsOwnerActorAuthoritative() || !InputTag.IsValid()) return false;
    TArray<FGameplayAbilitySpecHandle> Handles;
    {
        ABILITYLIST_SCOPE_LOCK();
        for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
            if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag)) Handles.Add(Spec.Handle);
    }
    // Activation may invoke callbacks that add or remove specs. The list lock and traversal have ended.
    bool bAnyActivated = false;
    for (const FGameplayAbilitySpecHandle Handle : Handles)
        if (TryActivateAbility(Handle)) bAnyActivated = true;
    return bAnyActivated;
}
