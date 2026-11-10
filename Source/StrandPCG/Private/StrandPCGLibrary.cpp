#include "StrandPCGLibrary.h"
#include "SplineDataAsset.h"
#include "Data/PCGSplineData.h"
#include "Components/SplineComponent.h"

UPCGSplineData* UStrandPCGLibrary::ApplySplineDataAssetToPcg(UObject* Outer,
    const USplineDataAsset* Asset, FTransform Transform)
{
    if (!IsValid(Asset) || !IsValid(Outer) || Asset->Points.IsEmpty()) { return nullptr; }
    UPCGSplineData* Result = NewObject<UPCGSplineData>(Outer, NAME_None, RF_Transient);
    TArray<FSplinePoint> SplinePoints;
    SplinePoints.Reserve(Asset->Points.Num());
    for (int32 I = 0; I < Asset->Points.Num(); ++I)
    {
        SplinePoints.Add(Asset->Points[I].ToSplinePoint(I));
    }
    Result->Initialize(SplinePoints, Asset->bIsClosedLoop, Transform, {});
    return Result;
}
