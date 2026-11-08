// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// All coordinates are viewport pixels. Lasso testing never modifies spline data.
namespace StrandSplineSelectionUtils
{
    inline TSet<int32> GrowAnchorSelection(const TSet<int32>& Selected, int32 Count, bool bClosed)
    {
        TSet<int32> Result = Selected;
        if (Count < 2) { return Result; }
        for (int32 Index : Selected)
        {
            if (Index < 0 || Index >= Count) { continue; }
            if (Index > 0) { Result.Add(Index - 1); }
            else if (bClosed) { Result.Add(Count - 1); }
            if (Index + 1 < Count) { Result.Add(Index + 1); }
            else if (bClosed) { Result.Add(0); }
        }
        return Result;
    }

    inline TSet<int32> ShrinkAnchorSelection(const TSet<int32>& Selected, int32 Count, bool bClosed)
    {
        TSet<int32> Result;
        if (Count < 2) { return Result; }
        for (int32 Index : Selected)
        {
            if (Index < 0 || Index >= Count) { continue; }
            const bool bHasPrevious = Index == 0 ? (!bClosed || Selected.Contains(Count - 1)) : Selected.Contains(Index - 1);
            const bool bHasNext = Index + 1 == Count ? (!bClosed || Selected.Contains(0)) : Selected.Contains(Index + 1);
            if (bHasPrevious && bHasNext) { Result.Add(Index); }
        }
        return Result;
    }

    inline bool IsInsideLasso(const FVector2D& Position, const TArray<FIntPoint>& Outline)
    {
        if (Outline.Num() < 3 || !FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y)) { return false; }
        bool bInside = false;
        for (int32 I = 0, J = Outline.Num() - 1; I < Outline.Num(); J = I++)
        {
            const FVector2D A(Outline[I].X, Outline[I].Y);
            const FVector2D B(Outline[J].X, Outline[J].Y);
            const FVector2D AB = B - A;
            const FVector2D AP = Position - A;
            const double Area = FMath::Abs(AB.X * AP.Y - AB.Y * AP.X);
            const double LengthSquared = AB.SizeSquared();
            const double Projection = FVector2D::DotProduct(AP, AB);
            // A point on the boundary is included, making selection stable.
            if (LengthSquared > UE_DOUBLE_SMALL_NUMBER && Area <= FMath::Sqrt(LengthSquared) * 0.25 &&
                Projection >= 0.0 && Projection <= LengthSquared) { return true; }
            if ((A.Y > Position.Y) != (B.Y > Position.Y))
            {
                const double CrossingX = A.X + (Position.Y - A.Y) * AB.X / (B.Y - A.Y);
                if (Position.X < CrossingX) { bInside = !bInside; }
            }
        }
        return bInside;
    }

    inline bool ShouldAppendStrokePoint(const TArray<FIntPoint>& Outline, FIntPoint Next)
    {
        return Outline.Num() == 0 ||
            (Outline.Last() - Next).SizeSquared() >= 9;
    }

    inline bool IsSelectionStroke(const TArray<FIntPoint>& Outline)
    {
        if (Outline.Num() < 3) { return false; }
        FIntPoint Min = Outline[0], Max = Outline[0];
        for (const FIntPoint& Point : Outline)
        {
            Min.X = FMath::Min(Min.X, Point.X); Min.Y = FMath::Min(Min.Y, Point.Y);
            Max.X = FMath::Max(Max.X, Point.X); Max.Y = FMath::Max(Max.Y, Point.Y);
        }
        // Short strokes are clicks, not closed polygons.
        return Max.X - Min.X >= 4 && Max.Y - Min.Y >= 4;
    }
}
