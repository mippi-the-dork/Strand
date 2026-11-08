// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

/** Screen-preserving camera-ray projection with adaptive native Hermite reconstruction.
 *  ALL coordinates are spline-local, using the asset viewport's identity preview transform.
 *  Unlike parallel projection, the projection is invariant under the captured perspective camera.
 */
namespace StrandPerspectiveFlatten
{
    struct FCamera
    {
        FVector Location = FVector::ZeroVector;
        FVector Forward = FVector::XAxisVector;
        double HorizontalFOVDegrees = 90.0;
        int32 ViewportWidth = 1280;
    };

    inline bool Project(const FVector& Position, const FCamera& Camera, double PlaneDepth, FVector& Out)
    {
        const FVector Forward = Camera.Forward.GetSafeNormal();
        if (Position.ContainsNaN() || Camera.Location.ContainsNaN() || Forward.IsNearlyZero() ||
            !FMath::IsFinite(PlaneDepth) || PlaneDepth <= 1.0) { return false; }
        const FVector Ray = Position - Camera.Location;
        const double Depth = FVector::DotProduct(Ray, Forward);
        if (!FMath::IsFinite(Depth) || Depth <= 1.0) { return false; }
        Out = Camera.Location + Ray * (PlaneDepth / Depth);
        return !Out.ContainsNaN();
    }

    inline FVector Hermite(const FVector& P0, const FVector& T0, const FVector& P1, const FVector& T1, double T)
    {
        const double T2 = T * T, T3 = T2 * T;
        return (2*T3-3*T2+1)*P0 + (T3-2*T2+T)*T0 + (-2*T3+3*T2)*P1 + (T3-T2)*T1;
    }

    /** Project entire evaluated native spline, rebuilding its cubic segments only where needed.
     *  The tolerance is measured in screen pixels at the captured camera/viewport width.
     *  Existing authored endpoints keep their rotation and scale; inserted anchors are interpolated.
     *  On failure Output is untouched; never partially modify an asset.
     */
    inline bool Build(const USplineComponent* Source, const TArray<FSplinePointData>& Original,
        bool bClosed, const FCamera& Camera, double TolerancePixels,
        TArray<FSplinePointData>& Output, FString& Reason, int32 MaxPoints = 2048)
    {
        Reason.Empty();
        const int32 N = Original.Num();
        if (!IsValid(Source) || N < 2 || Source->GetNumberOfSplinePoints() != N ||
            Camera.Forward.IsNearlyZero() || Camera.ViewportWidth <= 0 ||
            Camera.HorizontalFOVDegrees <= 1.0 || Camera.HorizontalFOVDegrees >= 170.0 ||
            !FMath::IsFinite(TolerancePixels) || TolerancePixels <= 0.0)
        { Reason = TEXT("Invalid camera, spline or tolerance."); return false; }
        const FVector Forward = Camera.Forward.GetSafeNormal();
        for (int32 I = 0; I < (bClosed ? N : N - 1); ++I)
        {
            if (Original[I].PointType == ESplinePointType::Constant)
            {
                Reason = TEXT("Constant-step segments cannot be flattened with continuous cubic reconstruction. Convert to linear/curve first.");
                return false;
            }
        }
        double DepthSum = 0.0;
        for (const FSplinePointData& P : Original)
        {
            const double Depth = FVector::DotProduct(P.LocalPosition - Camera.Location, Forward);
            if (!FMath::IsFinite(Depth) || Depth <= 1.0)
            { Reason = TEXT("One or more spline anchors are on or behind the camera. Reposition the camera."); return false; }
            DepthSum += Depth;
        }
        const double PlaneDepth = DepthSum / N;
        const double WorldUnitsPerPixel = (2.0 * PlaneDepth *
            FMath::Tan(FMath::DegreesToRadians(Camera.HorizontalFOVDegrees * 0.5))) /
            Camera.ViewportWidth;
        const double ToleranceUnits = WorldUnitsPerPixel * TolerancePixels;
        if (!FMath::IsFinite(ToleranceUnits) || ToleranceUnits <= 0.0)
        { Reason = TEXT("Could not determine pixel tolerance at this camera depth."); return false; }

        TArray<FSplinePointData> Result;
        Result.Reserve(FMath::Min(MaxPoints, N * 5));
        auto PositionAt = [Source](int32 Segment, double Alpha)
        {
            return Source->GetLocationAtSplineInputKey(Segment + Alpha, ESplineCoordinateSpace::Local);
        };
        auto ProjectAt = [&](int32 Segment, double Alpha, FVector& Out)
        {
            return Project(PositionAt(Segment, Alpha), Camera, PlaneDepth, Out);
        };
        FVector FirstProjected;
        if (!ProjectAt(0, 0.0, FirstProjected))
        { Reason = TEXT("Camera projection is undefined at the start of the spline."); return false; }
        FSplinePointData First = Original[0];
        First.LocalPosition = FirstProjected;
        First.PointType = ESplinePointType::CurveCustomTangent;
        First.HandleMode = EStrandBezierHandleMode::Free;
        Result.Add(First);
        const int32 Segments = bClosed ? N : N - 1;
        for (int32 Segment = 0; Segment < Segments; ++Segment)
        {
            const int32 NextIndex = (Segment + 1) % N;
            const bool bLinear = Original[Segment].PointType == ESplinePointType::Linear ||
                Original[Segment].PointType == ESplinePointType::Constant;
            auto DerivativeAt = [&](double A, double Lo, double Hi, FVector& Deriv)
            {
                constexpr double Eps = 0.0001;
                const double Left = A <= Lo + Eps ? A : A - Eps;
                const double Right = A >= Hi - Eps ? A : A + Eps;
                FVector P0, P1;
                if (!ProjectAt(Segment, Left, P0) || !ProjectAt(Segment, Right, P1) ||
                    FMath::IsNearlyEqual(Left, Right)) { return false; }
                Deriv = (P1 - P0) / (Right - Left);
                return !Deriv.ContainsNaN();
            };
            TFunction<bool(double,double,int32)> AppendInterval;
            AppendInterval = [&](double A, double B, int32 Depth)
            {
                FVector P0, P1, D0, D1;
                if (!ProjectAt(Segment, A, P0) || !ProjectAt(Segment, B, P1))
                { Reason = TEXT("A spline section crosses the camera plane. Move the camera."); return false; }
                if (!DerivativeAt(A, A, B, D0) || !DerivativeAt(B, A, B, D1))
                { Reason = TEXT("Could not evaluate projected spline tangent."); return false; }
                const double Span = B - A;
                const FVector T0 = D0 * Span, T1 = D1 * Span;
                double Error = 0.0;
                if (!bLinear)
                {
                    for (double Fraction : {0.25, 0.5, 0.75})
                    {
                        FVector Expected;
                        if (!ProjectAt(Segment, FMath::Lerp(A, B, Fraction), Expected))
                        { Reason = TEXT("Projection crosses the camera plane."); return false; }
                        Error = FMath::Max(Error, FVector::Distance(Expected, Hermite(P0,T0,P1,T1,Fraction)));
                    }
                }
                if (Error > ToleranceUnits)
                {
                    if (Depth >= 12)
                    { Reason = TEXT("The projected curve needs excessive subdivision. Increase tolerance or change the view."); return false; }
                    const double Mid = (A + B) * 0.5;
                    return AppendInterval(A, Mid, Depth + 1) && AppendInterval(Mid, B, Depth + 1);
                }
                if (Result.Num() >= MaxPoints)
                { Reason = TEXT("Flatten would exceed the 2048-anchor safety cap. Increase pixel tolerance."); return false; }
                // Native point interpolation is owned by the START of each segment.
                // Do not accidentally inherit the preceding segment's Linear mode.
                Result.Last().PointType = bLinear ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent;
                Result.Last().LeaveTangent = T0;
                // The final closed-loop segment terminates at anchor zero, not a duplicate anchor.
                if (bClosed && Segment == Segments - 1 && B >= 1.0 - 1.e-8)
                {
                    Result[0].ArriveTangent = T1;
                    return true;
                }
                FSplinePointData End;
                if (B >= 1.0 - 1.e-8)
                {
                    End = Original[NextIndex];
                }
                else
                {
                    End = Original[Segment];
                    End.LocalRotation = FMath::Lerp(Original[Segment].LocalRotation,
                        Original[NextIndex].LocalRotation, B);
                    End.LocalScale = FMath::Lerp(Original[Segment].LocalScale,
                        Original[NextIndex].LocalScale, B);
                }
                End.LocalPosition = P1;
                End.ArriveTangent = T1;
                End.LeaveTangent = FVector::ZeroVector;
                // The next interval/segment assigns its own outgoing interpolation.
                End.PointType = ESplinePointType::CurveCustomTangent;
                End.HandleMode = EStrandBezierHandleMode::Free;
                Result.Add(End);
                return true;
            };
            if (!AppendInterval(0.0, 1.0, 0)) { return false; }
        }
        Output = MoveTemp(Result);
        return true;
    }
}
