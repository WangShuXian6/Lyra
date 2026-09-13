using UnrealBuildTool;
public class MMOBackendService : ModuleRules
{
    public MMOBackendService(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePathModuleNames.Add("Launch");
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "Projects", "Json", "HTTPServer", "MMOContracts", "MMOPersistence" });
    }
}
