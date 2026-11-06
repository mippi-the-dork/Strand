#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/StrongObjectPtr.h"
#include "EditorUndoClient.h"
#include "ScopedTransaction.h"
#include "Components/SplineComponent.h"
#include "SplineDataAsset.h"

class IDetailsView;
class UToolMenu;
struct FToolMenuContext;
class USplineDataAsset;
class USplineComponent;
class SDockTab;
class SWindow;
class SStrandSplineViewport;
struct FStrandPenHit;
struct FPropertyChangedEvent;

/** Dedicated Strand Spline asset editor. The preview evaluates a native USplineComponent. */
enum class EStrandEditPlane : uint8
{
    Free3D,
    XY,
    XZ,
    YZ,
    CameraPlane
};

class FStrandSplineAssetEditor final : public FAssetEditorToolkit, public FSelfRegisteringEditorUndoClient
{
public:
    virtual ~FStrandSplineAssetEditor() override;
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
    virtual UToolMenu* GenerateCommonActionsToolbar(FToolMenuContext& MenuContext) override;
    virtual void PostRegenerateMenusAndToolbars() override;

private:
    TWeakObjectPtr<USplineDataAsset> Asset;
    TStrongObjectPtr<USplineComponent> NativePreview;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SStrandSplineViewport> PreviewWidget;
    TWeakPtr<SWindow> ActiveCleanupWindow; // One modeless live curve cleanup preview.
    int32 SelectedPointIndex = INDEX_NONE;
    TSet<int32> SelectedPointIndices;
    TUniquePtr<FScopedTransaction> ActivePointDragTransaction;
    bool bPointChangedDuringDrag = false;
    TArray<double> FrozenSoftSelectionWeights; // Capture only once per transform interaction.
    void RefreshSoftSelectionVisualization();
    TUniquePtr<FScopedTransaction> ActiveSegmentBendTransaction;
    TArray<FSplinePointData> BendBaselinePoints;
    FSplinePoint BendEvaluatedStart;
    FSplinePoint BendEvaluatedEnd;
    int32 BendSegmentIndex = INDEX_NONE;
    double BendSegmentAlpha = 0.5;
    bool bSegmentBendChanged = false;
    EStrandEditPlane EditPlane = EStrandEditPlane::Free3D;
    FVector LockedCameraPlaneNormal = FVector::XAxisVector;
    int32 SoftSelectionMode = 0; // 0 disabled, 1 Euclidean 3D, 2 native spline arc length
    int32 SoftSelectionFalloff = 1; // 0 linear, 1 smooth, 2 sharp
    double SoftSelectionRadius = 150.0;
    void FlattenSelectionToCameraPlane();
    void RotateSelectionAroundCamera(double Degrees);
    int32 SelectedHandle = 0; // 0 anchor, 1 incoming, 2 outgoing.
    bool bPenMode = false;
    bool bMarqueeMode = false;
    bool bLassoMode = false;
    int32 ActiveWidgetMode = 0; // Move, Rotate, Scale: native Unreal viewport widgets.
    bool bWidgetLocal = false; // Preview/asset axes vs primary point's local orientation.
    bool bRelativePositionEntry = false; // Absolute target vs additive numeric offset.
    int32 SubdivideCuts = 2; // Last-used exact number of cuts per segment.
    bool bDistanceUniformSubdivision = false;
    int32 ResampleTargetCount = 16; // Per-user remembered target count. Shared spline asset stays clean.
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
    TSharedRef<SDockTab> SpawnSourceTab(const FSpawnTabArgs& Args);
    TSharedRef<SWidget> BuildAssetActionToolbar();
    TSharedRef<SWidget> MakePrimitiveMenu();
    TSharedRef<SWidget> MakeReplacePrimitiveMenu();
    void ReplaceSplineWithPrimitive(int32 Kind);
    void CreatePrimitiveAsset(int32 Kind);
    void SplitAtSelectedAnchor();
    void RebuildNativePreview();
    void OnPropertyChanged(const FPropertyChangedEvent& Event);
    bool CanReimport() const;
    FReply ReimportSource();
    FReply NewSplineAsset();
    FReply ExportAlembic();
    FReply ExportSvg();
    FReply AddPointAfterSelection();
    void InsertPointOnViewportSegment(int32 Segment, double Alpha);
    void BeginViewportSegmentBend(int32 Segment, double Alpha);
    void UpdateViewportSegmentBend(const FVector& Delta);
    void EndViewportSegmentBend();
    TSharedRef<SWidget> MakeViewportContextMenu(int32 Point, int32 Segment, double Alpha);
    void NudgeSelectedPoints(const FVector& Delta);
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
    FReply GrowAnchorSelection();
    FReply ShrinkAnchorSelection();
    FReply SelectSharpCorners(double ThresholdDegrees);
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
    void SetLassoMode(bool bEnabled);
    void SetWidgetMode(int32 Mode);
    void SetWidgetLocal(bool bLocal);
    void SelectMarqueePoints(const TArray<int32>& Indices, bool bAdd, bool bToggle);
    void ApplyViewportRotation(const FQuat& RotationDelta);
    void ApplyViewportScale(double Factor);
    void SetPenSnapStep(double Step);
    TSharedRef<SWidget> MakePenSnapMenu();
    TSharedRef<SWidget> MakeViewportOptionsMenu();
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
    FReply ResampleSpline(int32 TargetPoints);
    FReply SubdivideWithCuts(int32 Cuts, bool bDistanceUniform);
    FReply SetSelectedContinuity(bool bC1);
    FReply NormalizeTangents(double LengthCm);
    FReply RecalculateTangents();
    FReply WeldEndpoints(double DistanceCm);
    FReply SelectDuplicateAnchors(double ToleranceCm);
    FReply SelectBySegmentLength(double ThresholdCm, bool bShorter);
    void OpenCurveCleanupDialog();
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
    TOptional<double> GetAnchorRotationCoordinate(int32 Axis) const;
    void SetAnchorRotationCoordinate(double Value, int32 Axis);
    FReply ResetSelectedOrientations();
    FReply SetSelectedRoll(double Degrees, bool bAdditive);
    FReply DistributeSelectedRoll();
    FReply AlignSelectedOrientationsToCurve();
    TOptional<double> GetTangentCoordinate(int32 Axis) const;
    void SetTangentCoordinate(double Value, int32 Axis);
    void SelectPoint(int32 Index, bool bRange = false, bool bToggle = false);
    void BeginViewportPointDrag();
    void ApplyViewportPointDrag(const FVector& LocalDelta);
    void EndViewportPointDrag();
    FReply SelectAdjacentPoint(int32 Direction);
    void RefreshAfterUndo();
};
