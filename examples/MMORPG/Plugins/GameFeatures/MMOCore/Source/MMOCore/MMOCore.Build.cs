using UnrealBuildTool;
public class MMOCore : ModuleRules
{
    public MMOCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameFeatures", "ModularGameplayActors", "MMORPG" });
    }
}

