#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MMOInputConfig.generated.h"

class UInputAction;

USTRUCT(BlueprintType)
struct MMOFRAMEWORK_API FMMOTaggedInputAction
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input", meta=(AssetBundles="Client"))
    TSoftObjectPtr<UInputAction> InputAction;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input", meta=(Categories="InputTag"))
    FGameplayTag InputTag;
};

/** Maps semantic input tags to assets. Physical keys remain in the Mapping Context. */
UCLASS(BlueprintType, Const)
class MMOFRAMEWORK_API UMMOInputConfig : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input", meta=(TitleProperty="InputTag", AssetBundles="Client"))
    TArray<FMMOTaggedInputAction> NativeInputActions;

    /** Experience loads these assets before Ready. Missing/unloaded entries return nullptr. */
    UFUNCTION(BlueprintPure, Category="MMO|Input")
    UInputAction* FindInputActionForTag(FGameplayTag InputTag) const;

    void CollectAssetPaths(TArray<FSoftObjectPath>& OutPaths) const;

    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("MMOInputConfig"), GetFName());
    }
};
