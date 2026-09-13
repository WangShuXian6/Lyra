#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMOAnimationTools.generated.h"

class UBlueprint;

/** Editor-only reproducible graph construction. The resulting assets remain editable in Persona. */
UCLASS()
class MMODOCTOOLS_API UMMOAnimationTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="MMO Documentation|Editor")
    static FString PrepareCharacterAnimation();
    /** Compile and round-trip existing graphs without reconstructing their source nodes. */
    UFUNCTION(BlueprintCallable, Category="MMO Documentation|Editor")
    static FString ValidateCharacterAnimation();
    UFUNCTION(BlueprintCallable, Category="MMO Documentation|Editor")
    static FString ConfigureCharacter(UBlueprint* Blueprint);
};
