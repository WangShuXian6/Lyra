using UnrealBuildTool;

public class MMODocTools : ModuleRules
{
    public MMODocTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "MMOBackendClient", "MMOFramework", "UnrealEd", "BlueprintGraph", "Kismet", "KismetCompiler", "AssetTools", "Json",
            "UMG", "UMGEditor", "SlateCore", "CommonUI", "ModelViewViewModel", "ModelViewViewModelBlueprint", "ModelViewViewModelEditor",
            "AnimGraph", "AnimGraphRuntime", "GameplayTags", "EnhancedInput", "InputCore"
        });
    }
}
