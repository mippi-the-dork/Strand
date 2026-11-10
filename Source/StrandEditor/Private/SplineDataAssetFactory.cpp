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
    USplineDataAsset* Asset = NewObject<USplineDataAsset>(InParent, InClass, InName, Flags | RF_Transactional);
    // A new authored spline is immediately visible/editable in its dedicated
    // 2D/3D viewport, rather than presenting an empty generic data array.
    FSplinePointData First;
    First.LocalPosition = FVector::ZeroVector;
    First.PointType = ESplinePointType::Curve;
    FSplinePointData Second = First;
    Second.LocalPosition = FVector(100.0, 0.0, 0.0);
    Asset->Points.Add(First);
    Asset->Points.Add(Second);
    return Asset;
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
