using UnrealBuildTool;
public class MMORPGClientTarget : TargetRules
{
    public MMORPGClientTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Client;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("MMORPG");
    }
}

