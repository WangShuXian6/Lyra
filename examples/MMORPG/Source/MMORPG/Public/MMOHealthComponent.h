#pragma once

#include "Components/ActorComponent.h"
#include "GameplayEffect.h"
#include "MMOHealthComponent.generated.h"

class UAbilitySystemComponent;
class UMMOPawnExtensionComponent;
struct FOnAttributeChangeData;

/** Pawn-scoped observer of the PlayerState's GAS health; identity and grants remain on PlayerState. */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class MMORPG_API UMMOHealthComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMMOHealthComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION(BlueprintPure, Category="MMO|Health") bool IsDead() const { return bIsDead; }
private:
    UFUNCTION() void BindAbilitySystem();
    UFUNCTION() void UnbindAbilitySystem();
    UFUNCTION() void OnRep_IsDead();
    void HealthChanged(const FOnAttributeChangeData& Change);
    UPROPERTY(ReplicatedUsing=OnRep_IsDead) bool bIsDead = false;
    UPROPERTY() TObjectPtr<UMMOPawnExtensionComponent> PawnExtension;
    TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
    FDelegateHandle HealthHandle;
};

/** A surviving training target retaliates with a genuine instant GAS health modifier. */
UCLASS()
class MMORPG_API UMMOTargetRetaliation : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UMMOTargetRetaliation();
};
