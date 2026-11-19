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
    /** Default-argument form uses the project's canvas origin, for one-off SVG reads. */
    bool LoadSvgCurves(const FString& Filename, TArray<FSplineImportedCurve>& Out,
        bool bMirrorX = true, double ImportScale = 1.0);
    /** Reimport form uses the asset's saved canvas origin, unaffected by project setting changes. */
    bool LoadSvgCurves(const FString& Filename, TArray<FSplineImportedCurve>& Out,
        bool bMirrorX, double ImportScale, EStrandSvgCanvasOrigin CanvasOrigin);

    /** Apply uniform import scale to raw imported geometry, preserving Bezier shape and source layout. */
    bool ScaleSvgCurves(TArray<FSplineImportedCurve>& Curves, double Scale);
    bool LoadAlembicCurves(const FString& Filename, const USplineDataAsset* Settings, TArray<FSplineImportedCurve>& Out);

    /** Preserves disconnected source curves by creating sibling assets, never splicing them. */
    USplineDataAsset* CommitCurves(UObject* Parent, FName RequestedName, EObjectFlags Flags,
        TArray<FSplineImportedCurve>& Curves, const FString& SourceFile);
}
