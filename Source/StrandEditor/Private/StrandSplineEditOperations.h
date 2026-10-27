#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

// Editor-only curve operations. All positions and tangent vectors stay local.
// Callers own transactions, notifications, and any required viewport refresh.
namespace StrandSplineEditOperations
{
    inline FVector ReflectVector(FVector Value, int32 Axis)
    {
        if (Axis == 0) { Value.X = -Value.X; }
        if (Axis == 1) { Value.Y = -Value.Y; }
        if (Axis == 2) { Value.Z = -Value.Z; }
        return Value;
    }

    inline void Mirror(TArray<FSplinePointData>& Points, int32 Axis)
    {
        if (Points.IsEmpty() || Axis < 0 || Axis > 2) { return; }
        FBox Bounds(ForceInit);
        for (const FSplinePointData& Point : Points) { Bounds += Point.LocalPosition; }
        const FVector Center = Bounds.GetCenter();
        for (FSplinePointData& Point : Points)
        {
            const FVector Relative = Point.LocalPosition - Center;
            Point.LocalPosition = Center + ReflectVector(Relative, Axis);
            Point.ArriveTangent = ReflectVector(Point.ArriveTangent, Axis);
            Point.LeaveTangent = ReflectVector(Point.LeaveTangent, Axis);
        }
    }

    // Orthogonal projection, including handles. This is an explicit destructive
    // modeling command, not a side effect of entering 2D editing mode.
    inline void Flatten(TArray<FSplinePointData>& Points, int32 Axis)
    {
        if (Points.IsEmpty() || Axis < 0 || Axis > 2) { return; }
        for (FSplinePointData& Point : Points)
        {
            Point.LocalPosition[Axis] = 0.0;
            Point.ArriveTangent[Axis] = 0.0;
            Point.LeaveTangent[Axis] = 0.0;
        }
    }

    inline void Reverse(TArray<FSplinePointData>& Points, bool bClosed)
    {
        if (Points.Num() < 2) { return; }
        const TArray<FSplinePointData> Old = Points;
        const int32 Count = Old.Num();
        for (int32 I = 0; I < Count; ++I)
        {
            const int32 OldIndex = Count - 1 - I;
            Points[I] = Old[OldIndex];
            Points[I].ArriveTangent = -Old[OldIndex].LeaveTangent;
            Points[I].LeaveTangent = -Old[OldIndex].ArriveTangent;
            // Segment interpolation is encoded on its starting vertex.
            // Reversed outgoing interval corresponds to old incoming interval.
            const int32 Previous = (OldIndex + Count - 1) % Count;
            if (bClosed || I < Count - 1)
            {
                Points[I].PointType = Old[Previous].PointType;
            }
        }
    }

    // Split one native evaluated segment at arbitrary normalized t. Preserve
    // its exact cubic shape by updating BOTH neighbors, not just the new point.
    // No mutation on invalid indices, endpoint t, or an open end segment.
    inline int32 InsertOnSegment(TArray<FSplinePointData>& Points, bool bClosed,
        const USplineComponent* Spline, int32 Segment, double Alpha)
    {
        const int32 Count = Points.Num();
        if (!Spline || Count < 2 || Count >= 100000 || Spline->GetNumberOfSplinePoints() != Count ||
            Segment < 0 || Segment >= (bClosed ? Count : Count - 1) ||
            !FMath::IsFinite(Alpha) || Alpha <= 0.001 || Alpha >= 0.999)
        {
            return INDEX_NONE;
        }
        const int32 Next = (Segment + 1) % Count;
        const FSplinePoint A = Spline->GetSplinePointAt(Segment, ESplineCoordinateSpace::Local);
        const FSplinePoint B = Spline->GetSplinePointAt(Next, ESplineCoordinateSpace::Local);
        FSplinePointData Middle = Points[Segment];
        if (A.Type == ESplinePointType::Linear)
        {
            Middle.LocalPosition = FMath::Lerp(A.Position, B.Position, Alpha);
            Middle.PointType = ESplinePointType::Linear;
            Middle.ArriveTangent = FVector::ZeroVector;
            Middle.LeaveTangent = FVector::ZeroVector;
        }
        else if (A.Type == ESplinePointType::Constant)
        {
            Middle.LocalPosition = A.Position;
            Middle.PointType = ESplinePointType::Constant;
            Middle.ArriveTangent = FVector::ZeroVector;
            Middle.LeaveTangent = FVector::ZeroVector;
        }
        else
        {
            const FVector C1 = A.Position + A.LeaveTangent / 3.0;
            const FVector C2 = B.Position - B.ArriveTangent / 3.0;
            const FVector L = FMath::Lerp(A.Position, C1, Alpha);
            const FVector M = FMath::Lerp(C1, C2, Alpha);
            const FVector R = FMath::Lerp(C2, B.Position, Alpha);
            const FVector LM = FMath::Lerp(L, M, Alpha);
            const FVector MR = FMath::Lerp(M, R, Alpha);
            const FVector Midpoint = FMath::Lerp(LM, MR, Alpha);
            Middle.LocalPosition = Midpoint;
            Middle.ArriveTangent = 3.0 * (Midpoint - LM);
            Middle.LeaveTangent = 3.0 * (MR - Midpoint);
            Middle.PointType = ESplinePointType::CurveCustomTangent;
            // Keep each neighbor's other handle as evaluated by Unreal.
            Points[Segment].PointType = ESplinePointType::CurveCustomTangent;
            Points[Segment].ArriveTangent = A.ArriveTangent;
            Points[Segment].LeaveTangent = 3.0 * (L - A.Position);
            Points[Next].PointType = ESplinePointType::CurveCustomTangent;
            Points[Next].ArriveTangent = 3.0 * (B.Position - R);
            Points[Next].LeaveTangent = B.LeaveTangent;
        }
        const int32 InsertIndex = Segment + 1;
        Points.Insert(Middle, InsertIndex);
        return InsertIndex;
    }

    inline int32 AppendPenPoint(TArray<FSplinePointData>& Points, const FVector& Position,
        bool bPrepend, bool bClosed, int32 Limit = 100000)
    {
        if (bClosed || Points.Num() >= Limit || Position.ContainsNaN()) { return INDEX_NONE; }
        FSplinePointData NewPoint;
        NewPoint.LocalPosition = Position;
        NewPoint.PointType = ESplinePointType::CurveCustomTangent;
        if (bPrepend) { Points.Insert(NewPoint, 0); return 0; }
        return Points.Add(NewPoint);
    }

    // Standard click-drag Bezier behavior: the cursor sets the outgoing handle
    // and the incoming handle mirrors it about the new anchor.
    inline bool SetPenHandles(TArray<FSplinePointData>& Points, int32 Index, const FVector& HandleOffset)
    {
        if (!Points.IsValidIndex(Index) || HandleOffset.ContainsNaN()) { return false; }
        FSplinePointData& Point = Points[Index];
        Point.PointType = ESplinePointType::CurveCustomTangent;
        Point.ArriveTangent = HandleOffset * 3.0;
        Point.LeaveTangent = HandleOffset * 3.0;
        return true;
    }

    inline void MakeAllLinear(TArray<FSplinePointData>& Points)
    {
        for (FSplinePointData& Point : Points)
        {
            Point.PointType = ESplinePointType::Linear;
            Point.ArriveTangent = FVector::ZeroVector;
            Point.LeaveTangent = FVector::ZeroVector;
        }
    }
    inline void MakeAllAuto(TArray<FSplinePointData>& Points)
    {
        for (FSplinePointData& Point : Points) { Point.PointType = ESplinePointType::Curve; }
    }

    // Bisect every *evaluated* source segment in one pass. Source is never
    // modified while being evaluated; de Casteljau reproduces each cubic exactly.
    // Resulting curve anchors use native custom tangents, avoiding further
    // automatic re-fitting of the original geometry.
    inline bool Subdivide(TArray<FSplinePointData>& Points, bool bClosed, const USplineComponent* Spline)
    {
        const int32 Count = Points.Num();
        if (!Spline || Count < 2 || Count > 8192 || Spline->GetNumberOfSplinePoints() != Count) { return false; }
        const int32 Segments = bClosed ? Count : Count - 1;
        TArray<FSplinePointData> Original = Points;
        TArray<FSplinePointData> Middle;
        Middle.SetNum(Segments);
        for (int32 I = 0; I < Segments; ++I)
        {
            const int32 J = (I + 1) % Count;
            const FSplinePoint A = Spline->GetSplinePointAt(I, ESplineCoordinateSpace::Local);
            const FSplinePoint B = Spline->GetSplinePointAt(J, ESplineCoordinateSpace::Local);
            FSplinePointData& Mid = Middle[I];
            Mid = Points[I];
            if (A.Type == ESplinePointType::Linear)
            {
                Mid.LocalPosition = (A.Position + B.Position) * 0.5;
                Mid.PointType = ESplinePointType::Linear;
                Mid.ArriveTangent = FVector::ZeroVector;
                Mid.LeaveTangent = FVector::ZeroVector;
                Original[I].PointType = ESplinePointType::Linear;
            }
            else if (A.Type == ESplinePointType::Constant)
            {
                Mid.LocalPosition = A.Position;
                Mid.PointType = ESplinePointType::Constant;
                Original[I].PointType = ESplinePointType::Constant;
            }
            else
            {
                const FVector C1 = A.Position + A.LeaveTangent / 3.0;
                const FVector C2 = B.Position - B.ArriveTangent / 3.0;
                const FVector L = (A.Position + C1) * 0.5;
                const FVector M = (C1 + C2) * 0.5;
                const FVector R = (C2 + B.Position) * 0.5;
                const FVector LM = (L + M) * 0.5;
                const FVector MR = (M + R) * 0.5;
                Mid.LocalPosition = (LM + MR) * 0.5;
                Mid.ArriveTangent = 3.0 * (Mid.LocalPosition - LM);
                Mid.LeaveTangent = 3.0 * (MR - Mid.LocalPosition);
                Mid.PointType = ESplinePointType::CurveCustomTangent;
                Original[I].PointType = ESplinePointType::CurveCustomTangent;
                Original[I].LeaveTangent = 3.0 * (L - A.Position);
                Original[J].ArriveTangent = 3.0 * (B.Position - R);
            }
        }
        TArray<FSplinePointData> Result;
        Result.Reserve(Count + Segments);
        for (int32 I = 0; I < Count; ++I)
        {
            Result.Add(Original[I]);
            if (I < Segments) { Result.Add(Middle[I]); }
        }
        Points = MoveTemp(Result);
        return true;
    }
}
