using UnrealBuildTool;

public class MMOFramework : ModuleRules
{
    public MMOFramework(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "GameplayAbilities", "GameplayTags",
            "GameplayTasks", "GameFeatures", "ModularGameplay", "EnhancedInput"
        });
    }
}
