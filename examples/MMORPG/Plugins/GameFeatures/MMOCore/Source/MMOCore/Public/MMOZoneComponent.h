#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMOZoneComponent.generated.h"

/** Added to ModularGameStateBase by MMOCore's AddComponents action. */
UCLASS(meta=(BlueprintSpawnableComponent))
class MMOCORE_API UMMOZoneComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UMMOZoneComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    // A reflected CDO reference makes the runtime-generated floor a declared Cook dependency.
    UPROPERTY() TObjectPtr<class UStaticMesh> ZoneMesh;
    UPROPERTY() TArray<TObjectPtr<AActor>> Spawned;
};

class UGameFeatureData;
/** Typed editor bridge because GameFeatureComponentEntry has no Python wrapper in this engine. */
UCLASS()
class MMOCORE_API UMMOContentLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="MMO|Editor") static void ConfigureFeatureData(UGameFeatureData* Data);
};
