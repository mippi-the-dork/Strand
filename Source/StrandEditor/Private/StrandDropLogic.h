#pragma once

#include "CoreMinimal.h"
#include "SplineDataAsset.h"

// The editor spawns any number of temporary previews for ONE asset. Counting
// PostSpawnActor calls is therefore never a valid way to detect multi-drop.
namespace StrandDropLogic
{
    inline bool HasMultipleDistinctStrandAssets(const TArray<UObject*>& DroppedObjects)
    {
        if (DroppedObjects.Num() < 2) { return false; }
        USplineDataAsset* First = nullptr;
        bool bDifferent = false;
        for (UObject* Object : DroppedObjects)
        {
            USplineDataAsset* Strand = Cast<USplineDataAsset>(Object);
            if (!IsValid(Strand)) { return false; }
            if (First && Strand != First) { bDifferent = true; }
            if (!First) { First = Strand; }
        }
        return bDifferent;
    }
}
