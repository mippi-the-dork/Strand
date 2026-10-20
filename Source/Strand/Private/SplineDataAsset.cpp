#include "SplineDataAsset.h"
#if WITH_EDITOR && WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

FSplinePoint FSplinePointData::ToSplinePoint(int32 Index) const
{
    return FSplinePoint(static_cast<float>(Index), LocalPosition, ArriveTangent, LeaveTangent,
                        LocalRotation, LocalScale, PointType.GetValue());
}

void USplineDataAsset::SerializeFromSplineComponent(const USplineComponent* SourceComponent)
{
    if (!IsValid(SourceComponent)) { return; }
    Modify();
    const int32 Count = SourceComponent->GetNumberOfSplinePoints();
    Points.Reset(Count);
    for (int32 I = 0; I < Count; ++I)
    {
        // GetSplinePointAt returns the raw per-point data, unlike derived spline tangents.
        const FSplinePoint Point = SourceComponent->GetSplinePointAt(I, ESplineCoordinateSpace::Local);
        FSplinePointData Data;
        Data.LocalPosition = Point.Position;
        Data.ArriveTangent = Point.ArriveTangent;
        Data.LeaveTangent = Point.LeaveTangent;
        Data.LocalRotation = Point.Rotation;
        Data.LocalScale = Point.Scale;
        Data.PointType = SourceComponent->GetSplinePointType(I);
        Points.Add(Data);
    }
    bIsClosedLoop = SourceComponent->IsClosedLoop();
    MarkPackageDirty();
}

void USplineDataAsset::ApplyToSplineComponent(USplineComponent* TargetComponent) const
{
    if (!IsValid(TargetComponent)) { return; }
    TargetComponent->Modify();
    TargetComponent->ClearSplinePoints(false);
    for (int32 I = 0; I < Points.Num(); ++I)
    {
        TargetComponent->AddPoint(Points[I].ToSplinePoint(I), false);
    }
    TargetComponent->SetClosedLoop(bIsClosedLoop && Points.Num() >= 2, false);
    TargetComponent->UpdateSpline();
    TargetComponent->MarkRenderStateDirty();
    TargetComponent->MarkPackageDirty();
}

bool USplineDataAsset::MatchesSplineComponent(const USplineComponent* Component, float Tolerance) const
{
    if (!IsValid(Component) || Component->GetNumberOfSplinePoints() != Points.Num() ||
        Component->IsClosedLoop() != bIsClosedLoop)
    {
        return false;
    }
    for (int32 I = 0; I < Points.Num(); ++I)
    {
        const FSplinePoint P = Component->GetSplinePointAt(I, ESplineCoordinateSpace::Local);
        const FSplinePointData& Q = Points[I];
        if (!P.Position.Equals(Q.LocalPosition, Tolerance) ||
            !P.ArriveTangent.Equals(Q.ArriveTangent, Tolerance) ||
            !P.LeaveTangent.Equals(Q.LeaveTangent, Tolerance) ||
            !P.Rotation.Equals(Q.LocalRotation, Tolerance) ||
            !P.Scale.Equals(Q.LocalScale, Tolerance) || Component->GetSplinePointType(I) != Q.PointType.GetValue())
        {
            return false;
        }
    }
    return true;
}

#if WITH_EDITOR && WITH_EDITORONLY_DATA
void USplineDataAsset::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject) && !AssetImportData)
    {
        AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"), RF_Transactional);
    }
}
#endif
