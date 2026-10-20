#pragma once

class USplineDataAsset;
class ULandscapeSplinesComponent;

namespace SplineLandscapeAdapter
{
    /** Matches one connected path. Preserves the first control point's existing location. */
    bool Apply(const USplineDataAsset* Asset, ULandscapeSplinesComponent* Component, FString& OutError);
}
