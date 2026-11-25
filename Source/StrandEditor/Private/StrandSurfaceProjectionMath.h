#pragma once

#include "CoreMinimal.h"

// Editor-only planning helpers. No component mutation and no scene dependency.
namespace StrandSurfaceProjectionMath
{
    // Perspective: one ray per anchor through the camera's optical center.
    // Parallel: each anchor emits a ray in the same world direction.
    inline bool MakeProjectionRay(const FVector& AnchorWorld, const FVector& CameraWorld,
        const FVector& Direction, bool bPerspective, double MaxDistance,
        FVector& OutStart, FVector& OutEnd)
    {
        if (!FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0 ||
            AnchorWorld.ContainsNaN() || CameraWorld.ContainsNaN() || Direction.ContainsNaN())
        { return false; }
        OutStart = bPerspective ? CameraWorld : AnchorWorld;
        const FVector RayDirection = bPerspective
            ? (AnchorWorld - CameraWorld).GetSafeNormal() : Direction.GetSafeNormal();
        if (RayDirection.IsNearlyZero()) { return false; }
        OutEnd = OutStart + RayDirection * MaxDistance;
        return true;
    }

    inline bool IsAnchorEligible(int32 Index, int32 Count, int32 First, int32 Last)
    {
        const int32 End = Last < 0 ? Count - 1 : FMath::Min(Count - 1, Last);
        return Index >= FMath::Max(0, First) && Index <= End;
    }

    // Selected-anchor mask and soft-selection use the same falloff as Strand's
    // editor transforms. An empty mask means projection applies to every anchor.
    inline double EffectiveStrength(double Strength, const TArray<double>& Weights, bool bUseWeights, int32 Index)
    {
        if (!bUseWeights) { return FMath::Clamp(Strength, 0.0, 1.0); }
        if (!Weights.IsValidIndex(Index) || !FMath::IsFinite(Weights[Index])) { return 0.0; }
        return FMath::Clamp(Strength, 0.0, 1.0) * FMath::Clamp(Weights[Index], 0.0, 1.0);
    }

    inline FVector BlendWorldHitToLocal(const FTransform& Source,
        const FVector& OriginalWorld, const FVector& HitWorld,
        double Strength)
    {
        const double Alpha = FMath::Clamp(Strength, 0.0, 1.0);
        return Source.InverseTransformPosition(FMath::Lerp(OriginalWorld, HitWorld, Alpha));
    }

    inline FVector WorldNormalToLocal(const FTransform& Source, const FVector& Normal)
    {
        // Up vectors are directional; ignore translation and scale for orientation.
        return Source.InverseTransformVectorNoScale(Normal.GetSafeNormal()).GetSafeNormal();
    }
}
