using UnrealBuildTool;
public class MMOBackendClient : ModuleRules
{
    public MMOBackendClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "HTTP", "Json", "MMOContracts" });
    }
}
