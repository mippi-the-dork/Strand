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
    ApplyToSplineComponentWithPlacement(TargetComponent, EStrandCoordinateMode::Absolute, EStrandAnchorMode::PointIndex, 0);
}

FVector USplineDataAsset::GetPlacementAnchor(EStrandAnchorMode AnchorMode, int32 AnchorPointIndex) const
{
    if (Points.IsEmpty()) { return FVector::ZeroVector; }
    if (AnchorMode == EStrandAnchorMode::PointCenter)
    {
        FVector Sum = FVector::ZeroVector;
        for (const FSplinePointData& Point : Points) { Sum += Point.LocalPosition; }
        return Sum / static_cast<double>(Points.Num());
    }
    return Points[FMath::Clamp(AnchorPointIndex, 0, Points.Num() - 1)].LocalPosition;
}

void USplineDataAsset::ApplyToSplineComponentWithPlacement(USplineComponent* Component,
    EStrandCoordinateMode Mode, EStrandAnchorMode AnchorMode, int32 AnchorIndex) const
{
    if (!IsValid(Component)) { return; }
    const FVector Offset = Mode == EStrandCoordinateMode::Relative
        ? GetPlacementAnchor(AnchorMode, AnchorIndex) : FVector::ZeroVector;
    Component->Modify();
#if WITH_EDITORONLY_DATA
    // SVG can join a straight segment to a cubic arc with INDEPENDENT arrive
    // and leave handles. Unreal's native spline editor defaults to continuous
    // tangents, which can make corners appear curled or open after editing.
    // Enable its built-in split-handle editing only when the saved data requires
    // it. Never turn an existing user's discontinuous-spline preference off.
    bool bNeedsSplitHandles = false;
    for (const FSplinePointData& Saved : Points)
    {
        if (Saved.PointType.GetValue() == ESplinePointType::CurveCustomTangent &&
            !Saved.ArriveTangent.Equals(Saved.LeaveTangent, 0.0001))
        {
            bNeedsSplitHandles = true;
            break;
        }
    }
    if (bNeedsSplitHandles)
    {
        Component->bAllowDiscontinuousSpline = true;
    }
#endif
    Component->ClearSplinePoints(false);
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        FSplinePoint Point = Points[Index].ToSplinePoint(Index);
        Point.Position -= Offset;
        Component->AddPoint(Point, false);
    }
    // The source assets use one-unit input keys (0..Num-1). When applying a
    // closed asset, explicitly put the wrap key at Num rather than inheriting a
    // potentially stale loop-position override from an existing component.
    // This is especially important when replacing a spline that was previously
    // edited as an open curve or used a custom loop key.
    const bool bShouldClose = bIsClosedLoop && Points.Num() >= 2;
    if (bShouldClose)
    {
        Component->SetClosedLoopAtPosition(true, static_cast<float>(Points.Num()), false);
    }
    else
    {
        Component->SetClosedLoop(false, false);
    }
    Component->UpdateSpline();
    ensureMsgf(Component->IsClosedLoop() == bShouldClose,
        TEXT("Strand failed to set the native spline's expected closed-loop state."));
    Component->MarkRenderStateDirty();
    Component->MarkPackageDirty();
}

bool USplineDataAsset::MatchesSplineComponentWithPlacement(const USplineComponent* Component,
    EStrandCoordinateMode Mode, EStrandAnchorMode AnchorMode, int32 AnchorIndex, float Tolerance) const
{
    if (!IsValid(Component) || Component->GetNumberOfSplinePoints() != Points.Num() ||
        Component->IsClosedLoop() != bIsClosedLoop) { return false; }
    const FVector Offset = Mode == EStrandCoordinateMode::Relative
        ? GetPlacementAnchor(AnchorMode, AnchorIndex) : FVector::ZeroVector;
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        const FSplinePoint Current = Component->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
        const FSplinePointData& Saved = Points[Index];
        if (!Current.Position.Equals(Saved.LocalPosition - Offset, Tolerance) ||
            !Current.ArriveTangent.Equals(Saved.ArriveTangent, Tolerance) ||
            !Current.LeaveTangent.Equals(Saved.LeaveTangent, Tolerance) ||
            !Current.Rotation.Equals(Saved.LocalRotation, Tolerance) ||
            !Current.Scale.Equals(Saved.LocalScale, Tolerance) ||
            Component->GetSplinePointType(Index) != Saved.PointType.GetValue()) { return false; }
    }
    return true;
}

void USplineDataAsset::SerializeFromSplineComponentWithPlacement(const USplineComponent* Source,
    EStrandCoordinateMode Mode, EStrandAnchorMode AnchorMode, int32 AnchorIndex)
{
    if (!IsValid(Source)) { return; }
    // Use the PREVIOUS asset anchor to reverse an instance's existing placement.
    // This preserves the source coordinate frame when one designer edits a shared asset.
    const FVector Offset = Mode == EStrandCoordinateMode::Relative
        ? GetPlacementAnchor(AnchorMode, AnchorIndex) : FVector::ZeroVector;
    SerializeFromSplineComponent(Source);
    for (FSplinePointData& Point : Points) { Point.LocalPosition += Offset; }
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
