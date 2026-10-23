using UnrealBuildTool;

public class Strand : ModuleRules
{
    public Strand(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        // Editor-only billboard texture is loaded from plugin Resources. Runtime builds
        // retain only the native spline; they do not use plugin resource APIs.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("Projects");
        }
    }
}
