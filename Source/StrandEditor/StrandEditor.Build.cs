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
            "AssetRegistry", "ContentBrowser", "PlacementMode", "PropertyEditor", "DetailCustomizations", "ToolMenus", "LevelEditor",
            "EditorFramework", "Landscape", "Projects", "ApplicationCore", "DesktopPlatform", "AlembicLib", "AlembicLibrary"
        });
    }
}
