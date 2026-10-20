#include "StrandLibrary.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"

bool UStrandLibrary::ApplySplineDataAsset(const USplineDataAsset* Asset, USplineComponent* TargetComponent)
{
    if (!IsValid(Asset) || !IsValid(TargetComponent)) { return false; }
    Asset->ApplyToSplineComponent(TargetComponent);
    return true;
}
