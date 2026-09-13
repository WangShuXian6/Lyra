using UnrealBuildTool;
using System;
using System.IO;
public class MMOThirdParty : ModuleRules
{
    public MMOThirdParty(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        if (Target.Type != TargetType.Program || Target.Platform != UnrealTargetPlatform.Win64)
            throw new BuildException("MMOThirdParty is currently Windows Backend Program only; never add it to a game module.");
        string Pg = Environment.GetEnvironmentVariable("MMO_POSTGRES_ROOT") ?? "D:/program/1-web/PostgreSQL/18";
        string Sodium = Environment.GetEnvironmentVariable("MMO_SODIUM_ROOT") ?? Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../.deps/sodium/libsodium"));
        PublicSystemIncludePaths.Add(Path.Combine(Pg, "include"));
        PublicSystemIncludePaths.Add(Path.Combine(Sodium, "include"));
        PublicAdditionalLibraries.Add(Path.Combine(Pg, "lib/libpq.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(Sodium, "x64/Release/v143/dynamic/libsodium.lib"));
        RuntimeDependencies.Add("$(TargetOutputDir)/libsodium.dll", Path.Combine(Sodium, "x64/Release/v143/dynamic/libsodium.dll"));
        foreach (string Dll in new[] {"libpq.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll", "libintl-9.dll", "libiconv-2.dll"})
            RuntimeDependencies.Add("$(TargetOutputDir)/" + Dll, Path.Combine(Pg, "bin", Dll));
    }
}
