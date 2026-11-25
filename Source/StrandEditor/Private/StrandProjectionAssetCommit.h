#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

// Shared by the asset-editor Projection Apply callback and its native automation
// regression. The scene actor is only a temporary geometry carrier: the Strand
// asset remains the authoring source, including its non-native handle policies.
namespace StrandProjectionAssetCommit
{
    inline void BakePointToWorld(FSplinePointData& Point, const FTransform& FromLocal)
    {
        Point.LocalPosition = FromLocal.TransformPosition(Point.LocalPosition);
        Point.ArriveTangent = FromLocal.TransformVector(Point.ArriveTangent);
        Point.LeaveTangent = FromLocal.TransformVector(Point.LeaveTangent);
        Point.LocalRotation = (FromLocal.GetRotation() * Point.LocalRotation.Quaternion()).Rotator();
        Point.LocalScale *= FromLocal.GetScale3D();
    }

    inline bool Apply(USplineDataAsset* Asset, const USplineComponent* Source,
        const TArray<int32>* OriginalIndices = nullptr, bool bBakeWorldPlacement = false)
    {
        if (!IsValid(Asset) || !IsValid(Source) ||
            (OriginalIndices ? OriginalIndices->Num() != Source->GetNumberOfSplinePoints()
                             : Asset->Points.Num() != Source->GetNumberOfSplinePoints()))
        { return false; }
        if (OriginalIndices)
        {
            TSet<int32> Seen;
            for (int32 Index : *OriginalIndices)
            {
                if (Index != INDEX_NONE)
                {
                    if (!Asset->Points.IsValidIndex(Index) || Seen.Contains(Index)) { return false; }
                    Seen.Add(Index);
                }
            }
            if (Seen.Num() != Asset->Points.Num()) { return false; }
        }
        Asset->Modify();
        const TArray<FSplinePointData> Previous = Asset->Points;
        Asset->SerializeFromSplineComponent(Source);
        const FTransform WorldPlacement = Source->GetComponentTransform();
        for (int32 Index = 0; Index < Asset->Points.Num(); ++Index)
        {
            if (bBakeWorldPlacement) { BakePointToWorld(Asset->Points[Index], WorldPlacement); }
            const int32 Original = OriginalIndices ? (*OriginalIndices)[Index] : Index;
            Asset->Points[Index].HandleMode = Original != INDEX_NONE
                ? Previous[Original].HandleMode : EStrandBezierHandleMode::Free;
        }
        if (bBakeWorldPlacement)
        {
            // World-baked geometry should default to no placement recentering
            // when spawned from the asset later.
            Asset->DefaultCoordinateMode = EStrandCoordinateMode::Absolute;
        }
        Asset->PostEditChange();
        Asset->MarkPackageDirty();
        return true;
    }
}
