#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"

struct FSplineImportedCurve
{
    FString Name;
    FString SourceId;
    TArray<FSplinePointData> Points;
    bool bClosedLoop = false;
};

namespace SplineAssetImport
{
    /** Parse source without changing any UObjects, suitable for safe reimport. */
    /** bMirrorX defaults to the top-down viewport-friendly SVG orientation. */
    bool LoadSvgCurves(const FString& Filename, TArray<FSplineImportedCurve>& Out, bool bMirrorX = true);
    bool LoadAlembicCurves(const FString& Filename, const USplineDataAsset* Settings, TArray<FSplineImportedCurve>& Out);

    /** Preserves disconnected source curves by creating sibling assets, never splicing them. */
    USplineDataAsset* CommitCurves(UObject* Parent, FName RequestedName, EObjectFlags Flags,
        TArray<FSplineImportedCurve>& Curves, const FString& SourceFile);
}
