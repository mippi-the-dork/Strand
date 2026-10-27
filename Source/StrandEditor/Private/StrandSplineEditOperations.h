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

    // Selection is editor state, never serialized into a Strand asset. Pure
    // operations make group edits deterministic and independently testable.
    inline FVector SelectionCenter(const TArray<FSplinePointData>& Points, const TArray<int32>& Indices)
    {
        FVector Sum = FVector::ZeroVector;
        int32 Count = 0;
        for (int32 Index : Indices)
        {
            if (Points.IsValidIndex(Index)) { Sum += Points[Index].LocalPosition; ++Count; }
        }
        return Count ? Sum / static_cast<double>(Count) : FVector::ZeroVector;
    }

    inline bool TranslateSelected(TArray<FSplinePointData>& Points, const TArray<int32>& Indices,
        const FVector& Delta)
    {
        if (Delta.ContainsNaN() || Delta.IsNearlyZero() || Indices.IsEmpty()) { return false; }
        bool bChanged = false;
        for (int32 Index : Indices)
        {
            if (Points.IsValidIndex(Index))
            {
                Points[Index].LocalPosition += Delta;
                bChanged = true;
            }
        }
        return bChanged;
    }

    inline bool AlignSelected(TArray<FSplinePointData>& Points, const TArray<int32>& Indices, int32 Axis)
    {
        if (Indices.Num() < 2 || Axis < 0 || Axis > 2) { return false; }
        const double Center = SelectionCenter(Points, Indices)[Axis];
        bool bChanged = false;
        for (int32 Index : Indices)
        {
            if (Points.IsValidIndex(Index))
            {
                bChanged |= !FMath::IsNearlyEqual(Points[Index].LocalPosition[Axis], Center, 0.000001);
                Points[Index].LocalPosition[Axis] = Center;
            }
        }
        return bChanged;
    }

    // Spatial-order distribution (not spline sequence). Endpoints of the selected
    // range stay where they are; unselected anchors and all tangents stay intact.
    inline bool DistributeSelected(TArray<FSplinePointData>& Points, const TArray<int32>& Indices, int32 Axis)
    {
        if (Indices.Num() < 3 || Axis < 0 || Axis > 2) { return false; }
        TArray<int32> Ordered;
        for (int32 Index : Indices) { if (Points.IsValidIndex(Index)) { Ordered.AddUnique(Index); } }
        if (Ordered.Num() < 3) { return false; }
        Ordered.Sort([&Points, Axis](int32 A, int32 B)
        {
            const double Left = Points[A].LocalPosition[Axis];
            const double Right = Points[B].LocalPosition[Axis];
            return Left == Right ? A < B : Left < Right;
        });
        const double First = Points[Ordered[0]].LocalPosition[Axis];
        const double Last = Points[Ordered.Last()].LocalPosition[Axis];
        if (FMath::IsNearlyEqual(First, Last)) { return false; }
        bool bChanged = false;
        const int32 LastIndex = Ordered.Num() - 1;
        for (int32 I = 1; I < LastIndex; ++I)
        {
            const double Target = FMath::Lerp(First, Last, static_cast<double>(I) / LastIndex);
            bChanged |= !FMath::IsNearlyEqual(Points[Ordered[I]].LocalPosition[Axis], Target, 0.000001);
            Points[Ordered[I]].LocalPosition[Axis] = Target;
        }
        return bChanged;
    }

    // One-shot handle cleanup, not a persistent tangent mode. Incoming and
    // outgoing Unreal tangent vectors both point in the direction of travel.
    inline bool MatchTangents(FSplinePointData& Point, const FVector& Arrive,
        const FVector& Leave, bool bMirror, bool bUseIncoming)
    {
        const FVector Driver = bUseIncoming ? Arrive : Leave;
        if (Driver.IsNearlyZero()) { return false; }
        const FVector Direction = Driver.GetSafeNormal();
        Point.PointType = ESplinePointType::CurveCustomTangent;
        Point.ArriveTangent = bUseIncoming ? Driver : Direction * (bMirror ? Driver.Size() : Arrive.Size());
        Point.LeaveTangent = bUseIncoming ? Direction * (bMirror ? Driver.Size() : Leave.Size()) : Driver;
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
