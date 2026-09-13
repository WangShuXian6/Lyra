using UnrealBuildTool;
public class MMORPG : ModuleRules
{
    public MMORPG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayAbilities", "GameplayTags", "GameplayTasks", "ModularGameplay", "ModularGameplayActors", "GameFeatures", "CommonUI", "CommonInput", "CommonGame", "UIExtension", "UMG", "Slate", "SlateCore", "ModelViewViewModel", "FieldNotification", "Json", "HTTP", "MMOBackendClient" });
        if (Target.Type != TargetType.Client) PrivateDependencyModuleNames.Add("MMOBackendServer");
        PublicDependencyModuleNames.Add("MMOFramework");
        PrivateDependencyModuleNames.Add("AnimGraphRuntime");
    }
}
