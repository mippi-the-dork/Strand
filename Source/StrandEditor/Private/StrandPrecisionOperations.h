#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include <cfloat>

/** Asset-local precision operations. Targets exclude the edited anchors; no world placement is assumed. */
namespace StrandPrecision
{
    inline bool PlaneFromThreePoints(const TArray<FSplinePointData>& Points,
        const TArray<int32>& Selection, FVector& OutOrigin, FVector& OutNormal)
    {
        if (Selection.Num() != 3 || !Points.IsValidIndex(Selection[0]) ||
            !Points.IsValidIndex(Selection[1]) || !Points.IsValidIndex(Selection[2]) ||
            Selection[0] == Selection[1] || Selection[0] == Selection[2] || Selection[1] == Selection[2])
        { return false; }
        const FVector A = Points[Selection[0]].LocalPosition;
        const FVector B = Points[Selection[1]].LocalPosition;
        const FVector C = Points[Selection[2]].LocalPosition;
        if (A.ContainsNaN() || B.ContainsNaN() || C.ContainsNaN()) { return false; }
        const FVector Cross = FVector::CrossProduct(B - A, C - A);
        if (Cross.SizeSquared() < 1.e-8) { return false; }
        OutOrigin = A;
        OutNormal = Cross.GetSafeNormal();
        return true;
    }

    inline bool FindNearestOtherAnchor(const TArray<FSplinePointData>& Points,
        const TArray<int32>& Selection, int32 Source, double MaxDistance,
        FVector& OutPosition, int32& OutIndex)
    {
        if (!Points.IsValidIndex(Source) || !Selection.Contains(Source) ||
            !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0) { return false; }
        const FVector SourcePos = Points[Source].LocalPosition;
        if (SourcePos.ContainsNaN()) { return false; }
        double Best = MaxDistance * MaxDistance;
        OutIndex = INDEX_NONE;
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            if (Selection.Contains(I) || Points[I].LocalPosition.ContainsNaN()) { continue; }
            const double Dist = FVector::DistSquared(SourcePos, Points[I].LocalPosition);
            if (Dist <= Best)
            {
                Best = Dist;
                OutIndex = I;
                OutPosition = Points[I].LocalPosition;
            }
        }
        return OutIndex != INDEX_NONE;
    }

    /** Find a point on another segment, using the actual evaluated native cubic.
      * Excludes segments touching any selected anchor; this prevents self-snap and
      * keeps results independent of iteration order when multiple anchors move. */
    inline bool FindNearestOtherSegment(const USplineComponent* Spline,
        const TArray<int32>& Selection, int32 Source, double MaxDistance,
        FVector& OutPosition, int32& OutSegment)
    {
        if (!IsValid(Spline) || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0 ||
            Source < 0 || Source >= Spline->GetNumberOfSplinePoints() || !Selection.Contains(Source))
        { return false; }
        const int32 Count = Spline->GetNumberOfSplinePoints();
        const int32 Segments = Count - (Spline->IsClosedLoop() ? 0 : 1);
        const FVector Position = Spline->GetLocationAtSplinePoint(Source, ESplineCoordinateSpace::Local);
        if (Position.ContainsNaN()) { return false; }
        double Best = MaxDistance * MaxDistance;
        OutSegment = INDEX_NONE;
        for (int32 S = 0; S < Segments; ++S)
        {
            if (Selection.Contains(S) || Selection.Contains((S + 1) % Count)) { continue; }
            auto Evaluate = [Spline, S](double T)
            {
                return Spline->GetLocationAtSplineInputKey(static_cast<float>(S + T), ESplineCoordinateSpace::Local);
            };
            // Coarse sweep plus bounded local refinement. This handles curve interior
            // minima, unlike using only the nearest segment endpoint.
            constexpr int32 Samples = 48;
            for (int32 J = 0; J <= Samples; ++J)
            {
                const double T = static_cast<double>(J) / Samples;
                const FVector Candidate = Evaluate(T);
                if (Candidate.ContainsNaN()) { continue; }
                const double Dist = FVector::DistSquared(Position, Candidate);
                if (Dist >= Best) { continue; }
                double A = FMath::Max(0.0, T - 1.0 / Samples);
                double B = FMath::Min(1.0, T + 1.0 / Samples);
                for (int32 K = 0; K < 14; ++K)
                {
                    const double L = A + (B - A) * 0.38196601125;
                    const double R = B - (B - A) * 0.38196601125;
                    if (FVector::DistSquared(Position, Evaluate(L)) < FVector::DistSquared(Position, Evaluate(R))) { B = R; }
                    else { A = L; }
                }
                const FVector Refined = Evaluate((A + B) * 0.5);
                const double RefinedDist = FVector::DistSquared(Position, Refined);
                if (RefinedDist <= Best)
                {
                    Best = RefinedDist;
                    OutPosition = Refined;
                    OutSegment = S;
                }
            }
        }
        return OutSegment != INDEX_NONE;
    }
    /** Snap a live primary anchor against unselected anchors, without changing spline data.
      * This is a pointer-driven placement helper: the supplied source is the current
      * (already dragged) position and the unchanged non-selected points are targets. */
    inline bool FindLiveAnchorTarget(const TArray<FSplinePointData>& Points,
        const TArray<int32>& Selection, const FVector& SourcePosition, double Tolerance,
        FVector& OutTarget, int32& OutTargetIndex)
    {
        OutTargetIndex = INDEX_NONE;
        if (Selection.IsEmpty() || SourcePosition.ContainsNaN() || !FMath::IsFinite(Tolerance) ||
            Tolerance <= 0.0) { return false; }
        double ClosestSquared = Tolerance * Tolerance;
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            if (Selection.Contains(I) || Points[I].LocalPosition.ContainsNaN()) { continue; }
            const double DistanceSquared = FVector::DistSquared(SourcePosition, Points[I].LocalPosition);
            if (DistanceSquared < ClosestSquared)
            {
                ClosestSquared = DistanceSquared;
                OutTarget = Points[I].LocalPosition;
                OutTargetIndex = I;
            }
        }
        return OutTargetIndex != INDEX_NONE;
    }

    /** Resolve snap candidates against an unchanged source. UI previews and Apply use the same plan. */
    inline void BuildSnapPlan(const TArray<FSplinePointData>& Points, const USplineComponent* Spline,
        const TArray<int32>& Selection, bool bSegments, double MaxDistance,
        TArray<TPair<int32, FVector>>& OutChanges)
    {
        OutChanges.Reset();
        if (!FMath::IsFinite(MaxDistance) || MaxDistance <= 0.0 ||
            (bSegments && !IsValid(Spline))) { return; }
        for (int32 Index : Selection)
        {
            if (!Points.IsValidIndex(Index)) { continue; }
            FVector Target = FVector::ZeroVector;
            int32 Found = INDEX_NONE;
            const bool bFound = bSegments
                ? FindNearestOtherSegment(Spline, Selection, Index, MaxDistance, Target, Found)
                : FindNearestOtherAnchor(Points, Selection, Index, MaxDistance, Target, Found);
            if (bFound && !Points[Index].LocalPosition.Equals(Target, 0.000001))
            { OutChanges.Emplace(Index, Target); }
        }
    }

}
