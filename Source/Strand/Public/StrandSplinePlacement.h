#pragma once

#include "CoreMinimal.h"
#include "StrandSplinePlacement.generated.h"

/** All Strand data stays in spline-local space; no world/actor transform is serialized. */
UENUM(BlueprintType)
enum class EStrandCoordinateMode : uint8
{
    /** Apply the point positions exactly as authored in the Strand asset. */
    Absolute UMETA(DisplayName="Absolute (Source Coordinates)"),
    /** Translate local point positions so the selected anchor occupies component origin (0,0,0). */
    Relative UMETA(DisplayName="Relative (Anchor at Origin)")
};

UENUM(BlueprintType)
enum class EStrandAnchorMode : uint8
{
    PointIndex UMETA(DisplayName="Spline Point"),
    PointCenter UMETA(DisplayName="Center of Spline Points")
};
