#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/WeakObjectPtr.h"

class USplineComponent;
class FPreviewScene;
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
    void SetShowControlPoints(bool bShow);
    bool GetShowControlPoints() const;
    void SetShowGrid(bool bShow);
    bool GetShowGrid() const;
    void SetShowTangents(bool bShow);
    bool GetShowTangents() const;
    void SetSelectedPointIndex(int32 Index);
    void SetSelectedPoints(const TArray<int32>& Indices, int32 PrimaryIndex);
    void SetOnPointSelected(TFunction<void(int32, bool, bool)> InCallback);
    void SetOnSelectAll(TFunction<void()> InCallback);
    void SetOnClearSelection(TFunction<void()> InCallback);
    void SetOnTangentSelected(TFunction<void(int32, int32)> InCallback);
    void SetTangentDragCallback(TFunction<void(int32, const FVector&)> InCallback);
    void SetSelectedHandle(int32 Handle);
    void SetPenMode(bool bEnabled);
    void SetMarqueeMode(bool bEnabled);
    void SetWidgetMode(int32 Mode);
    void SetOnMarqueeSelected(TFunction<void(const TArray<int32>&, bool, bool)> InCallback);
    void SetOnMarqueeExit(TFunction<void()> InCallback);
    void SetPointRotationCallback(TFunction<void(int32, double)> InCallback);
    void SetPointScaleCallback(TFunction<void(double)> InCallback);
    void SetOnWidgetModeChanged(TFunction<void(int32)> InCallback);
    void SetOnToolShortcut(TFunction<void(int32)> InCallback);
    void SetOnDeleteShortcut(TFunction<void()> InCallback);
    void SetPenPlane(int32 LockedAxis);
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
    TSharedPtr<FStrandSplineViewportClient> StrandViewportClient;
    TWeakObjectPtr<USplineComponent> Spline;
    bool bHasFramedInitialSpline = false;
};
