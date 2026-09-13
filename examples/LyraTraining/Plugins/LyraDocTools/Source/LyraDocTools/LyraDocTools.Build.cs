using UnrealBuildTool;

public class LyraDocTools : ModuleRules
{
    public LyraDocTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "LyraGame", "GameFeatures", "GameplayAbilities"
        });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "Kismet", "KismetCompiler", "BlueprintGraph", "GameplayAbilitiesEditor", "Json", "NavigationSystem"
        });
    }
}
