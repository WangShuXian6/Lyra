#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MMOPawnData.generated.h"

class APawn;
class UMMOAbilitySet;
class UMMOInputConfig;
class UInputMappingContext;
class UAnimInstance;

UCLASS(BlueprintType, Const)
class MMOFRAMEWORK_API UMMOPawnData : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Pawn", meta=(AssetBundles="Client,Server")) TSoftClassPtr<APawn> PawnClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities", meta=(AssetBundles="Client,Server")) TArray<TSoftObjectPtr<UMMOAbilitySet>> AbilitySets;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input", meta=(AssetBundles="Client")) TSoftObjectPtr<UInputMappingContext> InputMappingContext;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input", meta=(AssetBundles="Client")) TSoftObjectPtr<UMMOInputConfig> InputConfig;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Animation", meta=(AssetBundles="Client")) TSoftClassPtr<UAnimInstance> AnimInstanceClass;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("MMOPawnData"), GetFName()); }
};
