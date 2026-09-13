#pragma once

#include "Animation/AnimInstance.h"
#include "GameplayEffectTypes.h"
#include "MMOAnimInstance.generated.h"

class UAbilitySystemComponent;

/** Keep the property map at a stable address, as in LyraAnimInstance. */
USTRUCT()
struct FMMOAnimationTagPropertyMap : public FGameplayTagBlueprintPropertyMap
{
    GENERATED_BODY()
    void ResetBindings() { Unregister(); }
};

/** Game-thread data snapshot; editable AnimBlueprint graphs evaluate this data on workers. */
UCLASS(Blueprintable)
class MMORPG_API UMMOAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    UMMOAnimInstance();
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUninitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category="MMO|Animation")
    void InitializeWithAbilitySystem(UAbilitySystemComponent* AbilitySystem);

    UPROPERTY(BlueprintReadOnly, Category="MMO|Locomotion")
    float GroundSpeed = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="MMO|Locomotion")
    bool bIsFalling = false;
    UPROPERTY(BlueprintReadOnly, Category="MMO|Gameplay Tags")
    bool bSpellOnCooldown = false;
protected:
    UPROPERTY(EditDefaultsOnly, Category="MMO|Gameplay Tags")
    FMMOAnimationTagPropertyMap GameplayTagPropertyMap;
private:
    TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
};
