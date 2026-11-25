#pragma once

#include "CoreMinimal.h"

/** Pure placement calculations for a temporary level projection source. */
namespace StrandProjectionPlacement
{
    inline bool ValidCameraDistance(double DistanceCm)
    {
        return FMath::IsFinite(DistanceCm) && DistanceCm > 0.0;
    }

    /** Keep the spline's local anchor centroid at the requested camera-relative position.
     * Scale and orientation belong only to the temporary actor, not the authored spline points. */
    inline FTransform CameraPlacement(const FVector& CameraPosition, const FRotator& CameraRotation,
        double DistanceCm, const FVector& LocalAnchorCenter, const FQuat& ActorOrientation,
        const FVector& ActorScale = FVector::OneVector)
    {
        const FVector WorldCenter = CameraPosition + CameraRotation.Vector() * DistanceCm;
        const FVector ScaledCenter = LocalAnchorCenter * ActorScale;
        return FTransform(ActorOrientation, WorldCenter - ActorOrientation.RotateVector(ScaledCenter), ActorScale);
    }
}
