using UnrealBuildTool;
public class MMOBackendServer : ModuleRules
{
    public MMOBackendServer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "HTTP", "Json", "MMOContracts" });
    }
}
