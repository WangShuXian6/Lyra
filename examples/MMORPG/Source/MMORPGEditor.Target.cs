using UnrealBuildTool;
public class MMORPGEditorTarget : TargetRules
{
    public MMORPGEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("MMORPG");
    }
}

