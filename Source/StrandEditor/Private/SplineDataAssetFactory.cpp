#include "SplineDataAssetFactory.h"
#include "SplineDataAsset.h"
#include "Misc/Paths.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

USplineDataAssetFactory::USplineDataAssetFactory()
{
    SupportedClass = USplineDataAsset::StaticClass();
    bCreateNew = true;
    bEditAfterNew = true;
    bEditorImport = false;
}

uint32 USplineDataAssetFactory::GetMenuCategories() const
{
    return static_cast<uint32>(FAssetToolsModule::GetModule().Get().FindAdvancedAssetCategory(
        FName(TEXT("Strand"))));
}

UObject* USplineDataAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
    EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
    return NewObject<USplineDataAsset>(InParent, InClass, InName, Flags | RF_Transactional);
}

USplineSvgFactory::USplineSvgFactory()
{
    SupportedClass = USplineDataAsset::StaticClass();
    bCreateNew = false;
    bEditorImport = true;
    bText = true;
    Formats.Add(TEXT("svg;Spline Vector (SVG)"));
    ImportPriority = 100;
}

bool USplineSvgFactory::FactoryCanImport(const FString& Filename)
{
    return FPaths::GetExtension(Filename).Equals(TEXT("svg"), ESearchCase::IgnoreCase);
}

USplineFbxFactory::USplineFbxFactory()
{
    SupportedClass = USplineDataAsset::StaticClass();
    bCreateNew = false;
    bEditorImport = true;
    bText = false;
    Formats.Add(TEXT("fbx;Strand Legacy Curves (FBX)"));
    ImportPriority = -200; // Explicit legacy compatibility: do not take over ordinary FBX imports.
}

bool USplineFbxFactory::FactoryCanImport(const FString& Filename)
{
    return FPaths::GetExtension(Filename).Equals(TEXT("fbx"), ESearchCase::IgnoreCase);
}

UStrandAlembicFactory::UStrandAlembicFactory()
{
    SupportedClass = USplineDataAsset::StaticClass();
    bCreateNew = false;
    bEditorImport = true;
    bText = false;
    Formats.Add(TEXT("abc;Strand Curves (Alembic)"));
    ImportPriority = -100; // Do not steal ordinary Alembic mesh or Groom imports.
}

bool UStrandAlembicFactory::FactoryCanImport(const FString& Filename)
{
    return FPaths::GetExtension(Filename).Equals(TEXT("abc"), ESearchCase::IgnoreCase);
}

UStrandReimportFactory::UStrandReimportFactory()
{
    SupportedClass = USplineDataAsset::StaticClass();
    bCreateNew = false;
    bEditorImport = false;
}
