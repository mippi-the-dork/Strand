#include "SplineLandscapeAdapter.h"
#include "SplineDataAsset.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SplineLandscapeAdapter"

bool SplineLandscapeAdapter::Apply(const USplineDataAsset* Asset,
    ULandscapeSplinesComponent* Component, FString& OutError)
{
    if (!IsValid(Asset) || !IsValid(Component) || Asset->Points.Num() < 2)
    {
        OutError = TEXT("Asset/component missing, or asset has fewer than two points.");
        return false;
    }

    const auto& Controls = Component->GetControlPoints();
    const auto& Segments = Component->GetSegments();
    const int32 Num = Controls.Num();
    if (Asset->Points.Num() != Num || Segments.Num() != Num - (Asset->bIsClosedLoop ? 0 : 1))
    {
        OutError = TEXT("Landscape topology differs. Use a path with the same number of control points and segments.");
        return false;
    }

    // Build the only unambiguous walk: a connected, nonbranching chain or simple loop.
    // Any branch, disconnected component, or invalid link is rejected before touching the scene.
    ULandscapeSplineControlPoint* Start = nullptr;
    for (const auto& Control : Controls)
    {
        if (!IsValid(Control)) { OutError = TEXT("Landscape contains invalid control points."); return false; }
        const int32 Degree = Control->ConnectedSegments.Num();
        if (Degree != 2 && !(Degree == 1 && !Asset->bIsClosedLoop))
        {
            OutError = TEXT("Only a single nonbranching Landscape spline is supported.");
            return false;
        }
        if (!Asset->bIsClosedLoop && Degree == 1 && !Start) { Start = Control.Get(); }
    }
    if (Asset->bIsClosedLoop)
    {
        if (Num < 3) { OutError = TEXT("A Landscape loop needs at least three control points."); return false; }
        Start = Controls[0].Get();
    }
    if (!Start) { OutError = TEXT("Could not identify a Landscape path endpoint."); return false; }

    struct FStep { ULandscapeSplineControlPoint* Control; ULandscapeSplineSegment* ToNext; int32 FromEnd; };
    TArray<FStep> Path;
    TSet<ULandscapeSplineControlPoint*> Visited;
    ULandscapeSplineControlPoint* Current = Start;
    ULandscapeSplineSegment* Previous = nullptr;
    while (Current && !Visited.Contains(Current) && Path.Num() < Num)
    {
        Visited.Add(Current);
        FStep& Step = Path.AddDefaulted_GetRef();
        Step.Control = Current;
        Step.ToNext = nullptr;
        Step.FromEnd = 0;
        for (const FLandscapeSplineConnection& Connection : Current->ConnectedSegments)
        {
            ULandscapeSplineSegment* Segment = Connection.Segment.Get();
            if (!Segment || Segment == Previous) { continue; }
            const int32 End = static_cast<int32>(Connection.End);
            if (End < 0 || End > 1 || Segment->Connections[End].ControlPoint.Get() != Current)
            {
                OutError = TEXT("Invalid Landscape connection metadata."); return false;
            }
            Step.ToNext = Segment;
            Step.FromEnd = End;
            Previous = Segment;
            Current = Segment->Connections[1 - End].ControlPoint.Get();
            break;
        }
        if (!Step.ToNext) { Current = nullptr; }
    }
    if (Path.Num() != Num || (Asset->bIsClosedLoop && Current != Start) ||
        (!Asset->bIsClosedLoop && Current != nullptr))
    {
        OutError = TEXT("Landscape contains disconnected or inconsistent spline topology.");
        return false;
    }

    const FScopedTransaction Transaction(LOCTEXT("ApplyLandscape", "Apply Spline Asset to Landscape Splines"));
    Component->Modify();
    const FVector Anchor = Path[0].Control->Location;
    const FVector RelativeOrigin = Asset->Points[0].LocalPosition;
    for (int32 I = 0; I < Num; ++I)
    {
        Path[I].Control->Modify();
        Path[I].Control->Location = Anchor + Asset->Points[I].LocalPosition - RelativeOrigin;
        Path[I].Control->Rotation = Asset->Points[I].LocalRotation;
    }
    // Landscape tangent lengths are scalar along the control's local forward axis, not
    // independent 3D Bézier handles. Project accordingly; this is a lossy mapping.
    for (int32 I = 0; I < Num; ++I)
    {
        if (!Path[I].ToNext) { continue; }
        ULandscapeSplineSegment* Segment = Path[I].ToNext;
        Segment->Modify();
        const int32 Next = (I + 1) % Num;
        const int32 A = Path[I].FromEnd;
        const int32 B = 1 - A;
        const FVector ForwardA = Path[I].Control->Rotation.Vector();
        const FVector ForwardB = Path[Next].Control->Rotation.Vector();
        Segment->Connections[A].TangentLen = FVector::DotProduct(Asset->Points[I].LeaveTangent, ForwardA);
        Segment->Connections[B].TangentLen = -FVector::DotProduct(Asset->Points[Next].ArriveTangent, ForwardB);
        Segment->UpdateSplinePoints(true, true);
    }
    for (const FStep& Step : Path) { Step.Control->UpdateSplinePoints(true, true, true); }
    // ULandscapeSplinesComponent is MinimalAPI in installed UE builds.
    // ModifySplines() and RequestSplineLayerUpdate() are not exported here and
    // cannot be called directly from an external editor module (LNK2019).
    // Component/point/segment Modify() calls above record the transaction.
    // Use the existing virtual native rebuild path for splines and meshes.
    Component->RebuildAllSplines(true);
    Component->MarkRenderStateDirty();
    Component->MarkPackageDirty();
    return true;
}
#undef LOCTEXT_NAMESPACE
