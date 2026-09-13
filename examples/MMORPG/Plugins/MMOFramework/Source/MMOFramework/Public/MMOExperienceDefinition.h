#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFeatureAction.h"
#include "MMOExperienceDefinition.generated.h"

class UMMOPawnData;

/** Reusable world-scoped component/UI actions; configure on a Data Asset, not the level blueprint. */
UCLASS(BlueprintType, Const)
class MMOFRAMEWORK_API UMMOExperienceActionSet : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Experience") TArray<FString> GameFeaturesToEnable;
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Experience") TArray<TObjectPtr<UGameFeatureAction>> Actions;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("MMOActionSet"), GetFName()); }
};

/** A selected composition. Backend identity and character snapshots stay outside this asset. */
UCLASS(BlueprintType, Const)
class MMOFRAMEWORK_API UMMOExperienceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Experience", meta=(AssetBundles="Client,Server"))
    TSoftObjectPtr<UMMOPawnData> DefaultPawnData;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Experience") TArray<FString> GameFeaturesToEnable;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Experience", meta=(AssetBundles="Client,Server"))
    TArray<TSoftObjectPtr<UMMOExperienceActionSet>> ActionSets;
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Experience") TArray<TObjectPtr<UGameFeatureAction>> Actions;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("MMOExperience"), GetFName()); }
};
