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

    /** A fixed anchor-local plane: XY->local Z normal, XZ->local Y,
     * YZ->local X. The caller owns the snapshot lifetime. */
    inline bool MakeAnchorLocalPlane(const FQuat& Rotation, int32 LockedLocalAxis,
        FVector& OutNormal, FVector& OutPreferredAxis)
    {
        if (Rotation.ContainsNaN() || Rotation.SizeSquared() < SMALL_NUMBER ||
            LockedLocalAxis < 0 || LockedLocalAxis > 2) { return false; }
        const FQuat Frame = Rotation.GetNormalized();
        const FVector X = Frame.GetAxisX();
        const FVector Y = Frame.GetAxisY();
        const FVector Z = Frame.GetAxisZ();
        OutNormal = LockedLocalAxis == 0 ? X : LockedLocalAxis == 1 ? Y : Z;
        OutPreferredAxis = LockedLocalAxis == 0 ? Y : X;
        return !OutNormal.IsNearlyZero() && !OutPreferredAxis.IsNearlyZero();
    }

    struct FSurfaceTriangle
    {
        FVector A = FVector::ZeroVector;
        FVector B = FVector::ZeroVector;
        FVector C = FVector::ZeroVector;
    };

    struct FSurfaceProjectionCandidate
    {
        int32 SourceIndex = INDEX_NONE;
        int32 TriangleIndex = INDEX_NONE;
        FVector Hit = FVector::ZeroVector;
        FVector Normal = FVector::UpVector;
        FVector PositionWithClearance = FVector::ZeroVector;
        double Distance = 0.0;
    };

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
    inline bool ProjectRayToTriangleSoup(const FVector& Origin, const FVector& Direction,
        const TArray<FSurfaceTriangle>& Triangles, double MaxDistance, double Clearance,
        FSurfaceProjectionCandidate& OutCandidate)
    {
        if (!FMath::IsFinite(Clearance) || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0 ||
            Origin.ContainsNaN() || Direction.ContainsNaN()) { return false; }
        double Best = MaxDistance;
        bool bFound = false;
        for (int32 Index = 0; Index < Triangles.Num(); ++Index)
        {
            const FSurfaceTriangle& T = Triangles[Index];
            FVector Hit, Normal;
            double Distance = 0.0;
            if (ProjectRayToTriangle(Origin, Direction, T.A, T.B, T.C, Best, Hit, Normal, Distance) &&
                (!bFound || Distance < Best))
            {
                bFound = true;
                Best = Distance;
                OutCandidate.TriangleIndex = Index;
                OutCandidate.Hit = Hit;
                OutCandidate.Normal = Normal;
                OutCandidate.Distance = Distance;
                OutCandidate.PositionWithClearance = ApplyClearance(Hit, Normal, Clearance);
            }
        }
        return bFound;
    }

    /** A preview plan computed from immutable source positions and an explicit
     * supplied triangle set. Misses are omitted; callers retain originals. */
    inline void BuildSurfacePreviewPlan(const TArray<FVector>& Sources,
        const TArray<int32>& SelectedIndices, const TArray<FSurfaceTriangle>& Triangles,
        const FVector& Direction, double MaxDistance, double Clearance,
        TArray<FSurfaceProjectionCandidate>& OutChanges)
    {
        OutChanges.Reset();
        if (Direction.IsNearlyZero() || Direction.ContainsNaN() ||
            !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0 || !FMath::IsFinite(Clearance)) { return; }
        TSet<int32> Seen;
        for (int32 Index : SelectedIndices)
        {
            if (!Sources.IsValidIndex(Index) || Seen.Contains(Index)) { continue; }
            Seen.Add(Index);
            FSurfaceProjectionCandidate Candidate;
            if (ProjectRayToTriangleSoup(Sources[Index], Direction, Triangles,
                MaxDistance, Clearance, Candidate))
            {
                Candidate.SourceIndex = Index;
                OutChanges.Add(Candidate);
            }
        }
    }

}
