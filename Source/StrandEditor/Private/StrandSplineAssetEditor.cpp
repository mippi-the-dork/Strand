#include "StrandSplineAssetEditor.h"
#include "StrandSplineViewport.h"
#include "SplineDataAsset.h"
#include "SplineDataAssetFactory.h"
#include "SplineEditorCommands.h"
#include "StrandSplineExport.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Components/SplineComponent.h"
#include "EditorFramework/AssetImportData.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "PropertyEditorDelegates.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UIAction.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "StrandSplineAssetEditor"

namespace
{
    const FName PreviewTabId(TEXT("StrandSplineEditor_Preview"));
    const FName DetailsTabId(TEXT("StrandSplineEditor_Details"));
}

FText FStrandSplineAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("ToolkitName", "Strand Spline Editor");
}

FText FStrandSplineAssetEditor::GetToolkitName() const
{
    if (const USplineDataAsset* Current = Asset.Get())
    {
        return FText::Format(LOCTEXT("ToolkitAssetName", "Strand: {0}"), FText::FromString(Current->GetName()));
    }
    return GetBaseToolkitName();
}

FText FStrandSplineAssetEditor::GetToolkitToolTipText() const
{
    return LOCTEXT("ToolkitTip", "Edit the Strand Spline asset and preview its native spline geometry.");
}

void FStrandSplineAssetEditor::Init(USplineDataAsset* InAsset, const TSharedPtr<IToolkitHost>& InHost)
{
    check(InAsset);
    Asset = InAsset;
    NativePreview.Reset(NewObject<USplineComponent>(GetTransientPackage(), NAME_None, RF_Transient));
    RebuildNativePreview();

    FDetailsViewArgs DetailArgs;
    DetailArgs.bHideSelectionTip = true;
    DetailArgs.bAllowSearch = true;
    DetailArgs.bLockable = false;
    DetailArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    DetailsView = PropertyEditor.CreateDetailView(DetailArgs);
    DetailsView->SetRightColumnMinWidth(125.0f);
    // Avoid showing SVG-only import settings on Alembic assets, and vice versa.
    // They are kept on the asset for reimport serialization; this filters the editor UI only.
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda(
        [this](const FPropertyAndParent& PropertyAndParent)
        {
#if WITH_EDITORONLY_DATA
            const USplineDataAsset* Current = Asset.Get();
            if (Current)
            {
                const FName Name = PropertyAndParent.Property.GetFName();
                if (Current->SourceImportFormat.IsEmpty() &&
                    (Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, bMirrorSvgXOnImport) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, SourceUnitsToCentimeters) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, bAlembicYUp) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, ImportSampleSeconds) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, AssetImportData) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, SourceCurveId) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, SourceImportFormat)))
                {
                    return false;
                }
                if (Current->SourceImportFormat == TEXT("abc") && Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, bMirrorSvgXOnImport))
                {
                    return false;
                }
                if (Current->SourceImportFormat == TEXT("svg") &&
                    (Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, SourceUnitsToCentimeters) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, bAlembicYUp) ||
                     Name == GET_MEMBER_NAME_CHECKED(USplineDataAsset, ImportSampleSeconds)))
                {
                    return false;
                }
            }
#endif
            return true;
        }));
    DetailsView->SetObject(InAsset);
    DetailsView->OnFinishedChangingProperties().AddSP(this, &FStrandSplineAssetEditor::OnPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("StrandSplineEditor_Layout_v4")
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
            ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.60f)
                    ->AddTab(PreviewTabId, ETabState::OpenedTab)->SetHideTabWell(true))
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.40f)
                    ->AddTab(DetailsTabId, ETabState::OpenedTab)->SetHideTabWell(true))));
    TArray<UObject*> ObjectsToEdit;
    ObjectsToEdit.Add(InAsset);
    InitAssetEditor(InHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
        InHost, TEXT("StrandSplineEditor"), Layout, true, true, ObjectsToEdit);
}

void FStrandSplineAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
    InTabManager->RegisterTabSpawner(PreviewTabId, FOnSpawnTab::CreateSP(this, &FStrandSplineAssetEditor::SpawnPreviewTab))
        .SetDisplayName(LOCTEXT("PreviewTab", "Spline Preview"));
    InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FStrandSplineAssetEditor::SpawnDetailsTab))
        .SetDisplayName(LOCTEXT("DetailsTab", "Spline Details"));
}

void FStrandSplineAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    InTabManager->UnregisterTabSpawner(PreviewTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FStrandSplineAssetEditor::SpawnPreviewTab(const FSpawnTabArgs& Args)
{
    TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(LOCTEXT("PreviewTab", "Viewport"))
    [
        SNew(SVerticalBox)
        // Asset creation and export are first-class Strand operations; Save remains in the native toolkit toolbar.
        + SVerticalBox::Slot().AutoHeight().Padding(7.0f, 5.0f, 7.0f, 3.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("NewSpline", "New Spline"))
                .ToolTipText(LOCTEXT("NewSplineTip", "Create and open an independent, editable Strand Spline asset in the same Content Browser folder."))
                .OnClicked(this, &FStrandSplineAssetEditor::NewSplineAsset)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("ExportTip", "Export this native spline geometry without modifying the Strand asset."))
                .OnGetMenuContent_Lambda([this]()
                {
                    FMenuBuilder Menu(true, nullptr);
                    Menu.AddMenuEntry(LOCTEXT("ExportABC", "Alembic (.abc)  |  3D"),
                        LOCTEXT("ExportABCTip", "Export evaluated native curve geometry in centimeters, suitable for interchange as Alembic ICurves."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ExportAlembic(); })));
                    Menu.AddMenuEntry(LOCTEXT("ExportSVG", "SVG (.svg)  |  2D"),
                        LOCTEXT("ExportSVGTip", "Export 2D cubic SVG paths. 3D splines require selecting a projection plane."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ExportSvg(); })));
                    return Menu.MakeWidget();
                })
                .ButtonContent()[SNew(STextBlock).Text(LOCTEXT("Export", "Export"))]]
            + SHorizontalBox::Slot().AutoWidth().Padding(10.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SButton).Text(LOCTEXT("Reimport", "Reimport Source"))
                .ToolTipText(LOCTEXT("ReimportTip", "Regenerate this spline from Alembic or SVG using its current import settings. This replaces the asset's stored points but does not automatically change placed actors."))
                .IsEnabled(this, &FStrandSplineAssetEditor::CanReimport)
                .OnClicked(this, &FStrandSplineAssetEditor::ReimportSource)]
            + SHorizontalBox::Slot().AutoWidth().Padding(14.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SButton).Text(LOCTEXT("CreateActor", "Create Actor"))
                .ToolTipText(LOCTEXT("CreateActorTip", "Place a native Strand spline actor from this asset."))
                .OnClicked(this, &FStrandSplineAssetEditor::CreateActor)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("SelectUsers", "Select Users"))
                .ToolTipText(LOCTEXT("SelectUsersTip", "Select actors linked to this asset in the current level."))
                .OnClicked(this, &FStrandSplineAssetEditor::SelectUsers)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("InspectUsage", "Inspect Usage"))
                .ToolTipText(LOCTEXT("InspectUsageTip", "Inspect references to this asset in the current level."))
                .OnClicked(this, &FStrandSplineAssetEditor::InspectUsage)]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(7.0f, 1.0f, 7.0f, 5.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("ProjectionTip", "Choose a perspective or orthographic viewport."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeProjectionMenu)
                .ButtonContent()
                [SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetProjectionLabel)]]
            + SHorizontalBox::Slot().AutoWidth().Padding(7.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SButton).Text(LOCTEXT("Frame", "Frame All"))
                .ToolTipText(LOCTEXT("FrameTip", "Fit the entire spline in the viewport. Shortcut: F."))
                .OnClicked_Lambda([this]() { if (PreviewWidget.IsValid()) { PreviewWidget->FrameSpline(); } return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("FramePoint", "Frame Point"))
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .ToolTipText(LOCTEXT("FramePointTip", "Center the camera on the selected point."))
                .OnClicked_Lambda([this]() { if (PreviewWidget.IsValid()) { PreviewWidget->FrameSelectedPoint(); } return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(13.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SCheckBox)
                .IsChecked_Lambda([this]() { return PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (PreviewWidget.IsValid()) { PreviewWidget->SetShowControlPoints(State == ECheckBoxState::Checked); } })
                .ToolTipText(LOCTEXT("PointVisibilityTip", "Show clickable spline control points. Very dense splines display a representative set."))
                [SNew(STextBlock).Text(LOCTEXT("ShowPoints", "Points"))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SCheckBox)
                .IsChecked_Lambda([this]() { return PreviewWidget.IsValid() && PreviewWidget->GetShowGrid() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (PreviewWidget.IsValid()) { PreviewWidget->SetShowGrid(State == ECheckBoxState::Checked); } })
                .ToolTipText(LOCTEXT("GridVisibilityTip", "Show a bounded XY reference floor under the spline, rather than at world Z=0."))
                [SNew(STextBlock).Text(LOCTEXT("ShowGrid", "Grid"))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SCheckBox)
                .IsChecked_Lambda([this]() { return PreviewWidget.IsValid() && PreviewWidget->GetShowTangents() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (PreviewWidget.IsValid()) { PreviewWidget->SetShowTangents(State == ECheckBoxState::Checked); } })
                .ToolTipText(LOCTEXT("TangentsTip", "Display incoming and outgoing tangent directions for the selected point."))
                [SNew(STextBlock).Text(LOCTEXT("ShowTangents", "Tangents"))]]
        ]
        + SVerticalBox::Slot().FillHeight(1.0f).Padding(6.0f, 0.0f)
        [SAssignNew(PreviewWidget, SStrandSplineViewport)]
        + SVerticalBox::Slot().AutoHeight().Padding(9.0f, 6.0f, 9.0f, 2.0f)
        [SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetSummary)]
        + SVerticalBox::Slot().AutoHeight().Padding(9.0f, 0.0f, 9.0f, 2.0f)
        [SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetSourceDescription)]
        + SVerticalBox::Slot().AutoHeight().Padding(9.0f, 0.0f, 9.0f, 7.0f)
        [SNew(STextBlock)
            .Text(LOCTEXT("PointLegend", "Green: start   |   Amber: end   |   Yellow: selected   |   Click to select, drag translation axes to move"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
    ];
    if (PreviewWidget.IsValid() && NativePreview.IsValid())
    {
        PreviewWidget->SetSpline(NativePreview.Get());
        PreviewWidget->SetOnPointSelected([this](int32 Index) { SelectPoint(Index); });
        PreviewWidget->SetPointDragCallbacks(
            [this](const FVector& Delta) { ApplyViewportPointDrag(Delta); },
            [this]() { EndViewportPointDrag(); });
    }
    return Tab;
}

TSharedRef<SDockTab> FStrandSplineAssetEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
    check(DetailsView.IsValid());
    return SNew(SDockTab).Label(LOCTEXT("DetailsTab", "Spline Details"))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 8.0f, 8.0f, 2.0f)
        [SNew(STextBlock).Text(LOCTEXT("PointInspector", "SELECTED POINT"))
            .ColorAndOpacity(FSlateColor::UseForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetSelectedPointLabel)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("PreviousPoint", "<"))
                .ToolTipText(LOCTEXT("PreviousPointTip", "Select the previous spline point."))
                .OnClicked_Lambda([this]() { return SelectAdjacentPoint(-1); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("NextPoint", ">"))
                .ToolTipText(LOCTEXT("NextPointTip", "Select the next spline point."))
                .OnClicked_Lambda([this]() { return SelectAdjacentPoint(1); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 9.0f)
        [
            SNew(SHorizontalBox).IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("X")))]
                .Value_Lambda([this]() { return GetPointCoordinate(0); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 0); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
                .Value_Lambda([this]() { return GetPointCoordinate(1); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 1); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
                .Value_Lambda([this]() { return GetPointCoordinate(2); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 2); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 1.0f, 8.0f, 4.0f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("AddPoint", "Insert Point"))
                .ToolTipText(LOCTEXT("AddPointTip", "Insert a new control point after the selected point, splitting the segment at its midpoint. Undoable."))
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .OnClicked(this, &FStrandSplineAssetEditor::AddPointAfterSelection)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("DeletePoint", "Delete Point"))
                .ToolTipText(LOCTEXT("DeletePointTip", "Delete the selected point. A spline must retain at least two points. Undoable."))
                .IsEnabled_Lambda([this]() { return HasSelectedPoint() && Asset->Points.Num() > 2; })
                .OnClicked(this, &FStrandSplineAssetEditor::RemoveSelectedPoint)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SComboButton)
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakePointTypeMenu)
                .ButtonContent()[SNew(STextBlock).Text(LOCTEXT("PointType", "Point Type"))]]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 3.0f)
        [SNew(SButton).Text(LOCTEXT("ToggleLoop", "Toggle Closed Loop"))
            .ToolTipText(LOCTEXT("ToggleLoopTip", "Toggle the source spline's closed-loop state. Does not modify linked level instances."))
            .OnClicked(this, &FStrandSplineAssetEditor::ToggleClosedLoop)]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 8.0f)
        [SNew(STextBlock)
            .Text(LOCTEXT("PointInspectorHint", "Position in local centimeters. Drag the viewport gizmo or commit a value; one Undo action per gizmo drag."))
            .AutoWrapText(true)
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 6.0f)
        [SNew(STextBlock).Text(LOCTEXT("AdvancedProperties", "ASSET PROPERTIES"))]
        + SVerticalBox::Slot().FillHeight(1.0f)
        [DetailsView.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 6.0f, 8.0f, 8.0f)
        [SNew(STextBlock)
            .Text(LOCTEXT("ImportSettingsHint", "Import settings take effect on Reimport Source. Reimport replaces stored points; existing placed instances are not updated automatically."))
            .AutoWrapText(true)
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
    ];
}

FText FStrandSplineAssetEditor::GetProjectionLabel() const
{
    if (!PreviewWidget.IsValid()) { return LOCTEXT("PerspectiveDefault", "Perspective"); }
    switch (PreviewWidget->GetProjection())
    {
    case LVT_OrthoXY: return LOCTEXT("TopProjection", "Top");
    case LVT_OrthoXZ: return LOCTEXT("FrontProjection", "Front");
    case LVT_OrthoYZ: return LOCTEXT("SideProjection", "Side");
    default: return LOCTEXT("PerspectiveProjection", "Perspective");
    }
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeProjectionMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto AddView = [this, &Menu](const FText& Label, const FText& Tip, ELevelViewportType View)
    {
        Menu.AddMenuEntry(Label, Tip, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, View]()
        {
            if (PreviewWidget.IsValid()) { PreviewWidget->SetProjection(View); }
        })));
    };
    AddView(LOCTEXT("PerspectiveMenu", "Perspective"), LOCTEXT("PerspectiveMenuTip", "Free-orbit 3D preview."), LVT_Perspective);
    AddView(LOCTEXT("TopMenu", "Top (XY)"), LOCTEXT("TopMenuTip", "View along the Z axis."), LVT_OrthoXY);
    AddView(LOCTEXT("FrontMenu", "Front (XZ)"), LOCTEXT("FrontMenuTip", "View along the Y axis."), LVT_OrthoXZ);
    AddView(LOCTEXT("SideMenu", "Side (YZ)"), LOCTEXT("SideMenuTip", "View along the X axis."), LVT_OrthoYZ);
    return Menu.MakeWidget();
}

bool FStrandSplineAssetEditor::HasSelectedPoint() const
{
    const USplineDataAsset* Current = Asset.Get();
    return Current && Current->Points.IsValidIndex(SelectedPointIndex);
}

FText FStrandSplineAssetEditor::GetSelectedPointLabel() const
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->Points.IsEmpty()) { return LOCTEXT("NoPoints", "No spline points"); }
    if (!HasSelectedPoint()) { return LOCTEXT("SelectPointHint", "Click a control point in the viewport"); }
    return FText::FromString(FString::Printf(TEXT("Point %d of %d"), SelectedPointIndex + 1, Current->Points.Num()));
}

TOptional<double> FStrandSplineAssetEditor::GetPointCoordinate(int32 Axis) const
{
    if (!HasSelectedPoint()) { return TOptional<double>(); }
    const FVector& Position = Asset->Points[SelectedPointIndex].LocalPosition;
    return Axis == 0 ? Position.X : Axis == 1 ? Position.Y : Position.Z;
}

void FStrandSplineAssetEditor::SetPointCoordinate(double Value, int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || !FMath::IsFinite(Value)) { return; }
    FVector& Position = Current->Points[SelectedPointIndex].LocalPosition;
    double* Target = Axis == 0 ? &Position.X : Axis == 1 ? &Position.Y : &Position.Z;
    if (FMath::IsNearlyEqual(*Target, Value, 0.000001)) { return; }
    const FScopedTransaction Transaction(LOCTEXT("MovePointTransaction", "Move Strand Spline Point"));
    Current->Modify();
    *Target = Value;
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
}

void FStrandSplineAssetEditor::BeginViewportPointDrag()
{
    if (ActivePointDragTransaction || !HasSelectedPoint()) { return; }
    ActivePointDragTransaction = MakeUnique<FScopedTransaction>(
        LOCTEXT("DragSplinePoint", "Move Strand Spline Point"));
    bPointChangedDuringDrag = false;
    Asset->Modify();
}

void FStrandSplineAssetEditor::ApplyViewportPointDrag(const FVector& LocalDelta)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !Current->Points.IsValidIndex(SelectedPointIndex)
        || LocalDelta.IsNearlyZero() || LocalDelta.ContainsNaN())
    {
        return;
    }
    // InputWidgetDelta can be used by nudge/keyboard operations, not only mouse
    // tracking. In that case open the transaction lazily on the first delta.
    if (!ActivePointDragTransaction) { BeginViewportPointDrag(); }
    if (!ActivePointDragTransaction) { return; }
    Current->Points[SelectedPointIndex].LocalPosition += LocalDelta;
    bPointChangedDuringDrag = true;
    Current->PostEditChange();
    RebuildNativePreview();
}

void FStrandSplineAssetEditor::EndViewportPointDrag()
{
    if (!ActivePointDragTransaction) { return; }
    if (bPointChangedDuringDrag)
    {
        if (USplineDataAsset* Current = Asset.Get()) { Current->MarkPackageDirty(); }
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    ActivePointDragTransaction.Reset();
    bPointChangedDuringDrag = false;
}

void FStrandSplineAssetEditor::SelectPoint(int32 Index)
{
    EndViewportPointDrag();
    const USplineDataAsset* Current = Asset.Get();
    SelectedPointIndex = Current && Current->Points.IsValidIndex(Index) ? Index : INDEX_NONE;
    if (PreviewWidget.IsValid()) { PreviewWidget->SetSelectedPointIndex(SelectedPointIndex); }
}

FReply FStrandSplineAssetEditor::SelectAdjacentPoint(int32 Direction)
{
    if (const USplineDataAsset* Current = Asset.Get())
    {
        const int32 Count = Current->Points.Num();
        if (Count > 0)
        {
            const int32 Candidate = SelectedPointIndex == INDEX_NONE ? (Direction < 0 ? Count - 1 : 0)
                : FMath::Clamp(SelectedPointIndex + Direction, 0, Count - 1);
            SelectPoint(Candidate);
        }
    }
    return FReply::Handled();
}

void FStrandSplineAssetEditor::RefreshAfterUndo()
{
    EndViewportPointDrag();
    if (!HasSelectedPoint()) { SelectPoint(INDEX_NONE); }
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
}
void FStrandSplineAssetEditor::PostUndo(bool bSuccess) { if (bSuccess) { RefreshAfterUndo(); } }
void FStrandSplineAssetEditor::PostRedo(bool bSuccess) { if (bSuccess) { RefreshAfterUndo(); } }

void FStrandSplineAssetEditor::RebuildNativePreview()
{
    if (Asset.IsValid() && NativePreview.IsValid())
    {
        Asset->ApplyToSplineComponent(NativePreview.Get());
        if (PreviewWidget.IsValid()) { PreviewWidget->SetSpline(NativePreview.Get()); }
    }
}

void FStrandSplineAssetEditor::OnPropertyChanged(const FPropertyChangedEvent& Event)
{
    EndViewportPointDrag();
    if (!HasSelectedPoint()) { SelectPoint(INDEX_NONE); }
    RebuildNativePreview();
}



bool FStrandSplineAssetEditor::CanReimport() const
{
#if WITH_EDITORONLY_DATA
    if (const USplineDataAsset* Current = Asset.Get())
    {
        return Current->AssetImportData && !Current->AssetImportData->GetFirstFilename().IsEmpty()
            && !Current->SourceCurveId.IsEmpty()
            && (Current->SourceImportFormat == TEXT("abc") || Current->SourceImportFormat == TEXT("svg"));
    }
#endif
    return false;
}

FReply FStrandSplineAssetEditor::ReimportSource()
{
    if (USplineDataAsset* Current = Asset.Get())
    {
#if WITH_EDITORONLY_DATA
        if (CanReimport() &&
            (Current->SourceImportFormat == TEXT("abc") || Current->SourceImportFormat == TEXT("svg")))
        {
            // Reimport replaces the stored geometry. Make that destructive action
            // explicit, especially after a point was moved in this editor.
            if (FMessageDialog::Open(EAppMsgType::YesNo,
                LOCTEXT("ConfirmReimport", "Reimport will replace this Strand asset's stored spline points with the source file's geometry. Unsaved point edits will be lost. Continue?")) != EAppReturnType::Yes)
            {
                return FReply::Handled();
            }
            EndViewportPointDrag();
            UStrandReimportFactory* Factory = NewObject<UStrandReimportFactory>(GetTransientPackage());
            if (Factory->Reimport(Current) == EReimportResult::Succeeded)
            {
                DetailsView->ForceRefresh();
                if (!HasSelectedPoint()) { SelectPoint(INDEX_NONE); }
                RebuildNativePreview();
            }
        }
        else
#endif
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSource", "This spline has no supported linked source file to reimport."));
        }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::CreateActor()
{
    if (USplineDataAsset* Current = Asset.Get()) { SplineEditorCommands::CreateNativeActorFromAsset(Current); }
    return FReply::Handled();
}
FReply FStrandSplineAssetEditor::SelectUsers()
{
    if (USplineDataAsset* Current = Asset.Get()) { SplineEditorCommands::SelectActorsUsingAsset(Current); }
    return FReply::Handled();
}
FReply FStrandSplineAssetEditor::InspectUsage()
{
    if (USplineDataAsset* Current = Asset.Get()) { SplineEditorCommands::InspectAssetUsage(Current); }
    return FReply::Handled();
}
FReply FStrandSplineAssetEditor::NewSplineAsset()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current) { return FReply::Handled(); }
    const FString ParentFolder = FPackageName::GetLongPackagePath(Current->GetOutermost()->GetName());
    IAssetTools& Tools = FAssetToolsModule::GetModule().Get();
    FString PackageName, AssetName;
    Tools.CreateUniqueAssetName(FPaths::Combine(ParentFolder, TEXT("StrandSpline")), TEXT(""), PackageName, AssetName);
    UObject* Created = Tools.CreateAsset(AssetName, ParentFolder, USplineDataAsset::StaticClass(), NewObject<USplineDataAssetFactory>());
    if (Created && GEditor)
    {
        if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            Editors->OpenEditorForAsset(Created);
        }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ExportAlembic()
{
    StrandSplineExport::ExportInteractive(Asset.Get(), true);
    return FReply::Handled();
}
FReply FStrandSplineAssetEditor::ExportSvg()
{
    StrandSplineExport::ExportInteractive(Asset.Get(), false);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::AddPointAfterSelection()
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || Current->Points.Num() >= 100000) { return FReply::Handled(); }
    EndViewportPointDrag();
    const int32 Index = SelectedPointIndex;
    const int32 NextIndex = (Index + 1) % Current->Points.Num();
    // Split the evaluated Hermite/Bézier segment at t=0.5 using de Casteljau.
    // This preserves the 3D curve exactly, unlike averaging its anchor positions.
    // An append after the final point of an open spline is an intentional extension.
    FSplinePointData NewPoint = Current->Points[Index];
    const bool bAppend = !Current->bIsClosedLoop && Index == Current->Points.Num() - 1;
    bool bSplitBezier = false;
    // Tangents are only consumed after a cubic split, but initialize them so
    // all control-flow paths remain well-defined (including MSVC /analyze).
    FVector FirstLeave = FVector::ZeroVector;
    FVector NextArrive = FVector::ZeroVector;
    if (bAppend)
    {
        const FVector Direction = Current->Points[Index].LocalPosition - Current->Points[Index - 1].LocalPosition;
        NewPoint.LocalPosition += Direction * 0.5;
        NewPoint.PointType = ESplinePointType::Curve;
        NewPoint.ArriveTangent = FVector::ZeroVector;
        NewPoint.LeaveTangent = FVector::ZeroVector;
    }
    else
    {
        // Use the transient native spline, which has already evaluated Curve
        // and CurveClamped tangents according to Unreal's own spline semantics.
        const FSplinePoint A = NativePreview->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
        const FSplinePoint B = NativePreview->GetSplinePointAt(NextIndex, ESplineCoordinateSpace::Local);
        if (A.Type == ESplinePointType::Linear)
        {
            NewPoint.LocalPosition = (A.Position + B.Position) * 0.5;
            NewPoint.PointType = ESplinePointType::Linear;
            NewPoint.ArriveTangent = FVector::ZeroVector;
            NewPoint.LeaveTangent = FVector::ZeroVector;
        }
        else if (A.Type == ESplinePointType::Constant)
        {
            // Insert at the middle of the parameter range without changing
            // the constant segment's discontinuous value.
            NewPoint.LocalPosition = A.Position;
            NewPoint.PointType = ESplinePointType::Constant;
        }
        else
        {
            const FVector C1 = A.Position + A.LeaveTangent / 3.0;
            const FVector C2 = B.Position - B.ArriveTangent / 3.0;
            const FVector L = (A.Position + C1) * 0.5;
            const FVector M = (C1 + C2) * 0.5;
            const FVector R = (C2 + B.Position) * 0.5;
            const FVector LM = (L + M) * 0.5;
            const FVector MR = (M + R) * 0.5;
            const FVector Midpoint = (LM + MR) * 0.5;
            FirstLeave = (L - A.Position) * 3.0;
            const FVector MidArrive = (Midpoint - LM) * 3.0;
            const FVector MidLeave = (MR - Midpoint) * 3.0;
            NextArrive = (B.Position - R) * 3.0;
            NewPoint.LocalPosition = Midpoint;
            NewPoint.ArriveTangent = MidArrive;
            NewPoint.LeaveTangent = MidLeave;
            NewPoint.PointType = ESplinePointType::CurveCustomTangent;
            bSplitBezier = true;
        }
    }
    const FScopedTransaction Transaction(LOCTEXT("InsertStrandPoint", "Insert Strand Spline Point"));
    Current->Modify();
    // When splitting a smooth segment, the neighboring anchors become custom-
    // tangent anchors. This preserves the exact original geometry even if they
    // previously used auto-generated tangent modes.
    if (bSplitBezier)
    {
        Current->Points[Index].PointType = ESplinePointType::CurveCustomTangent;
        Current->Points[Index].ArriveTangent = NativePreview->GetSplinePointAt(Index, ESplineCoordinateSpace::Local).ArriveTangent;
        Current->Points[Index].LeaveTangent = FirstLeave;
        Current->Points[NextIndex].PointType = ESplinePointType::CurveCustomTangent;
        Current->Points[NextIndex].LeaveTangent = NativePreview->GetSplinePointAt(NextIndex, ESplineCoordinateSpace::Local).LeaveTangent;
        Current->Points[NextIndex].ArriveTangent = NextArrive;
    }
    Current->Points.Insert(NewPoint, Index + 1);
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    SelectPoint(Index + 1);
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::RemoveSelectedPoint()
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || Current->Points.Num() <= 2) { return FReply::Handled(); }
    EndViewportPointDrag();
    const int32 Index = SelectedPointIndex;
    const FScopedTransaction Transaction(LOCTEXT("DeleteStrandPoint", "Delete Strand Spline Point"));
    Current->Modify();
    Current->Points.RemoveAt(Index);
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    SelectPoint(FMath::Min(Index, Current->Points.Num() - 1));
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ToggleClosedLoop()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ToggleStrandLoop", "Toggle Strand Closed Loop"));
    Current->Modify();
    Current->bIsClosedLoop = !Current->bIsClosedLoop;
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SetSelectedPointType(ESplinePointType::Type NewType)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint()) { return FReply::Handled(); }
    EndViewportPointDrag();
    if (Current->Points[SelectedPointIndex].PointType == NewType) { return FReply::Handled(); }
    const FScopedTransaction Transaction(LOCTEXT("StrandPointType", "Change Strand Spline Point Type"));
    Current->Modify();
    Current->Points[SelectedPointIndex].PointType = NewType;
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    return FReply::Handled();
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakePointTypeMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto Add = [this, &Menu](ESplinePointType::Type Mode, const FText& Label)
    {
        Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Mode]()
        {
            SetSelectedPointType(Mode);
        })));
    };
    Add(ESplinePointType::Curve, LOCTEXT("CurveAuto", "Curve (Auto)"));
    Add(ESplinePointType::CurveClamped, LOCTEXT("CurveClamped", "Curve (Clamped)"));
    Add(ESplinePointType::CurveCustomTangent, LOCTEXT("CurveCustom", "Curve (Custom Tangents)"));
    Add(ESplinePointType::Linear, LOCTEXT("LinearPoint", "Linear"));
    Add(ESplinePointType::Constant, LOCTEXT("ConstantPoint", "Constant (not exportable yet)"));
    return Menu.MakeWidget();
}

FText FStrandSplineAssetEditor::GetSummary() const
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current) { return LOCTEXT("NotAvailable", "Spline unavailable"); }
    const double Length = NativePreview.IsValid() ? NativePreview->GetSplineLength() : 0.0;
    return FText::FromString(FString::Printf(TEXT("%d control points   |   Length: %.1f cm   |   %s spline"),
        Current->Points.Num(), Length, Current->bIsClosedLoop ? TEXT("Closed") : TEXT("Open")));
}
FText FStrandSplineAssetEditor::GetSourceDescription() const
{
#if WITH_EDITORONLY_DATA
    if (const USplineDataAsset* Current = Asset.Get())
    {
        if (Current->AssetImportData && !Current->SourceCurveId.IsEmpty() &&
            !Current->AssetImportData->GetFirstFilename().IsEmpty())
        {
            return FText::FromString(FString::Printf(TEXT("Source: %s   |   Curve: %s   |   Format: %s"),
                *FPaths::GetCleanFilename(Current->AssetImportData->GetFirstFilename()),
                *Current->SourceCurveId, *Current->SourceImportFormat));
        }
    }
#endif
    return LOCTEXT("ManualSpline", "Native/manual Strand spline. Changes to points are saved in this asset only.");
}

#undef LOCTEXT_NAMESPACE
