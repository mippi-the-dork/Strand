// Copyright Mippithedork 2026, Inc. All Rights Reserved.

using UnrealBuildTool;

public class StrandPCG : ModuleRules
{
    public StrandPCG(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Strand", "PCG" });
    }
}
