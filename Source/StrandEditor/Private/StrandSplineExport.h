#pragma once

#include "CoreMinimal.h"

class USplineDataAsset;

/** Export only evaluated native spline geometry. Source Strand assets are never changed. */
namespace StrandSplineExport
{
    enum class EProjection : uint8 { XY, XZ, YZ };

    /** True when all anchors and Bézier handles lie on the requested plane. */
    bool IsPlanar(const USplineDataAsset* Asset, EProjection Plane, double ToleranceCm = 0.01);

    /** File writer functions for deterministic regression tests and automated workflows. */
    bool WriteSvg(const USplineDataAsset* Asset, const FString& Filename, EProjection Plane, FString& OutError);
    bool WriteAlembic(const USplineDataAsset* Asset, const FString& Filename, FString& OutError);

    /** Native Save dialogs; a non-XY planar or genuinely 3D spline prompts for SVG projection. */
    void ExportInteractive(USplineDataAsset* Asset, bool bAlembic);
}
