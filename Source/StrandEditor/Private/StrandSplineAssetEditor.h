#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/StrongObjectPtr.h"
#include "EditorUndoClient.h"
#include "ScopedTransaction.h"
#include "Components/SplineComponent.h"

class IDetailsView;
class USplineDataAsset;
class USplineComponent;
class SDockTab;
class SStrandSplineViewport;
struct FPropertyChangedEvent;

/** Dedicated Strand Spline asset editor. The preview evaluates a native USplineComponent. */
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
    TUniquePtr<FScopedTransaction> ActivePointDragTransaction;
    bool bPointChangedDuringDrag = false;

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
    FReply ToggleClosedLoop();
    FReply SetSelectedPointType(ESplinePointType::Type NewType);
    TSharedRef<SWidget> MakePointTypeMenu();
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
    void SelectPoint(int32 Index);
    void BeginViewportPointDrag();
    void ApplyViewportPointDrag(const FVector& LocalDelta);
    void EndViewportPointDrag();
    FReply SelectAdjacentPoint(int32 Direction);
    void RefreshAfterUndo();
};
