#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMODocToolsLibrary.generated.h"

class UBlueprint;
class UWidgetBlueprint;
class UStringTable;

UCLASS()
class MMODOCTOOLS_API UMMODocToolsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Constructs the health tutorial, compiles it, exports native text, imports a fresh duplicate, and returns JSON evidence. Does not save assets or execute HTTP. */
    UFUNCTION(BlueprintCallable, Category = "MMO Documentation|Editor")
    static FString ConfigureBackendTutorial(UBlueprint* Blueprint);
    /** Builds a real UMG Designer tree and two manual-source MVVM editor bindings, then compiles. */
    UFUNCTION(BlueprintCallable, Category = "MMO Documentation|Editor")
    static FString ConfigureManaStatus(UWidgetBlueprint* Blueprint);
    /** Editor-only: writes a real Designer tree, localized text and MVVM bindings; compiles but does not save or run gameplay. */
    UFUNCTION(BlueprintCallable, Category = "MMO Documentation|Editor")
    static FString ConfigureDesignerWidget(UWidgetBlueprint* Blueprint);
    /** Populates the original ST_MMO asset from the stable English/source translation manifest. */
    UFUNCTION(BlueprintCallable, Category = "MMO Documentation|Editor")
    static FString ConfigureDesignerStrings(UStringTable* StringTable);
};
