#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMODocExperienceTools.generated.h"
class UDataAsset;
UCLASS()
class MMODOCTOOLS_API UMMODocExperienceTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="MMO Documentation|Editor")
    static FString ConfigureExperienceAssets(UDataAsset* Experience, UDataAsset* PawnData, UDataAsset* AbilitySet);
    /** Precisely edits only InputTag/InputID of the existing two tutorial ability grants; never saves. */
    UFUNCTION(BlueprintCallable, Category="MMO Documentation|Editor")
    static FString ConfigureTaggedAbilityInputs(UObject* AbilityAsset);
    UFUNCTION(BlueprintCallable, Category="MMO Documentation|Editor")
    static FString RecompileGameplayBlueprints();
};
