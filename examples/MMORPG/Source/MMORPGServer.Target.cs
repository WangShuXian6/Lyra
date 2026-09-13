using UnrealBuildTool;
public class MMORPGServerTarget : TargetRules
{
    public MMORPGServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("MMORPG");
    }
}

