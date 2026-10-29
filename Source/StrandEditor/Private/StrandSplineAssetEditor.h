#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/StrongObjectPtr.h"
#include "EditorUndoClient.h"
#include "ScopedTransaction.h"
#include "Components/SplineComponent.h"
#include "SplineDataAsset.h"

class IDetailsView;
class USplineDataAsset;
class USplineComponent;
class SDockTab;
class SStrandSplineViewport;
struct FStrandPenHit;
struct FPropertyChangedEvent;

/** Dedicated Strand Spline asset editor. The preview evaluates a native USplineComponent. */
enum class EStrandEditPlane : uint8
{
    Free3D,
    XY,
    XZ,
    YZ
};

class FStrandSplineAssetEditor final : public FAssetEditorToolkit, public FSelfRegisteringEditorUndoClient
{
public:
    void Init(USplineDataAsset* InAsset, const TSharedPtr<IToolkitHost>& InHost);
    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;

    virtual FName GetToolkitFName() const override { return TEXT("StrandSplineEditor"); }
    virtual FText GetBaseToolkitName() const override;
    virtual FText GetToolkitName() const override;
    virtual FText GetToolkitToolTipText() const override;
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Strand "); }
    virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.0f, 0.706f, 0.655f); }
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

private:
    TWeakObjectPtr<USplineDataAsset> Asset;
    TStrongObjectPtr<USplineComponent> NativePreview;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SStrandSplineViewport> PreviewWidget;
    int32 SelectedPointIndex = INDEX_NONE;
    TSet<int32> SelectedPointIndices;
    TUniquePtr<FScopedTransaction> ActivePointDragTransaction;
    bool bPointChangedDuringDrag = false;
    EStrandEditPlane EditPlane = EStrandEditPlane::Free3D;
    int32 SelectedHandle = 0; // 0 anchor, 1 incoming, 2 outgoing.
    bool bPenMode = false;
    bool bMarqueeMode = false;
    int32 ActiveWidgetMode = 0; // Move, Rotate, Scale: native Unreal viewport widgets.
    double PenSnapStep = 0.0;
    TUniquePtr<FScopedTransaction> ActivePenTransaction;
    TArray<FSplinePointData> PenPreviousPoints;
    bool bPenPreviousClosed = false;
    int32 PenEditingPoint = INDEX_NONE;
    FVector LastPenHandleOffset = FVector::ZeroVector;
    bool bPenHandleMoved = false;
    FSplinePointData PenPointBeforeHandleDrag;

    TSharedRef<SDockTab> SpawnPreviewTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnDetailsTab(const FSpawnTabArgs& Args);
    void RebuildNativePreview();
    void OnPropertyChanged(const FPropertyChangedEvent& Event);
    bool CanReimport() const;
    FReply ReimportSource();
    FReply NewSplineAsset();
    FReply ExportAlembic();
    FReply ExportSvg();
    FReply AddPointAfterSelection();
    FReply RemoveSelectedPoint();
    FReply AlignSelection(int32 Axis);
    FReply RotateSelection(int32 Axis, double Degrees);
    FReply ScaleSelection(double Factor);
    TSharedRef<SWidget> MakeTransformMenu();
    FReply DistributeSelection(int32 Axis);
    FReply MatchSelectedTangents(bool bMirror);
    FReply SelectAllPoints();
    FReply InvertPointSelection();
    FReply ClearPointSelection();
    TSharedRef<SWidget> MakeSelectionMenu();
    bool HasMultipleSelectedPoints() const;
    bool CanEditAxis(int32 Axis) const;
    bool CanDeleteSelection() const;
    TArray<int32> GetSelectedIndices() const;
    void SyncViewportSelection();
    void PruneSelection();
    FReply ToggleClosedLoop();
    FReply ClearSpline();
    void SetPenMode(bool bEnabled);
    void SetMarqueeMode(bool bEnabled);
    void SetWidgetMode(int32 Mode);
    void SelectMarqueePoints(const TArray<int32>& Indices, bool bAdd, bool bToggle);
    void ApplyViewportRotation(int32 Axis, double Degrees);
    void ApplyViewportScale(double Factor);
    void SetPenSnapStep(double Step);
    TSharedRef<SWidget> MakePenSnapMenu();
    FText GetPenSnapLabel() const;
    FText GetEditorHint() const;
    bool BeginPenStroke(const FStrandPenHit& Hit);
    void UpdatePenStroke(const FVector& HandleOffset);
    void EndPenStroke();
    void CancelPenStroke();
    FReply ExtendStart();
    FReply ReverseSpline();
    FReply SetSelectedAsFirst();
    FReply SnapSelectedPoint(double Step);
    FReply SubdivideSpline();
    FReply LinearizeSpline();
    FReply SmoothSpline();
    FReply MirrorSpline(int32 Axis);
    FReply FlattenSpline(int32 Axis);
    TSharedRef<SWidget> MakeSplineToolsMenu();
    TSharedRef<SWidget> MakeEditPlaneMenu();
    FText GetEditPlaneLabel() const;
    void SetEditPlane(EStrandEditPlane NewPlane);
    void CommitSplineEdit(int32 NewSelection, bool bPreserveSelection = false);
    void SelectTangentHandle(int32 Point, int32 Handle);
    void ApplyViewportTangentDrag(int32 Handle, const FVector& LocalDelta);
    FReply SetSelectedPointType(ESplinePointType::Type NewType);
    TSharedRef<SWidget> MakePointTypeMenu();
    FReply SetSelectedHandleMode(EStrandBezierHandleMode Mode);
    TSharedRef<SWidget> MakeHandleModeMenu();
    FText GetHandleModeLabel() const;
    FText GetHandleModeDescription() const;
    FReply CreateActor();
    FReply SelectUsers();
    FReply InspectUsage();
    FText GetSummary() const;
    FText GetSourceDescription() const;
    FText GetProjectionLabel() const;
    TSharedRef<SWidget> MakeProjectionMenu();
    bool HasSelectedPoint() const;
    FText GetSelectedPointLabel() const;
    TOptional<double> GetPointCoordinate(int32 Axis) const;
    void SetPointCoordinate(double Value, int32 Axis);
    TOptional<double> GetTangentCoordinate(int32 Axis) const;
    void SetTangentCoordinate(double Value, int32 Axis);
    void SelectPoint(int32 Index, bool bRange = false, bool bToggle = false);
    void BeginViewportPointDrag();
    void ApplyViewportPointDrag(const FVector& LocalDelta);
    void EndViewportPointDrag();
    FReply SelectAdjacentPoint(int32 Direction);
    void RefreshAfterUndo();
};
