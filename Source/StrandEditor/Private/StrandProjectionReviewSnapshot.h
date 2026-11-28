#pragma once

#include "CoreMinimal.h"
#include "Components/SplineComponent.h"

// Native, editor-only guard for the interval between Review and Apply. The
// original input must not be edited underneath a previously computed preview.
namespace StrandProjectionReviewSnapshot
{
    struct FSnapshot
    {
        FTransform Transform;
        TArray<FSplinePoint> Points;
        bool bClosed = false;

        static FSnapshot Capture(const USplineComponent* Spline)
        {
            FSnapshot Result;
            if (!IsValid(Spline)) { return Result; }
            Result.Transform = Spline->GetComponentTransform();
            Result.bClosed = Spline->IsClosedLoop();
            const int32 Count = Spline->GetNumberOfSplinePoints();
            Result.Points.Reserve(Count);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Result.Points.Add(Spline->GetSplinePointAt(Index, ESplineCoordinateSpace::Local));
            }
            return Result;
        }

        bool Matches(const USplineComponent* Spline) const
        {
            if (!IsValid(Spline) || !Spline->GetComponentTransform().Equals(Transform) ||
                Spline->IsClosedLoop() != bClosed || Spline->GetNumberOfSplinePoints() != Points.Num())
            { return false; }
            for (int32 Index = 0; Index < Points.Num(); ++Index)
            {
                const FSplinePoint Current = Spline->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
                const FSplinePoint& Saved = Points[Index];
                if (!Current.Position.Equals(Saved.Position, 0.001) ||
                    !Current.ArriveTangent.Equals(Saved.ArriveTangent, 0.001) ||
                    !Current.LeaveTangent.Equals(Saved.LeaveTangent, 0.001) ||
                    !Current.Rotation.Equals(Saved.Rotation, 0.001) ||
                    !Current.Scale.Equals(Saved.Scale, 0.001) ||
                    Current.Type != Saved.Type)
                { return false; }
            }
            return true;
        }

        void Restore(USplineComponent* Spline) const
        {
            if (!IsValid(Spline)) { return; }
            Spline->ClearSplinePoints(false);
            for (const FSplinePoint& Point : Points) { Spline->AddPoint(Point, false); }
            Spline->SetClosedLoop(bClosed, false);
            Spline->UpdateSpline();
        }
    };
}
