#pragma once
#include "CoreMinimal.h"
#include <cfloat>
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

/** Pure math shared by Strand's camera-plane editing, soft-selection and tests. */
namespace StrandCameraSpace
{
    enum class EInfluence : uint8 { Off, Spatial3D, AlongSpline };
    enum class EFalloff : uint8 { Linear, Smooth, Sharp };
    inline FVector ProjectDelta(const FVector& Delta, const FVector& Normal)
    {
        if (Delta.ContainsNaN() || Normal.ContainsNaN() || Normal.IsNearlyZero()) { return FVector::ZeroVector; }
        return FVector::VectorPlaneProject(Delta, Normal.GetSafeNormal());
    }
    inline FVector FlattenPoint(const FVector& Position, const FVector& PlaneOrigin, const FVector& Normal)
    {
        if (Position.ContainsNaN() || PlaneOrigin.ContainsNaN() || Normal.ContainsNaN() || Normal.IsNearlyZero()) { return Position; }
        return Position - FVector::DotProduct(Position - PlaneOrigin, Normal.GetSafeNormal()) * Normal.GetSafeNormal();
    }
    inline double Falloff(double Distance, double Radius, EFalloff Profile)
    {
        if (!FMath::IsFinite(Distance) || !FMath::IsFinite(Radius) || Radius <= 0.0) { return 0.0; }
        const double T = FMath::Clamp(1.0 - Distance / Radius, 0.0, 1.0);
        if (Profile == EFalloff::Smooth) { return T * T * (3.0 - 2.0 * T); }
        if (Profile == EFalloff::Sharp) { return T * T * T; }
        return T;
    }
    inline double InfluenceAt(const TArray<FSplinePointData>& Points, const TArray<int32>& Sources,
        int32 TargetIndex, const USplineComponent* EvaluatedSpline, EInfluence Mode, double Radius, EFalloff Profile)
    {
        if (!Points.IsValidIndex(TargetIndex) || Sources.IsEmpty()) { return 0.0; }
        if (Sources.Contains(TargetIndex)) { return 1.0; }
        if (Mode == EInfluence::Off || Radius <= 0.0) { return 0.0; }
        double MinDistance = DBL_MAX;
        for (int32 Source : Sources)
        {
            if (!Points.IsValidIndex(Source)) { continue; }
            double Distance = 0.0;
            if (Mode == EInfluence::Spatial3D)
            {
                Distance = FVector::Distance(Points[TargetIndex].LocalPosition, Points[Source].LocalPosition);
            }
            else
            {
                if (!IsValid(EvaluatedSpline) || EvaluatedSpline->GetNumberOfSplinePoints() != Points.Num()) { continue; }
                const double TargetDistance = EvaluatedSpline->GetDistanceAlongSplineAtSplinePoint(TargetIndex);
                const double SourceDistance = EvaluatedSpline->GetDistanceAlongSplineAtSplinePoint(Source);
                Distance = FMath::Abs(TargetDistance - SourceDistance);
                if (EvaluatedSpline->IsClosedLoop())
                {
                    const double Length = EvaluatedSpline->GetSplineLength();
                    Distance = FMath::Min(Distance, FMath::Max(0.0, Length - Distance));
                }
            }
            MinDistance = FMath::Min(MinDistance, Distance);
        }
        return Falloff(MinDistance, Radius, Profile);
    }
    /** Stable drag application: weights are evaluated at drag start and never recomputed mid-drag. */
    inline bool ApplyWeightedDelta(TArray<FSplinePointData>& Points, const TArray<double>& FrozenWeights,
        const FVector& Delta)
    {
        if (Points.Num() != FrozenWeights.Num() || Delta.ContainsNaN() || Delta.IsNearlyZero()) { return false; }
        bool bChanged = false;
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            const double Weight = FrozenWeights[Index];
            if (!FMath::IsFinite(Weight) || Weight <= 0.0) { continue; }
            Points[Index].LocalPosition += Delta * FMath::Clamp(Weight, 0.0, 1.0);
            bChanged = true;
        }
        return bChanged;
    }
    inline bool Flatten(TArray<FSplinePointData>& Points, const TArray<int32>& Selection,
        const FVector& PlaneOrigin, const FVector& PlaneNormal)
    {
        if (Selection.IsEmpty() || PlaneNormal.ContainsNaN() || PlaneNormal.IsNearlyZero()) { return false; }
        bool bChanged = false;
        for (int32 Index : Selection)
        {
            if (!Points.IsValidIndex(Index)) { continue; }
            FSplinePointData& Point = Points[Index];
            const FVector Projected = FlattenPoint(Point.LocalPosition, PlaneOrigin, PlaneNormal);
            const FVector Arrive = ProjectDelta(Point.ArriveTangent, PlaneNormal);
            const FVector Leave = ProjectDelta(Point.LeaveTangent, PlaneNormal);
            if (!Projected.Equals(Point.LocalPosition, 0.00001) ||
                !Arrive.Equals(Point.ArriveTangent, 0.00001) ||
                !Leave.Equals(Point.LeaveTangent, 0.00001))
            {
                Point.LocalPosition = Projected;
                // Tangents are derivatives, not positions: remove only their normal component.
                // Even an anchor already on the plane can have out-of-plane handles.
                Point.ArriveTangent = Arrive;
                Point.LeaveTangent = Leave;
                bChanged = true;
            }
        }
        return bChanged;
    }
}
