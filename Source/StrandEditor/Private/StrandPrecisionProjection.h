#pragma once

#include "CoreMinimal.h"

/** Asset-local constraint frame and deterministic projection primitives.
 * Scene collision acquisition is deliberately not done in the asset preview world.
 */
namespace StrandPrecisionProjection
{
    inline bool MakePlaneFrame(const FVector& Normal, const FVector& PreferredAxis,
        FVector& OutU, FVector& OutV, FVector& OutN)
    {
        if (Normal.ContainsNaN() || Normal.IsNearlyZero()) { return false; }
        OutN = Normal.GetSafeNormal();
        FVector U = FVector::VectorPlaneProject(PreferredAxis, OutN);
        if (U.IsNearlyZero())
        {
            const FVector Fallback = FMath::Abs(OutN.Z) < 0.9 ? FVector::ZAxisVector : FVector::YAxisVector;
            U = FVector::CrossProduct(Fallback, OutN);
        }
        OutU = U.GetSafeNormal();
        OutV = FVector::CrossProduct(OutN, OutU).GetSafeNormal();
        return !OutU.IsNearlyZero() && !OutV.IsNearlyZero();
    }

    inline bool ProjectRayToTriangle(const FVector& RayOrigin, const FVector& RayDirection,
        const FVector& A, const FVector& B, const FVector& C, double MaxDistance,
        FVector& OutHit, FVector& OutNormal, double& OutDistance)
    {
        if (RayOrigin.ContainsNaN() || RayDirection.ContainsNaN() || A.ContainsNaN() ||
            B.ContainsNaN() || C.ContainsNaN() || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0)
        { return false; }
        const FVector D = RayDirection.GetSafeNormal();
        if (D.IsNearlyZero()) { return false; }
        const FVector E1 = B - A, E2 = C - A;
        const FVector P = FVector::CrossProduct(D, E2);
        const double Det = FVector::DotProduct(E1, P);
        if (FMath::Abs(Det) < 1.e-10) { return false; }
        const double InvDet = 1.0 / Det;
        const FVector T = RayOrigin - A;
        const double U = FVector::DotProduct(T, P) * InvDet;
        if (U < -1.e-9 || U > 1.0 + 1.e-9) { return false; }
        const FVector Q = FVector::CrossProduct(T, E1);
        const double V = FVector::DotProduct(D, Q) * InvDet;
        if (V < -1.e-9 || U + V > 1.0 + 1.e-9) { return false; }
        const double Distance = FVector::DotProduct(E2, Q) * InvDet;
        if (Distance < 0.0 || Distance > MaxDistance) { return false; }
        OutHit = RayOrigin + D * Distance;
        OutNormal = FVector::CrossProduct(E1, E2).GetSafeNormal();
        OutDistance = Distance;
        return true;
    }

    inline FVector ApplyClearance(const FVector& Hit, const FVector& SurfaceNormal, double Clearance)
    {
        if (Hit.ContainsNaN() || SurfaceNormal.ContainsNaN() || !FMath::IsFinite(Clearance) ||
            SurfaceNormal.IsNearlyZero()) { return Hit; }
        return Hit + SurfaceNormal.GetSafeNormal() * Clearance;
    }
}
