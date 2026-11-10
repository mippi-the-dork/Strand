#include "StrandSplineViewport.h"
#include "StrandEditorPreferences.h"

#include "Components/SplineComponent.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "HitProxies.h"
#include "UnrealWidget.h"
#include "InputCoreTypes.h"
#include "PreviewScene.h"
#include "PrimitiveDrawInterface.h"
#include "SceneView.h"
#include "Math/RotationMatrix.h"
#include "StrandSplineEditOperations.h"
#include "StrandSplineInputUtils.h"
#include "StrandSplineSelectionUtils.h"
#include "StrandSplineFraming.h"
#include "StrandCameraSpaceMath.h"
#include "StrandPrecisionProjection.h"
#include "StrandSoftSelectionVisuals.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
    constexpr int32 MaxPreviewSegments = 4096;
    constexpr int32 MaxVisibleMarkers = 512;
    const FLinearColor StrandCurveColor(0.0f, 0.71f, 0.66f);
    const FLinearColor StrandControlColor(0.86f, 0.90f, 0.95f);
    const FLinearColor StrandSelectedColor(1.0f, 0.83f, 0.25f);
    const FLinearColor StrandStartColor(0.20f, 0.95f, 0.35f);
    const FLinearColor StrandEndColor(0.94f, 0.24f, 0.25f);
    const FLinearColor StrandHoverColor(0.94f, 0.97f, 1.0f);
    // Directional tangent colors remain constant across selection/hover:
    // leave/outgoing (forward/leading) = blue; arrive/incoming (trailing) = orange.
    const FLinearColor StrandIncomingHandleColor(1.0f, 0.54f, 0.16f);
    const FLinearColor StrandOutgoingHandleColor(0.24f, 0.63f, 1.0f);
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

    void GatherFrameSamples(const USplineComponent* Spline, TArray<FVector>& OutSamples)
    {
        OutSamples.Reset();
        if (!IsValid(Spline) || Spline->GetNumberOfSplinePoints() == 0) { return; }
        const int32 Count = Spline->GetNumberOfSplinePoints();
        const int32 Steps = FMath::Clamp(Count * 16, 96, MaxPreviewSegments);
        OutSamples.Reserve(Count + Steps + 1);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            OutSamples.Add(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World));
        }
        if (Count > 1)
        {
            const double Length = Spline->GetSplineLength();
            for (int32 Index = 0; Index <= Steps; ++Index)
            {
                OutSamples.Add(Spline->GetLocationAtDistanceAlongSpline(
                    Length * static_cast<double>(Index) / Steps, ESplineCoordinateSpace::World));
            }
        }
    }

    FBox GetSplineBounds(const USplineComponent* Spline)
    {
        FBox Bounds(ForceInit);
        TArray<FVector> Samples;
        GatherFrameSamples(Spline, Samples);
        for (const FVector& P : Samples) { Bounds += P; }
        return Bounds;
    }
}

struct HStrandSplinePointProxy : public HHitProxy
{
    DECLARE_HIT_PROXY();
    explicit HStrandSplinePointProxy(int32 InPointIndex) : HHitProxy(HPP_UI), PointIndex(InPointIndex) {}
    int32 PointIndex = INDEX_NONE;
    virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Hand; }
};
IMPLEMENT_HIT_PROXY(HStrandSplinePointProxy, HHitProxy);

struct HStrandSplineTangentProxy : public HHitProxy
{
    DECLARE_HIT_PROXY();
    HStrandSplineTangentProxy(int32 InPoint, int32 InHandle)
        : HHitProxy(HPP_UI), PointIndex(InPoint), Handle(InHandle) {}
    int32 PointIndex = INDEX_NONE;
    int32 Handle = 0;
    virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::GrabHand; }
};
IMPLEMENT_HIT_PROXY(HStrandSplineTangentProxy, HHitProxy);

struct HStrandSplineSegmentProxy : public HHitProxy
{
    DECLARE_HIT_PROXY();
    HStrandSplineSegmentProxy(int32 InSegment, double InAlpha)
        : HHitProxy(HPP_UI), Segment(InSegment), Alpha(InAlpha) {}
    int32 Segment = INDEX_NONE;
    double Alpha = 0.5;
    virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Default; }
};
IMPLEMENT_HIT_PROXY(HStrandSplineSegmentProxy, HHitProxy);

class FStrandSplineViewportClient final : public FEditorViewportClient
{
public:
    FStrandSplineViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InScene,
        const TSharedRef<SEditorViewport>& InWidget)
        : FEditorViewportClient(InModeTools, InScene, InWidget)
    {
        SetViewportType(LVT_Perspective);
        const UStrandEditorPreferences* Preferences = GetDefault<UStrandEditorPreferences>();
        OrientationSpacing = FMath::IsFinite(Preferences->OrientationSpacing) ? FMath::Clamp(Preferences->OrientationSpacing, 0.01, 100000.0) : 100.0;
        OrientationSize = FMath::IsFinite(Preferences->OrientationSize) ? FMath::Clamp(Preferences->OrientationSize, 0.01, 10000.0) : 35.0;
        SetViewMode(VMI_Unlit);
        SetRealtime(false);
        bDrawAxes = true;
        // The widget needs a real editor-mode manager as well as these viewport
        // flags. Strand owns its manager; it never uses the Level Editor's modes
        // or actor selection. Selection synchronization enables it as needed.
        ShowWidget(false);
        EngineShowFlags.SetModeWidgets(true);
        // The spline preview draws a bounded local floor below the geometry, not a world-Z=0 ceiling.
        EngineShowFlags.SetGrid(false);
        SetViewLocation(FVector(-350.0, -350.0, 270.0));
        SetViewRotation(FRotator(-25.0, 45.0, 0.0));
    }

    FVector GetCameraForward() const { return GetViewRotation().Vector().GetSafeNormal(); }
    FVector GetCameraLocation() const { return GetViewLocation(); }
    double GetCameraHorizontalFOV() const { return ViewFOV; }
    int32 GetCameraViewportWidth() const { return Viewport ? Viewport->GetSizeXY().X : 1280; }
    void SetSpline(USplineComponent* InSpline) { if (Spline.Get() != InSpline) { bWidgetFrameFrozen = false; } Spline = InSpline; RefreshNativeWidget(); Invalidate(); }
    void SetCleanupPreviewPolyline(const TArray<FVector>& Points) { CleanupPreviewPolyline = Points; Invalidate(); }
    void SetSnapPreviewSegments(const TArray<TPair<FVector, FVector>>& Segments) { SnapPreviewSegments = Segments; Invalidate(); }
    void SetSoftSelectionWeights(const TArray<double>& Weights) { SoftSelectionWeights = Weights; Invalidate(); }
    void SetShowPoints(bool bShow) { bShowPoints = bShow; RefreshNativeWidget(); Invalidate(); }
    bool GetShowPoints() const { return bShowPoints; }
    void SetShowGrid(bool bShow) { bShowGrid = bShow; Invalidate(); }
    bool GetShowGrid() const { return bShowGrid; }
    void SetShowTangents(bool bShow) { bShowTangents = bShow; Invalidate(); }
    bool GetShowTangents() const { return bShowTangents; }
    void SetOrientationVisual(int32 Mode, bool bEnabled)
    {
        if (Mode >= 0 && Mode < 4) { bOrientationVisuals[Mode] = bEnabled; Invalidate(); }
    }
    bool GetOrientationVisual(int32 Mode) const
    {
        return Mode >= 0 && Mode < 4 && bOrientationVisuals[Mode];
    }
    void SetOrientationSpacing(double Value)
    {
        if (!FMath::IsFinite(Value) || Value <= 0.0) { return; }
        OrientationSpacing = FMath::Clamp(Value, 0.01, 100000.0);
        UStrandEditorPreferences* Preferences = GetMutableDefault<UStrandEditorPreferences>();
        Preferences->OrientationSpacing = OrientationSpacing;
        Preferences->SaveConfig();
        Invalidate();
    }
    double GetOrientationSpacing() const { return OrientationSpacing; }
    void SetOrientationSize(double Value)
    {
        if (!FMath::IsFinite(Value) || Value <= 0.0) { return; }
        OrientationSize = FMath::Clamp(Value, 0.01, 10000.0);
        UStrandEditorPreferences* Preferences = GetMutableDefault<UStrandEditorPreferences>();
        Preferences->OrientationSize = OrientationSize;
        Preferences->SaveConfig();
        Invalidate();
    }
    double GetOrientationSize() const { return OrientationSize; }
    void SetSelectedPoint(int32 InIndex)
    {
        TArray<int32> Indices;
        if (InIndex != INDEX_NONE) { Indices.Add(InIndex); }
        SetSelectedPoints(Indices, InIndex);
    }
    void SetSelectedPoints(const TArray<int32>& Indices, int32 PrimaryIndex)
    {
        bWidgetFrameFrozen = false;
        if (SelectedPoint != PrimaryIndex) { SelectedHandle = 0; }
        SelectedPoints.Reset();
        for (int32 Index : Indices) { SelectedPoints.Add(Index); }
        SelectedPoint = PrimaryIndex;
        if (SelectedPoints.Num() > 1) { SelectedHandle = 0; }
        RefreshNativeWidget();
        Invalidate();
    }
    void SetSelectedHandle(int32 InHandle)
    {
        bWidgetFrameFrozen = false;
        SelectedHandle = FMath::Clamp(InHandle, 0, 2);
        RefreshNativeWidget();
        Invalidate();
    }
    void SetMarqueeMode(bool bEnable)
    {
        bMarqueeMode = bEnable;
        bMarqueeDragging = false;
        LassoPath.Reset();
        bLassoMode = false;
        RefreshNativeWidget();
        Invalidate();
    }
    void SetLassoMode(bool bEnable)
    {
        SetMarqueeMode(bEnable);
        bLassoMode = bEnable;
        Invalidate();
    }
    void SetAuthoredAnchorRotation(const FRotator& InRotation)
    {
        // A preview rebuild can update the authored rotation DURING a native
        // widget drag. Keep the cached widget frame until tracking stops.
        // Supplied by the Strand asset's actual stored anchor, not by the
        // spline's evaluated transform (which can include tangent framing).
        AuthoredAnchorRotation = InRotation;
        Invalidate();
    }
    FRotator GetAuthoredAnchorRotation() const { return AuthoredAnchorRotation; }
    FVector GetCurrentWidgetAxisX() const { return GetWidgetCoordSystem().GetScaledAxis(EAxis::X); }
    void SetStrandWidgetLocal(bool bLocal)
    {
        bWidgetFrameFrozen = false;
        bWidgetLocal = bLocal;
        // The mode manager owns only this Strand asset preview.
        FEditorViewportClient::SetWidgetCoordSystemSpace(bLocal ? COORD_Local : COORD_World);
        RefreshNativeWidget();
        Invalidate();
    }
    void SetStrandWidgetMode(int32 Mode)
    {
        bWidgetFrameFrozen = false;
        WidgetMode = FMath::Clamp(Mode, 0, 2);
        RefreshNativeWidget();
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
    void SetPointRotationCallback(TFunction<void(const FQuat&)> Callback)
    {
        OnPointRotation = MoveTemp(Callback);
    }
    void SetTangentRollCallback(TFunction<void(double)> Callback)
    {
        OnTangentRoll = MoveTemp(Callback);
    }
    void SetCameraAlignedEditing(bool bEnabled)
    {
        bCameraAlignedEditing = bEnabled;
        bWidgetFrameFrozen = false;
        RefreshNativeWidget();
        Invalidate();
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
        RefreshNativeWidget();
        Invalidate();
    }
    void SetPenPlane(int32 InLockedAxis)
    {
        LockedAxis = InLockedAxis;
        bWidgetFrameFrozen = false;
        Invalidate();
    }
    void SetPenCustomPlane(bool bEnabled, const FVector& Origin, const FVector& Normal)
    {
        bPenCustomPlane = bEnabled && !Origin.ContainsNaN() && !Normal.ContainsNaN() && !Normal.IsNearlyZero();
        PenCustomOrigin = Origin;
        PenCustomNormal = Normal.GetSafeNormal();
        bWidgetFrameFrozen = false;
        Invalidate();
    }
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
    void SetOnSegmentInsert(TFunction<void(int32, double)> InCallback) { OnSegmentInsert = MoveTemp(InCallback); }
    void SetSegmentBendCallbacks(TFunction<void(int32, double)> Begin,
        TFunction<void(const FVector&)> Update, TFunction<void()> End)
    {
        OnSegmentBendBegin = MoveTemp(Begin);
        OnSegmentBendUpdate = MoveTemp(Update);
        OnSegmentBendEnd = MoveTemp(End);
    }
    void SetOnContextMenu(TFunction<TSharedRef<SWidget>(int32, int32, double)> InCallback)
    {
        OnContextMenu = MoveTemp(InCallback);
    }
    void SetOnNudge(TFunction<void(const FVector&)> InCallback) { OnNudge = MoveTemp(InCallback); }
    FText GetInteractionHint() const
    {
        if (bLassoMode) { return FText::FromString(TEXT("LASSO SELECT  |  Draw freely around anchors  |  Shift: add  |  Ctrl: toggle  |  Esc: exit")); }
        if (bPenMode || bMarqueeMode) { return FText::GetEmpty(); }
        if (bDraggingScreenPlane) { return FText::FromString(TEXT("CAMERA 2D MOVE  |  Drag in screen X/Y, depth preserved  |  Release: finish")); }
        if (bCameraAlignedEditing && WidgetMode == 0 && IsSelectionWidgetAvailable())
        { return FText::FromString(TEXT("CAMERA 2D  |  Drag square handle in screen X/Y  |  Orbit freely between edits")); }
        if (WidgetMode == 1 && IsSelectionWidgetAvailable())
        {
            return FText::FromString(TEXT("ROTATE  |  Native screen rotation ring: Editor Preferences > Level Editor > Viewports > Enable Screen Rotate"));
        }
        if (GetCurrentWidgetAxis() != EAxisList::None)
        {
            return FText::FromString(TEXT("TRANSFORM GIZMO  |  Drag the highlighted axis or ring  |  Space: cycle mode  |  RMB+WASD: fly"));
        }
        if (HoveredPoint != INDEX_NONE)
        {
            if (HoveredHandle == 1) { return FText::FromString(TEXT("INCOMING HANDLE (ORANGE)  |  Click to edit tangent  |  Drag native widget to shape  |  RMB on anchor: actions")); }
            if (HoveredHandle == 2) { return FText::FromString(TEXT("OUTGOING HANDLE (BLUE)  |  Click to edit tangent  |  Drag native widget to shape  |  RMB on anchor: actions")); }
            return FText::FromString(TEXT("ANCHOR  |  Click: select  |  Shift: range  |  Ctrl: toggle  |  Right-click: point actions"));
        }
        if (HoveredSegment != INDEX_NONE)
        {
            return FText::FromString(TEXT("SEGMENT  |  Ctrl+drag: bend curve (anchors stay fixed)  |  Shift+click: insert  |  RMB: actions"));
        }
        return FText::GetEmpty();
    }


    // Illustrator-like intention: arrow on empty canvas, hand on anchor,
    // grab on tangent, crosshair only for adding a point. Native widget and
    // camera cursors always take precedence over Strand's editing cursors.
    virtual EMouseCursor::Type GetCursor(FViewport* InViewport, int32 X, int32 Y) override
    {
        if (bDraggingScreenPlane) { return EMouseCursor::GrabHandClosed; }
        if (ScreenPlaneHit(X, Y)) { return EMouseCursor::GrabHand; }
        if (bSegmentBending) { return EMouseCursor::GrabHandClosed; }
        if (!InViewport || InViewport->KeyState(EKeys::RightMouseButton) ||
            InViewport->KeyState(EKeys::MiddleMouseButton) ||
            InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt) ||
            InViewport->KeyState(EKeys::LeftMouseButton))
        {
            return FEditorViewportClient::GetCursor(InViewport, X, Y);
        }
        if (bMarqueeMode || bPenMode) { return EMouseCursor::Crosshairs; }
        const HHitProxy* Hit = InViewport->GetHitProxy(X, Y);
        if (Hit && Hit->IsA(HWidgetAxis::StaticGetType()))
        {
            return FEditorViewportClient::GetCursor(InViewport, X, Y);
        }
        if (Hit && Hit->IsA(HStrandSplineTangentProxy::StaticGetType()))
        {
            return EMouseCursor::GrabHand;
        }
        if (Hit && Hit->IsA(HStrandSplinePointProxy::StaticGetType()))
        {
            return EMouseCursor::Hand;
        }
        if (HoveredSegment != INDEX_NONE &&
            FSlateApplication::Get().GetModifierKeys().IsControlDown())
        {
            return EMouseCursor::GrabHand;
        }
        if (HoveredSegment != INDEX_NONE &&
            FSlateApplication::Get().GetModifierKeys().IsShiftDown())
        {
            return EMouseCursor::Crosshairs;
        }
        return EMouseCursor::Default;
    }

    // A 2D screen-space nearest-segment query is stable across redraws. The
    // old segment HHitProxy was alternating with the floor/preview dot when
    // Shift changed its render color, so moving the mouse made the spline and
    // status line flash. Only native widgets, anchors and handles need proxies.
    bool FindNearestSegmentOnScreen(const USplineComponent* Native, int32 X, int32 Y,
        int32& OutSegment, double& OutAlpha, double MaxDistancePixels = 11.0) const
    {
        OutSegment = INDEX_NONE;
        OutAlpha = 0.5;
        if (!bHasCachedView || !IsValid(Native) || Native->GetNumberOfSplinePoints() < 2) { return false; }
        const int32 Spans = Native->GetNumberOfSplinePoints() - (Native->IsClosedLoop() ? 0 : 1);
        if (Spans <= 0 || Spans > MaxPreviewSegments) { return false; }
        const int32 Steps = FMath::Clamp(MaxPreviewSegments / Spans, 6, 24);
        const FVector2D Cursor(static_cast<double>(X), static_cast<double>(Y));
        double BestDistanceSq = MaxDistancePixels * MaxDistancePixels;
        for (int32 Segment = 0; Segment < Spans; ++Segment)
        {
            FVector2D Previous;
            bool bPreviousValid = ProjectAnchor(SegmentPosition(Native, Segment, 0.0), Previous);
            for (int32 Step = 1; Step <= Steps; ++Step)
            {
                FVector2D Next;
                const bool bNextValid = ProjectAnchor(
                    SegmentPosition(Native, Segment, static_cast<double>(Step) / Steps), Next);
                if (bPreviousValid && bNextValid)
                {
                    const FVector2D Edge = Next - Previous;
                    const double EdgeLengthSquared = Edge.SizeSquared();
                    if (EdgeLengthSquared > 0.000001)
                    {
                        const double Fraction = FMath::Clamp(
                            FVector2D::DotProduct(Cursor - Previous, Edge) / EdgeLengthSquared, 0.0, 1.0);
                        const double DistanceSq = (Cursor - (Previous + Fraction * Edge)).SizeSquared();
                        if (DistanceSq < BestDistanceSq)
                        {
                            BestDistanceSq = DistanceSq;
                            OutSegment = Segment;
                            OutAlpha = (Step - 1.0 + Fraction) / Steps;
                        }
                    }
                }
                Previous = Next;
                bPreviousValid = bNextValid;
            }
        }
        if (OutSegment == INDEX_NONE) { return false; }
        // Refine the exact point of insertion against the native curve.
        OutAlpha = RefineSegmentHit(Native, OutSegment, OutAlpha, X, Y);
        return true;
    }

    void UpdateHover(FViewport* InViewport, int32 X, int32 Y)
    {
        if (!InViewport || InViewport->KeyState(EKeys::LeftMouseButton) ||
            InViewport->KeyState(EKeys::RightMouseButton) || bMarqueeDragging || bPenDragging)
        {
            return; // Do not change the target during manipulation.
        }
        const HHitProxy* Proxy = InViewport->GetHitProxy(X, Y);
        EAxisList::Type Axis = EAxisList::None;
        int32 Point = INDEX_NONE;
        int32 Handle = 0;
        int32 Segment = INDEX_NONE;
        double Alpha = 0.5;
        if (Proxy && IsNativeWidgetReady() && Proxy->IsA(HWidgetAxis::StaticGetType()))
        {
            const HWidgetAxis* Hit = static_cast<const HWidgetAxis*>(Proxy);
            if (!Hit->bDisabled) { Axis = Hit->Axis; }
        }
        else if (Proxy && Proxy->IsA(HStrandSplineTangentProxy::StaticGetType()))
        {
            const HStrandSplineTangentProxy* Hit = static_cast<const HStrandSplineTangentProxy*>(Proxy);
            Point = Hit->PointIndex;
            Handle = Hit->Handle;
        }
        else if (Proxy && Proxy->IsA(HStrandSplinePointProxy::StaticGetType()))
        {
            Point = static_cast<const HStrandSplinePointProxy*>(Proxy)->PointIndex;
        }
        else if (!bMarqueeMode && !bPenMode)
        {
            // Small hysteresis avoids target loss on the edge of the stroke.
            FindNearestSegmentOnScreen(Spline.Get(), X, Y, Segment, Alpha,
                HoveredSegment != INDEX_NONE ? 14.0 : 10.0);
        }
        const bool bChanged = HoveredPoint != Point || HoveredHandle != Handle ||
            HoveredSegment != Segment ||
            (Segment != INDEX_NONE && !FMath::IsNearlyEqual(HoveredSegmentAlpha, Alpha, 0.002)) ||
            GetCurrentWidgetAxis() != Axis;
        HoveredPoint = Point;
        HoveredHandle = Handle;
        HoveredSegment = Segment;
        HoveredSegmentAlpha = Alpha;
        if (GetCurrentWidgetAxis() != Axis) { SetCurrentWidgetAxis(Axis); }
        if (bChanged) { Invalidate(); }
    }
    virtual void MouseLeave(FViewport* InViewport) override
    {
        HoveredPoint = INDEX_NONE;
        HoveredSegment = INDEX_NONE;
        HoveredSegmentAlpha = 0.5;
        HoveredHandle = 0;
        if (!bWidgetAxisControlledByDrag) { SetCurrentWidgetAxis(EAxisList::None); }
        FEditorViewportClient::MouseLeave(InViewport);
        Invalidate();
    }

    // Camera-aligned 2D translate handle. The camera frame is captured at
    // gesture start; the mouse maps to world units at the original pivot depth.
    // Both anchor and tangent drags use the same plane interaction.
    static constexpr double ScreenPlaneHandleHalfSizePx = 12.0;
    static constexpr double ScreenPlaneAxisLengthPx = 54.0;
    bool GetScreenPlaneCenter(FVector2D& OutCenter) const
    {
        return bCameraAlignedEditing && WidgetMode == 0 &&
            IsSelectionWidgetAvailable() && bHasCachedView &&
            ProjectAnchor(GetWidgetLocation(), OutCenter);
    }
    bool ScreenPlaneHit(int32 X, int32 Y) const
    {
        FVector2D Center;
        if (!GetScreenPlaneCenter(Center)) { return false; }
        return FMath::Abs(X - Center.X) <= ScreenPlaneHandleHalfSizePx &&
            FMath::Abs(Y - Center.Y) <= ScreenPlaneHandleHalfSizePx;
    }
    void UpdateScreenPlaneDrag(int32 X, int32 Y)
    {
        if (!bDraggingScreenPlane) { return; }
        const FIntPoint Mouse(X, Y);
        const FIntPoint Pixels = Mouse - ScreenPlaneLastMouse;
        ScreenPlaneLastMouse = Mouse;
        if (Pixels.X == 0 && Pixels.Y == 0) { return; }
        const FVector Delta = StrandCameraSpace::ScreenPixelsToPlaneDelta(
            FVector2D(Pixels.X, Pixels.Y), ScreenPlaneRight, ScreenPlaneUp,
            ScreenPlaneLockedDepth, ScreenPlaneLockedFov, ScreenPlaneLockedWidth);
        if (SelectedHandle != 0 && OnTangentDragDelta)
        { OnTangentDragDelta(SelectedHandle, Delta); }
        else if (OnPointDragDelta) { OnPointDragDelta(Delta); }
        Invalidate();
    }
    void EndScreenPlaneDrag()
    {
        if (!bDraggingScreenPlane) { return; }
        bDraggingScreenPlane = false;
        if (OnPointDragEnd) { OnPointDragEnd(); }
        Invalidate();
    }
    bool IsSelectionWidgetAvailable() const
    {
        const USplineComponent* Native = Spline.Get();
        return !bPenMode && !bMarqueeMode && bShowPoints && IsValid(Native) &&
            SelectedPoint >= 0 && SelectedPoint < Native->GetNumberOfSplinePoints() &&
            SelectedPoints.Contains(SelectedPoint);
    }
    void SetOnSelectAll(TFunction<void()> InCallback) { OnSelectAll = MoveTemp(InCallback); }
    void SetOnClearSelection(TFunction<void()> InCallback) { OnClearSelection = MoveTemp(InCallback); }
    void SetPointDragCallbacks(TFunction<void(const FVector&)> InDelta, TFunction<void()> InEnd)
    {
        OnPointDragDelta = MoveTemp(InDelta);
        OnPointDragEnd = MoveTemp(InEnd);
    }

    // The built-in FWidget renderer depends on the viewport's editor-mode
    // manager, not just ShowWidget() and GetWidgetMode(). Update the private
    // manager whenever Strand's selection or authoring tool changes.
    void RefreshNativeWidget()
    {
        FEditorModeTools* Modes = GetModeTools();
        if (!Modes) { ShowWidget(false); return; }

        const USplineComponent* Native = Spline.Get();
        const bool bHasAnchor = IsValid(Native) &&
            SelectedPoint >= 0 && SelectedPoint < Native->GetNumberOfSplinePoints() &&
            SelectedPoints.Contains(SelectedPoint);
        const bool bCanTransform = bHasAnchor && bShowPoints && !bPenMode && !bMarqueeMode;
        Modes->SetSelectionHasSceneComponent(bCanTransform);
        // Camera-aligned translation uses Strand's planar drag handle, not a
        // misleading native 3D widget with a selectable depth axis.
        const bool bNativeWidget = bCanTransform && !(bCameraAlignedEditing && WidgetMode == 0);
        Modes->SetShowWidget(bNativeWidget);
        Modes->SetWidgetMode(bNativeWidget ? GetWidgetMode() : UE::Widget::WM_None);
        if (bCanTransform)
        {
            Modes->SetPivotLocation(GetWidgetLocation(), false);
        }
        ShowWidget(bCanTransform && !(bCameraAlignedEditing && WidgetMode == 0));
    }

    bool IsNativeWidgetReady()
    {
        const USplineComponent* Native = Spline.Get();
        return GetModeTools() != nullptr && GetShowWidget() &&
            IsValid(Native) && SelectedPoint >= 0 &&
            SelectedPoint < Native->GetNumberOfSplinePoints() &&
            SelectedPoints.Contains(SelectedPoint) && !bPenMode && !bMarqueeMode;
    }

    // The preview component remains transient; native widget deltas are routed
    // back to Strand's asset editor and its Undo/Redo transaction machinery.
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

    virtual ECoordSystem GetWidgetCoordSystemSpace() const override
    {
        return (WidgetMode == 0 && (bCameraAlignedEditing || LockedAxis >= 0 || bPenCustomPlane)) || bWidgetLocal
            ? COORD_Local : COORD_World;
    }

    virtual FMatrix GetWidgetCoordSystem() const override
    {
        const USplineComponent* Native = Spline.Get();
        if (bWidgetFrameFrozen) { return FrozenWidgetFrame; }
        // Constrained translation uses actual plane axes, never an unrelated
        // native world/local frame. Keep rotation and scale on their authored frames.
        if (WidgetMode == 0 && (LockedAxis >= 0 || bPenCustomPlane) && !bCameraAlignedEditing)
        {
            FVector Normal = bPenCustomPlane ? PenCustomNormal :
                LockedAxis == 0 ? FVector::XAxisVector :
                LockedAxis == 1 ? FVector::YAxisVector : FVector::ZAxisVector;
            FVector Preferred = LockedAxis == 0 ? FVector::YAxisVector : FVector::XAxisVector;
            FVector U, V, N;
            if (StrandPrecisionProjection::MakePlaneFrame(Normal, Preferred, U, V, N))
            {
                return FRotationMatrix::MakeFromXY(U, V);
            }
        }
        if (bCameraAlignedEditing && WidgetMode == 0)
        {
            // The X/Y handles lie in screen space; native Z remains visible
            // but the editor's movement constraint rejects depth translation.
            const FRotationMatrix Camera(GetViewRotation());
            return FRotationMatrix::MakeFromXY(
                Camera.GetScaledAxis(EAxis::Y), Camera.GetScaledAxis(EAxis::Z));
        }
        if (!bWidgetLocal || !IsValid(Native) ||
            SelectedPoint < 0 || SelectedPoint >= Native->GetNumberOfSplinePoints())
        {
            return FMatrix::Identity;
        }
        // Keep the native gizmo's local axes fixed for an entire drag. Rotate
        // modifies spline tangents, so recomputing the frame each mouse tick
        // would otherwise turn the axes underneath the artist's cursor.
        if (bWidgetFrameFrozen) { return FrozenWidgetFrame; }

        // Local X follows the actual FORWARD/outgoing tangent at this anchor,
        // including auto tangents evaluated by the preview spline. A Strand
        // point's saved rotation is independent of its tangents; it provides
        // an up/roll REFERENCE, never an alternative forward direction.
        FVector Forward = Native->GetTangentAtSplinePoint(SelectedPoint, ESplineCoordinateSpace::Local);
        if (Forward.IsNearlyZero())
        {
            const FSplinePoint Point = Native->GetSplinePointAt(SelectedPoint, ESplineCoordinateSpace::Local);
            Forward = !Point.LeaveTangent.IsNearlyZero() ? Point.LeaveTangent : Point.ArriveTangent;
        }
        if (Forward.IsNearlyZero())
        {
            // Linear/constant points can have zero tangents. Use the next
            // anchor's direction, including a closed loop's closing segment.
            const int32 Count = Native->GetNumberOfSplinePoints();
            const int32 Next = SelectedPoint + 1 < Count ? SelectedPoint + 1
                : (Native->IsClosedLoop() && Count > 1 ? 0 : INDEX_NONE);
            if (Next != INDEX_NONE)
            {
                Forward = Native->GetLocationAtSplinePoint(Next, ESplineCoordinateSpace::Local) -
                    Native->GetLocationAtSplinePoint(SelectedPoint, ESplineCoordinateSpace::Local);
            }
            if (Forward.IsNearlyZero() && SelectedPoint > 0)
            {
                Forward = Native->GetLocationAtSplinePoint(SelectedPoint, ESplineCoordinateSpace::Local) -
                    Native->GetLocationAtSplinePoint(SelectedPoint - 1, ESplineCoordinateSpace::Local);
            }
        }
        if (Forward.IsNearlyZero())
        {
            // A completely coincident/isolated point has no geometric
            // direction. Prefer its authored forward axis over an invalid frame.
            Forward = AuthoredAnchorRotation.Vector();
        }
        Forward = Forward.GetSafeNormal(SMALL_NUMBER, FVector::XAxisVector);

        FVector ReferenceUp = AuthoredAnchorRotation.RotateVector(FVector::UpVector).GetSafeNormal();
        if (ReferenceUp.IsNearlyZero() ||
            FMath::Abs(FVector::DotProduct(Forward, ReferenceUp)) > 0.98)
        {
            ReferenceUp = FMath::Abs(FVector::DotProduct(Forward, FVector::UpVector)) > 0.98
                ? FVector::YAxisVector : FVector::UpVector;
        }
        return FRotationMatrix::MakeFromXZ(Forward, ReferenceUp);
    }

    virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis,
        FVector& Drag, FRotator& Rot, FVector& Scale) override
    {
        const USplineComponent* Native = Spline.Get();
        if (!Native || SelectedPoint < 0 || SelectedPoint >= Native->GetNumberOfSplinePoints()
            || CurrentAxis == EAxisList::None)
        {
            return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
        }
        // Cache the tangent-aligned basis before the first edit delta.
        // The spline's outgoing tangent may rotate during this drag.
        if (!bWidgetFrameFrozen && (bWidgetLocal || bCameraAlignedEditing ||
            (WidgetMode == 0 && (LockedAxis >= 0 || bPenCustomPlane))))
        {
            FrozenWidgetFrame = GetWidgetCoordSystem();
            bWidgetFrameFrozen = true;
        }
        if (SelectedHandle == 0 && WidgetMode == 1 && OnPointRotation)
        {
            // Let Unreal's native widget determine the signed angular delta.
            // Avoid reinterpreting cursor motion, which reverses on some views.
            const FQuat WidgetDelta = Rot.Quaternion();
            if (!WidgetDelta.IsIdentity(0.000001))
            {
                // For a single anchor, local red/X is a pure roll around the
                // outgoing tangent, not a curve-shape rotation. Keep the
                // authored tangents and anchor position intact.
                if (bWidgetLocal && CurrentAxis == EAxisList::X &&
                    SelectedPoints.Num() == 1 && OnTangentRoll)
                {
                    FVector Axis;
                    double Angle = 0.0;
                    WidgetDelta.ToAxisAndAngle(Axis, Angle);
                    const double SignedDegrees = FMath::RadiansToDegrees(Angle * Axis.X);
                    if (FMath::IsFinite(SignedDegrees) && FMath::Abs(SignedDegrees) > 0.00001)
                    {
                        OnTangentRoll(SignedDegrees);
                    }
                    return true;
                }
                const FQuat Frame = bWidgetLocal
                    ? FQuat(GetWidgetCoordSystem()).GetNormalized()
                    : FQuat::Identity;
                const FQuat AssetDelta = StrandSplineEditOperations::WidgetRotationToAsset(
                    WidgetDelta, Frame, bWidgetLocal);
                OnPointRotation(AssetDelta);
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
        // Release the frozen frame after each native widget drag. Keyboard
        // nudges can emit a delta without bIsDraggingWidget.
        bWidgetFrameFrozen = false;
        if (OnPointDragEnd) { OnPointDragEnd(); }
        FEditorViewportClient::TrackingStopped();
    }

    void FrameSpline()
    {
        const FIntPoint Size = Viewport ? Viewport->GetSizeXY() : FIntPoint(0, 0);
        if (Size.X <= 1 || Size.Y <= 1)
        {
            // An asset can be assigned before its Slate viewport has a size.
            // Defer initial framing instead of using an incorrect aspect ratio.
            bFrameSplineWhenReady = true;
            return;
        }
        bFrameSplineWhenReady = false;
        TArray<FVector> Samples;
        GatherFrameSamples(Spline.Get(), Samples);
        if (Samples.IsEmpty()) { return; }

        FBox Bounds(ForceInit);
        for (const FVector& Point : Samples) { Bounds += Point; }
        if (!Bounds.IsValid) { return; }
        if (GetViewportType() == LVT_Perspective)
        {
            StrandSplineFraming::FPerspectiveFit Fit;
            const double Aspect = static_cast<double>(Size.X) / Size.Y;
            // 74% of the safe viewport half-angle leaves 26% margin around
            // the projected silhouette, rather than just barely fitting it.
            if (StrandSplineFraming::FitPerspective(Samples, GetViewRotation(),
                static_cast<double>(ViewFOV), Aspect, 0.74, Fit))
            {
                SetViewLocation(Fit.Location);
                SetLookAtLocation(Fit.Target, false);
            }
            else
            {
                FocusViewportOnBox(Bounds, true);
            }
        }
        else
        {
            // Keep UE's native orthographic conventions, then adjust zoom to
            // allow comfortable margins in the actual rectangular viewport.
            FocusViewportOnBox(Bounds, true);
            const ELevelViewportType Type = GetViewportType();
            FVector2D Minimum(DBL_MAX, DBL_MAX), Maximum(-DBL_MAX, -DBL_MAX);
            for (const FVector& P : Samples)
            {
                const FVector2D Plane = (Type == LVT_OrthoXY || Type == LVT_OrthoNegativeXY)
                    ? FVector2D(P.X, P.Y)
                    : (Type == LVT_OrthoXZ || Type == LVT_OrthoNegativeXZ)
                        ? FVector2D(P.X, P.Z) : FVector2D(P.Y, P.Z);
                Minimum.X = FMath::Min(Minimum.X, Plane.X);
                Minimum.Y = FMath::Min(Minimum.Y, Plane.Y);
                Maximum.X = FMath::Max(Maximum.X, Plane.X);
                Maximum.Y = FMath::Max(Maximum.Y, Plane.Y);
            }
            const double UnitsPerPixel = GetOrthoUnitsPerPixel(Viewport);
            if (UnitsPerPixel > 0.0 && FMath::IsFinite(UnitsPerPixel))
            {
                const double DesiredUnitsPerPixel = FMath::Max(
                    FMath::Max((Maximum.X - Minimum.X) / (Size.X * 0.74),
                        (Maximum.Y - Minimum.Y) / (Size.Y * 0.74)), 0.01);
                const double Factor = DesiredUnitsPerPixel / UnitsPerPixel;
                if (FMath::IsFinite(Factor) && Factor > 0.0)
                {
                    SetOrthoZoom(static_cast<float>(FMath::Clamp(
                        static_cast<double>(GetOrthoZoom()) * Factor, 1.0, 1.0e9)));
                }
            }
            CenterViewportAtPoint(Bounds.GetCenter(), true);
        }
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
        if (EventArgs.Key == EKeys::LeftMouseButton && EventArgs.Event == IE_Pressed &&
            !bDraggingScreenPlane && !EventArgs.Viewport->KeyState(EKeys::LeftAlt) &&
            !EventArgs.Viewport->KeyState(EKeys::RightAlt) &&
            ScreenPlaneHit(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY()))
        {
            const FRotationMatrix Camera(GetViewRotation());
            ScreenPlaneRight = Camera.GetScaledAxis(EAxis::Y).GetSafeNormal();
            ScreenPlaneUp = Camera.GetScaledAxis(EAxis::Z).GetSafeNormal();
            const double Depth = FMath::Max(1.0, FVector::DotProduct(
                GetWidgetLocation() - GetViewLocation(), GetCameraForward()));
            const double Width = FMath::Max(1, EventArgs.Viewport->GetSizeXY().X);
            ScreenPlaneLockedDepth = Depth;
            ScreenPlaneLockedFov = ViewFOV;
            ScreenPlaneLockedWidth = Width;
            ScreenPlaneLastMouse = FIntPoint(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
            bDraggingScreenPlane = true;
            Invalidate();
            return true;
        }
        if (EventArgs.Key == EKeys::LeftMouseButton && EventArgs.Event == IE_Released && bDraggingScreenPlane)
        {
            EndScreenPlaneDrag();
            return true;
        }
        if (EventArgs.Key == EKeys::Escape && bDraggingScreenPlane)
        {
            EndScreenPlaneDrag();
            return true;
        }
        // Only a stationary RMB click on an editing target opens Strand's
        // context menu. RMB drags and RMB+WASD always stay native camera input.
        if (EventArgs.Key == EKeys::LeftMouseButton && EventArgs.Event == IE_Pressed &&
            !bPenMode && !bMarqueeMode && OnSegmentBendBegin && OnSegmentBendUpdate &&
            FSlateApplication::Get().GetModifierKeys().IsControlDown() &&
            !FSlateApplication::Get().GetModifierKeys().IsAltDown() &&
            !FSlateApplication::Get().GetModifierKeys().IsShiftDown())
        {
            const int32 X = EventArgs.Viewport->GetMouseX();
            const int32 Y = EventArgs.Viewport->GetMouseY();
            const HHitProxy* Hit = EventArgs.Viewport->GetHitProxy(X, Y);
            if (!Hit || (!Hit->IsA(HWidgetAxis::StaticGetType()) &&
                         !Hit->IsA(HStrandSplinePointProxy::StaticGetType()) &&
                         !Hit->IsA(HStrandSplineTangentProxy::StaticGetType())))
            {
                int32 Segment = INDEX_NONE;
                double Alpha = 0.5;
                if (FindNearestSegmentOnScreen(Spline.Get(), X, Y, Segment, Alpha) &&
                    Alpha >= 0.1 && Alpha <= 0.9 && bHasCachedView)
                {
                    SegmentBendOrigin = SegmentPosition(Spline.Get(), Segment, Alpha);
                    SegmentBendPlaneNormal = GetViewRotation().Vector().GetSafeNormal();
                    if (!SegmentBendPlaneNormal.IsNearlyZero())
                    {
                        bSegmentBending = true;
                        SegmentBendCursorStart = FIntPoint(X, Y);
                        OnSegmentBendBegin(Segment, Alpha);
                        Invalidate();
                        return true;
                    }
                }
            }
        }
        if (EventArgs.Key == EKeys::LeftMouseButton && EventArgs.Event == IE_Released && bSegmentBending)
        {
            bSegmentBending = false;
            if (OnSegmentBendEnd) { OnSegmentBendEnd(); }
            Invalidate();
            return true;
        }
        if (EventArgs.Key == EKeys::RightMouseButton && EventArgs.Event == IE_Pressed)
        {
            bRightClickCandidate = !bPenMode && !bMarqueeMode;
            bRightClickMoved = false;
            RightClickPoint = INDEX_NONE;
            RightClickSegment = INDEX_NONE;
            RightClickAlpha = 0.5;
            EventArgs.Viewport->GetMousePos(RightClickStart, true);
            if (bRightClickCandidate)
            {
                const HHitProxy* Hit = EventArgs.Viewport->GetHitProxy(RightClickStart.X, RightClickStart.Y);
                if (Hit && Hit->IsA(HStrandSplinePointProxy::StaticGetType()))
                {
                    RightClickPoint = static_cast<const HStrandSplinePointProxy*>(Hit)->PointIndex;
                }
                else
                {
                    FindNearestSegmentOnScreen(Spline.Get(), RightClickStart.X, RightClickStart.Y,
                        RightClickSegment, RightClickAlpha);
                }
            }
        }
        if (EventArgs.Key == EKeys::RightMouseButton && EventArgs.Event == IE_Released)
        {
            FIntPoint ReleasePosition;
            EventArgs.Viewport->GetMousePos(ReleasePosition, true);
            const bool bShowContext = bRightClickCandidate && !bRightClickMoved &&
                (ReleasePosition - RightClickStart).SizeSquared() <= 36 &&
                (RightClickPoint != INDEX_NONE || RightClickSegment != INDEX_NONE) &&
                !!OnContextMenu;
            bRightClickCandidate = false;
            const bool bBaseHandled = FEditorViewportClient::InputKey(EventArgs);
            if (bShowContext)
            {
                TSharedRef<SWidget> Menu = OnContextMenu(RightClickPoint, RightClickSegment, RightClickAlpha);
                if (TSharedPtr<SEditorViewport> ViewportWidget = GetEditorViewportWidget())
                {
                    FSlateApplication::Get().PushMenu(ViewportWidget.ToSharedRef(), FWidgetPath(), Menu,
                        FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
                }
                return true;
            }
            return bBaseHandled;
        }
        if (bRightClickCandidate && EventArgs.Event == IE_Pressed &&
            EventArgs.Key != EKeys::RightMouseButton && EventArgs.Key != EKeys::LeftShift &&
            EventArgs.Key != EKeys::RightShift) { bRightClickMoved = true; }
        if (EventArgs.Key == EKeys::LeftShift || EventArgs.Key == EKeys::RightShift)
        {
            // The insertion preview must also update when Shift toggles while
            // the mouse is stationary; do not capture Shift from native input.
            Invalidate();
        }
        if (EventArgs.Event == IE_Pressed)
        {
            const FModifierKeysState Mods = FSlateApplication::Get().GetModifierKeys();
            if (!EventArgs.Viewport->KeyState(EKeys::RightMouseButton) &&
                Mods.IsShiftDown() && !Mods.IsAltDown() && !Mods.IsControlDown() &&
                EventArgs.Key == EKeys::B && OnToolShortcut)
            {
                OnToolShortcut(3); return true;
            }
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
            // Unreal owns Arrow/Page keys for camera navigation. Never use
            // those for Strand edits: viewport KeyState can drive the camera
            // independently even when an InputKey override consumes the press.
            // Ctrl+Shift+I/J/K/L/U/O is an isolated six-direction nudge chord.
            if (!bFlying && !bPenMode && !bMarqueeMode && SelectedPoint != INDEX_NONE &&
                Mods.IsControlDown() && Mods.IsShiftDown() && !Mods.IsAltDown() && OnNudge)
            {
                const FVector Direction = StrandSplineInputUtils::NudgeDirection(
                    EventArgs.Key, Mods.IsControlDown(), Mods.IsShiftDown(), Mods.IsAltDown());
                if (!Direction.IsNearlyZero())
                {
                    OnNudge(Direction);
                    return true;
                }
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
                LassoPath.Reset();
                if (bLassoMode) { LassoPath.Add(MarqueeStart); }
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
                if (bLassoMode) { UpdateMarqueeCursor(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY()); }
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
                    const bool bClick = bLassoMode
                        ? !StrandSplineSelectionUtils::IsSelectionStroke(LassoPath)
                        : MaxX - MinX < 4 && MaxY - MinY < 4;
                    if (bClick)
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
                        LassoPath.Reset();
                        Invalidate();
                        return true;
                    }
                    if (const USplineComponent* Native = Spline.Get())
                    {
                        for (int32 Index = 0; Index < Native->GetNumberOfSplinePoints(); ++Index)
                        {
                            FVector2D Screen;
                            if (ProjectAnchor(Native->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local), Screen) &&
                                (bLassoMode ? StrandSplineSelectionUtils::IsInsideLasso(Screen, LassoPath)
                                            : (Screen.X >= MinX && Screen.X <= MaxX && Screen.Y >= MinY && Screen.Y <= MaxY)))
                            {
                                Indices.Add(Index);
                            }
                        }
                    }
                    OnMarqueeSelected(Indices, bMarqueeAdd, bMarqueeToggle);
                }
                LassoPath.Reset();
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
        // Raw mouse axis movement can occur while RMB flight navigation captures
        // and locks the OS pointer. It must cancel contextual right-click.
        if (bRightClickCandidate && (EventArgs.Key == EKeys::MouseX || EventArgs.Key == EKeys::MouseY) &&
            FMath::Abs(EventArgs.AmountDepressed) > KINDA_SMALL_NUMBER)
        {
            bRightClickMoved = true;
        }
        // Consuming press/release alone is insufficient: native viewport mouse
        // deltas can still move the camera during an active Pen drag.
        if ((bPenMode && bPenDragging) || bMarqueeDragging || bSegmentBending || bDraggingScreenPlane) { return true; }
        return FEditorViewportClient::InputAxis(EventArgs);
    }

    virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override
    {
        PenMouseX = X; PenMouseY = Y;
        if (bRightClickCandidate && (FIntPoint(X, Y) - RightClickStart).SizeSquared() > 36)
        { bRightClickMoved = true; }
        if (bMarqueeDragging) { UpdateMarqueeCursor(X, Y); }
        UpdatePenMouse();
        if (bDraggingScreenPlane) { UpdateScreenPlaneDrag(X, Y); return; }
        if (bSegmentBending) { UpdateSegmentBendCursor(X, Y); return; }
        if ((!bPenMode || !bPenDragging) && !bMarqueeDragging)
        {
            FEditorViewportClient::MouseMove(InViewport, X, Y);
            UpdateHover(InViewport, X, Y);
        }
    }

    virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override
    {
        PenMouseX = X; PenMouseY = Y;
        if (bRightClickCandidate && (FIntPoint(X, Y) - RightClickStart).SizeSquared() > 36)
        { bRightClickMoved = true; }
        if (bMarqueeDragging) { UpdateMarqueeCursor(X, Y); }
        UpdatePenMouse();
        if (bDraggingScreenPlane) { UpdateScreenPlaneDrag(X, Y); return; }
        if (bSegmentBending) { UpdateSegmentBendCursor(X, Y); return; }
        if ((!bPenMode || !bPenDragging) && !bMarqueeDragging)
        {
            FEditorViewportClient::CapturedMouseMove(InViewport, X, Y);
            UpdateHover(InViewport, X, Y);
        }
    }

    void SetPenExit(TFunction<void()> InExit) { OnPenExit = MoveTemp(InExit); }

    virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key,
        EInputEvent Event, uint32 HitX, uint32 HitY) override
    {
        if (bSegmentBending && Key == EKeys::LeftMouseButton) { return; }
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
            // Match the stable screen-space Shift-hover preview. Hit proxies
            // may be stale or covered by the insertion marker itself.
            if (FSlateApplication::Get().GetModifierKeys().IsShiftDown() && OnSegmentInsert &&
                (!HitProxy || (!HitProxy->IsA(HStrandSplinePointProxy::StaticGetType()) &&
                               !HitProxy->IsA(HStrandSplineTangentProxy::StaticGetType()))))
            {
                int32 InsertSegment = INDEX_NONE;
                double InsertAlpha = 0.5;
                if (FindNearestSegmentOnScreen(Spline.Get(), static_cast<int32>(HitX),
                    static_cast<int32>(HitY), InsertSegment, InsertAlpha) &&
                    InsertAlpha > 0.001 && InsertAlpha < 0.999)
                {
                    OnSegmentInsert(InsertSegment, InsertAlpha);
                    return;
                }
            }
            // Ordinary curve click preserves point selection.
            if (HitProxy && HitProxy->IsA(HStrandSplineSegmentProxy::StaticGetType()))
            {
                // Ordinary click on a segment does not discard point selection.
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
        if (bFrameSplineWhenReady && Viewport &&
            Viewport->GetSizeXY().X > 1 && Viewport->GetSizeXY().Y > 1)
        {
            FrameSpline();
        }
        if (!PDI || !View) { return; }
        CachedInvViewProjection = View->ViewMatrices.GetClipToWorld();
        CachedWorldToClip = CachedInvViewProjection.InverseFast();
        CachedViewRect = View->UnscaledViewRect;
        bHasCachedView = true;
        const USplineComponent* Native = Spline.Get();
        if (!IsValid(Native)) { return; }
        const int32 PointCount = Native->GetNumberOfSplinePoints();
        FVector2D ScreenPlaneScreenCenter;
        if (GetScreenPlaneCenter(ScreenPlaneScreenCenter) && !bPenMode && !bMarqueeMode)
        {
            const FVector Pivot = GetWidgetLocation();
            const FRotationMatrix Camera(GetViewRotation());
            const FVector Right = Camera.GetScaledAxis(EAxis::Y).GetSafeNormal();
            const FVector Up = Camera.GetScaledAxis(EAxis::Z).GetSafeNormal();
            const double Depth = FMath::Max(1.0, FVector::DotProduct(
                Pivot - GetViewLocation(), GetCameraForward()));
            const double PixelScale = Depth * 2.0 *
                FMath::Tan(FMath::DegreesToRadians(ViewFOV * 0.5)) /
                FMath::Max(1, CachedViewRect.Width());
            const double CenterSize = PixelScale * ScreenPlaneHandleHalfSizePx;
            const double AxisSize = PixelScale * ScreenPlaneAxisLengthPx;
            const bool bHovered = bDraggingScreenPlane || ScreenPlaneHit(PenMouseX, PenMouseY);
            const FLinearColor Color = bHovered ? FLinearColor(1.0f, 0.85f, 0.3f) :
                FLinearColor(0.82f, 0.90f, 0.97f);
            const FVector A = Pivot - Right * CenterSize - Up * CenterSize;
            const FVector B = Pivot + Right * CenterSize - Up * CenterSize;
            const FVector C = Pivot + Right * CenterSize + Up * CenterSize;
            const FVector D = Pivot - Right * CenterSize + Up * CenterSize;
            PDI->DrawLine(A, B, Color, SDPG_Foreground, 2.0f);
            PDI->DrawLine(B, C, Color, SDPG_Foreground, 2.0f);
            PDI->DrawLine(C, D, Color, SDPG_Foreground, 2.0f);
            PDI->DrawLine(D, A, Color, SDPG_Foreground, 2.0f);
            PDI->DrawLine(Pivot - Right * AxisSize, Pivot - Right * CenterSize,
                FLinearColor(0.95f, 0.30f, 0.30f), SDPG_Foreground, 1.5f);
            PDI->DrawLine(Pivot + Right * CenterSize, Pivot + Right * AxisSize,
                FLinearColor(0.95f, 0.30f, 0.30f), SDPG_Foreground, 1.5f);
            PDI->DrawLine(Pivot - Up * AxisSize, Pivot - Up * CenterSize,
                FLinearColor(0.25f, 0.86f, 0.40f), SDPG_Foreground, 1.5f);
            PDI->DrawLine(Pivot + Up * CenterSize, Pivot + Up * AxisSize,
                FLinearColor(0.25f, 0.86f, 0.40f), SDPG_Foreground, 1.5f);
        }

        // Foreground marquee uses four projected cursor rays, so it works in
        // perspective and all orthographic viewport projections.
        if (bMarqueeDragging && bHasCachedView)
        {
            if (bLassoMode)
            {
                auto DrawStrokeLine = [this, PDI](FIntPoint First, FIntPoint Last)
                {
                    FVector FirstOrigin, FirstDirection, LastOrigin, LastDirection;
                    FSceneView::DeprojectScreenToWorld(FVector2D(First.X, First.Y),
                        CachedViewRect, CachedInvViewProjection, FirstOrigin, FirstDirection);
                    FSceneView::DeprojectScreenToWorld(FVector2D(Last.X, Last.Y),
                        CachedViewRect, CachedInvViewProjection, LastOrigin, LastDirection);
                    PDI->DrawLine(FirstOrigin + FirstDirection.GetSafeNormal() * 100.0,
                        LastOrigin + LastDirection.GetSafeNormal() * 100.0,
                        StrandSelectedColor, SDPG_Foreground, 0.85f);
                };
                for (int32 I = 1; I < LassoPath.Num(); ++I)
                {
                    DrawStrokeLine(LassoPath[I - 1], LassoPath[I]);
                }
                if (LassoPath.Num() >= 2)
                {
                    DrawStrokeLine(LassoPath.Last(), LassoPath[0]);
                }
            }
            else
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
                    StrandSelectedColor, SDPG_Foreground, 0.85f);
            }
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

        // For editable splines, hit-proxy segment lines also render the curve.
        // Do not paint an overlapping second curve over them: coplanar curves
        // with different sampling were creating unstable flashing/thickness.
        const int32 ActiveSpanCount = PointCount - (Native->IsClosedLoop() ? 0 : 1);
        if (PointCount >= 2 && (bMarqueeMode || ActiveSpanCount > MaxPreviewSegments))
        {
            const int32 SpanCount = ActiveSpanCount;
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
        if (!bMarqueeMode && PointCount >= 2 &&
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
                    const bool bInsertionTarget = !bPenMode &&
                        FSlateApplication::Get().GetModifierKeys().IsShiftDown() &&
                        HoveredSegment == Segment;
                    const bool bBendTarget = !bPenMode && !bMarqueeMode &&
                        FSlateApplication::Get().GetModifierKeys().IsControlDown() &&
                        HoveredSegment == Segment;
                    PDI->DrawLine(Prev, Next,
                        bBendTarget ? FLinearColor(0.95f, 0.76f, 0.23f) :
                            (bInsertionTarget ? FLinearColor(0.24f, 0.88f, 0.88f) : StrandCurveColor),
                        SDPG_World, 3.0f);
                    PDI->SetHitProxy(nullptr);
                    Prev = Next;
                }
            }
        }

        // Editor-only orientation visualization, evaluated using the same native
        // spline frames consumed by spline-following Unreal components.
        // Cap the number of samples regardless of length to bound draw cost.
        if (PointCount >= 2 &&
            (bOrientationVisuals[0] || bOrientationVisuals[1] || bOrientationVisuals[2] || bOrientationVisuals[3]))
        {
            const double Length = Native->GetSplineLength();
            if (Length > UE_DOUBLE_SMALL_NUMBER)
            {
                const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Min(Length / OrientationSpacing, 2048.0)), 1, 2048);
                const bool bClosed = Native->IsClosedLoop();
                const int32 Samples = bClosed ? Steps : Steps + 1;
                const FLinearColor UpColor(0.25f, 0.58f, 1.0f);
                const FLinearColor RightColor(0.24f, 0.84f, 0.40f);
                const FLinearColor ForwardColor(1.0f, 0.30f, 0.28f);
                const FLinearColor RibbonColor(0.95f, 0.68f, 0.20f, 0.85f);
                FVector PreviousEdge = FVector::ZeroVector;
                bool bHavePreviousEdge = false;
                for (int32 Index = 0; Index < Samples; ++Index)
                {
                    const double Distance = Length * static_cast<double>(Index) / Steps;
                    const FVector Position = Native->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
                    const FQuat Rotation = Native->GetQuaternionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
                    const FVector Forward = Rotation.GetAxisX();
                    const FVector Right = Rotation.GetAxisY();
                    const FVector Up = Rotation.GetAxisZ();
                    if (Position.ContainsNaN() || Rotation.ContainsNaN()) { bHavePreviousEdge = false; continue; }
                    const auto DrawArrow = [PDI, &Position](const FVector& Direction, double Size, const FLinearColor& Color)
                    {
                        const FVector Tip = Position + Direction * Size;
                        PDI->DrawLine(Position, Tip, Color, SDPG_World, 1.4f);
                        // Small unambiguous arrowhead, orthogonal to arrow direction.
                        FVector Side = FVector::CrossProduct(Direction, FVector::XAxisVector).GetSafeNormal();
                        if (Side.IsNearlyZero()) { Side = FVector::CrossProduct(Direction, FVector::ZAxisVector).GetSafeNormal(); }
                        PDI->DrawLine(Tip, Tip - Direction * (Size * 0.22) + Side * (Size * 0.12), Color, SDPG_World, 1.4f);
                        PDI->DrawLine(Tip, Tip - Direction * (Size * 0.22) - Side * (Size * 0.12), Color, SDPG_World, 1.4f);
                    };
                    if (bOrientationVisuals[0] && !bOrientationVisuals[2]) { DrawArrow(Up, OrientationSize, UpColor); }
                    if (bOrientationVisuals[1] && !bOrientationVisuals[2]) { DrawArrow(Right, OrientationSize, RightColor); }
                    if (bOrientationVisuals[2])
                    {
                        DrawArrow(Forward, OrientationSize * 0.7, ForwardColor);
                        DrawArrow(Right, OrientationSize * 0.7, RightColor);
                        DrawArrow(Up, OrientationSize * 0.7, UpColor);
                    }
                    if (bOrientationVisuals[3])
                    {
                        const FVector Edge = Position + Right * OrientationSize * 0.7;
                        PDI->DrawLine(Position, Edge, RibbonColor, SDPG_World, 0.9f);
                        if (bHavePreviousEdge) { PDI->DrawLine(PreviousEdge, Edge, RibbonColor, SDPG_World, 2.5f); }
                        PreviousEdge = Edge;
                        bHavePreviousEdge = true;
                    }
                }
                if (bClosed && bOrientationVisuals[3] && bHavePreviousEdge)
                {
                    const FVector StartPos = Native->GetLocationAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::Local);
                    const FVector StartRight = Native->GetQuaternionAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::Local).GetAxisY();
                    PDI->DrawLine(PreviousEdge, StartPos + StartRight * OrientationSize * 0.7, RibbonColor, SDPG_World, 2.5f);
                }
            }
        }

        // All constrained editing modes show the same non-interactive plane
        // feedback. The plane is centered at the selection, not world origin,
        // except a captured three-anchor plane which has a fixed authored origin.
        if (bPenCustomPlane || LockedAxis >= 0 || bCameraAlignedEditing)
        {
            const FVector Normal = bPenCustomPlane ? PenCustomNormal :
                bCameraAlignedEditing ? GetCameraForward() :
                LockedAxis == 0 ? FVector::XAxisVector :
                LockedAxis == 1 ? FVector::YAxisVector : FVector::ZAxisVector;
            const FVector Origin = bPenCustomPlane ? PenCustomOrigin :
                SelectedPoint >= 0 && SelectedPoint < PointCount ? GetWidgetLocation() :
                FVector::ZeroVector;
            const FVector Preferred = bCameraAlignedEditing ?
                FRotationMatrix(GetViewRotation()).GetScaledAxis(EAxis::Y) :
                LockedAxis == 0 ? FVector::YAxisVector : FVector::XAxisVector;
            FVector U, V, N;
            if (StrandPrecisionProjection::MakePlaneFrame(Normal, Preferred, U, V, N))
            {
                constexpr double Step = 50.0;
                constexpr int32 HalfCells = 4;
                const FLinearColor PlaneColor(0.60f, 0.33f, 0.82f, 0.55f);
                for (int32 I = -HalfCells; I <= HalfCells; ++I)
                {
                    const double Offset = static_cast<double>(I) * Step;
                    PDI->DrawLine(Origin + U * Offset - V * (HalfCells * Step),
                        Origin + U * Offset + V * (HalfCells * Step), PlaneColor, SDPG_World, 0.6f);
                    PDI->DrawLine(Origin + V * Offset - U * (HalfCells * Step),
                        Origin + V * Offset + U * (HalfCells * Step), PlaneColor, SDPG_World, 0.6f);
                }
                PDI->DrawLine(Origin, Origin + N * 65.0,
                    FLinearColor(0.78f, 0.45f, 1.0f), SDPG_Foreground, 2.0f);
            }
        }

        // Preview of a pending cleanup operation. This is never a second hit
        // proxy and never modifies the Strand asset or the native preview.
        for (int32 Index = 1; Index < CleanupPreviewPolyline.Num(); ++Index)
        {
            PDI->DrawLine(CleanupPreviewPolyline[Index - 1], CleanupPreviewPolyline[Index],
                FLinearColor(1.0f, 0.64f, 0.20f, 1.0f), SDPG_Foreground, 3.0f);
        }

        // Precision snap preview: yellow source to magenta target. No hit proxy and no asset mutation.
        for (const TPair<FVector, FVector>& Segment : SnapPreviewSegments)
        {
            PDI->DrawLine(Segment.Key, Segment.Value, FLinearColor(1.0f, 0.32f, 0.80f), SDPG_Foreground, 2.0f);
            PDI->DrawPoint(Segment.Value, FLinearColor(1.0f, 0.0f, 0.75f), 12.0f, SDPG_Foreground);
        }

        // Shift previews the insertion dot while the sampled segment itself
        // is tinted in the base draw pass to avoid overlapping curve lines.
        const bool bPreviewInsert = !bPenMode && !bMarqueeMode &&
            FSlateApplication::Get().GetModifierKeys().IsShiftDown() &&
            HoveredSegment >= 0 && PointCount >= 2 &&
            HoveredSegment < PointCount - (Native->IsClosedLoop() ? 0 : 1);
        if (bPreviewInsert)
        {
            // Only the cursor marker is painted as an overlay. The targeted
            // segment is tinted in its NORMAL draw pass, so the two line paths
            // never z-fight or steal each other's hover hit proxies.
            const FVector InsertAt = SegmentPosition(Native, HoveredSegment, HoveredSegmentAlpha);
            PDI->DrawPoint(InsertAt, FLinearColor(0.03f, 0.10f, 0.10f), 18.0f, SDPG_Foreground);
            PDI->DrawPoint(InsertAt, FLinearColor(0.24f, 0.88f, 0.88f), 12.0f, SDPG_Foreground);
            PDI->DrawPoint(InsertAt, StrandHoverColor, 4.0f, SDPG_Foreground);
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
                const bool bIsSelected = SelectedPoints.Contains(Index);
                const bool bSoftSelectionActive = SoftSelectionWeights.Num() == PointCount;
                const FLinearColor Color = StrandSoftSelectionVisuals::GetDisplayedAnchorColor(
                    bSoftSelectionActive, bIsSelected, bSoftSelectionActive ? SoftSelectionWeights[Index] : 0.0,
                    Index == 0 ? StrandStartColor
                        : (Index == PointCount - 1 && !Native->IsClosedLoop() ? StrandEndColor : StrandControlColor),
                    StrandSelectedColor);
                if (HoveredPoint == Index && HoveredHandle == 0)
                {
                    PDI->DrawPoint(Position, StrandHoverColor, 22.0f, SDPG_Foreground);
                }
                PDI->SetHitProxy(new HStrandSplinePointProxy(Index));
                // The selected anchor is the 100% influence reference. Show its
                // magenta core, surrounded by Strand's existing yellow selection
                // marker so selection stays legible against the falloff gradient.
                if (bSoftSelectionActive && bIsSelected)
                {
                    PDI->DrawPoint(Position, StrandSelectedColor,
                        Index == SelectedPoint ? FMath::Clamp(GetDefault<UStrandEditorPreferences>()->SelectedAnchorMarkerSize, 16.0f, 64.0f) + 5.0f : 16.0f, SDPG_Foreground);
                }
                PDI->DrawPoint(Position, Color,
                    Index == SelectedPoint ? (bSoftSelectionActive ? FMath::Clamp(GetDefault<UStrandEditorPreferences>()->SelectedAnchorMarkerSize, 16.0f, 64.0f) : 17.0f)
                        : bIsSelected ? (bSoftSelectionActive ? 11.0f : 13.0f) : 10.0f,
                    SDPG_Foreground);
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
                    const FVector SelectedLocation = Native->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
                    const bool bSoftSelectionActive = SoftSelectionWeights.Num() == PointCount;
                    if (bSoftSelectionActive)
                    {
                        PDI->DrawPoint(SelectedLocation, StrandSelectedColor,
                            Index == SelectedPoint ? FMath::Clamp(GetDefault<UStrandEditorPreferences>()->SelectedAnchorMarkerSize, 16.0f, 64.0f) + 5.0f : 16.0f, SDPG_Foreground);
                    }
                    PDI->DrawPoint(SelectedLocation,
                        bSoftSelectionActive ? StrandSoftSelectionVisuals::GetAnchorColor(1.0) : StrandSelectedColor,
                        Index == SelectedPoint ? (bSoftSelectionActive ? FMath::Clamp(GetDefault<UStrandEditorPreferences>()->SelectedAnchorMarkerSize, 16.0f, 64.0f) : 17.0f)
                            : (bSoftSelectionActive ? 11.0f : 13.0f), SDPG_Foreground);
                    PDI->SetHitProxy(nullptr);
                }
            }
            else if (SelectedPoint >= 0 && SelectedPoint < PointCount && SelectedPoint % Step != 0)
            {
                PDI->SetHitProxy(new HStrandSplinePointProxy(SelectedPoint));
                const FVector SelectedLocation = Native->GetLocationAtSplinePoint(SelectedPoint, ESplineCoordinateSpace::Local);
                const bool bSoftSelectionActive = SoftSelectionWeights.Num() == PointCount;
                if (bSoftSelectionActive)
                {
                    PDI->DrawPoint(SelectedLocation, StrandSelectedColor, FMath::Clamp(GetDefault<UStrandEditorPreferences>()->SelectedAnchorMarkerSize, 16.0f, 64.0f) + 5.0f, SDPG_Foreground);
                }
                PDI->DrawPoint(SelectedLocation,
                    bSoftSelectionActive ? StrandSoftSelectionVisuals::GetAnchorColor(1.0) : StrandSelectedColor,
                    bSoftSelectionActive ? FMath::Clamp(GetDefault<UStrandEditorPreferences>()->SelectedAnchorMarkerSize, 16.0f, 64.0f) : 17.0f, SDPG_Foreground);
                PDI->SetHitProxy(nullptr);
            }
            if (PointCount > 1 && !Native->IsClosedLoop() && (PointCount - 1) % Step != 0 && SelectedPoint != PointCount - 1)
            {
                PDI->SetHitProxy(new HStrandSplinePointProxy(PointCount - 1));
                const bool bSoftSelectionActive = SoftSelectionWeights.Num() == PointCount;
                const bool bIsSelected = SelectedPoints.Contains(PointCount - 1);
                const FVector EndLocation = Native->GetLocationAtSplinePoint(PointCount - 1, ESplineCoordinateSpace::Local);
                if (bSoftSelectionActive && bIsSelected)
                {
                    PDI->DrawPoint(EndLocation, StrandSelectedColor, 16.0f, SDPG_Foreground);
                }
                PDI->DrawPoint(EndLocation,
                    StrandSoftSelectionVisuals::GetDisplayedAnchorColor(bSoftSelectionActive, bIsSelected,
                        bSoftSelectionActive ? SoftSelectionWeights.Last() : 0.0, StrandEndColor, StrandSelectedColor),
                    11.0f, SDPG_Foreground);
                PDI->SetHitProxy(nullptr);
            }
        }
        if (bShowTangents && !bPenMode && SelectedPoints.Num() == 1 &&
            SelectedPoint >= 0 && SelectedPoint < PointCount)
        {
            const FSplinePoint Point = Native->GetSplinePointAt(SelectedPoint, ESplineCoordinateSpace::Local);
            const FVector InHandle = Point.Position - Point.ArriveTangent / 3.0;
            const FVector OutHandle = Point.Position + Point.LeaveTangent / 3.0;
            PDI->DrawLine(Point.Position, InHandle, StrandIncomingHandleColor, SDPG_Foreground, 1.5f);
            PDI->DrawLine(Point.Position, OutHandle, StrandOutgoingHandleColor, SDPG_Foreground, 1.5f);
            if (HoveredPoint == SelectedPoint && HoveredHandle == 1)
            {
                PDI->DrawPoint(InHandle, StrandHoverColor, 16.0f, SDPG_Foreground);
            }
            PDI->SetHitProxy(new HStrandSplineTangentProxy(SelectedPoint, 1));
            PDI->DrawPoint(InHandle, StrandIncomingHandleColor,
                SelectedHandle == 1 ? 13.0f : 9.0f, SDPG_Foreground);
            PDI->SetHitProxy(nullptr);
            if (HoveredPoint == SelectedPoint && HoveredHandle == 2)
            {
                PDI->DrawPoint(OutHandle, StrandHoverColor, 16.0f, SDPG_Foreground);
            }
            PDI->SetHitProxy(new HStrandSplineTangentProxy(SelectedPoint, 2));
            PDI->DrawPoint(OutHandle, StrandOutgoingHandleColor,
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
        FVector Normal = bPenCustomPlane ? PenCustomNormal : LockedAxis == 0 ? FVector::XAxisVector
            : LockedAxis == 1 ? FVector::YAxisVector : LockedAxis == 2 ? FVector::ZAxisVector : GetViewRotation().Vector();
        if (bPenCustomPlane) { PlanePoint = PenCustomOrigin; }
        const double Denominator = FVector::DotProduct(RayDirection, Normal);
        if (FMath::Abs(Denominator) < 0.00001) { return false; }
        const double T = FVector::DotProduct(PlanePoint - RayOrigin, Normal) / Denominator;
        if (!FMath::IsFinite(T) || T < 0.0) { return false; }
        FVector Position = RayOrigin + RayDirection * T;
        if (!bPenCustomPlane && LockedAxis >= 0) { Position[LockedAxis] = PlanePoint[LockedAxis]; }
        if (PenSnap > 0.0)
        {
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                if (Axis != LockedAxis) { Position[Axis] = FMath::RoundHalfFromZero(Position[Axis] / PenSnap) * PenSnap; }
            }
        }
        // Grid quantization may leave a tilted plane; reproject to enforce the
        // construction-plane constraint exactly, even when snapping is enabled.
        if (bPenCustomPlane)
        {
            Position -= FVector::DotProduct(Position - PenCustomOrigin, PenCustomNormal) * PenCustomNormal;
        }
        if (Position.ContainsNaN()) { return false; }
        OutHit.Position = Position;
        return true;
    }

    void UpdateSegmentBendCursor(int32 X, int32 Y)
    {
        if (!bSegmentBending || !bHasCachedView || !OnSegmentBendUpdate) { return; }
        FVector Origin, Direction;
        FSceneView::DeprojectScreenToWorld(FVector2D(X, Y), CachedViewRect,
            CachedInvViewProjection, Origin, Direction);
        const double Denom = FVector::DotProduct(Direction, SegmentBendPlaneNormal);
        if (FMath::Abs(Denom) < 0.00001) { return; }
        const double T = FVector::DotProduct(SegmentBendOrigin - Origin, SegmentBendPlaneNormal) / Denom;
        if (!FMath::IsFinite(T) || T < 0.0) { return; }
        FVector StartOrigin, StartDirection;
        FSceneView::DeprojectScreenToWorld(FVector2D(SegmentBendCursorStart.X, SegmentBendCursorStart.Y), CachedViewRect,
            CachedInvViewProjection, StartOrigin, StartDirection);
        const double StartDenom = FVector::DotProduct(StartDirection, SegmentBendPlaneNormal);
        if (FMath::Abs(StartDenom) < 0.00001) { return; }
        const double StartT = FVector::DotProduct(SegmentBendOrigin - StartOrigin, SegmentBendPlaneNormal) / StartDenom;
        if (!FMath::IsFinite(StartT) || StartT < 0.0) { return; }
        const FVector Delta = (Origin + Direction * T) - (StartOrigin + StartDirection * StartT);
        if (!Delta.ContainsNaN()) { OnSegmentBendUpdate(Delta); }
        Invalidate();
    }

    void UpdateMarqueeCursor(int32 X, int32 Y)
    {
        MarqueeEnd = FIntPoint(X, Y);
        if (bLassoMode && StrandSplineSelectionUtils::ShouldAppendStrokePoint(LassoPath, MarqueeEnd))
        {
            // Keep long freehand strokes responsive without losing the closed
            // outline: decimate older samples when the 1024-point budget fills.
            if (LassoPath.Num() >= 1024)
            {
                TArray<FIntPoint> Reduced;
                Reduced.Reserve(513);
                for (int32 Index = 0; Index < LassoPath.Num(); Index += 2)
                {
                    Reduced.Add(LassoPath[Index]);
                }
                LassoPath = MoveTemp(Reduced);
            }
            LassoPath.Add(MarqueeEnd);
        }
        Invalidate();
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
    TArray<FVector> CleanupPreviewPolyline;
    TArray<TPair<FVector, FVector>> SnapPreviewSegments;
    TArray<double> SoftSelectionWeights;
    TFunction<void(int32, bool, bool)> OnPointSelected;
    TFunction<void(int32, double)> OnSegmentInsert;
    TFunction<void(int32, double)> OnSegmentBendBegin;
    TFunction<void(const FVector&)> OnSegmentBendUpdate;
    TFunction<void()> OnSegmentBendEnd;
    bool bSegmentBending = false;
    FIntPoint SegmentBendCursorStart = FIntPoint::ZeroValue;
    FVector SegmentBendOrigin = FVector::ZeroVector;
    FVector SegmentBendPlaneNormal = FVector::ZAxisVector;
    TFunction<TSharedRef<SWidget>(int32, int32, double)> OnContextMenu;
    TFunction<void(const FVector&)> OnNudge;
    bool bRightClickCandidate = false;
    bool bRightClickMoved = false;
    FIntPoint RightClickStart = FIntPoint::ZeroValue;
    int32 RightClickPoint = INDEX_NONE;
    int32 RightClickSegment = INDEX_NONE;
    double RightClickAlpha = 0.5;
    int32 HoveredPoint = INDEX_NONE;
    int32 HoveredHandle = 0;
    int32 HoveredSegment = INDEX_NONE;
    double HoveredSegmentAlpha = 0.5;
    TFunction<void()> OnSelectAll;
    TFunction<void()> OnClearSelection;
    TFunction<void(const TArray<int32>&, bool, bool)> OnMarqueeSelected;
    TFunction<void()> OnMarqueeExit;
    TFunction<void(const FQuat&)> OnPointRotation;
    TFunction<void(double)> OnTangentRoll;
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
    bool bOrientationVisuals[4] = { false, false, false, false };
    double OrientationSpacing = 100.0;
    double OrientationSize = 35.0;
    bool bPenMode = false;
    bool bMarqueeMode = false;
    bool bLassoMode = false;
    TArray<FIntPoint> LassoPath;
    bool bMarqueeDragging = false;
    bool bMarqueeAdd = false;
    bool bMarqueeToggle = false;
    bool bDelegatedMarqueeCameraClick = false;
    int32 WidgetMode = 0;
    bool bWidgetLocal = false;
    bool bDraggingScreenPlane = false;
    FIntPoint ScreenPlaneLastMouse = FIntPoint::ZeroValue;
    FVector ScreenPlaneRight = FVector::YAxisVector;
    FVector ScreenPlaneUp = FVector::ZAxisVector;
    double ScreenPlaneLockedDepth = 1.0;
    double ScreenPlaneLockedFov = 90.0;
    double ScreenPlaneLockedWidth = 1280.0;
    bool bCameraAlignedEditing = false;
    bool bWidgetFrameFrozen = false;
    FMatrix FrozenWidgetFrame = FMatrix::Identity;
    FRotator AuthoredAnchorRotation = FRotator::ZeroRotator;
    FIntPoint MarqueeStart = FIntPoint::ZeroValue;
    FIntPoint MarqueeEnd = FIntPoint::ZeroValue;
    bool bPenDragging = false;
    bool bDelegatedPenCameraClick = false;
    bool bHasCachedView = false;
    bool bHasPenHover = false;
    bool bFrameSplineWhenReady = false;
    int32 LockedAxis = -1;
    bool bPenCustomPlane = false;
    FVector PenCustomOrigin = FVector::ZeroVector;
    FVector PenCustomNormal = FVector::ZAxisVector;
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
    // A nullptr mode manager leaves a standalone asset viewport without the
    // native widget ownership/visibility path. Keep a private manager for this
    // editor only, never GLevelEditorModeTools or any Level Editor selection.
    ModeTools = MakeShared<FEditorModeTools>();
    ModeTools->SetSupportsViewportITF(false); // Use Unreal's classic FWidget, not an unrelated ITF actor gizmo.
    ModeTools->SetSelectionHasSceneComponent(false);
    ModeTools->SetShowWidget(false);
    StrandViewportClient = MakeShared<FStrandSplineViewportClient>(
        ModeTools.Get(), PreviewScene.Get(), StaticCastSharedRef<SEditorViewport>(SharedThis(this)));
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
        // Changing perspective/orthographic must not silently frame the entire asset.
        // Framing is available as a separate, explicit action.
        StrandViewportClient->Invalidate();
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
void SStrandSplineViewport::SetOrientationVisual(int32 Mode, bool bEnabled)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOrientationVisual(Mode, bEnabled); }
}
bool SStrandSplineViewport::GetOrientationVisual(int32 Mode) const
{
    return StrandViewportClient.IsValid() && StrandViewportClient->GetOrientationVisual(Mode);
}
void SStrandSplineViewport::SetOrientationSpacing(double Value)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOrientationSpacing(Value); }
}
double SStrandSplineViewport::GetOrientationSpacing() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetOrientationSpacing() : 100.0;
}
void SStrandSplineViewport::SetOrientationSize(double Value)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOrientationSize(Value); }
}
double SStrandSplineViewport::GetOrientationSize() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetOrientationSize() : 35.0;
}

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
void SStrandSplineViewport::SetLassoMode(bool bEnabled)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetLassoMode(bEnabled); }
}
void SStrandSplineViewport::SetSoftSelectionWeights(const TArray<double>& Weights)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetSoftSelectionWeights(Weights); }
}
void SStrandSplineViewport::SetCleanupPreviewPolyline(const TArray<FVector>& Points)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetCleanupPreviewPolyline(Points); }
}
void SStrandSplineViewport::SetSnapPreviewSegments(const TArray<TPair<FVector, FVector>>& Segments)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetSnapPreviewSegments(Segments); }
}

void SStrandSplineViewport::SetAuthoredAnchorRotation(const FRotator& Rotation)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetAuthoredAnchorRotation(Rotation); }
}
FRotator SStrandSplineViewport::GetAuthoredAnchorRotation() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetAuthoredAnchorRotation() : FRotator::ZeroRotator;
}
FVector SStrandSplineViewport::GetCurrentWidgetAxisX() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetCurrentWidgetAxisX() : FVector::XAxisVector;
}
void SStrandSplineViewport::SetCameraAlignedEditing(bool bEnabled)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetCameraAlignedEditing(bEnabled); }
}

void SStrandSplineViewport::SetTangentRollCallback(TFunction<void(double)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetTangentRollCallback(MoveTemp(Callback)); }
}

void SStrandSplineViewport::SetWidgetLocal(bool bLocal)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetStrandWidgetLocal(bLocal); }
}
void SStrandSplineViewport::SetWidgetMode(int32 Mode)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetStrandWidgetMode(Mode); }
}
bool SStrandSplineViewport::IsNativeWidgetReady()
{
    return StrandViewportClient.IsValid() && StrandViewportClient->IsNativeWidgetReady();
}
void SStrandSplineViewport::SetSegmentBendCallbacks(TFunction<void(int32, double)> Begin,
    TFunction<void(const FVector&)> Update, TFunction<void()> End)
{
    if (StrandViewportClient.IsValid())
    {
        StrandViewportClient->SetSegmentBendCallbacks(MoveTemp(Begin), MoveTemp(Update), MoveTemp(End));
    }
}

void SStrandSplineViewport::SetOnSegmentInsert(TFunction<void(int32, double)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnSegmentInsert(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetOnContextMenu(TFunction<TSharedRef<SWidget>(int32, int32, double)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnContextMenu(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetOnNudge(TFunction<void(const FVector&)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnNudge(MoveTemp(Callback)); }
}
FText SStrandSplineViewport::GetInteractionHint() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetInteractionHint() : FText::GetEmpty();
}

void SStrandSplineViewport::SetOnMarqueeSelected(TFunction<void(const TArray<int32>&, bool, bool)> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnMarqueeSelected(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetOnMarqueeExit(TFunction<void()> Callback)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetOnMarqueeExit(MoveTemp(Callback)); }
}
void SStrandSplineViewport::SetPointRotationCallback(TFunction<void(const FQuat&)> Callback)
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
void SStrandSplineViewport::SetPenCustomPlane(bool bEnabled, const FVector& Origin, const FVector& Normal)
{
    if (StrandViewportClient.IsValid()) { StrandViewportClient->SetPenCustomPlane(bEnabled, Origin, Normal); }
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

FVector SStrandSplineViewport::GetCameraLocation() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetCameraLocation() : FVector::ZeroVector;
}
double SStrandSplineViewport::GetCameraHorizontalFOV() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetCameraHorizontalFOV() : 90.0;
}
int32 SStrandSplineViewport::GetCameraViewportWidth() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetCameraViewportWidth() : 1280;
}
FVector SStrandSplineViewport::GetCameraForward() const
{
    return StrandViewportClient.IsValid() ? StrandViewportClient->GetCameraForward() : FVector::XAxisVector;
}
