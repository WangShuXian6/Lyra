#pragma once
#include "CoreMinimal.h"
#include "Components/GameStateComponent.h"
#include "Engine/AssetManagerTypes.h"
#include "MMOExperienceManagerComponent.generated.h"

class UMMOExperienceDefinition;
class UMMOPawnData;
class UGameFeatureAction;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class EMMOExperienceLoadState : uint8 { Unloaded, LoadingAssets, LoadingGameFeatures, ExecutingActions, Ready, Failed, Deactivating };
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMMOExperienceReady, const UMMOExperienceDefinition*);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMMOExperienceReadyEvent, const UMMOExperienceDefinition*, Experience);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMMOExperienceFailureEvent, const FString&, Reason);

/** Server selects an ID; each world loads its dependencies and activates features before broadcasting Ready. */
UCLASS(ClassGroup=(MMO), meta=(BlueprintSpawnableComponent))
class MMOFRAMEWORK_API UMMOExperienceManagerComponent : public UGameStateComponent
{
    GENERATED_BODY()
public:
    UMMOExperienceManagerComponent(const FObjectInitializer& Initializer);
    static UMMOExperienceManagerComponent* FindExperienceManager(const UWorld* World);
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="MMO|Experience") bool SetCurrentExperience(FPrimaryAssetId ExperienceId);
    UFUNCTION(BlueprintPure, Category="MMO|Experience") bool IsExperienceReady() const { return LoadState == EMMOExperienceLoadState::Ready; }
    UFUNCTION(BlueprintPure, Category="MMO|Experience") UMMOExperienceDefinition* GetCurrentExperience() const { return Experience; }
    UFUNCTION(BlueprintPure, Category="MMO|Experience") UMMOPawnData* GetPawnData() const;
    UFUNCTION(BlueprintPure, Category="MMO|Experience") FPrimaryAssetId GetSelectedExperienceId() const { return SelectedExperienceId; }
    void CallOrRegister_OnReady(FOnMMOExperienceReady::FDelegate Delegate);
    UPROPERTY(BlueprintReadOnly, Category="MMO|Experience") EMMOExperienceLoadState LoadState = EMMOExperienceLoadState::Unloaded;
    UPROPERTY(BlueprintReadOnly, Category="MMO|Experience") FString FailureReason;
    UPROPERTY(BlueprintAssignable, Category="MMO|Experience") FMMOExperienceReadyEvent OnExperienceReady;
    UPROPERTY(BlueprintAssignable, Category="MMO|Experience") FMMOExperienceFailureEvent OnExperienceFailed;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void OnRep_SelectedExperienceId();
    void StartExperienceLoad();
    void LoadDependencyClosure();
    void ActivateFeatures();
    void FinishActivation();
    void Fail(const FString& Reason);
    void ReleaseExperience();
    UPROPERTY(ReplicatedUsing=OnRep_SelectedExperienceId) FPrimaryAssetId SelectedExperienceId;
    UPROPERTY(Transient) TObjectPtr<UMMOExperienceDefinition> Experience;
    UPROPERTY(Transient) TArray<TObjectPtr<UGameFeatureAction>> ActivatedActions;
    TArray<TSharedPtr<FStreamableHandle>> LoadHandles;
    TSet<FSoftObjectPath> RequestedPaths;
    TArray<FString> AcquiredPluginURLs;
    FOnMMOExperienceReady ReadyCallbacks;
    FTimerHandle LoadTimeout;
    int32 PendingPlugins = 0;
};
