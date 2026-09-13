#include "MMOAbilitySet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AttributeSet.h"
#include "GameplayEffect.h"

void FMMOGrantedAbilityHandles::TakeFromAbilitySystem(UAbilitySystemComponent* ASC)
{
    if (!ASC || !ASC->IsOwnerActorAuthoritative() || GrantedASC.Get() != ASC) return;
    for (const auto& Handle : Abilities) { ASC->CancelAbilityHandle(Handle); ASC->ClearAbility(Handle); }
    for (const auto& Handle : Effects) ASC->RemoveActiveGameplayEffect(Handle);
    for (UAttributeSet* Set : AttributeSets) if (Set) ASC->RemoveSpawnedAttribute(Set);
    Abilities.Reset(); Effects.Reset(); AttributeSets.Reset(); GrantedASC.Reset();
}

void UMMOAbilitySet::CollectAssetPaths(TArray<FSoftObjectPath>& OutPaths) const
{
    for (const auto& Item : GrantedAbilities) if (!Item.Ability.IsNull()) OutPaths.AddUnique(Item.Ability.ToSoftObjectPath());
    for (const auto& Item : GrantedEffects) if (!Item.Effect.IsNull()) OutPaths.AddUnique(Item.Effect.ToSoftObjectPath());
    for (const auto& Item : GrantedAttributes) if (!Item.IsNull()) OutPaths.AddUnique(Item.ToSoftObjectPath());
}

bool UMMOAbilitySet::GrantToAbilitySystem(UAbilitySystemComponent* ASC, UObject* SourceObject, FMMOGrantedAbilityHandles& OutHandles) const
{
    if (!ASC || !ASC->IsOwnerActorAuthoritative() || OutHandles.IsGranted()) return false;
    // Preflight the entire set. Grant never synchronously loads on the game thread or partially accepts missing classes.
    for (const auto& Item : GrantedAbilities) if (!Item.Ability.Get() || Item.Level < 1) return false;
    for (const auto& Item : GrantedEffects) if (!Item.Effect.Get() || Item.Level < 0) return false;
    for (const auto& Item : GrantedAttributes) if (!Item.Get()) return false;
    OutHandles.GrantedASC = ASC;
    for (const auto& Item : GrantedAttributes)
    {
        UAttributeSet* Set = NewObject<UAttributeSet>(ASC->GetOwnerActor(), Item.Get());
        ASC->AddAttributeSetSubobject(Set); OutHandles.AttributeSets.Add(Set);
    }
    for (const auto& Item : GrantedAbilities)
    {
        FGameplayAbilitySpec Spec(Item.Ability.Get(), Item.Level, Item.InputID);
        Spec.SourceObject = SourceObject;
        if (Item.InputTag.IsValid()) Spec.GetDynamicSpecSourceTags().AddTag(Item.InputTag);
        OutHandles.Abilities.Add(ASC->GiveAbility(Spec));
    }
    for (const auto& Item : GrantedEffects)
    {
        FGameplayEffectContextHandle Context = ASC->MakeEffectContext(); Context.AddSourceObject(SourceObject);
        const auto Handle = ASC->ApplyGameplayEffectToSelf(Item.Effect.Get()->GetDefaultObject<UGameplayEffect>(), Item.Level, Context);
        if (Handle.IsValid()) OutHandles.Effects.Add(Handle);
    }
    return true;
}
