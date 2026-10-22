#pragma once

#include "CoreMinimal.h"

class AActor;
class USplineComponent;
class USplineDataAsset;

/** Editor-only instance-component creation. Uses native USplineComponent, never custom spline geometry. */
namespace StrandPlacement
{
    /** Adds one native spline to an existing actor, preserving its root and scene hierarchy. */
    USplineComponent* AddNativeSpline(AActor* Actor);

    /** Creates a regular level actor with a native spline linked to a Strand asset at the world origin. */
    AActor* CreateNativeSplineActor(USplineDataAsset* Asset, FString& OutError);
}
