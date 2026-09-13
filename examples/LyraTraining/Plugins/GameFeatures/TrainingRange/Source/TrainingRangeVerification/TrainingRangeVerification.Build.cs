using UnrealBuildTool;

public class TrainingRangeVerification : ModuleRules
{
    public TrainingRangeVerification(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        if (Target.Configuration == UnrealTargetConfiguration.Development && Target.Type != TargetType.Server)
        {
            PrivateDependencyModuleNames.AddRange(new[] {
                "LyraGame", "GameplayAbilities", "GameplayTags", "CommonUI", "CommonLoadingScreen", "UMG", "Json"
            });
        }
    }
}
