#include "StrandSplineViewport.h"

#include "Components/SplineComponent.h"
#include "EditorViewportClient.h"
#include "HitProxies.h"
#include "UnrealWidget.h"
#include "InputCoreTypes.h"
#include "PreviewScene.h"
#include "PrimitiveDrawInterface.h"
#include "SceneView.h"
#include "Framework/Application/SlateApplication.h"

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

    // Equivalent to the spline's native per-segment interpolation. Keeping hit
    // targets parameterized (not distance sampled) makes arbitrary-t inserts exact.
    FVector SegmentPosition(const USplineComponent* Native, int32 Index, double Alpha)
    {
        const int32 Count = Native->GetNumberOfSplinePoints();
        const FSplinePoint A = Native->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
        const FSplinePoint B = Native->GetSplinePointAt((Index + 1) % Count, ESplineCoordinateSpace::Local);
        if (A.Type == ESplinePointType::Constant) { return A.Position; }
        if (A.Type == ESplinePointType::Linear) { return FMath::Lerp(A.Position, B.Position, Alpha); }
        return FMath::CubicInterp(A.Position, A.LeaveTangent, B.Position, B.ArriveTangent, Alpha);
    }

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

struct HStrandSplineTangentProxy : public HHitProxy
{
    DECLARE_HIT_PROXY();
    HStrandSplineTangentProxy(int32 InPoint, int32 InHandle)
        : HHitProxy(HPP_UI), PointIndex(InPoint), Handle(InHandle) {}
    int32 PointIndex = INDEX_NONE;
    int32 Handle = 0;
    virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }
};
IMPLEMENT_HIT_PROXY(HStrandSplineTangentProxy, HHitProxy);

struct HStrandSplineSegmentProxy : public HHitProxy
{
    DECLARE_HIT_PROXY();
    HStrandSplineSegmentProxy(int32 InSegment, double InAlpha)
        : HHitProxy(HPP_UI), Segment(InSegment), Alpha(InAlpha) {}
    int32 Segment = INDEX_NONE;
    double Alpha = 0.5;
    virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }
};
IMPLEMENT_HIT_PROXY(HStrandSplineSegmentProxy, HHitProxy);

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
        // Draw Unreal's real transform widget in this standalone asset editor.
        // Strand supplies the selection pivot/mode and handles widget deltas,
        // without changing the Level Editor's mode manager or actor selection.
        ShowWidget(true);
        EngineShowFlags.SetModeWidgets(true);
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
    void SetSelectedPoint(int32 InIndex)
    {
        TArray<int32> Indices;
        if (InIndex != INDEX_NONE) { Indices.Add(InIndex); }
        SetSelectedPoints(Indices, InIndex);
    }
    void SetSelectedPoints(const TArray<int32>& Indices, int32 PrimaryIndex)
    {
        if (SelectedPoint != PrimaryIndex) { SelectedHandle = 0; }
        SelectedPoints.Reset();
        for (int32 Index : Indices) { SelectedPoints.Add(Index); }
        SelectedPoint = PrimaryIndex;
        if (SelectedPoints.Num() > 1) { SelectedHandle = 0; }
        Invalidate();
    }
    void SetSelectedHandle(int32 InHandle) { SelectedHandle = FMath::Clamp(InHandle, 0, 2); Invalidate(); }
    void SetMarqueeMode(bool bEnable)
    {
        bMarqueeMode = bEnable;
        bMarqueeDragging = false;
        Invalidate();
    }
    void SetStrandWidgetMode(int32 Mode)
    {
        WidgetMode = FMath::Clamp(Mode, 0, 2);
        Invalidate();
    }
    void SetOnMarqueeSelected(TFunction<void(const TArray<int32>&, bool, bool)> Callback)
    {
        OnMarqueeSelected = MoveTemp(Callback);
    }
    void SetOnMarqueeExit(TFunction<void()> Callback)
    {
        OnMarqueeExit = MoveTemp(Callback);
    }
    void SetPointRotationCallback(TFunction<void(int32, double)> Callback)
    {
        OnPointRotation = MoveTemp(Callback);
    }
    void SetPointScaleCallback(TFunction<void(double)> Callback)
    {
        OnPointScale = MoveTemp(Callback);
    }
    void SetOnWidgetModeChanged(TFunction<void(int32)> Callback)
    {
        OnWidgetModeChanged = MoveTemp(Callback);
    }
    void SetOnToolShortcut(TFunction<void(int32)> Callback) { OnToolShortcut = MoveTemp(Callback); }
    void SetOnDeleteShortcut(TFunction<void()> Callback) { OnDeleteShortcut = MoveTemp(Callback); }
    void SetPenMode(bool bEnable)
    {
        if (!bEnable && bPenDragging && OnPenCancel) { OnPenCancel(); }
        bPenMode = bEnable;
        bPenDragging = false;
        bDelegatedPenCameraClick = false;
        Invalidate();
    }
    void SetPenPlane(int32 InLockedAxis) { LockedAxis = InLockedAxis; Invalidate(); }
    void SetPenSnap(double InStep) { PenSnap = InStep; Invalidate(); }
    void SetPenCallbacks(TFunction<bool(const FStrandPenHit&)> InDown,
        TFunction<void(const FVector&)> InDrag, TFunction<void()> InUp,
        TFunction<void()> InCancel)
    {
        OnPenDown = MoveTemp(InDown);
        OnPenDrag = MoveTemp(InDrag);
        OnPenUp = MoveTemp(InUp);
        OnPenCancel = MoveTemp(InCancel);
    }
    void SetOnTangentSelected(TFunction<void(int32, int32)> InCallback) { OnTangentSelected = MoveTemp(InCallback); }
    void SetTangentDragCallback(TFunction<void(int32, const FVector&)> InCallback) { OnTangentDragDelta = MoveTemp(InCallback); }
    void SetOnPointSelected(TFunction<void(int32, bool, bool)> InCallback) { OnPointSelected = MoveTemp(InCallback); }
    void SetOnSelectAll(TFunction<void()> InCallback) { OnSelectAll = MoveTemp(InCallback); }
    void SetOnClearSelection(TFunction<void()> InCallback) { OnClearSelection = MoveTemp(InCallback); }
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
        if (!Native || SelectedPoint < 0 || SelectedPoint >= Native->GetNumberOfSplinePoints())
        {
            return FVector::ZeroVector;
        }
        const FSplinePoint Point = Native->GetSplinePointAt(SelectedPoint, ESplineCoordinateSpace::Local);
        if (SelectedHandle == 1) { return Point.Position - Point.ArriveTangent / 3.0; }
        if (SelectedHandle == 2) { return Point.Position + Point.LeaveTangent / 3.0; }
        if (SelectedPoints.Num() > 1)
        {
            FVector Center = FVector::ZeroVector;
            int32 Count = 0;
            for (int32 Index : SelectedPoints)
            {
                if (Index >= 0 && Index < Native->GetNumberOfSplinePoints())
                {
                    Center += Native->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
                    ++Count;
                }
            }
            if (Count) { return Center / static_cast<double>(Count); }
        }
        return Point.Position;
    }

    virtual UE::Widget::EWidgetMode GetWidgetMode() const override
    {
        const USplineComponent* Native = Spline.Get();
        return !bPenMode && !bMarqueeMode && bShowPoints && Native && SelectedPoint >= 0 && SelectedPoint < Native->GetNumberOfSplinePoints()
            ? (SelectedHandle != 0 ? UE::Widget::WM_Translate :
                WidgetMode == 1 ? UE::Widget::WM_Rotate :
                WidgetMode == 2 ? UE::Widget::WM_Scale :
                UE::Widget::WM_Translate) : UE::Widget::WM_None;
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
        if (SelectedHandle == 0 && WidgetMode == 1 && OnPointRotation)
        {
            // Unreal's rotation deltas use Pitch=Y, Yaw=Z, Roll=X.
            const double Degrees[3] = { Rot.Roll, Rot.Pitch, Rot.Yaw };
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                if (FMath::Abs(Degrees[Axis]) > 0.000001) { OnPointRotation(Axis, Degrees[Axis]); }
            }
        }
        else if (SelectedHandle == 0 && WidgetMode == 2 && OnPointScale)
        {
            // The Strand group scale is uniform: the active widget axis controls
            // one proportional factor, while all anchor offsets/tangents scale.
            double Delta = Scale.X;
            if (FMath::Abs(Scale.Y) > FMath::Abs(Delta)) { Delta = Scale.Y; }
            if (FMath::Abs(Scale.Z) > FMath::Abs(Delta)) { Delta = Scale.Z; }
            if (FMath::IsFinite(Delta) && FMath::Abs(Delta) > 0.000001)
            {
                OnPointScale(FMath::Clamp(1.0 + Delta, 0.01, 100.0));
            }
        }
        else if (!Drag.IsNearlyZero() && OnPointDragDelta)
        {
            if (SelectedHandle != 0 && OnTangentDragDelta) { OnTangentDragDelta(SelectedHandle, Drag); }
            else { OnPointDragDelta(Drag); }
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

    // Use InputKey for the pen so a drag produces only one anchor, unlike the
    // standard click-on-release selection path in ProcessClick.
    virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
    {
        if (EventArgs.Event == IE_Pressed)
        {
            const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
            if (!EventArgs.Viewport->KeyState(EKeys::RightMouseButton) &&
                !Mods.IsControlDown() && !Mods.IsAltDown() && !Mods.IsShiftDown())
            {
                if (EventArgs.Key == EKeys::P && OnToolShortcut) { OnToolShortcut(1); return true; }
                if (EventArgs.Key == EKeys::B && OnToolShortcut) { OnToolShortcut(2); return true; }
                if (EventArgs.Key == EKeys::V && OnToolShortcut) { OnToolShortcut(0); return true; }
                if (!bPenMode && (EventArgs.Key == EKeys::Delete || EventArgs.Key == EKeys::BackSpace) && OnDeleteShortcut)
                {
                    OnDeleteShortcut();
                    return true;
                }
            }
        }
        if (!bPenMode && EventArgs.Event == IE_Pressed)
        {
            const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
            // RMB + WASD is Unreal's viewport flight navigation. Never consume
            // movement/transform shortcuts while the camera is being driven.
            const bool bFlying = EventArgs.Viewport->KeyState(EKeys::RightMouseButton);
            if (!bFlying && !Mods.IsControlDown() && !Mods.IsAltDown() && !Mods.IsShiftDown())
            {
                // Unreal's standard Spacebar cycle, scoped to Strand's viewport.
                // W/E/R are intentionally NOT intercepted: they belong to the
                // engine camera controls when RMB is held.
                if (EventArgs.Key == EKeys::SpaceBar && !bMarqueeMode)
                {
                    // Consume the shortcut even when nothing is selected, so
                    // the base viewport cannot silently change another mode.
                    if (SelectedPoint != INDEX_NONE)
                    {
                        const int32 Next = (WidgetMode + 1) % 3;
                        if (OnWidgetModeChanged) { OnWidgetModeChanged(Next); }
                        else { SetStrandWidgetMode(Next); }
                    }
                    return true;
                }
                if (EventArgs.Key == EKeys::F) { FrameSpline(); return true; }
            }
            if (EventArgs.Key == EKeys::A && Mods.IsControlDown() && !Mods.IsAltDown())
            {
                if (OnSelectAll) { OnSelectAll(); }
                return true;
            }
            if (EventArgs.Key == EKeys::Escape)
            {
                if (bMarqueeMode && OnMarqueeExit) { OnMarqueeExit(); return true; }
                if (OnClearSelection) { OnClearSelection(); }
                return true;
            }
        }
        if (bPenMode && EventArgs.Key == EKeys::Escape && EventArgs.Event == IE_Pressed)
        {
            if (bPenDragging && OnPenCancel) { OnPenCancel(); }
            bPenDragging = false;
            // Esc exits Pen mode through the editor's owner, not via another click.
            if (OnPenExit) { OnPenExit(); }
            Invalidate();
            return true;
        }
        if (bMarqueeMode && EventArgs.Key == EKeys::LeftMouseButton)
        {
            if (EventArgs.Event == IE_Pressed)
            {
                if (EventArgs.Viewport->KeyState(EKeys::LeftAlt) || EventArgs.Viewport->KeyState(EKeys::RightAlt))
                {
                    bDelegatedMarqueeCameraClick = true;
                    return FEditorViewportClient::InputKey(EventArgs);
                }
                bMarqueeDragging = true;
                MarqueeStart = FIntPoint(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
                MarqueeEnd = MarqueeStart;
                const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
                bMarqueeAdd = Mods.IsShiftDown();
                bMarqueeToggle = Mods.IsControlDown();
                Invalidate();
                return true;
            }
            if (EventArgs.Event == IE_Released && bDelegatedMarqueeCameraClick)
            {
                bDelegatedMarqueeCameraClick = false;
                return FEditorViewportClient::InputKey(EventArgs);
            }
            if (EventArgs.Event == IE_Released && bMarqueeDragging)
            {
                bMarqueeDragging = false;
                if (OnMarqueeSelected)
                {
                    TArray<int32> Indices;
                    const int32 MinX = FMath::Min(MarqueeStart.X, MarqueeEnd.X);
                    const int32 MaxX = FMath::Max(MarqueeStart.X, MarqueeEnd.X);
                    const int32 MinY = FMath::Min(MarqueeStart.Y, MarqueeEnd.Y);
                    const int32 MaxY = FMath::Max(MarqueeStart.Y, MarqueeEnd.Y);
                    // A click without a drag behaves like Select, including
                    // additive/toggle modifiers. It must not clear all anchors.
                    if (MaxX - MinX < 4 && MaxY - MinY < 4)
                    {
                        int32 ClickedIndex = INDEX_NONE;
                        if (HHitProxy* ClickProxy = EventArgs.Viewport->GetHitProxy(MarqueeStart.X, MarqueeStart.Y))
                        {
                            if (ClickProxy->IsA(HStrandSplinePointProxy::StaticGetType()))
                            {
                                ClickedIndex = static_cast<HStrandSplinePointProxy*>(ClickProxy)->PointIndex;
                            }
                        }
                        if (OnPointSelected) { OnPointSelected(ClickedIndex, bMarqueeAdd, bMarqueeToggle); }
                        Invalidate();
                        return true;
                    }
                    if (const USplineComponent* Native = Spline.Get())
                    {
                        for (int32 Index = 0; Index < Native->GetNumberOfSplinePoints(); ++Index)
                        {
                            FVector2D Screen;
                            if (ProjectAnchor(Native->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local), Screen) &&
                                Screen.X >= MinX && Screen.X <= MaxX && Screen.Y >= MinY && Screen.Y <= MaxY)
                            {
                                Indices.Add(Index);
                            }
                        }
                    }
                    OnMarqueeSelected(Indices, bMarqueeAdd, bMarqueeToggle);
                }
                Invalidate();
                return true;
            }
            if (EventArgs.Event == IE_Released) { return true; }
        }
        if (bPenMode && EventArgs.Key == EKeys::LeftMouseButton)
        {
            if (EventArgs.Event == IE_Pressed)
            {
                // Leave Alt+LMB camera orbit/navigation to Unreal.
                if (EventArgs.Viewport->KeyState(EKeys::LeftAlt) || EventArgs.Viewport->KeyState(EKeys::RightAlt))
                {
                    bDelegatedPenCameraClick = true;
                    return FEditorViewportClient::InputKey(EventArgs);
                }
                FStrandPenHit Hit;
                if (!GetPenRayHit(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY(), Hit))
                {
                    return true;
                }
                HHitProxy* Proxy = EventArgs.Viewport->GetHitProxy(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
                if (Proxy && Proxy->IsA(HStrandSplinePointProxy::StaticGetType()))
                {
                    Hit.PointIndex = static_cast<HStrandSplinePointProxy*>(Proxy)->PointIndex;
                }
                else if (Proxy && Proxy->IsA(HStrandSplineSegmentProxy::StaticGetType()))
                {
                    Hit.SegmentIndex = static_cast<HStrandSplineSegmentProxy*>(Proxy)->Segment;
                    Hit.SegmentAlpha = static_cast<HStrandSplineSegmentProxy*>(Proxy)->Alpha;
                    if (const USplineComponent* Native = Spline.Get())
                    {
                        Hit.SegmentAlpha = RefineSegmentHit(Native, Hit.SegmentIndex,
                            Hit.SegmentAlpha, EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
                        Hit.Position = SegmentPosition(Native, Hit.SegmentIndex, Hit.SegmentAlpha);
                    }
                }
                bPenDragging = OnPenDown && OnPenDown(Hit);
                PenAnchor = Hit.Position;
                Invalidate();
                return true;
            }
            if (EventArgs.Event == IE_Released && bDelegatedPenCameraClick)
            {
                bDelegatedPenCameraClick = false;
                return FEditorViewportClient::InputKey(EventArgs);
            }
            if (EventArgs.Event == IE_Released && bPenDragging)
            {
                bPenDragging = false;
                if (OnPenUp) { OnPenUp(); }
                Invalidate();
                return true;
            }
            if (EventArgs.Event == IE_Released) { return true; }
        }
        // Suppress the viewport's built-in W/E/R transform shortcuts when not
        // flying. Strand uses Spacebar instead, but RMB+WASD must always reach
        // the base camera controller unchanged (press, repeat and release).
        if (!EventArgs.Viewport->KeyState(EKeys::RightMouseButton) &&
            (EventArgs.Key == EKeys::W || EventArgs.Key == EKeys::E || EventArgs.Key == EKeys::R) &&
            EventArgs.Event == IE_Pressed)
        {
            return false;
        }
        return FEditorViewportClient::InputKey(EventArgs);
    }

    virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override
    {
        // Consuming press/release alone is insufficient: native viewport mouse
        // deltas can still move the camera during an active Pen drag.
        if ((bPenMode && bPenDragging) || bMarqueeDragging) { return true; }
        return FEditorViewportClient::InputAxis(EventArgs);
    }

    virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override
    {
        PenMouseX = X; PenMouseY = Y;
        if (bMarqueeDragging) { MarqueeEnd = FIntPoint(X, Y); Invalidate(); }
        UpdatePenMouse();
        if ((!bPenMode || !bPenDragging) && !bMarqueeDragging)
        {
            FEditorViewportClient::MouseMove(InViewport, X, Y);
        }
    }

    virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override
    {
        PenMouseX = X; PenMouseY = Y;
        if (bMarqueeDragging) { MarqueeEnd = FIntPoint(X, Y); Invalidate(); }
        UpdatePenMouse();
        if ((!bPenMode || !bPenDragging) && !bMarqueeDragging)
        {
            FEditorViewportClient::CapturedMouseMove(InViewport, X, Y);
        }
    }

    void SetPenExit(TFunction<void()> InExit) { OnPenExit = MoveTemp(InExit); }

    virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key,
        EInputEvent Event, uint32 HitX, uint32 HitY) override
    {
        if ((bPenMode || bMarqueeMode) && Key == EKeys::LeftMouseButton) { return; }
        if (Key == EKeys::LeftMouseButton)
        {
            // Preserve native gizmo hit testing. Widget drags belong to the
            // engine; only spline anchors/tangents belong to Strand.
            if (HitProxy && HitProxy->IsA(HWidgetAxis::StaticGetType()))
            {
                FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
                return;
            }
            if (HitProxy && HitProxy->IsA(HStrandSplineTangentProxy::StaticGetType()))
            {
                const HStrandSplineTangentProxy* Tangent = static_cast<HStrandSplineTangentProxy*>(HitProxy);
                SelectedPoint = Tangent->PointIndex;
                SelectedPoints.Reset();
                SelectedPoints.Add(SelectedPoint);
                SelectedHandle = Tangent->Handle;
                if (OnTangentSelected) { OnTangentSelected(SelectedPoint, SelectedHandle); }
                Invalidate();
                return;
            }
            int32 NewPoint = INDEX_NONE;
            if (HitProxy && HitProxy->IsA(HStrandSplinePointProxy::StaticGetType()))
            {
                NewPoint = static_cast<HStrandSplinePointProxy*>(HitProxy)->PointIndex;
            }
            const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
            // Owner handles Shift range and Ctrl toggling, including choosing the
            // new primary anchor. Never preemptively discard the selection here.
            if (OnPointSelected) { OnPointSelected(NewPoint, Mods.IsShiftDown(), Mods.IsControlDown()); }
            Invalidate();
            return;
        }
        FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
    }

    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View, PDI);
        if (!PDI || !View) { return; }
        CachedInvViewProjection = View->ViewMatrices.GetClipToWorld();
        CachedWorldToClip = CachedInvViewProjection.InverseFast();
        CachedViewRect = View->UnscaledViewRect;
        bHasCachedView = true;
        const USplineComponent* Native = Spline.Get();
        if (!IsValid(Native)) { return; }
        const int32 PointCount = Native->GetNumberOfSplinePoints();

        // Foreground marquee uses four projected cursor rays, so it works in
        // perspective and all orthographic viewport projections.
        if (bMarqueeDragging && bHasCachedView)
        {
            const int32 MinX = FMath::Min(MarqueeStart.X, MarqueeEnd.X);
            const int32 MaxX = FMath::Max(MarqueeStart.X, MarqueeEnd.X);
            const int32 MinY = FMath::Min(MarqueeStart.Y, MarqueeEnd.Y);
            const int32 MaxY = FMath::Max(MarqueeStart.Y, MarqueeEnd.Y);
            const FIntPoint Corners[4] = { {MinX, MinY}, {MaxX, MinY}, {MaxX, MaxY}, {MinX, MaxY} };
            FVector WorldCorners[4];
            for (int32 I = 0; I < 4; ++I)
            {
                FVector Origin, Direction;
                FSceneView::DeprojectScreenToWorld(FVector2D(Corners[I].X, Corners[I].Y),
                    CachedViewRect, CachedInvViewProjection, Origin, Direction);
                WorldCorners[I] = Origin + Direction.GetSafeNormal() * 100.0;
            }
            for (int32 I = 0; I < 4; ++I)
            {
                PDI->DrawLine(WorldCorners[I], WorldCorners[(I + 1) % 4],
                    StrandSelectedColor, SDPG_Foreground, 1.5f);
            }
        }

        if (bShowGrid)
        {
            // Source coordinates are spline-local, and may be far above/below Z=0.
            // Put a small floor below the *actual curve*, with a little breathing room.
            // This avoids a horizon-filling grid floating through the spline.
            FBox Bounds = GetSplineBounds(Native);
            if (!Bounds.IsValid && bPenMode)
            {
                // Keep a usable origin reference when an artist clears all
                // points in preparation for drawing a new spline.
                Bounds = FBox(FVector(-100.0, -100.0, 0.0), FVector(100.0, 100.0, 0.0));
            }
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

        // Segment hit targets exist only while authoring. Otherwise ordinary
        // spline selection uses just anchors and tangent handles.
        if (bPenMode && PointCount >= 2 &&
            PointCount - (Native->IsClosedLoop() ? 0 : 1) <= MaxPreviewSegments)
        {
            const int32 Spans = PointCount - (Native->IsClosedLoop() ? 0 : 1);
            const int32 SamplesPerSpan = FMath::Clamp(MaxPreviewSegments / FMath::Max(Spans, 1), 2, 24);
            for (int32 Segment = 0; Segment < Spans; ++Segment)
            {
                FVector Prev = SegmentPosition(Native, Segment, 0.0);
                for (int32 Step = 1; Step <= SamplesPerSpan; ++Step)
                {
                    const double Alpha = static_cast<double>(Step) / SamplesPerSpan;
                    const FVector Next = SegmentPosition(Native, Segment, Alpha);
                    PDI->SetHitProxy(new HStrandSplineSegmentProxy(Segment, (Step - 0.5) / SamplesPerSpan));
                    PDI->DrawLine(Prev, Next, StrandCurveColor, SDPG_World, 3.0f);
                    PDI->SetHitProxy(nullptr);
                    Prev = Next;
                }
            }
        }

        if (bPenMode && bHasPenHover)
        {
            const FVector Cursor = PenHover;
            PDI->DrawPoint(Cursor, StrandSelectedColor, 11.0f, SDPG_Foreground);
            if (PointCount && !Native->IsClosedLoop() && !bPenDragging)
            {
                const int32 Endpoint = SelectedPoint == 0 ? 0 : PointCount - 1;
                const FSplinePoint End = Native->GetSplinePointAt(Endpoint, ESplineCoordinateSpace::Local);
                FVector Prev = End.Position;
                for (int32 Step = 1; Step <= 20; ++Step)
                {
                    const double T = Step / 20.0;
                    const FVector Next = Endpoint == 0
                        ? FMath::CubicInterp(Cursor, FVector::ZeroVector, End.Position, End.ArriveTangent, T)
                        : FMath::CubicInterp(End.Position, End.LeaveTangent, Cursor, FVector::ZeroVector, T);
                    PDI->DrawLine(Prev, Next, StrandSelectedColor, SDPG_World, 1.2f);
                    Prev = Next;
                }
            }
            if (bPenDragging)
            {
                PDI->DrawLine(PenAnchor, Cursor, StrandSelectedColor, SDPG_Foreground, 1.2f);
            }
        }

        if (bShowPoints)
        {
            const int32 Step = FMath::Max(1, FMath::DivideAndRoundUp(PointCount, MaxVisibleMarkers));
            for (int32 Index = 0; Index < PointCount; Index += Step)
            {
                const FVector Position = Native->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
                const FLinearColor Color = SelectedPoints.Contains(Index) ? StrandSelectedColor
                    : Index == 0 ? StrandStartColor
                    : (Index == PointCount - 1 && !Native->IsClosedLoop() ? StrandEndColor : StrandControlColor);
                PDI->SetHitProxy(new HStrandSplinePointProxy(Index));
                PDI->DrawPoint(Position, Color, Index == SelectedPoint ? 17.0f
                    : SelectedPoints.Contains(Index) ? 13.0f : 10.0f, SDPG_Foreground);
                PDI->SetHitProxy(nullptr);
            }
            // All selected anchors are drawn on smaller splines. On extremely
            // dense selections keep the representative points + primary only:
            // never submit tens of thousands of extra hit proxies each frame.
            if (SelectedPoints.Num() <= MaxVisibleMarkers)
            {
                for (int32 Index : SelectedPoints)
                {
                    if (Index < 0 || Index >= PointCount || Index % Step == 0) { continue; }
                    PDI->SetHitProxy(new HStrandSplinePointProxy(Index));
                    PDI->DrawPoint(Native->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local),
                        StrandSelectedColor, Index == SelectedPoint ? 17.0f : 13.0f, SDPG_Foreground);
                    PDI->SetHitProxy(nullptr);
                }
            }
            else if (SelectedPoint >= 0 && SelectedPoint < PointCount && SelectedPoint % Step != 0)
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
        if (bShowTangents && !bPenMode && SelectedPoints.Num() == 1 &&
            SelectedPoint >= 0 && SelectedPoint < PointCount)
        {
            const FSplinePoint Point = Native->GetSplinePointAt(SelectedPoint, ESplineCoordinateSpace::Local);
            const FVector InHandle = Point.Position - Point.ArriveTangent / 3.0;
            const FVector OutHandle = Point.Position + Point.LeaveTangent / 3.0;
            PDI->DrawLine(Point.Position, InHandle, StrandSelectedColor, SDPG_Foreground, 1.0f);
            PDI->DrawLine(Point.Position, OutHandle, StrandSelectedColor, SDPG_Foreground, 1.0f);
            PDI->SetHitProxy(new HStrandSplineTangentProxy(SelectedPoint, 1));
            PDI->DrawPoint(InHandle, SelectedHandle == 1 ? StrandStartColor : StrandSelectedColor,
                SelectedHandle == 1 ? 13.0f : 9.0f, SDPG_Foreground);
            PDI->SetHitProxy(nullptr);
            PDI->SetHitProxy(new HStrandSplineTangentProxy(SelectedPoint, 2));
            PDI->DrawPoint(OutHandle, SelectedHandle == 2 ? StrandStartColor : StrandSelectedColor,
                SelectedHandle == 2 ? 13.0f : 9.0f, SDPG_Foreground);
            PDI->SetHitProxy(nullptr);
        }

    }

private:
    bool ProjectAnchor(const FVector& WorldPosition, FVector2D& Screen) const
    {
        if (!bHasCachedView || CachedViewRect.Width() <= 0 || CachedViewRect.Height() <= 0) { return false; }
        const FVector4 Clip = CachedWorldToClip.TransformFVector4(FVector4(WorldPosition.X, WorldPosition.Y, WorldPosition.Z, 1.0));
        if (Clip.W <= 0.000001 || !FMath::IsFinite(Clip.W)) { return false; }
        const double X = Clip.X / Clip.W;
        const double Y = Clip.Y / Clip.W;
        Screen.X = CachedViewRect.Min.X + (X * 0.5 + 0.5) * CachedViewRect.Width();
        Screen.Y = CachedViewRect.Min.Y + (0.5 - Y * 0.5) * CachedViewRect.Height();
        return FMath::IsFinite(Screen.X) && FMath::IsFinite(Screen.Y);
    }

    double RefineSegmentHit(const USplineComponent* Native, int32 Segment, double Alpha,
        int32 X, int32 Y) const
    {
        if (!bHasCachedView) { return Alpha; }
        FVector RayOrigin, RayDirection;
        FSceneView::DeprojectScreenToWorld(FVector2D(X, Y), CachedViewRect,
            CachedInvViewProjection, RayOrigin, RayDirection);
        if (!RayDirection.Normalize()) { return Alpha; }
        // Each line hit proxy represents 1/24 (or fewer) of a segment. Refine
        // within its own span so the inserted point lands at the cursor instead
        // of the drawn subsegment's midpoint.
        const int32 Count = Native->GetNumberOfSplinePoints();
        const int32 Spans = Count - (Native->IsClosedLoop() ? 0 : 1);
        const int32 Steps = FMath::Clamp(MaxPreviewSegments / FMath::Max(Spans, 1), 2, 24);
        const double Half = 0.5 / Steps;
        double A = FMath::Max(0.0011, Alpha - Half);
        double B = FMath::Min(0.9989, Alpha + Half);
        auto DistanceSq = [Native, Segment, &RayOrigin, &RayDirection](double T)
        {
            const FVector P = SegmentPosition(Native, Segment, T) - RayOrigin;
            return (P - RayDirection * FVector::DotProduct(P, RayDirection)).SizeSquared();
        };
        // Golden-section search over a tiny piece of the sampled curve.
        for (int32 I = 0; I < 12; ++I)
        {
            const double L = A + (B - A) * 0.382;
            const double R = B - (B - A) * 0.382;
            if (DistanceSq(L) < DistanceSq(R)) { B = R; } else { A = L; }
        }
        return (A + B) * 0.5;
    }

    bool GetPenRayHit(int32 X, int32 Y, FStrandPenHit& OutHit) const
    {
        if (!bHasCachedView || CachedViewRect.Width() <= 0 || CachedViewRect.Height() <= 0) { return false; }
        FVector RayOrigin, RayDirection;
        FSceneView::DeprojectScreenToWorld(FVector2D(X, Y), CachedViewRect,
            CachedInvViewProjection, RayOrigin, RayDirection);
        if (RayDirection.IsNearlyZero()) { return false; }
        FVector PlanePoint = FVector::ZeroVector;
        if (const USplineComponent* Native = Spline.Get())
        {
            const int32 Count = Native->GetNumberOfSplinePoints();
            if (Count > 0)
            {
                const int32 Anchor = SelectedPoint == 0 ? 0 : Count - 1;
                PlanePoint = Native->GetLocationAtSplinePoint(Anchor, ESplineCoordinateSpace::Local);
            }
        }
        FVector Normal = LockedAxis == 0 ? FVector::XAxisVector : LockedAxis == 1 ? FVector::YAxisVector
            : LockedAxis == 2 ? FVector::ZAxisVector : GetViewRotation().Vector();
        const double Denominator = FVector::DotProduct(RayDirection, Normal);
        if (FMath::Abs(Denominator) < 0.00001) { return false; }
        const double T = FVector::DotProduct(PlanePoint - RayOrigin, Normal) / Denominator;
        if (!FMath::IsFinite(T) || T < 0.0) { return false; }
        FVector Position = RayOrigin + RayDirection * T;
        if (LockedAxis >= 0) { Position[LockedAxis] = PlanePoint[LockedAxis]; }
        if (PenSnap > 0.0)
        {
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                if (Axis != LockedAxis) { Position[Axis] = FMath::RoundHalfFromZero(Position[Axis] / PenSnap) * PenSnap; }
            }
        }
        if (Position.ContainsNaN()) { return false; }
        OutHit.Position = Position;
        return true;
    }

    void UpdatePenMouse()
    {
        if (!bPenMode) { return; }
        FStrandPenHit Hit;
        bHasPenHover = GetPenRayHit(PenMouseX, PenMouseY, Hit);
        if (bHasPenHover)
        {
            PenHover = Hit.Position;
            if (bPenDragging && OnPenDrag) { OnPenDrag(PenHover - PenAnchor); }
        }
        Invalidate();
    }

    TWeakObjectPtr<USplineComponent> Spline;
    TFunction<void(int32, bool, bool)> OnPointSelected;
    TFunction<void()> OnSelectAll;
    TFunction<void()> OnClearSelection;
    TFunction<void(const TArray<int32>&, bool, bool)> OnMarqueeSelected;
    TFunction<void()> OnMarqueeExit;
    TFunction<void(int32, double)> OnPointRotation;
    TFunction<void(double)> OnPointScale;
    TFunction<void(int32)> OnWidgetModeChanged;
    TFunction<void(int32)> OnToolShortcut;
    TFunction<void()> OnDeleteShortcut;
    TSet<int32> SelectedPoints;
    TFunction<void(int32, int32)> OnTangentSelected;
    TFunction<void(int32, const FVector&)> OnTangentDragDelta;
    TFunction<void(const FVector&)> OnPointDragDelta;
    TFunction<void()> OnPointDragEnd;
    int32 SelectedPoint = INDEX_NONE;
    int32 SelectedHandle = 0;
    bool bShowPoints = true;
    bool bShowGrid = true;
    bool bShowTangents = true;
    bool bPenMode = false;
    bool bMarqueeMode = false;
    bool bMarqueeDragging = false;
    bool bMarqueeAdd = false;
    bool bMarqueeToggle = false;
    bool bDelegatedMarqueeCameraClick = false;
    int32 WidgetMode = 0;
    FIntPoint MarqueeStart = FIntPoint::ZeroValue;
    FIntPoint MarqueeEnd = FIntPoint::ZeroValue;
    bool bPenDragging = false;
    bool bDelegatedPenCameraClick = false;
    bool bHasCachedView = false;
    bool bHasPenHover = false;
    int32 LockedAxis = -1;
    int32 PenMouseX = 0;
    int32 PenMouseY = 0;
    double PenSnap = 0.0;
    FMatrix CachedInvViewProjection = FMatrix::Identity;
    FMatrix CachedWorldToClip = FMatrix::Identity;
    FIntRect CachedViewRect;
    FVector PenHover = FVector::ZeroVector;
    FVector PenAnchor = FVector::ZeroVector;
    TFunction<bool(const FStrandPenHit&)> OnPenDown;
    TFunction<void(const FVector&)> OnPenDrag;
    TFunction<void()> OnPenUp;
    TFunction<void()> OnPenCancel;
    TFunction<void()> OnPenExit;
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
void SStrandSplineViewport::SetSelectedPoints(const TArray<int32>& Indices, int32 PrimaryIndex)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetSelectedPoints(Indices, PrimaryIndex); }
}
void SStrandSplineViewport::SetOnPointSelected(TFunction<void(int32, bool, bool)> InCallback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnPointSelected(MoveTemp(InCallback)); }
}
void SStrandSplineViewport::SetOnSelectAll(TFunction<void()> InCallback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnSelectAll(MoveTemp(InCallback)); }
}
void SStrandSplineViewport::SetOnClearSelection(TFunction<void()> InCallback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnClearSelection(MoveTemp(InCallback)); }
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

void SStrandSplineViewport::SetSelectedHandle(int32 Handle)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetSelectedHandle(Handle); }
}
void SStrandSplineViewport::SetOnTangentSelected(TFunction<void(int32, int32)> InCallback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnTangentSelected(MoveTemp(InCallback)); }
}
void SStrandSplineViewport::SetTangentDragCallback(TFunction<void(int32, const FVector&)> InCallback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetTangentDragCallback(MoveTemp(InCallback)); }
}

void SStrandSplineViewport::SetMarqueeMode(bool bEnabled)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetMarqueeMode(bEnabled); }
}
void SStrandSplineViewport::SetWidgetMode(int32 Mode)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetStrandWidgetMode(Mode); }
}
void SStrandSplineViewport::SetOnMarqueeSelected(TFunction<void(const TArray<int32>&, bool, bool)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnMarqueeSelected(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetOnMarqueeExit(TFunction<void()> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnMarqueeExit(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetPointRotationCallback(TFunction<void(int32, double)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetPointRotationCallback(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetPointScaleCallback(TFunction<void(double)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetPointScaleCallback(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetOnWidgetModeChanged(TFunction<void(int32)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnWidgetModeChanged(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetOnToolShortcut(TFunction<void(int32)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnToolShortcut(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetOnDeleteShortcut(TFunction<void()> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnDeleteShortcut(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetPenMode(bool bEnabled)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetPenMode(bEnabled); }
}
void SStrandSplineViewport::SetPenPlane(int32 LockedAxis)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetPenPlane(LockedAxis); }
}
void SStrandSplineViewport::SetPenSnap(double Step)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetPenSnap(Step); }
}
void SStrandSplineViewport::SetPenCallbacks(TFunction<bool(const FStrandPenHit&)> OnDown,
    TFunction<void(const FVector&)> OnDrag, TFunction<void()> OnUp,
    TFunction<void()> OnCancel, TFunction<void()> OnExit)
{
    if (StrandViewportClient.IsValid())
    {
        StrandViewportClient->SetPenCallbacks(MoveTemp(OnDown), MoveTemp(OnDrag), MoveTemp(OnUp), MoveTemp(OnCancel));
        StrandViewportClient->SetPenExit(MoveTemp(OnExit));
    }
}
