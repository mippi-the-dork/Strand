#pragma once

class USplineMeshComponent;
class USplineDataAsset;

namespace StrandSplineMeshAdapter
{
    /** A mesh represents one segment. Multi-point and closed assets are intentionally rejected. */
    bool CanApply(const USplineDataAsset* Asset);
    bool Matches(const USplineMeshComponent* Mesh, const USplineDataAsset* Asset);
    bool Apply(USplineMeshComponent* Mesh, USplineDataAsset* Asset);
    void Capture(USplineMeshComponent* Mesh, USplineDataAsset* Asset, bool bRestoreSourceFrame);
    void NotifyChanged(USplineMeshComponent* Mesh);
}
