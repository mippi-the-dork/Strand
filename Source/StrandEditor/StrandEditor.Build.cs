using UnrealBuildTool;

public class StrandEditor : ModuleRules
{
    public StrandEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Alembic C++ SDK signals malformed archives with standard exceptions.
        bEnableExceptions = true;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Strand" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd", "InputCore", "Slate", "SlateCore", "XmlParser", "AssetTools",
            "AssetRegistry", "ContentBrowser", "PlacementMode", "PropertyEditor", "ToolMenus", "LevelEditor",
            "EditorFramework", "Landscape", "Projects", "ApplicationCore", "DesktopPlatform", "AlembicLib", "AlembicLibrary"
        });
        // FBX is shipped with Unreal as an engine ThirdParty static library, not a normal module.
        AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
    }
}
