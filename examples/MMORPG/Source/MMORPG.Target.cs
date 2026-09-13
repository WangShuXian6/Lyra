using UnrealBuildTool;
public class MMORPGTarget : TargetRules
{
    public MMORPGTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("MMORPG");
    }
}

