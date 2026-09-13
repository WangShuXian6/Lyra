using UnrealBuildTool;
public class MMOContracts : ModuleRules
{
    public MMOContracts(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new[] { "Core", "Json" });
    }
}
