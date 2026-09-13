#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "MMOAbilitySet.generated.h"

class UAbilitySystemComponent;
class UAttributeSet;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct MMOFRAMEWORK_API FMMOAbilityGrant
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSoftClassPtr<UGameplayAbility> Ability;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="1")) int32 Level = 1;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) int32 InputID = INDEX_NONE;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(Categories="InputTag")) FGameplayTag InputTag;
};

USTRUCT(BlueprintType)
struct MMOFRAMEWORK_API FMMOEffectGrant
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSoftClassPtr<UGameplayEffect> Effect;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0")) float Level = 1.f;
};

/** Store on PlayerState/Equipment owner. One handle set represents one grant operation. */
USTRUCT()
struct MMOFRAMEWORK_API FMMOGrantedAbilityHandles
{
    GENERATED_BODY()
    UPROPERTY() TArray<FGameplayAbilitySpecHandle> Abilities;
    UPROPERTY() TArray<FActiveGameplayEffectHandle> Effects;
    UPROPERTY() TArray<TObjectPtr<UAttributeSet>> AttributeSets;
    UPROPERTY() TWeakObjectPtr<UAbilitySystemComponent> GrantedASC;
    bool IsGranted() const { return GrantedASC.IsValid(); }
    void TakeFromAbilitySystem(UAbilitySystemComponent* ASC);
};

UCLASS(BlueprintType, Const)
class MMOFRAMEWORK_API UMMOAbilitySet : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities", meta=(AssetBundles="Client,Server")) TArray<FMMOAbilityGrant> GrantedAbilities;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities", meta=(AssetBundles="Client,Server")) TArray<FMMOEffectGrant> GrantedEffects;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities", meta=(AssetBundles="Client,Server")) TArray<TSoftClassPtr<UAttributeSet>> GrantedAttributes;
    bool GrantToAbilitySystem(UAbilitySystemComponent* ASC, UObject* SourceObject, FMMOGrantedAbilityHandles& OutHandles) const;
    void CollectAssetPaths(TArray<FSoftObjectPath>& OutPaths) const;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("MMOAbilitySet"), GetFName()); }
};
