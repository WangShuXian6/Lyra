#pragma once
#include "CoreMinimal.h"
#include "Components/PawnComponent.h"
#include "Components/GameFrameworkInitStateInterface.h"
#include "MMOPawnExtensionComponent.generated.h"

class UAbilitySystemComponent;
class UMMOPawnData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMMOPawnLifecycleEvent);

/** Coordinates replicated data, controller, input and ASC readiness; does not grant duplicate player abilities. */
UCLASS(ClassGroup=(MMO), meta=(BlueprintSpawnableComponent))
class MMOFRAMEWORK_API UMMOPawnExtensionComponent : public UPawnComponent, public IGameFrameworkInitStateInterface
{
    GENERATED_BODY()
public:
    UMMOPawnExtensionComponent(const FObjectInitializer& Initializer);
    static const FName FeatureName;
    virtual FName GetFeatureName() const override { return FeatureName; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool CanChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag Current, FGameplayTag Desired) const override;
    virtual void HandleChangeInitState(UGameFrameworkComponentManager* Manager, FGameplayTag Current, FGameplayTag Desired) override;
    virtual void OnActorInitStateChanged(const FActorInitStateChangedParams& Params) override;
    virtual void CheckDefaultInitialization() override;
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="MMO|Pawn") bool SetPawnData(UMMOPawnData* InData);
    UFUNCTION(BlueprintPure, Category="MMO|Pawn") UMMOPawnData* GetPawnData() const { return PawnData; }
    UFUNCTION(BlueprintPure, Category="MMO|Pawn") UAbilitySystemComponent* GetAbilitySystemComponent() const { return AbilitySystem; }
    UFUNCTION(BlueprintPure, Category="MMO|Pawn") bool IsPawnReady() const;
    UFUNCTION(BlueprintCallable, Category="MMO|Pawn") bool InitializeAbilitySystem(UAbilitySystemComponent* InASC, AActor* OwnerActor);
    UFUNCTION(BlueprintCallable, Category="MMO|Pawn") void UninitializeAbilitySystem();
    void HandleControllerChanged();
    void HandlePlayerStateReplicated();
    void SetupPlayerInputComponent();
    UPROPERTY(BlueprintAssignable, Category="MMO|Pawn") FMMOPawnLifecycleEvent OnDataAvailable;
    UPROPERTY(BlueprintAssignable, Category="MMO|Pawn") FMMOPawnLifecycleEvent OnAbilitySystemInitialized;
    UPROPERTY(BlueprintAssignable, Category="MMO|Pawn") FMMOPawnLifecycleEvent OnAbilitySystemUninitialized;
    UPROPERTY(BlueprintAssignable, Category="MMO|Pawn") FMMOPawnLifecycleEvent OnGameplayReady;
protected:
    virtual void OnRegister() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void OnRep_PawnData();
    UPROPERTY(ReplicatedUsing=OnRep_PawnData) TObjectPtr<UMMOPawnData> PawnData;
    UPROPERTY(Transient) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    bool bInputReady = false;
    bool bReadyAnnounced = false;
    bool bCheckingInitialization = false;
};
