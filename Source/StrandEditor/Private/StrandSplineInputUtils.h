// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

// Strand uses chorded letter keys for 3D precision movement because native
// Unreal editor viewport navigation owns Arrow, Page Up and Page Down.
// No camera key is ever intercepted for a nudge.
namespace StrandSplineInputUtils
{
    inline FVector NudgeDirection(const FKey& Key, bool bControl, bool bShift, bool bAlt)
    {
        if (!bControl || !bShift || bAlt) { return FVector::ZeroVector; }
        if (Key == EKeys::J) { return FVector(-1.0, 0.0, 0.0); }
        if (Key == EKeys::L) { return FVector(1.0, 0.0, 0.0); }
        if (Key == EKeys::I) { return FVector(0.0, 1.0, 0.0); }
        if (Key == EKeys::K) { return FVector(0.0, -1.0, 0.0); }
        if (Key == EKeys::U) { return FVector(0.0, 0.0, 1.0); }
        if (Key == EKeys::O) { return FVector(0.0, 0.0, -1.0); }
        return FVector::ZeroVector;
    }
}
