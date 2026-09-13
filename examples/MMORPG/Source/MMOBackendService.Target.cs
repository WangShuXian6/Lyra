using UnrealBuildTool;
[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class MMOBackendServiceTarget : TargetRules
{
    public MMOBackendServiceTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Program;
        LinkType = TargetLinkType.Monolithic;
        LaunchModuleName = "MMOBackendService";
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        bBuildDeveloperTools = false;
        bBuildWithEditorOnlyData = false;
        bCompileAgainstEngine = false;
        bCompileAgainstCoreUObject = false;
        bCompileAgainstApplicationCore = false;
        bCompileICU = false;
        bIsBuildingConsoleApplication = true;
        bCompileWithPluginSupport = false;
        // Build without the gameplay sample's optional local copies of Lyra plugins.
        DisablePlugins.AddRange(new[] { "GameplayAbilities", "EnhancedInput", "ModularGameplay", "ModularGameplayActors",
            "GameFeatures", "CommonUI", "CommonGame", "CommonUser", "UIExtension", "ModelViewViewModel",
            "MMOIntegration", "MMOCore", "PythonScriptPlugin", "EditorScriptingUtilities" });
    }
}
