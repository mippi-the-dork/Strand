#include "StrandSplineViewport.h"

#include "Components/SplineComponent.h"
#include "EditorViewportClient.h"
#include "HitProxies.h"
#include "UnrealWidget.h"
#include "InputCoreTypes.h"
#include "PreviewScene.h"
#include "PrimitiveDrawInterface.h"
#include "SceneView.h"

namespace
{
    constexpr int32 MaxPreviewSegments = 4096;
    constexpr int32 MaxVisibleMarkers = 512;
    const FLinearColor StrandCurveColor(0.0f, 0.71f, 0.66f);
    const FLinearColor StrandControlColor(0.86f, 0.90f, 0.95f);
    const FLinearColor StrandSelectedColor(1.0f, 0.83f, 0.25f);
    const FLinearColor StrandStartColor(0.20f, 0.95f, 0.35f);
    const FLinearColor StrandEndColor(1.0f, 0.68f, 0.23f);
    // The reference grid is intentionally quieter than the editable spline.
    const FLinearColor StrandGridColor(0.105f, 0.115f, 0.125f);
    const FLinearColor StrandGridAxisColor(0.22f, 0.255f, 0.27f);

    FBox GetSplineBounds(const USplineComponent* Spline)
    {
        FBox Bounds(ForceInit);
        if (!IsValid(Spline) || Spline->GetNumberOfSplinePoints() == 0) { return Bounds; }
        const int32 PointCount = Spline->GetNumberOfSplinePoints();
        for (int32 Index = 0; Index < PointCount; ++Index)
        {
            Bounds += Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
        }
        if (PointCount > 1)
        {
            const int32 Samples = FMath::Clamp(PointCount * 8, 64, MaxPreviewSegments);
            const double Length = Spline->GetSplineLength();
            for (int32 Index = 0; Index <= Samples; ++Index)
            {
                Bounds += Spline->GetLocationAtDistanceAlongSpline(Length * Index / Samples, ESplineCoordinateSpace::Local);
            }
        }
        return Bounds;
    }
}

struct HStrandSplinePointProxy : public HHitProxy
{
    DECLARE_HIT_PROXY();
    explicit HStrandSplinePointProxy(int32 InPointIndex) : HHitProxy(HPP_UI), PointIndex(InPointIndex) {}
    int32 PointIndex = INDEX_NONE;
    virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }
};
IMPLEMENT_HIT_PROXY(HStrandSplinePointProxy, HHitProxy);

class FStrandSplineViewportClient final : public FEditorViewportClient
{
public:
    FStrandSplineViewportClient(FPreviewScene* InScene, const TSharedRef<SEditorViewport>& InWidget)
        : FEditorViewportClient(nullptr, InScene, InWidget)
    {
        SetViewportType(LVT_Perspective);
        SetViewMode(VMI_Unlit);
        SetRealtime(false);
        bDrawAxes = true;
        ShowWidget(true);
        // The spline preview draws a bounded local floor below the geometry, not a world-Z=0 ceiling.
        EngineShowFlags.SetGrid(false);
        SetViewLocation(FVector(-350.0, -350.0, 270.0));
        SetViewRotation(FRotator(-25.0, 45.0, 0.0));
    }

    void SetSpline(USplineComponent* InSpline) { Spline = InSpline; Invalidate(); }
    void SetShowPoints(bool bShow) { bShowPoints = bShow; Invalidate(); }
    bool GetShowPoints() const { return bShowPoints; }
    void SetShowGrid(bool bShow) { bShowGrid = bShow; Invalidate(); }
    bool GetShowGrid() const { return bShowGrid; }
    void SetShowTangents(bool bShow) { bShowTangents = bShow; Invalidate(); }
    bool GetShowTangents() const { return bShowTangents; }
    void SetSelectedPoint(int32 InIndex) { SelectedPoint = InIndex; Invalidate(); }
    void SetOnPointSelected(TFunction<void(int32)> InCallback) { OnPointSelected = MoveTemp(InCallback); }
    void SetPointDragCallbacks(TFunction<void(const FVector&)> InDelta, TFunction<void()> InEnd)
    {
        OnPointDragDelta = MoveTemp(InDelta);
        OnPointDragEnd = MoveTemp(InEnd);
    }

    // A native translation widget is shown at the selected spline point. The preview
    // component remains transient; edits are routed back to the asset editor.
    virtual FVector GetWidgetLocation() const override
    {
        const USplineComponent* Native = Spline.Get();
        return Native && SelectedPoint >= 0 && SelectedPoint < Native->GetNumberOfSplinePoints()
            ? Native->GetLocationAtSplinePoint(SelectedPoint, ESplineCoordinateSpace::Local)
            : FVector::ZeroVector;
    }

    virtual UE::Widget::EWidgetMode GetWidgetMode() const override
    {
        const USplineComponent* Native = Spline.Get();
        return bShowPoints && Native && SelectedPoint >= 0 && SelectedPoint < Native->GetNumberOfSplinePoints()
            ? UE::Widget::WM_Translate : UE::Widget::WM_None;
    }

    virtual FMatrix GetWidgetCoordSystem() const override { return FMatrix::Identity; }

    virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis,
        FVector& Drag, FRotator& Rot, FVector& Scale) override
    {
        const USplineComponent* Native = Spline.Get();
        if (!Native || SelectedPoint < 0 || SelectedPoint >= Native->GetNumberOfSplinePoints()
            || CurrentAxis == EAxisList::None)
        {
            return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
        }
        if (!Drag.IsNearlyZero() && OnPointDragDelta)
        {
            OnPointDragDelta(Drag);
        }
        return true;
    }

    virtual void TrackingStopped() override
    {
        // Keyboard nudges can emit a widget delta without bIsDraggingWidget.
        // Always let the owner close any lazily created transaction.
        if (OnPointDragEnd) { OnPointDragEnd(); }
        FEditorViewportClient::TrackingStopped();
    }

    void FrameSpline()
    {
        FBox Bounds = GetSplineBounds(Spline.Get());
        if (!Bounds.IsValid) { Bounds = FBox(FVector(-50.0), FVector(50.0)); }
        const FVector Center = Bounds.GetCenter();
        const FVector Extent = Bounds.GetExtent();
        const FVector SafeExtent(FMath::Max(Extent.X, 10.0), FMath::Max(Extent.Y, 10.0), FMath::Max(Extent.Z, 10.0));
        FocusViewportOnBox(FBox(Center - SafeExtent, Center + SafeExtent), true);
        Invalidate();
    }

    void FrameSelectedPoint()
    {
        const USplineComponent* Native = Spline.Get();
        if (!IsValid(Native) || SelectedPoint < 0 || SelectedPoint >= Native->GetNumberOfSplinePoints())
        {
            FrameSpline();
            return;
        }
        const FVector Center = Native->GetLocationAtSplinePoint(SelectedPoint, ESplineCoordinateSpace::Local);
        const double Radius = FMath::Clamp(Native->GetSplineLength() * 0.06, 30.0, 300.0);
        FocusViewportOnBox(FBox(Center - FVector(Radius), Center + FVector(Radius)), true);
        Invalidate();
    }

    virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key,
        EInputEvent Event, uint32 HitX, uint32 HitY) override
    {
        if (Key == EKeys::LeftMouseButton)
        {
            // Axis hit proxies belong to Unreal's transform gizmo. Forward these
            // clicks to the base client without clearing the spline selection.
            if (HitProxy && HitProxy->IsA(HWidgetAxis::StaticGetType()))
            {
                FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
                return;
            }
            int32 NewPoint = INDEX_NONE;
            if (HitProxy && HitProxy->IsA(HStrandSplinePointProxy::StaticGetType()))
            {
                NewPoint = static_cast<HStrandSplinePointProxy*>(HitProxy)->PointIndex;
            }
            SelectedPoint = NewPoint;
            if (OnPointSelected) { OnPointSelected(NewPoint); }
            Invalidate();
            return;
        }
        FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
    }

    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View, PDI);
        const USplineComponent* Native = Spline.Get();
        if (!IsValid(Native) || !PDI) { return; }
        const int32 PointCount = Native->GetNumberOfSplinePoints();
        if (PointCount == 0) { return; }

        if (bShowGrid)
        {
            // Source coordinates are spline-local, and may be far above/below Z=0.
            // Put a small floor below the *actual curve*, with a little breathing room.
            // This avoids a horizon-filling grid floating through the spline.
            const FBox Bounds = GetSplineBounds(Native);
            if (Bounds.IsValid)
            {
                const FVector Size = Bounds.GetSize();
                const double ExtentXY = FMath::Max3(Size.X, Size.Y, 25.0);
                const double Magnitude = FMath::Pow(10.0, FMath::Floor(FMath::LogX(10.0, ExtentXY / 6.0)));
                const double Step = FMath::Clamp(Magnitude, 1.0, 100000.0);
                const double Span = static_cast<double>(FMath::CeilToInt(ExtentXY * 0.65 / Step)) * Step;
                const double GridZ = Bounds.Min.Z - FMath::Max(ExtentXY * 0.025, 5.0);
                const FVector Center = Bounds.GetCenter();
                const double BaseX = static_cast<double>(FMath::RoundToInt64(Center.X / Step)) * Step;
                const double BaseY = static_cast<double>(FMath::RoundToInt64(Center.Y / Step)) * Step;
                const int32 HalfLines = FMath::Clamp(FMath::CeilToInt(Span / Step), 2, 6);
                const double HalfSpan = HalfLines * Step;
                for (int32 I = -HalfLines; I <= HalfLines; ++I)
                {
                    const double X = BaseX + I * Step;
                    const double Y = BaseY + I * Step;
                    const bool bMajor = (I == 0);
                    const FLinearColor& Color = bMajor ? StrandGridAxisColor : StrandGridColor;
                    PDI->DrawLine(FVector(X, BaseY - HalfSpan, GridZ),
                        FVector(X, BaseY + HalfSpan, GridZ), Color, SDPG_World, bMajor ? 0.8f : 0.45f);
                    PDI->DrawLine(FVector(BaseX - HalfSpan, Y, GridZ),
                        FVector(BaseX + HalfSpan, Y, GridZ), Color, SDPG_World, bMajor ? 0.8f : 0.45f);
                }
            }
        }

        if (PointCount >= 2)
        {
            const int32 SpanCount = PointCount - (Native->IsClosedLoop() ? 0 : 1);
            const int32 Samples = FMath::Clamp(SpanCount * 24, 48, MaxPreviewSegments);
            const double Length = Native->GetSplineLength();
            FVector Previous = Native->GetLocationAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::Local);
            for (int32 Index = 1; Index <= Samples; ++Index)
            {
                const FVector Next = Native->GetLocationAtDistanceAlongSpline(
                    Length * static_cast<double>(Index) / Samples, ESplineCoordinateSpace::Local);
                if (!Previous.ContainsNaN() && !Next.ContainsNaN())
                {
                    PDI->DrawLine(Previous, Next, StrandCurveColor, SDPG_World, 2.5f);
                }
                Previous = Next;
            }
        }

        if (bShowPoints)
        {
            const int32 Step = FMath::Max(1, FMath::DivideAndRoundUp(PointCount, MaxVisibleMarkers));
            for (int32 Index = 0; Index < PointCount; Index += Step)
            {
                const FVector Position = Native->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
                const FLinearColor Color = Index == SelectedPoint ? StrandSelectedColor
                    : Index == 0 ? StrandStartColor
                    : (Index == PointCount - 1 && !Native->IsClosedLoop() ? StrandEndColor : StrandControlColor);
                PDI->SetHitProxy(new HStrandSplinePointProxy(Index));
                PDI->DrawPoint(Position, Color, Index == SelectedPoint ? 17.0f : 10.0f, SDPG_Foreground);
                PDI->SetHitProxy(nullptr);
            }
            // Make sure a selected point remains visible and clickable even if density culling skips it.
            if (SelectedPoint >= 0 && SelectedPoint < PointCount && SelectedPoint % Step != 0)
            {
                PDI->SetHitProxy(new HStrandSplinePointProxy(SelectedPoint));
                PDI->DrawPoint(Native->GetLocationAtSplinePoint(SelectedPoint, ESplineCoordinateSpace::Local),
                    StrandSelectedColor, 17.0f, SDPG_Foreground);
                PDI->SetHitProxy(nullptr);
            }
            if (PointCount > 1 && !Native->IsClosedLoop() && (PointCount - 1) % Step != 0 && SelectedPoint != PointCount - 1)
            {
                PDI->SetHitProxy(new HStrandSplinePointProxy(PointCount - 1));
                PDI->DrawPoint(Native->GetLocationAtSplinePoint(PointCount - 1, ESplineCoordinateSpace::Local),
                    StrandEndColor, 11.0f, SDPG_Foreground);
                PDI->SetHitProxy(nullptr);
            }
        }
        if (bShowTangents && SelectedPoint >= 0 && SelectedPoint < PointCount)
        {
            const FSplinePoint Point = Native->GetSplinePointAt(SelectedPoint, ESplineCoordinateSpace::Local);
            const FVector InHandle = Point.Position - Point.ArriveTangent / 3.0;
            const FVector OutHandle = Point.Position + Point.LeaveTangent / 3.0;
            PDI->DrawLine(Point.Position, InHandle, StrandSelectedColor, SDPG_Foreground, 1.0f);
            PDI->DrawLine(Point.Position, OutHandle, StrandSelectedColor, SDPG_Foreground, 1.0f);
            PDI->DrawPoint(InHandle, StrandSelectedColor, 6.0f, SDPG_Foreground);
            PDI->DrawPoint(OutHandle, StrandSelectedColor, 6.0f, SDPG_Foreground);
        }
    }

private:
    TWeakObjectPtr<USplineComponent> Spline;
    TFunction<void(int32)> OnPointSelected;
    TFunction<void(const FVector&)> OnPointDragDelta;
    TFunction<void()> OnPointDragEnd;
    int32 SelectedPoint = INDEX_NONE;
    bool bShowPoints = true;
    bool bShowGrid = true;
    bool bShowTangents = false;
};

void SStrandSplineViewport::Construct(const FArguments& Args)
{
    PreviewScene = MakeShared<FPreviewScene>(FPreviewScene::ConstructionValues());
    SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SStrandSplineViewport::MakeEditorViewportClient()
{
    StrandViewportClient = MakeShared<FStrandSplineViewportClient>(
        PreviewScene.Get(), StaticCastSharedRef<SEditorViewport>(SharedThis(this)));
    return StrandViewportClient.ToSharedRef();
}

void SStrandSplineViewport::SetSpline(USplineComponent* InSpline)
{
    Spline = InSpline;
    if (StrandViewportClient.IsValid())
    {
        StrandViewportClient->SetSpline(InSpline);
        if (!bHasFramedInitialSpline && IsValid(InSpline) && InSpline->GetNumberOfSplinePoints() > 0)
        {
            bHasFramedInitialSpline = true;
            StrandViewportClient->FrameSpline();
        }
    }
}

void SStrandSplineViewport::SetProjection(ELevelViewportType Projection)
{
    if (StrandViewportClient.IsValid())
    {
        StrandViewportClient->SetViewportType(Projection);
        StrandViewportClient->FrameSpline();
    }
}
ELevelViewportType SStrandSplineViewport::GetProjection() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetViewportType() : LVT_Perspective;
}
void SStrandSplineViewport::FrameSpline() { if (StrandViewportClient.IsValid()) { StrandViewportClient->FrameSpline(); } }
void SStrandSplineViewport::FrameSelectedPoint() { if (StrandViewportClient.IsValid()) { StrandViewportClient->FrameSelectedPoint(); } }
void SStrandSplineViewport::SetShowControlPoints(bool bShow) { if (StrandViewportClient.IsValid()) { StrandViewportClient->SetShowPoints(bShow); } }
bool SStrandSplineViewport::GetShowControlPoints() const { return StrandViewportClient.IsValid() && StrandViewportClient->GetShowPoints(); }
void SStrandSplineViewport::SetShowGrid(bool bShow) { if (StrandViewportClient.IsValid()) { StrandViewportClient->SetShowGrid(bShow); } }
bool SStrandSplineViewport::GetShowGrid() const { return StrandViewportClient.IsValid() && StrandViewportClient->GetShowGrid(); }
void SStrandSplineViewport::SetShowTangents(bool bShow) { if (StrandViewportClient.IsValid()) { StrandViewportClient->SetShowTangents(bShow); } }
bool SStrandSplineViewport::GetShowTangents() const { return StrandViewportClient.IsValid() && StrandViewportClient->GetShowTangents(); }
void SStrandSplineViewport::SetSelectedPointIndex(int32 Index) { if (StrandViewportClient.IsValid()) { StrandViewportClient->SetSelectedPoint(Index); } }
void SStrandSplineViewport::SetOnPointSelected(TFunction<void(int32)> InCallback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnPointSelected(MoveTemp(InCallback)); }
}
void SStrandSplineViewport::OnFocusViewportToSelection() { FrameSpline(); }

void SStrandSplineViewport::SetPointDragCallbacks(
    TFunction<void(const FVector&)> InDelta, TFunction<void()> InEnd)
{
    if (StrandViewportClient.IsValid())
    {
        StrandViewportClient->SetPointDragCallbacks(MoveTemp(InDelta), MoveTemp(InEnd));
    }
}
