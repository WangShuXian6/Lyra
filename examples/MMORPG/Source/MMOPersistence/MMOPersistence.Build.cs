using UnrealBuildTool;
public class MMOPersistence : ModuleRules
{
    public MMOPersistence(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new[] { "Core", "Json", "MMOContracts" });
        PrivateDependencyModuleNames.Add("MMOThirdParty");
    }
}
