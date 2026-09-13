#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NativeGameplayTags.h"
#include "MMOFrameworkSubsystem.generated.h"

namespace MMOInitTags
{
    MMOFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Spawned);
    MMOFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DataAvailable);
    MMOFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DataInitialized);
    MMOFRAMEWORK_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayReady);
}

/** Registers the ordered states once per GameInstance, including each separate PIE world context. */
UCLASS()
class MMOFRAMEWORK_API UMMOFrameworkSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
};
