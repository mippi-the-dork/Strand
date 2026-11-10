#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/WeakObjectPtr.h"

class USplineComponent;
class FPreviewScene;
class FEditorModeTools;
class FStrandSplineViewportClient;

/** Screen-space pen target, evaluated in native spline-local centimeters. */
struct FStrandPenHit
{
    FVector Position = FVector::ZeroVector;
    int32 PointIndex = INDEX_NONE;
    int32 SegmentIndex = INDEX_NONE;
    double SegmentAlpha = 0.5;
};

/** 3D, camera-navigable Strand preview. Selection is editor-only; source data stays in USplineDataAsset. */
class SStrandSplineViewport final : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SStrandSplineViewport) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    void SetSpline(USplineComponent* InSpline);
    void SetProjection(ELevelViewportType Projection);
    ELevelViewportType GetProjection() const;
    void FrameSpline();
    void FrameSelectedPoint();
    /** Non-destructive cleanup preview sampled in spline-local space. */
    void SetCleanupPreviewPolyline(const TArray<FVector>& Points);
    /** World-independent asset-local source/target indicators for non-destructive snap preview. */
    void SetSnapPreviewSegments(const TArray<TPair<FVector, FVector>>& Segments);
    void SetSoftSelectionWeights(const TArray<double>& Weights);
    void SetShowControlPoints(bool bShow);
    bool GetShowControlPoints() const;
    void SetShowGrid(bool bShow);
    bool GetShowGrid() const;
    void SetShowTangents(bool bShow);
    bool GetShowTangents() const;
    void SetOrientationVisual(int32 Mode, bool bEnabled);
    bool GetOrientationVisual(int32 Mode) const;
    void SetOrientationSpacing(double Centimeters);
    double GetOrientationSpacing() const;
    void SetOrientationSize(double Centimeters);
    double GetOrientationSize() const;
    FVector GetCameraForward() const;
    FVector GetCameraLocation() const;
    double GetCameraHorizontalFOV() const;
    int32 GetCameraViewportWidth() const;
    void SetSelectedPointIndex(int32 Index);
    void SetSelectedPoints(const TArray<int32>& Indices, int32 PrimaryIndex);
    void SetOnPointSelected(TFunction<void(int32, bool, bool)> InCallback);
    void SetOnSegmentInsert(TFunction<void(int32, double)> InCallback);
    /** Ctrl+drag on a segment bends its interior; callbacks own the undo transaction. */
    void SetSegmentBendCallbacks(TFunction<void(int32, double)> Begin,
        TFunction<void(const FVector&)> Update, TFunction<void()> End);
    void SetOnContextMenu(TFunction<TSharedRef<SWidget>(int32, int32, double)> InCallback);
    void SetOnNudge(TFunction<void(const FVector&)> InCallback);
    FText GetInteractionHint() const;
    void SetOnSelectAll(TFunction<void()> InCallback);
    void SetOnClearSelection(TFunction<void()> InCallback);
    void SetOnTangentSelected(TFunction<void(int32, int32)> InCallback);
    void SetTangentDragCallback(TFunction<void(int32, const FVector&)> InCallback);
    void SetSelectedHandle(int32 Handle);
    void SetPenMode(bool bEnabled);
    void SetMarqueeMode(bool bEnabled);
    void SetLassoMode(bool bEnabled);
    void SetWidgetMode(int32 Mode);
    void SetWidgetLocal(bool bLocal);
    void SetCameraAlignedEditing(bool bEnabled);
    void SetTangentRollCallback(TFunction<void(double)> InCallback);
    void SetAuthoredAnchorRotation(const FRotator& Rotation);
    /** The actual saved rotation driving Local gizmo axes; also used by regression tests. */
    FRotator GetAuthoredAnchorRotation() const;
    /** Exposes actual native coordinate-frame X axis for editor automation. */
    FVector GetCurrentWidgetAxisX() const;
    /** Diagnostic: true when the selected anchor has an enabled native widget path. */
    bool IsNativeWidgetReady();
    void SetOnMarqueeSelected(TFunction<void(const TArray<int32>&, bool, bool)> InCallback);
    void SetOnMarqueeExit(TFunction<void()> InCallback);
    void SetPointRotationCallback(TFunction<void(const FQuat&)> InCallback);
    void SetPointScaleCallback(TFunction<void(double)> InCallback);
    void SetOnWidgetModeChanged(TFunction<void(int32)> InCallback);
    void SetOnToolShortcut(TFunction<void(int32)> InCallback);
    void SetOnDeleteShortcut(TFunction<void()> InCallback);
    void SetPenPlane(int32 LockedAxis);
    void SetPenCustomPlane(bool bEnabled, const FVector& Origin, const FVector& Normal);
    void SetPenSnap(double Step);
    void SetPenCallbacks(TFunction<bool(const FStrandPenHit&)> OnDown,
        TFunction<void(const FVector&)> OnDrag, TFunction<void()> OnUp,
        TFunction<void()> OnCancel, TFunction<void()> OnExit);
    /** Called for native translation widget deltas and when tracking finishes. */
    void SetPointDragCallbacks(TFunction<void(const FVector&)> InDelta, TFunction<void()> InEnd);

protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void OnFocusViewportToSelection() override;

private:
    TSharedPtr<FPreviewScene> PreviewScene;
    // Must outlive StrandViewportClient, which holds a non-owning mode-manager pointer.
    TSharedPtr<FEditorModeTools> ModeTools;
    TSharedPtr<FStrandSplineViewportClient> StrandViewportClient;
    TWeakObjectPtr<USplineComponent> Spline;
    bool bHasFramedInitialSpline = false;
};
