// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

/** Strand orientation edits: authored point rotations only; never reshape geometry. */
namespace StrandSplineOrientationOperations
{
    inline bool SetRoll(TArray<FSplinePointData>& Points, const TArray<int32>& Selection, double Degrees)
    {
        if (!FMath::IsFinite(Degrees)) { return false; }
        bool bChanged = false;
        for (const int32 Index : Selection)
        {
            if (!Points.IsValidIndex(Index)) { continue; }
            FRotator& Rotation = Points[Index].LocalRotation;
            if (FMath::IsNearlyEqual(Rotation.Roll, Degrees, 0.000001)) { continue; }
            Rotation.Roll = Degrees;
            bChanged = true;
        }
        return bChanged;
    }

    inline bool AddRoll(TArray<FSplinePointData>& Points, const TArray<int32>& Selection, double Degrees)
    {
        if (!FMath::IsFinite(Degrees) || FMath::IsNearlyZero(Degrees)) { return false; }
        bool bChanged = false;
        for (const int32 Index : Selection)
        {
            if (!Points.IsValidIndex(Index)) { continue; }
            const double Target = Points[Index].LocalRotation.Roll + Degrees;
            if (!FMath::IsFinite(Target)) { continue; }
            Points[Index].LocalRotation.Roll = Target;
            bChanged = true;
        }
        return bChanged;
    }

    /** Roll from first to last selected anchor, along native cumulative spline distance.
     * The shortest angular path is intentional: use AddRoll for extra full revolutions.
     * Unselected intermediate anchors are left unchanged.
     */
    inline bool DistributeRoll(TArray<FSplinePointData>& Points, const TArray<int32>& Selection,
        const USplineComponent* Spline)
    {
        if (!IsValid(Spline) || Selection.Num() < 3) { return false; }
        TArray<int32> Indices;
        for (const int32 Index : Selection)
        {
            if (Points.IsValidIndex(Index) && Index < Spline->GetNumberOfSplinePoints()) { Indices.AddUnique(Index); }
        }
        Indices.Sort();
        if (Indices.Num() < 3) { return false; }
        const int32 First = Indices[0];
        const int32 Last = Indices.Last();
        const double Begin = Spline->GetDistanceAlongSplineAtSplinePoint(First);
        const double End = Spline->GetDistanceAlongSplineAtSplinePoint(Last);
        if (End - Begin <= UE_DOUBLE_SMALL_NUMBER) { return false; }
        const double StartRoll = Points[First].LocalRotation.Roll;
        const double RollDelta = FMath::FindDeltaAngleDegrees(StartRoll, Points[Last].LocalRotation.Roll);
        bool bChanged = false;
        for (int32 I = 1; I < Indices.Num() - 1; ++I)
        {
            const int32 Index = Indices[I];
            const double Distance = Spline->GetDistanceAlongSplineAtSplinePoint(Index);
            const double Fraction = FMath::Clamp((Distance - Begin) / (End - Begin), 0.0, 1.0);
            const double Target = StartRoll + RollDelta * Fraction;
            if (!FMath::IsNearlyEqual(Points[Index].LocalRotation.Roll, Target, 0.000001))
            {
                Points[Index].LocalRotation.Roll = Target;
                bChanged = true;
            }
        }
        return bChanged;
    }
}
