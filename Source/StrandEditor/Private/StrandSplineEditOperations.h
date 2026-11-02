#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

// Editor-only curve operations. All positions and tangent vectors stay local.
// Callers own transactions, notifications, and any required viewport refresh.
namespace StrandSplineEditOperations
{

    // Persistent Bezier editor rules. Unreal custom tangent vectors both point in
    // the forward travel direction; incoming HANDLE is drawn at P - Arrive/3.
    inline EStrandBezierHandleMode EffectiveHandleMode(const FSplinePointData& Point)
    {
        if (Point.PointType.GetValue() == ESplinePointType::Curve ||
            Point.PointType.GetValue() == ESplinePointType::CurveClamped)
        {
            return EStrandBezierHandleMode::Auto;
        }
        return Point.HandleMode;
    }

    // Called after any direct handle edit. The edited handle always wins; the
    // opposite tangent follows only when the saved mode asks it to.
    inline bool EditHandle(FSplinePointData& Point, int32 Handle, const FVector& NewTangent)
    {
        if ((Handle != 1 && Handle != 2) || NewTangent.ContainsNaN()) { return false; }
        const EStrandBezierHandleMode Mode = EffectiveHandleMode(Point);
        // Dragging an automatic or collapsed corner handle breaks it into Free.
        // Users can subsequently opt into persistent Aligned / Mirrored behavior.
        if (Mode == EStrandBezierHandleMode::Auto || Mode == EStrandBezierHandleMode::Corner)
        {
            Point.HandleMode = EStrandBezierHandleMode::Free;
        }
        Point.PointType = ESplinePointType::CurveCustomTangent;
        FVector& Driver = Handle == 1 ? Point.ArriveTangent : Point.LeaveTangent;
        FVector& Follower = Handle == 1 ? Point.LeaveTangent : Point.ArriveTangent;
        const double OtherLength = Follower.Size();
        const bool bDifferent = !Driver.Equals(NewTangent, 0.000001);
        Driver = NewTangent;
        if (Mode == EStrandBezierHandleMode::Mirrored)
        {
            const bool bFollowerDifferent = !Follower.Equals(NewTangent, 0.000001);
            Follower = NewTangent;
            return bDifferent || bFollowerDifferent;
        }
        if (Mode == EStrandBezierHandleMode::Aligned && !NewTangent.IsNearlyZero())
        {
            const FVector Aligned = NewTangent.GetSafeNormal() * OtherLength;
            const bool bFollowerDifferent = !Follower.Equals(Aligned, 0.000001);
            Follower = Aligned;
            return bDifferent || bFollowerDifferent;
        }
        return bDifferent;
    }

    // Evaluate old Auto/Clamped geometry BEFORE converting to custom tangents.
    // This avoids a sudden shape change when switching handle modes.
    inline bool SetHandleMode(FSplinePointData& Point, EStrandBezierHandleMode Mode,
        const FVector& EvaluatedArrive, const FVector& EvaluatedLeave)
    {
        if (EvaluatedArrive.ContainsNaN() || EvaluatedLeave.ContainsNaN()) { return false; }
        const EStrandBezierHandleMode OldMode = EffectiveHandleMode(Point);
        const EStrandBezierHandleMode OldStoredMode = Point.HandleMode;
        const ESplinePointType::Type OldType = Point.PointType.GetValue();
        const FVector OldArrive = Point.ArriveTangent;
        const FVector OldLeave = Point.LeaveTangent;
        Point.HandleMode = Mode;
        if (Mode == EStrandBezierHandleMode::Auto)
        {
            Point.PointType = ESplinePointType::Curve;
        }
        else
        {
            if (Point.PointType.GetValue() != ESplinePointType::CurveCustomTangent)
            {
                Point.ArriveTangent = EvaluatedArrive;
                Point.LeaveTangent = EvaluatedLeave;
            }
            Point.PointType = ESplinePointType::CurveCustomTangent;
            if (Mode == EStrandBezierHandleMode::Corner)
            {
                Point.ArriveTangent = FVector::ZeroVector;
                Point.LeaveTangent = FVector::ZeroVector;
            }
            else if (Mode == EStrandBezierHandleMode::Aligned || Mode == EStrandBezierHandleMode::Mirrored)
            {
                // Prefer the outgoing handle if available. In zero-handle cases,
                // persist the constraint until the first nonzero user drag.
                const FVector Driver = !Point.LeaveTangent.IsNearlyZero()
                    ? Point.LeaveTangent : Point.ArriveTangent;
                if (!Driver.IsNearlyZero())
                {
                    const FVector Axis = Driver.GetSafeNormal();
                    const double Length = Mode == EStrandBezierHandleMode::Mirrored
                        ? Driver.Size() : Point.ArriveTangent.Size();
                    Point.ArriveTangent = Axis * Length;
                    Point.LeaveTangent = Driver;
                }
            }
        }
        return OldMode != Mode || OldStoredMode != Point.HandleMode ||
            OldType != Point.PointType.GetValue() ||
            !OldArrive.Equals(Point.ArriveTangent, 0.000001) ||
            !OldLeave.Equals(Point.LeaveTangent, 0.000001);
    }

    // Edit the interior of a Hermite span without moving either endpoint.
    // For fixed endpoints, the only free terms are outgoing M0 and incoming M1.
    // Their Hermite basis weights at t are h10 and h11. Use a least-norm
    // derivative update so the displacement at t exactly matches Delta.
    // Automatic/linear endpoints become custom while retaining all tangents
    // on neighboring segments, preventing an unwanted shape jump elsewhere.
    inline bool BendSegment(TArray<FSplinePointData>& Points, bool bClosed,
        int32 Segment, double Alpha, const FVector& Delta,
        const FSplinePoint& EvaluatedStart, const FSplinePoint& EvaluatedEnd)
    {
        const int32 Count = Points.Num();
        if (Count < 2 || Segment < 0 || Segment >= Count - (bClosed ? 0 : 1) ||
            !FMath::IsFinite(Alpha) || Alpha < 0.1 || Alpha > 0.9 ||
            Delta.ContainsNaN() || Delta.IsNearlyZero()) { return false; }
        const int32 Next = (Segment + 1) % Count;
        FSplinePointData& A = Points[Segment];
        FSplinePointData& B = Points[Next];
        const FVector Chord = B.LocalPosition - A.LocalPosition;
        if (EvaluatedStart.Type == ESplinePointType::Constant) { return false; }
        // A linear segment has a Hermite representation using this chord for
        // both tangent vectors; make that the baseline before bending it.
        const bool bLinear = EvaluatedStart.Type == ESplinePointType::Linear;
        A.ArriveTangent = EvaluatedStart.ArriveTangent;
        A.LeaveTangent = bLinear ? Chord : EvaluatedStart.LeaveTangent;
        B.ArriveTangent = bLinear ? Chord : EvaluatedEnd.ArriveTangent;
        B.LeaveTangent = EvaluatedEnd.LeaveTangent;
        const double T = Alpha;
        const double H10 = T * (1.0 - T) * (1.0 - T);
        const double H11 = -T * T * (1.0 - T);
        const double NormSq = H10 * H10 + H11 * H11;
        if (NormSq <= UE_DOUBLE_SMALL_NUMBER) { return false; }
        A.LeaveTangent += Delta * (H10 / NormSq);
        B.ArriveTangent += Delta * (H11 / NormSq);
        A.PointType = B.PointType = ESplinePointType::CurveCustomTangent;
        A.HandleMode = B.HandleMode = EStrandBezierHandleMode::Free;
        return !A.LeaveTangent.ContainsNaN() && !B.ArriveTangent.ContainsNaN();
    }

    // Standard editable spline primitives, authored directly as native
    // spline point data. Do not replace them with sampled polylines.
    inline bool MakePrimitive(int32 Kind, TArray<FSplinePointData>& OutPoints, bool& bOutClosed)
    {
        if (Kind < 0 || Kind > 2) { return false; }
        TArray<FSplinePointData> Result;
        const int32 Count = Kind == 2 ? 25 : 4;
        Result.Reserve(Count);
        constexpr double Radius = 100.0;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FSplinePointData Point;
            if (Kind == 0)
            {
                const double Angle = UE_PI * 0.5 * Index;
                const FVector Forward(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0);
                Point.LocalPosition = FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0);
                // Cubic Hermite derivative = 3 * Bezier control-arm length.
                Point.ArriveTangent = Point.LeaveTangent = Forward *
                    (4.0 * Radius * FMath::Tan(UE_PI / 8.0));
                Point.PointType = ESplinePointType::CurveCustomTangent;
            }
            else if (Kind == 1)
            {
                const FVector Corners[4] = {
                    FVector(-100.0, -75.0, 0.0), FVector(100.0, -75.0, 0.0),
                    FVector(100.0, 75.0, 0.0), FVector(-100.0, 75.0, 0.0) };
                Point.LocalPosition = Corners[Index];
                Point.PointType = ESplinePointType::Linear;
            }
            else
            {
                const double T = static_cast<double>(Index) / (Count - 1);
                const double Angle = 4.0 * UE_PI * T;
                const double StepAngle = 4.0 * UE_PI / (Count - 1);
                Point.LocalPosition = FVector(Radius * FMath::Cos(Angle),
                    Radius * FMath::Sin(Angle), 400.0 * T);
                Point.ArriveTangent = Point.LeaveTangent = FVector(
                    -Radius * FMath::Sin(Angle) * StepAngle,
                    Radius * FMath::Cos(Angle) * StepAngle, 400.0 / (Count - 1));
                Point.PointType = ESplinePointType::CurveCustomTangent;
            }
            Point.HandleMode = EStrandBezierHandleMode::Free;
            Result.Add(Point);
        }
        OutPoints = MoveTemp(Result);
        bOutClosed = Kind != 2;
        return true;
    }

    // Mutate an existing Strand asset in place, not create a second asset.
    // Callers own FScopedTransaction, Modify, source-link handling and refresh.
    inline bool ReplacePrimitiveGeometry(USplineDataAsset& Target, int32 Kind)
    {
        TArray<FSplinePointData> Points;
        bool bClosed = false;
        if (!MakePrimitive(Kind, Points, bClosed)) { return false; }
        Target.Points = MoveTemp(Points);
        Target.bIsClosedLoop = bClosed;
        return true;
    }

    // Extend a native open spline in the *evaluated* endpoint direction. Auto
    // points often store zero tangents; the evaluated native spline is the
    // source of truth. Converting the old endpoint to custom tangents preserves
    // its current evaluated handles when the new anchor is added.
    inline bool ExtendOpenEndpoint(TArray<FSplinePointData>& Points,
        const USplineComponent* Spline, bool bAtStart)
    {
        const int32 Count = Points.Num();
        if (!IsValid(Spline) || Count < 2 || Count >= 100000 ||
            Spline->GetNumberOfSplinePoints() != Count) { return false; }
        const int32 Edge = bAtStart ? 0 : Count - 1;
        const int32 Neighbor = bAtStart ? 1 : Count - 2;
        const FSplinePoint Evaluated = Spline->GetSplinePointAt(Edge, ESplineCoordinateSpace::Local);
        FSplinePointData& Endpoint = Points[Edge];
        const double NeighborDistance = (Endpoint.LocalPosition - Points[Neighbor].LocalPosition).Size();
        if (NeighborDistance <= KINDA_SMALL_NUMBER) { return false; }
        const FVector AlongCurve = bAtStart ? -Evaluated.ArriveTangent : Evaluated.LeaveTangent;
        const FVector Fallback = Endpoint.LocalPosition - Points[Neighbor].LocalPosition;
        const FVector Direction = !AlongCurve.IsNearlyZero() ? AlongCurve.GetSafeNormal() : Fallback.GetSafeNormal();
        if (Direction.IsNearlyZero()) { return false; }
        const double Step = FMath::Max(NeighborDistance * 0.5, 1.0);
        FSplinePointData Added = Endpoint;
        Added.LocalPosition += Direction * Step;
        Added.PointType = ESplinePointType::CurveCustomTangent;
        Added.HandleMode = EStrandBezierHandleMode::Aligned;
        // Native arrive/leave tangent vectors both point in *forward*
        // spline travel direction, even at the start of the curve.
        const FVector NewTangent = (bAtStart ? -Direction : Direction) * Step;
        Added.ArriveTangent = NewTangent;
        Added.LeaveTangent = NewTangent;
        if (Endpoint.PointType.GetValue() == ESplinePointType::Curve ||
            Endpoint.PointType.GetValue() == ESplinePointType::CurveClamped)
        {
            Endpoint.ArriveTangent = Evaluated.ArriveTangent;
            Endpoint.LeaveTangent = Evaluated.LeaveTangent;
            Endpoint.PointType = ESplinePointType::CurveCustomTangent;
            Endpoint.HandleMode = EStrandBezierHandleMode::Free;
        }
        if (bAtStart) { Points.Insert(Added, 0); }
        else { Points.Add(Added); }
        return true;
    }

    // Arc-length resampling rebuilds control points at roughly equal spacing.
    // Unlike exact Bezier subdivision, resampling is an approximation and the
    // caller must make it an explicit, undoable operation.
    inline bool ResampleByArcLength(const USplineComponent* Spline,
        const TArray<FSplinePointData>& Original, bool bClosed, int32 TargetPoints,
        TArray<FSplinePointData>& OutPoints)
    {
        if (!IsValid(Spline) || Original.Num() < 2 || TargetPoints < 2 || TargetPoints > 4096)
        {
            return false;
        }
        const double Length = Spline->GetSplineLength();
        if (!FMath::IsFinite(Length) || Length <= KINDA_SMALL_NUMBER) { return false; }
        bool bAllLinear = true;
        for (const FSplinePointData& Point : Original)
        {
            if (Point.PointType.GetValue() != ESplinePointType::Linear) { bAllLinear = false; break; }
        }
        const double Spacing = Length / static_cast<double>(bClosed ? TargetPoints : TargetPoints - 1);
        TArray<FSplinePointData> NewPoints;
        NewPoints.Reserve(TargetPoints);
        for (int32 Index = 0; Index < TargetPoints; ++Index)
        {
            const double Distance = FMath::Min(Length, static_cast<double>(Index) * Spacing);
            FSplinePointData Point;
            Point.LocalPosition = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
            Point.LocalRotation = Spline->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
            Point.LocalScale = Spline->GetScaleAtDistanceAlongSpline(Distance);
            Point.PointType = bAllLinear ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent;
            if (!bAllLinear)
            {
                const FVector Tangent = Spline->GetDirectionAtDistanceAlongSpline(
                    Distance, ESplineCoordinateSpace::Local) * Spacing;
                Point.ArriveTangent = Tangent;
                Point.LeaveTangent = Tangent;
                Point.HandleMode = EStrandBezierHandleMode::Aligned;
            }
            if (Point.LocalPosition.ContainsNaN() || Point.ArriveTangent.ContainsNaN() ||
                Point.LeaveTangent.ContainsNaN()) { return false; }
            NewPoints.Add(Point);
        }
        OutPoints = MoveTemp(NewPoints);
        return true;
    }

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
                // Unreal stores interpolation on the segment START, while
                // Strand stores handle intent on the geometric anchor.
                // If a shifted native segment type is auto, native smoothing
                // takes precedence; never leave an Auto rule on custom data.
                if (Points[I].PointType.GetValue() == ESplinePointType::CurveCustomTangent &&
                    Points[I].HandleMode == EStrandBezierHandleMode::Auto)
                {
                    Points[I].HandleMode = EStrandBezierHandleMode::Free;
                }
                if ((Points[I].PointType.GetValue() == ESplinePointType::Linear ||
                     Points[I].PointType.GetValue() == ESplinePointType::Constant) &&
                    Points[I].HandleMode == EStrandBezierHandleMode::Auto)
                {
                    Points[I].HandleMode = EStrandBezierHandleMode::Free;
                }
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
            Middle.HandleMode = EStrandBezierHandleMode::Free;
            Middle.ArriveTangent = FVector::ZeroVector;
            Middle.LeaveTangent = FVector::ZeroVector;
        }
        else if (A.Type == ESplinePointType::Constant)
        {
            Middle.LocalPosition = A.Position;
            Middle.PointType = ESplinePointType::Constant;
            Middle.HandleMode = EStrandBezierHandleMode::Free;
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
            Points[Segment].HandleMode = EStrandBezierHandleMode::Free;
            Points[Next].PointType = ESplinePointType::CurveCustomTangent;
            Points[Next].ArriveTangent = 3.0 * (B.Position - R);
            Points[Next].LeaveTangent = B.LeaveTangent;
            Points[Next].HandleMode = EStrandBezierHandleMode::Free;
            Middle.HandleMode = EStrandBezierHandleMode::Free;
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
        NewPoint.HandleMode = EStrandBezierHandleMode::Corner;
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
        Point.HandleMode = EStrandBezierHandleMode::Mirrored;
        return true;
    }

    // Marquee selection is transient viewport state. Keep the replace/add/
    // toggle rules in one testable helper, independent of hit testing.
    inline void ApplySelectionHits(TSet<int32>& Selected, int32 PointCount,
        const TArray<int32>& Hits, bool bAdd, bool bToggle)
    {
        if (!bAdd && !bToggle) { Selected.Reset(); }
        for (int32 Index : Hits)
        {
            if (Index < 0 || Index >= PointCount) { continue; }
            if (bToggle && Selected.Contains(Index)) { Selected.Remove(Index); }
            else { Selected.Add(Index); }
        }
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

    // Direct orientation authoring changes ONLY the rotation channel. This must
    // never call USplineComponent::SetRotationAtSplinePoint, which can also
    // change spline tangents. Tangent alignment remains a separate operation.
    inline bool SetSelectedOrientation(TArray<FSplinePointData>& Points,
        const TArray<int32>& Indices, const FRotator& NewOrientation)
    {
        if (NewOrientation.ContainsNaN()) { return false; }
        const FQuat Target = NewOrientation.Quaternion().GetNormalized();
        bool bChanged = false;
        for (int32 Index : Indices)
        {
            if (!Points.IsValidIndex(Index)) { continue; }
            FSplinePointData& Point = Points[Index];
            if (Point.LocalRotation.Quaternion().Equals(Target, 0.000001)) { continue; }
            Point.LocalRotation = Target.Rotator();
            bChanged = true;
        }
        return bChanged;
    }

    // Group transforms use the average selected anchor as pivot. Transform
    // tangents as vectors, never translate them. Unselected points are untouched.
    inline double PositionEntryDelta(double CurrentCoordinate, double EnteredValue, bool bRelative)
    {
        return bRelative ? EnteredValue : EnteredValue - CurrentCoordinate;
    }

    // Unreal reports signed widget angles in its selected coordinate frame.
    // A local delta is conjugated into spline asset axes before changing points.
    inline FQuat WidgetRotationToAsset(const FQuat& WidgetDelta,
        const FQuat& WidgetFrame, bool bLocal)
    {
        if (WidgetDelta.ContainsNaN() || WidgetFrame.ContainsNaN()) { return FQuat::Identity; }
        const FQuat Delta = WidgetDelta.GetNormalized();
        if (!bLocal) { return Delta; }
        const FQuat Frame = WidgetFrame.GetNormalized();
        return (Frame * Delta * Frame.Inverse()).GetNormalized();
    }

    inline bool RotateSelectedQuat(TArray<FSplinePointData>& Points, const TArray<int32>& Indices,
        const FQuat& InRotation)
    {
        if (Indices.IsEmpty() || InRotation.ContainsNaN()) { return false; }
        const FQuat Rotation = InRotation.GetNormalized();
        if (Rotation.IsIdentity(0.000001)) { return false; }
        const FVector Pivot = SelectionCenter(Points, Indices);
        bool bChanged = false;
        for (int32 Index : Indices)
        {
            if (!Points.IsValidIndex(Index)) { continue; }
            FSplinePointData& Point = Points[Index];
            Point.LocalPosition = Pivot + Rotation.RotateVector(Point.LocalPosition - Pivot);
            Point.ArriveTangent = Rotation.RotateVector(Point.ArriveTangent);
            Point.LeaveTangent = Rotation.RotateVector(Point.LeaveTangent);
            Point.LocalRotation = (Rotation * Point.LocalRotation.Quaternion()).Rotator();
            bChanged = true;
        }
        return bChanged;
    }

    inline bool RotateSelected(TArray<FSplinePointData>& Points, const TArray<int32>& Indices,
        int32 Axis, double Degrees)
    {
        if (Axis < 0 || Axis > 2 || !FMath::IsFinite(Degrees)) { return false; }
        const FVector Basis = Axis == 0 ? FVector::XAxisVector :
            Axis == 1 ? FVector::YAxisVector : FVector::ZAxisVector;
        return RotateSelectedQuat(Points, Indices,
            FQuat(Basis, FMath::DegreesToRadians(Degrees)));
    }

    inline bool ScaleSelected(TArray<FSplinePointData>& Points, const TArray<int32>& Indices,
        double Factor)
    {
        // Negative or zero scales can collapse/reverse curve topology. Provide
        // positive uniform scaling only; mirroring has its own explicit tools.
        if (Indices.IsEmpty() || !FMath::IsFinite(Factor) || Factor <= 0.0 ||
            FMath::IsNearlyEqual(Factor, 1.0, 0.000001)) { return false; }
        const FVector Pivot = SelectionCenter(Points, Indices);
        bool bChanged = false;
        for (int32 Index : Indices)
        {
            if (!Points.IsValidIndex(Index)) { continue; }
            FSplinePointData& Point = Points[Index];
            Point.LocalPosition = Pivot + (Point.LocalPosition - Pivot) * Factor;
            Point.ArriveTangent *= Factor;
            Point.LeaveTangent *= Factor;
            bChanged = true;
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
        Point.HandleMode = EStrandBezierHandleMode::Free; // one-shot command is not a persistent link
        Point.ArriveTangent = bUseIncoming ? Driver : Direction * (bMirror ? Driver.Size() : Arrive.Size());
        Point.LeaveTangent = bUseIncoming ? Direction * (bMirror ? Driver.Size() : Leave.Size()) : Driver;
        return true;
    }

    inline void MakeAllLinear(TArray<FSplinePointData>& Points)
    {
        for (FSplinePointData& Point : Points)
        {
            Point.PointType = ESplinePointType::Linear;
            Point.HandleMode = EStrandBezierHandleMode::Free;
            Point.ArriveTangent = FVector::ZeroVector;
            Point.LeaveTangent = FVector::ZeroVector;
        }
    }
    inline void MakeAllAuto(TArray<FSplinePointData>& Points)
    {
        for (FSplinePointData& Point : Points) { Point.PointType = ESplinePointType::Curve; Point.HandleMode = EStrandBezierHandleMode::Auto; }
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
                Mid.HandleMode = EStrandBezierHandleMode::Free;
                Mid.ArriveTangent = FVector::ZeroVector;
                Mid.LeaveTangent = FVector::ZeroVector;
                Original[I].PointType = ESplinePointType::Linear;
            }
            else if (A.Type == ESplinePointType::Constant)
            {
                Mid.LocalPosition = A.Position;
                Mid.PointType = ESplinePointType::Constant;
                Mid.HandleMode = EStrandBezierHandleMode::Free;
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
                Mid.HandleMode = EStrandBezierHandleMode::Free;
                Original[I].HandleMode = EStrandBezierHandleMode::Free;
                Original[J].HandleMode = EStrandBezierHandleMode::Free;
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
