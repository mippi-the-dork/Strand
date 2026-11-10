#pragma once

#include "CoreMinimal.h"
#include <cfloat>

// Pure framing math for Strand's standalone preview. No dependency on a level
// viewport, Editor viewport camera, scene actors, or transient widget state.
namespace StrandSplineFraming
{
    struct FPerspectiveFit
    {
        FVector Location = FVector::ZeroVector;
        FVector Target = FVector::ZeroVector;
        double Distance = 0.0;
        bool bValid = false;
    };

    inline bool FitPerspective(const TArray<FVector>& Samples, const FRotator& ViewRotation,
        double HorizontalFovDegrees, double Aspect, double Coverage, FPerspectiveFit& Out)
    {
        Out = FPerspectiveFit();
        if (Samples.IsEmpty() || !FMath::IsFinite(HorizontalFovDegrees) ||
            !FMath::IsFinite(Aspect) || Aspect <= 0.01 ||
            !FMath::IsFinite(Coverage) || Coverage <= 0.1 || Coverage >= 0.99) { return false; }

        const FVector Origin = Samples[0];
        if (Origin.ContainsNaN()) { return false; }
        const FQuat Camera = ViewRotation.Quaternion();
        const FVector Forward = Camera.GetForwardVector();
        const FVector Right = Camera.GetRightVector();
        const FVector Up = Camera.GetUpVector();
        const double HorizontalTan = FMath::Tan(FMath::DegreesToRadians(
            FMath::Clamp(HorizontalFovDegrees, 35.0, 155.0) * 0.5));
        const double VerticalTan = HorizontalTan / Aspect;
        if (HorizontalTan <= 0.0 || VerticalTan <= 0.0) { return false; }

        struct FPoint { double X, Y, Z; };
        TArray<FPoint> Points;
        Points.Reserve(Samples.Num());
        double MinX = DBL_MAX, MinY = DBL_MAX, MinZ = DBL_MAX;
        double MaxX = -DBL_MAX, MaxY = -DBL_MAX, MaxZ = -DBL_MAX;
        for (const FVector& P : Samples)
        {
            if (P.ContainsNaN()) { return false; }
            const FVector Relative = P - Origin;
            const FPoint Value{ FVector::DotProduct(Relative, Right),
                FVector::DotProduct(Relative, Up), FVector::DotProduct(Relative, Forward) };
            if (!FMath::IsFinite(Value.X) || !FMath::IsFinite(Value.Y) || !FMath::IsFinite(Value.Z)) { return false; }
            Points.Add(Value);
            MinX = FMath::Min(MinX, Value.X); MaxX = FMath::Max(MaxX, Value.X);
            MinY = FMath::Min(MinY, Value.Y); MaxY = FMath::Max(MaxY, Value.Y);
            MinZ = FMath::Min(MinZ, Value.Z); MaxZ = FMath::Max(MaxZ, Value.Z);
        }
        double CenterX = 0.5 * (MinX + MaxX);
        double CenterY = 0.5 * (MinY + MaxY);
        const double CenterZ = 0.5 * (MinZ + MaxZ);
        double Distance = 50.0;
        // Alternate fitting and optical centering. A 3D AABB center is not the
        // visual center of a perspective-projected shape at varying depths.
        for (int32 Iteration = 0; Iteration < 4; ++Iteration)
        {
            Distance = 35.0;
            for (const FPoint& P : Points)
            {
                Distance = FMath::Max(Distance, CenterZ - P.Z + 25.0);
                Distance = FMath::Max(Distance, FMath::Abs(P.X - CenterX) / (HorizontalTan * Coverage) + CenterZ - P.Z);
                Distance = FMath::Max(Distance, FMath::Abs(P.Y - CenterY) / (VerticalTan * Coverage) + CenterZ - P.Z);
            }
            // Move the camera laterally until the projected minimum/maximum
            // have opposite signs and equal magnitudes. Monotone in center X/Y.
            const auto OpticalCenter = [&Points, CenterZ, Distance](double Min, double Max, bool bHorizontal)
            {
                double Low = Min, High = Max;
                for (int32 Step = 0; Step < 24; ++Step)
                {
                    const double Mid = 0.5 * (Low + High);
                    double ProjectedMin = DBL_MAX, ProjectedMax = -DBL_MAX;
                    for (const FPoint& P : Points)
                    {
                        const double Depth = FMath::Max(1.0, Distance + P.Z - CenterZ);
                        const double Projected = ((bHorizontal ? P.X : P.Y) - Mid) / Depth;
                        ProjectedMin = FMath::Min(ProjectedMin, Projected);
                        ProjectedMax = FMath::Max(ProjectedMax, Projected);
                    }
                    if (ProjectedMin + ProjectedMax > 0.0) { Low = Mid; }
                    else { High = Mid; }
                }
                return 0.5 * (Low + High);
            };
            CenterX = OpticalCenter(MinX, MaxX, true);
            CenterY = OpticalCenter(MinY, MaxY, false);
        }
        // Refit after the final centering step, so padding is never lost.
        Distance = 35.0;
        for (const FPoint& P : Points)
        {
            Distance = FMath::Max(Distance, CenterZ - P.Z + 25.0);
            Distance = FMath::Max(Distance, FMath::Abs(P.X - CenterX) / (HorizontalTan * Coverage) + CenterZ - P.Z);
            Distance = FMath::Max(Distance, FMath::Abs(P.Y - CenterY) / (VerticalTan * Coverage) + CenterZ - P.Z);
        }
        if (!FMath::IsFinite(Distance) || Distance > 1.0e13) { return false; }
        Out.Target = Origin + Right * CenterX + Up * CenterY + Forward * CenterZ;
        Out.Location = Out.Target - Forward * Distance;
        Out.Distance = Distance;
        Out.bValid = !Out.Location.ContainsNaN();
        return Out.bValid;
    }
}
