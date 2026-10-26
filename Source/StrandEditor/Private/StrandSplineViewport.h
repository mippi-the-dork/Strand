#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/WeakObjectPtr.h"

class USplineComponent;
class FPreviewScene;
class FStrandSplineViewportClient;

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
    void SetOnPointSelected(TFunction<void(int32)> InCallback);
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
