#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"

// Protect the live asset from applying stale preview geometry after another
// editor or command has modified the asset during a Projection session.
namespace StrandProjectionAssetBaseline
{
    struct FSnapshot
    {
        TArray<FSplinePointData> Points;
        bool bClosed = false;
        EStrandCoordinateMode CoordinateMode = EStrandCoordinateMode::Relative;
        EStrandAnchorMode AnchorMode = EStrandAnchorMode::PointCenter;
        int32 AnchorIndex = 0;

        static FSnapshot Capture(const USplineDataAsset* Asset)
        {
            FSnapshot Result;
            if (!IsValid(Asset)) { return Result; }
            Result.Points = Asset->Points;
            Result.bClosed = Asset->bIsClosedLoop;
            Result.CoordinateMode = Asset->DefaultCoordinateMode;
            Result.AnchorMode = Asset->DefaultAnchorMode;
            Result.AnchorIndex = Asset->DefaultAnchorPointIndex;
            return Result;
        }

        bool Matches(const USplineDataAsset* Asset) const
        {
            if (!IsValid(Asset) || Asset->bIsClosedLoop != bClosed ||
                Asset->DefaultCoordinateMode != CoordinateMode ||
                Asset->DefaultAnchorMode != AnchorMode ||
                Asset->DefaultAnchorPointIndex != AnchorIndex ||
                Asset->Points.Num() != Points.Num())
            { return false; }
            for (int32 Index = 0; Index < Points.Num(); ++Index)
            {
                const FSplinePointData& A = Points[Index];
                const FSplinePointData& B = Asset->Points[Index];
                if (!A.LocalPosition.Equals(B.LocalPosition, 0.001) ||
                    !A.ArriveTangent.Equals(B.ArriveTangent, 0.001) ||
                    !A.LeaveTangent.Equals(B.LeaveTangent, 0.001) ||
                    !A.LocalRotation.Equals(B.LocalRotation, 0.001) ||
                    !A.LocalScale.Equals(B.LocalScale, 0.001) ||
                    A.PointType != B.PointType || A.HandleMode != B.HandleMode)
                { return false; }
            }
            return true;
        }
    };
}
