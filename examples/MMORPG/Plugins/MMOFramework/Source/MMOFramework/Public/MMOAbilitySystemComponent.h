#pragma once

#include "AbilitySystemComponent.h"
#include "MMOAbilitySystemComponent.generated.h"

/** Value snapshot: callers never retain a pointer into the mutable ability spec array. */
struct MMOFRAMEWORK_API FMMOGrantedAbilityInput
{
    FGameplayAbilitySpecHandle Handle;
    int32 InputID = INDEX_NONE;
    FString AbilityClassPath;
};

/** Tag-based grant lookup shared by the MMO's server-authoritative, instant abilities. */
UCLASS()
class MMOFRAMEWORK_API UMMOAbilitySystemComponent : public UAbilitySystemComponent
{
    GENERATED_BODY()
public:
    /** Copies all matching grants while the ability list is locked. Does not load or grant abilities. */
    TArray<FMMOGrantedAbilityInput> GetGrantedAbilitiesForInputTag(const FGameplayTag& InputTag);

    /** Collects handles first, then activates outside the list traversal. Same-tag multiple grants are supported. */
    bool TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag);
};
