#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SubclassOf.h"
#include "LyraDocToolsLibrary.generated.h"

class UBlueprint;
class UGameFeatureData;
class ULyraAbilitySet;
class ULyraGameplayAbility;
class UWorld;
class ULyraExperienceDefinition;

/** Editor-only teaching helpers. Mutations are restricted to the isolated TrainingRange assets. */
UCLASS()
class LYRADOCTOOLS_API ULyraDocToolsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Returns entries matching Old or already New; -1 means invalid input/schema. Does not save. */
    UFUNCTION(BlueprintCallable, Category = "Lyra Documentation|Editor")
    static int32 ReplaceAbilityInSet(ULyraAbilitySet* Asset,
        TSubclassOf<ULyraGameplayAbility> Old, TSubclassOf<ULyraGameplayAbility> New);

    /** Upserts Experience and Map scan rules under /TrainingRange. Does not save or activate the feature. */
    UFUNCTION(BlueprintCallable, Category = "Lyra Documentation|Editor")
    static bool ConfigureTrainingFeature(UGameFeatureData* Data);

    /** Sets a training map's EditDefaultsOnly WorldSettings experience through native reflection. */
    UFUNCTION(BlueprintCallable, Category = "Lyra Documentation|Editor")
    static bool ConfigureTrainingWorld(UWorld* World, TSubclassOf<ULyraExperienceDefinition> Experience);

    /** Builds only a training editor World's navigation without graphical editor progress windows. Does not save. */
    UFUNCTION(BlueprintCallable, Category = "Lyra Documentation|Editor")
    static bool BuildTrainingNavigation(UWorld* World);

    /** Imports a complete graph into a duplicate under /TrainingRange/Tests/ and compiles with SkipSave. */
    UFUNCTION(BlueprintCallable, Category = "Lyra Documentation|Editor")
    static FString RoundTripBlueprint(UBlueprint* TestCopy, const FString& GraphName, const FString& ClipboardPath);

    /** Exports native node text and reports existing Blueprint status. Never compiles or saves Source. */
    UFUNCTION(BlueprintCallable, Category = "Lyra Documentation|Editor")
    static FString ExportBlueprintGraph(UBlueprint* Source, const FString& GraphName, const FString& OutputPath);
};
