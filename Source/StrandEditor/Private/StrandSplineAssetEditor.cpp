#include "StrandSplineAssetEditor.h"
#include "StrandSplineViewport.h"
#include "SplineDataAsset.h"
#include "SplineDataAssetFactory.h"
#include "SplineEditorCommands.h"
#include "StrandSplineExport.h"
#include "StrandSplineEditOperations.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
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

    TSharedRef<SWidget> ActionContent(const FName Icon, const FText& Label)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
              [SNew(SImage).Image(FAppStyle::GetBrush(Icon)).DesiredSizeOverride(FVector2D(16.0f, 16.0f))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [SNew(STextBlock).Text(Label)];
    }

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

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("StrandSplineEditor_Layout_v6")
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
            ->Split(FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.71f)
                    ->AddTab(PreviewTabId, ETabState::OpenedTab)->SetHideTabWell(true))
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.29f)
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
            [SNew(SButton)
                .ToolTipText(LOCTEXT("NewSplineTip", "Create and open an independent, editable Strand Spline asset in the same Content Browser folder."))
                .OnClicked(this, &FStrandSplineAssetEditor::NewSplineAsset)
                [ActionContent(TEXT("Icons.Plus"), LOCTEXT("NewSpline", "New Spline"))]]
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
                .ButtonContent()[ActionContent(TEXT("Icons.Export"), LOCTEXT("Export", "Export"))]]
            + SHorizontalBox::Slot().AutoWidth().Padding(10.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SButton)
                .ToolTipText(LOCTEXT("ReimportTip", "Regenerate this spline from Alembic or SVG using its current import settings. This replaces the asset's stored points but does not automatically change placed actors."))
                .IsEnabled(this, &FStrandSplineAssetEditor::CanReimport)
                .OnClicked(this, &FStrandSplineAssetEditor::ReimportSource)
                [ActionContent(TEXT("Icons.Refresh"), LOCTEXT("Reimport", "Reimport"))]]
            + SHorizontalBox::Slot().AutoWidth().Padding(11.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("ActorActionTip", "Actions involving spline actors and their asset links in the current level."))
                .OnGetMenuContent_Lambda([this]()
                {
                    FMenuBuilder Menu(true, nullptr);
                    Menu.AddMenuEntry(LOCTEXT("CreateActor", "Create Actor"), LOCTEXT("CreateActorTip", "Place a native Strand spline actor from this asset."),
                        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"), FUIAction(FExecuteAction::CreateLambda([this]() { CreateActor(); })));
                    Menu.AddMenuEntry(LOCTEXT("SelectUsers", "Select Users"), LOCTEXT("SelectUsersTip", "Select actors linked to this spline in the level."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SelectUsers(); })));
                    Menu.AddMenuEntry(LOCTEXT("InspectUsage", "Inspect Usage"), LOCTEXT("InspectUsageTip", "Inspect actors using this asset."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { InspectUsage(); })));
                    return Menu.MakeWidget();
                })
                .ButtonContent()[ActionContent(TEXT("Icons.Actor"), LOCTEXT("ActorsButton", "Actors"))]]
            + SHorizontalBox::Slot().AutoWidth().Padding(5.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("SplineToolsTip", "Selection modeling, point alignment/distribution, whole-spline operations, and curve refinement."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeSplineToolsMenu)
                .ButtonContent()[ActionContent(TEXT("Icons.Edit"), LOCTEXT("SplineToolsLabel", "Spline Tools"))]]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(7.0f, 1.0f, 7.0f, 5.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)
            [SNew(SCheckBox)
                .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return bPenMode ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { SetPenMode(false); })
                .ToolTipText(LOCTEXT("SelectModeTip", "Click to select, Shift-click a contiguous range, Ctrl-click to toggle. Ctrl+A selects all; Esc clears. Move selected points together with the gizmo."))
                [SNew(STextBlock).Text(LOCTEXT("SelectMode", "Select"))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)
            [SNew(SCheckBox)
                .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return bPenMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetPenMode(State == ECheckBoxState::Checked); })
                .ToolTipText(LOCTEXT("PenModeTip", "Pen: click to add anchors, drag to shape symmetric handles, click a segment to insert, click an endpoint to continue or close; Esc to exit."))
                [ActionContent(TEXT("Icons.Edit"), LOCTEXT("PenMode", "Pen"))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("SelectionTip", "Select All, Invert Selection, or Clear Selection. Multi-point tools are under Spline Tools."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeSelectionMenu)
                .ButtonContent()[SNew(STextBlock).Text(LOCTEXT("SelectionDropdown", "Selection"))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("ProjectionTip", "Choose a perspective or orthographic viewport."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeProjectionMenu)
                .ButtonContent()
                [SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetProjectionLabel)]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("EditPlaneTip", "Constrain point and tangent movement to a 2D plane, or use unrestricted 3D editing. Choosing a plane does not flatten existing geometry."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeEditPlaneMenu)
                .ButtonContent()[SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetEditPlaneLabel)]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SComboButton)
                .IsEnabled_Lambda([this]() { return bPenMode; })
                .ToolTipText(LOCTEXT("PenSnapTip", "Pen anchor and handle snapping in centimeters. Does not modify existing spline geometry."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakePenSnapMenu)
                .ButtonContent()[SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetPenSnapLabel)]]
            + SHorizontalBox::Slot().AutoWidth().Padding(7.0f, 0.0f, 2.0f, 0.0f)
            [SNew(SButton).Text(LOCTEXT("Frame", "Frame All"))
                .ToolTipText(LOCTEXT("FrameTip", "Fit the entire spline in the viewport. Shortcut: F."))
                .OnClicked_Lambda([this]() { if (PreviewWidget.IsValid()) { PreviewWidget->FrameSpline(); } return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("FramePoint", "Frame Primary"))
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .ToolTipText(LOCTEXT("FramePointTip", "Center the camera on the primary selected point."))
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
        [SNew(STextBlock)
            .Text(this, &FStrandSplineAssetEditor::GetSummary)
            .ToolTipText(this, &FStrandSplineAssetEditor::GetSourceDescription)
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(9.0f, 0.0f, 9.0f, 4.0f)
        [SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetEditorHint)
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
    ];
    if (PreviewWidget.IsValid() && NativePreview.IsValid())
    {
        PreviewWidget->SetSpline(NativePreview.Get());
        PreviewWidget->SetOnPointSelected([this](int32 Index, bool bShift, bool bCtrl) { SelectPoint(Index, bShift, bCtrl); });
        PreviewWidget->SetOnSelectAll([this]() { SelectAllPoints(); });
        PreviewWidget->SetOnClearSelection([this]() { ClearPointSelection(); });
        PreviewWidget->SetOnTangentSelected([this](int32 Index, int32 Handle) { SelectTangentHandle(Index, Handle); });
        PreviewWidget->SetTangentDragCallback([this](int32 Handle, const FVector& Delta) { ApplyViewportTangentDrag(Handle, Delta); });
        PreviewWidget->SetPointDragCallbacks(
            [this](const FVector& Delta) { ApplyViewportPointDrag(Delta); },
            [this]() { EndViewportPointDrag(); });
        PreviewWidget->SetPenCallbacks(
            [this](const FStrandPenHit& Hit) { return BeginPenStroke(Hit); },
            [this](const FVector& Offset) { UpdatePenStroke(Offset); },
            [this]() { EndPenStroke(); },
            [this]() { CancelPenStroke(); },
            [this]() { SetPenMode(false); });
        PreviewWidget->SetPenMode(bPenMode);
        PreviewWidget->SetPenSnap(PenSnapStep);
        PreviewWidget->SetPenPlane(EditPlane == EStrandEditPlane::XY ? 2 :
            EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1);
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
        [SNew(STextBlock).Text(LOCTEXT("PointInspector", "SELECTION / POINT EDITOR"))
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
            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 2.0f)
            [SNew(SButton).Text(LOCTEXT("SelectAllShort", "All"))
                .ToolTipText(LOCTEXT("SelectAllShortTip", "Select all spline anchors (Ctrl+A in viewport)."))
                .OnClicked(this, &FStrandSplineAssetEditor::SelectAllPoints)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("ClearSelectedShort", "None"))
                .ToolTipText(LOCTEXT("ClearSelectedShortTip", "Clear current point selection (Esc in viewport)."))
                .OnClicked(this, &FStrandSplineAssetEditor::ClearPointSelection)]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 0.0f, 8.0f, 3.0f)
        [SNew(STextBlock)
            .Text_Lambda([this]() { return HasMultipleSelectedPoints()
                ? LOCTEXT("GroupCoordinates", "SELECTION CENTER (local cm, edit to move group)")
                : LOCTEXT("SingleCoordinates", "POINT POSITION (local cm)"); })
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 9.0f)
        [
            SNew(SHorizontalBox).IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("X")))]
                .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::YZ; })
                .Value_Lambda([this]() { return GetPointCoordinate(0); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 0); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
                .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XZ; })
                .Value_Lambda([this]() { return GetPointCoordinate(1); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 1); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
                .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XY; })
                .Value_Lambda([this]() { return GetPointCoordinate(2); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 2); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 7.0f)
        [SNew(SVerticalBox)
            .Visibility_Lambda([this]() { return HasSelectedPoint() && !HasMultipleSelectedPoints() && SelectedHandle != 0 ? EVisibility::Visible : EVisibility::Collapsed; })
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
            [SNew(STextBlock).Text(LOCTEXT("SelectedTangentCoords", "TANGENT VALUES (cm per spline key)"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(3)
                    .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::YZ; })
                    .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("X")))]
                    .Value_Lambda([this]() { return GetTangentCoordinate(0); })
                    .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetTangentCoordinate(Value, 0); })]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(3)
                    .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XZ; })
                    .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
                    .Value_Lambda([this]() { return GetTangentCoordinate(1); })
                    .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetTangentCoordinate(Value, 1); })]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(3)
                    .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XY; })
                    .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
                    .Value_Lambda([this]() { return GetTangentCoordinate(2); })
                    .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetTangentCoordinate(Value, 2); })]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 3.0f)
        [SNew(SHorizontalBox)
            .Visibility_Lambda([this]() { return HasSelectedPoint() && !HasMultipleSelectedPoints() ? EVisibility::Visible : EVisibility::Collapsed; })
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("IncomingHandleButton", "Incoming Handle"))
                .ToolTipText(LOCTEXT("IncomingHandleTip", "Select the incoming Bezier handle for numeric or gizmo editing. Tangent visibility must be enabled."))
                .OnClicked_Lambda([this]() { SelectTangentHandle(SelectedPointIndex, 1); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("OutgoingHandleButton", "Outgoing Handle"))
                .ToolTipText(LOCTEXT("OutgoingHandleTip", "Select the outgoing Bezier handle for numeric or gizmo editing. Tangent visibility must be enabled."))
                .OnClicked_Lambda([this]() { SelectTangentHandle(SelectedPointIndex, 2); return FReply::Handled(); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 1.0f, 8.0f, 4.0f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton)
                .ToolTipText(LOCTEXT("AddPointTip", "Insert after one selected anchor; bisect the next segment or extend an open endpoint. Undoable."))
                .IsEnabled_Lambda([this]() { return HasSelectedPoint() && !HasMultipleSelectedPoints(); })
                .OnClicked(this, &FStrandSplineAssetEditor::AddPointAfterSelection)
                [ActionContent(TEXT("Icons.Plus"), LOCTEXT("AddPoint", "Insert After"))]]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton)
                .ToolTipText(LOCTEXT("DeletePointTip", "Delete all selected anchors in one undo step; at least two points must remain."))
                .IsEnabled(this, &FStrandSplineAssetEditor::CanDeleteSelection)
                .OnClicked(this, &FStrandSplineAssetEditor::RemoveSelectedPoint)
                [ActionContent(TEXT("Icons.Delete"), LOCTEXT("DeletePoint", "Delete Selected"))]]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SComboButton)
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakePointTypeMenu)
                .ButtonContent()[SNew(STextBlock).Text(LOCTEXT("PointType", "Interpolation"))]]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 4.0f, 8.0f, 6.0f)
        [SNew(STextBlock).Text(LOCTEXT("AdvancedProperties", "ASSET SETTINGS"))
            .ToolTipText(LOCTEXT("AssetSettingsTooltip", "Source and import settings affect Reimport, not the current authored geometry until you reimport."))]
        + SVerticalBox::Slot().FillHeight(1.0f)
        [DetailsView.ToSharedRef()]
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
    if (HasMultipleSelectedPoints())
    {
        return FText::FromString(FString::Printf(TEXT("%d of %d points selected"),
            SelectedPointIndices.Num(), Current->Points.Num()));
    }
    const TCHAR* HandleName = SelectedHandle == 1 ? TEXT("  |  Incoming tangent") : SelectedHandle == 2 ? TEXT("  |  Outgoing tangent") : TEXT("");
    return FText::FromString(FString::Printf(TEXT("Point %d of %d%s"), SelectedPointIndex + 1, Current->Points.Num(), HandleName));
}

TOptional<double> FStrandSplineAssetEditor::GetPointCoordinate(int32 Axis) const
{
    if (!HasSelectedPoint()) { return TOptional<double>(); }
    const FVector Center = StrandSplineEditOperations::SelectionCenter(Asset->Points, GetSelectedIndices());
    return Center[Axis];
}

void FStrandSplineAssetEditor::SetPointCoordinate(double Value, int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || !CanEditAxis(Axis) || !FMath::IsFinite(Value)) { return; }
    const TArray<int32> Indices = GetSelectedIndices();
    const double Old = StrandSplineEditOperations::SelectionCenter(Current->Points, Indices)[Axis];
    const double DeltaValue = Value - Old;
    if (FMath::IsNearlyZero(DeltaValue, 0.000001)) { return; }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("MovePointTransaction", "Move Strand Spline Selection"));
    Current->Modify();
    FVector Delta = FVector::ZeroVector;
    Delta[Axis] = DeltaValue;
    if (StrandSplineEditOperations::TranslateSelected(Current->Points, Indices, Delta))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
}

TOptional<double> FStrandSplineAssetEditor::GetTangentCoordinate(int32 Axis) const
{
    if (!HasSelectedPoint() || HasMultipleSelectedPoints() || !NativePreview.IsValid() || SelectedHandle == 0 || Axis < 0 || Axis > 2)
    {
        return TOptional<double>();
    }
    const FSplinePoint Point = NativePreview->GetSplinePointAt(SelectedPointIndex, ESplineCoordinateSpace::Local);
    return SelectedHandle == 1 ? Point.ArriveTangent[Axis] : Point.LeaveTangent[Axis];
}

void FStrandSplineAssetEditor::SetTangentCoordinate(double Value, int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || HasMultipleSelectedPoints() || !NativePreview.IsValid() || SelectedHandle == 0 || Axis < 0 || Axis > 2 || !FMath::IsFinite(Value))
    {
        return;
    }
    const int32 LockedAxis = EditPlane == EStrandEditPlane::XY ? 2 : EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1;
    if (Axis == LockedAxis) { return; }
    const FSplinePoint Evaluated = NativePreview->GetSplinePointAt(SelectedPointIndex, ESplineCoordinateSpace::Local);
    const double Previous = SelectedHandle == 1 ? Evaluated.ArriveTangent[Axis] : Evaluated.LeaveTangent[Axis];
    if (FMath::IsNearlyEqual(Previous, Value, 0.000001)) { return; }
    const FScopedTransaction Transaction(LOCTEXT("EditTangentUndo", "Edit Strand Spline Tangent"));
    Current->Modify();
    FSplinePointData& Saved = Current->Points[SelectedPointIndex];
    if (Saved.PointType != ESplinePointType::CurveCustomTangent)
    {
        Saved.ArriveTangent = Evaluated.ArriveTangent;
        Saved.LeaveTangent = Evaluated.LeaveTangent;
        Saved.PointType = ESplinePointType::CurveCustomTangent;
    }
    if (SelectedHandle == 1) { Saved.ArriveTangent[Axis] = Value; }
    else { Saved.LeaveTangent[Axis] = Value; }
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
}

void FStrandSplineAssetEditor::BeginViewportPointDrag()
{
    if (ActivePointDragTransaction || !HasSelectedPoint()) { return; }
    ActivePointDragTransaction = MakeUnique<FScopedTransaction>(
        LOCTEXT("DragSplinePoint", "Move Strand Spline Selection"));
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
    FVector Constrained = LocalDelta;
    const int32 LockedAxis = EditPlane == EStrandEditPlane::XY ? 2 : EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1;
    if (LockedAxis >= 0) { Constrained[LockedAxis] = 0.0; }
    if (Constrained.IsNearlyZero()) { return; }
    if (StrandSplineEditOperations::TranslateSelected(Current->Points, GetSelectedIndices(), Constrained))
    {
        bPointChangedDuringDrag = true;
        Current->PostEditChange();
        RebuildNativePreview();
    }
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

TArray<int32> FStrandSplineAssetEditor::GetSelectedIndices() const
{
    TArray<int32> Result = SelectedPointIndices.Array();
    Result.Sort();
    return Result;
}

bool FStrandSplineAssetEditor::HasMultipleSelectedPoints() const { return SelectedPointIndices.Num() > 1; }

bool FStrandSplineAssetEditor::CanEditAxis(int32 Axis) const
{
    if (Axis < 0 || Axis > 2) { return false; }
    const int32 Locked = EditPlane == EStrandEditPlane::XY ? 2 :
        EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1;
    return Axis != Locked;
}

bool FStrandSplineAssetEditor::CanDeleteSelection() const
{
    const USplineDataAsset* Current = Asset.Get();
    return Current && !SelectedPointIndices.IsEmpty() &&
        Current->Points.Num() - SelectedPointIndices.Num() >= 2;
}

void FStrandSplineAssetEditor::SyncViewportSelection()
{
    if (PreviewWidget.IsValid())
    {
        PreviewWidget->SetSelectedPoints(GetSelectedIndices(), SelectedPointIndex);
        PreviewWidget->SetSelectedHandle(HasMultipleSelectedPoints() ? 0 : SelectedHandle);
    }
}

void FStrandSplineAssetEditor::PruneSelection()
{
    const USplineDataAsset* Current = Asset.Get();
    for (int32 Index : GetSelectedIndices())
    {
        if (!Current || !Current->Points.IsValidIndex(Index)) { SelectedPointIndices.Remove(Index); }
    }
    if (!SelectedPointIndices.Contains(SelectedPointIndex))
    {
        const TArray<int32> Remaining = GetSelectedIndices();
        SelectedPointIndex = Remaining.IsEmpty() ? INDEX_NONE : Remaining.Last();
        SelectedHandle = 0;
    }
    SyncViewportSelection();
}

void FStrandSplineAssetEditor::SelectPoint(int32 Index, bool bRange, bool bToggle)
{
    EndViewportPointDrag();
    const USplineDataAsset* Current = Asset.Get();
    const bool bValid = Current && Current->Points.IsValidIndex(Index);
    const int32 OldPrimary = SelectedPointIndex;
    if (!bValid)
    {
        if (!bRange && !bToggle) { SelectedPointIndices.Reset(); SelectedPointIndex = INDEX_NONE; }
    }
    else if (bToggle)
    {
        if (SelectedPointIndices.Contains(Index)) { SelectedPointIndices.Remove(Index); }
        else { SelectedPointIndices.Add(Index); }
        if (SelectedPointIndices.Contains(Index)) { SelectedPointIndex = Index; }
        else if (!SelectedPointIndices.Contains(SelectedPointIndex))
        {
            const TArray<int32> Remaining = GetSelectedIndices();
            SelectedPointIndex = Remaining.IsEmpty() ? INDEX_NONE : Remaining.Last();
        }
    }
    else if (bRange && Current->Points.IsValidIndex(OldPrimary))
    {
        for (int32 I = FMath::Min(Index, OldPrimary); I <= FMath::Max(Index, OldPrimary); ++I)
        {
            SelectedPointIndices.Add(I);
        }
        SelectedPointIndex = Index;
    }
    else
    {
        SelectedPointIndices.Reset();
        SelectedPointIndices.Add(Index);
        SelectedPointIndex = Index;
    }
    SelectedHandle = 0;
    SyncViewportSelection();
}

FReply FStrandSplineAssetEditor::SelectAllPoints()
{
    EndViewportPointDrag();
    SelectedPointIndices.Reset();
    if (const USplineDataAsset* Current = Asset.Get())
    {
        for (int32 I = 0; I < Current->Points.Num(); ++I) { SelectedPointIndices.Add(I); }
        SelectedPointIndex = Current->Points.IsEmpty() ? INDEX_NONE :
            (Current->Points.IsValidIndex(SelectedPointIndex) ? SelectedPointIndex : 0);
    }
    else { SelectedPointIndex = INDEX_NONE; }
    SelectedHandle = 0;
    SyncViewportSelection();
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ClearPointSelection()
{
    SelectPoint(INDEX_NONE);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::InvertPointSelection()
{
    EndViewportPointDrag();
    if (const USplineDataAsset* Current = Asset.Get())
    {
        TSet<int32> Inverted;
        for (int32 I = 0; I < Current->Points.Num(); ++I)
        {
            if (!SelectedPointIndices.Contains(I)) { Inverted.Add(I); }
        }
        SelectedPointIndices = MoveTemp(Inverted);
        const TArray<int32> Indices = GetSelectedIndices();
        SelectedPointIndex = Indices.IsEmpty() ? INDEX_NONE : Indices.Last();
        SelectedHandle = 0;
        SyncViewportSelection();
    }
    return FReply::Handled();
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeSelectionMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.AddMenuEntry(LOCTEXT("SelectAllMenu", "Select All (Ctrl+A)"), FText::GetEmpty(), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { SelectAllPoints(); })));
    Menu.AddMenuEntry(LOCTEXT("InvertSelectMenu", "Invert Selection"), FText::GetEmpty(), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { InvertPointSelection(); })));
    Menu.AddMenuEntry(LOCTEXT("ClearSelectMenu", "Clear Selection (Esc)"), FText::GetEmpty(), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { ClearPointSelection(); })));
    return Menu.MakeWidget();
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
    PruneSelection();
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
    PruneSelection();
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
                ClearPointSelection(); // reimport replaces topology; old indices no longer mean the same anchors
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
    if (!HasSelectedPoint() || HasMultipleSelectedPoints() || !NativePreview.IsValid() || Current->Points.Num() >= 100000) { return FReply::Handled(); }
    EndViewportPointDrag();
    const int32 Index = SelectedPointIndex;
    const bool bAppend = !Current->bIsClosedLoop && Index == Current->Points.Num() - 1;
    const FScopedTransaction Transaction(LOCTEXT("InsertStrandPoint", "Insert Strand Spline Point"));
    Current->Modify();
    int32 InsertIndex = INDEX_NONE;
    if (bAppend)
    {
        const FVector Direction = Index > 0
            ? (Current->Points[Index].LocalPosition - Current->Points[Index - 1].LocalPosition) * 0.5
            : FVector(100.0, 0.0, 0.0);
        InsertIndex = StrandSplineEditOperations::AppendPenPoint(Current->Points,
            Current->Points[Index].LocalPosition + Direction, false, false);
    }
    else
    {
        InsertIndex = StrandSplineEditOperations::InsertOnSegment(Current->Points,
            Current->bIsClosedLoop, NativePreview.Get(), Index, 0.5);
    }
    if (InsertIndex != INDEX_NONE) { CommitSplineEdit(InsertIndex); }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::RemoveSelectedPoint()
{
    USplineDataAsset* Current = Asset.Get();
    if (!CanDeleteSelection()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const TArray<int32> Indices = GetSelectedIndices();
    const int32 NewIndex = FMath::Min(Indices[0], Current->Points.Num() - Indices.Num() - 1);
    const FScopedTransaction Transaction(LOCTEXT("DeleteStrandPoint", "Delete Selected Strand Spline Points"));
    Current->Modify();
    for (int32 I = Indices.Num() - 1; I >= 0; --I) { Current->Points.RemoveAt(Indices[I]); }
    CommitSplineEdit(NewIndex);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ToggleClosedLoop()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || (!Current->bIsClosedLoop && Current->Points.Num() < 2)) { return FReply::Handled(); }
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
    const TArray<int32> Indices = GetSelectedIndices();
    bool bChanged = false;
    for (int32 Index : Indices) { bChanged |= Current->Points[Index].PointType != NewType; }
    if (!bChanged) { return FReply::Handled(); }
    const FScopedTransaction Transaction(LOCTEXT("StrandPointType", "Change Selected Strand Spline Interpolation"));
    Current->Modify();
    for (int32 Index : Indices)
    {
        FSplinePointData& Saved = Current->Points[Index];
        if (NewType == ESplinePointType::CurveCustomTangent &&
            Saved.PointType != ESplinePointType::CurveCustomTangent && NativePreview.IsValid())
        {
            // Switching an auto curve to custom must not instantly collapse its handles.
            const FSplinePoint Evaluated = NativePreview->GetSplinePointAt(Index, ESplineCoordinateSpace::Local);
            Saved.ArriveTangent = Evaluated.ArriveTangent;
            Saved.LeaveTangent = Evaluated.LeaveTangent;
        }
        Saved.PointType = NewType;
    }
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

FText FStrandSplineAssetEditor::GetEditPlaneLabel() const
{
    switch (EditPlane)
    {
    case EStrandEditPlane::XY: return LOCTEXT("EditXYLabel", "Edit: XY");
    case EStrandEditPlane::XZ: return LOCTEXT("EditXZLabel", "Edit: XZ");
    case EStrandEditPlane::YZ: return LOCTEXT("EditYZLabel", "Edit: YZ");
    default: return LOCTEXT("Edit3DLabel", "Edit: 3D");
    }
}

void FStrandSplineAssetEditor::SetEditPlane(EStrandEditPlane NewPlane)
{
    EndViewportPointDrag();
    EditPlane = NewPlane;
    if (PreviewWidget.IsValid())
    {
        PreviewWidget->SetPenPlane(NewPlane == EStrandEditPlane::XY ? 2 :
            NewPlane == EStrandEditPlane::XZ ? 1 : NewPlane == EStrandEditPlane::YZ ? 0 : -1);
    }
    if (PreviewWidget.IsValid() && NewPlane != EStrandEditPlane::Free3D)
    {
        PreviewWidget->SetProjection(NewPlane == EStrandEditPlane::XY ? LVT_OrthoXY
            : NewPlane == EStrandEditPlane::XZ ? LVT_OrthoXZ : LVT_OrthoYZ);
    }
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeEditPlaneMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto Entry = [this, &Menu](EStrandEditPlane Plane, const FText& Label, const FText& Description)
    {
        Menu.AddMenuEntry(Label, Description, FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Plane]() { SetEditPlane(Plane); })));
    };
    Entry(EStrandEditPlane::Free3D, LOCTEXT("Edit3D", "Free 3D"), LOCTEXT("Edit3DHelp", "Unrestricted X, Y, Z movement."));
    Entry(EStrandEditPlane::XY, LOCTEXT("EditXY", "2D Top (XY)"), LOCTEXT("EditXYHelp", "Edit only X/Y; leave existing Z coordinates untouched."));
    Entry(EStrandEditPlane::XZ, LOCTEXT("EditXZ", "2D Front (XZ)"), LOCTEXT("EditXZHelp", "Edit only X/Z; leave existing Y coordinates untouched."));
    Entry(EStrandEditPlane::YZ, LOCTEXT("EditYZ", "2D Side (YZ)"), LOCTEXT("EditYZHelp", "Edit only Y/Z; leave existing X coordinates untouched."));
    return Menu.MakeWidget();
}

void FStrandSplineAssetEditor::SelectTangentHandle(int32 Point, int32 Handle)
{
    SelectPoint(Point);
    if (!HasSelectedPoint() || Handle < 1 || Handle > 2) { return; }
    SelectedHandle = Handle;
    if (PreviewWidget.IsValid()) { PreviewWidget->SetSelectedHandle(Handle); }
}

void FStrandSplineAssetEditor::ApplyViewportTangentDrag(int32 Handle, const FVector& LocalDelta)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || HasMultipleSelectedPoints() || (Handle != 1 && Handle != 2)
        || LocalDelta.IsNearlyZero() || LocalDelta.ContainsNaN() || !NativePreview.IsValid()) { return; }
    FVector Constrained = LocalDelta;
    const int32 LockedAxis = EditPlane == EStrandEditPlane::XY ? 2 : EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1;
    if (LockedAxis >= 0) { Constrained[LockedAxis] = 0.0; }
    if (Constrained.IsNearlyZero()) { return; }
    if (!ActivePointDragTransaction) { BeginViewportPointDrag(); }
    if (!ActivePointDragTransaction) { return; }
    FSplinePointData& Point = Current->Points[SelectedPointIndex];
    if (Point.PointType != ESplinePointType::CurveCustomTangent)
    {
        const FSplinePoint Evaluated = NativePreview->GetSplinePointAt(SelectedPointIndex, ESplineCoordinateSpace::Local);
        Point.ArriveTangent = Evaluated.ArriveTangent;
        Point.LeaveTangent = Evaluated.LeaveTangent;
        Point.PointType = ESplinePointType::CurveCustomTangent;
    }
    if (Handle == 1) { Point.ArriveTangent -= Constrained * 3.0; }
    else { Point.LeaveTangent += Constrained * 3.0; }
    bPointChangedDuringDrag = true;
    Current->PostEditChange();
    RebuildNativePreview();
}

void FStrandSplineAssetEditor::CommitSplineEdit(int32 NewSelection, bool bPreserveSelection)
{
    if (USplineDataAsset* Current = Asset.Get())
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (bPreserveSelection) { PruneSelection(); }
        else { SelectPoint(NewSelection); }
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
}

FReply FStrandSplineAssetEditor::ExtendStart()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->bIsClosedLoop || Current->Points.Num() < 2 || Current->Points.Num() >= 100000) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ExtendStartUndo", "Extend Strand Spline Start"));
    Current->Modify();
    FSplinePointData First = Current->Points[0];
    const FVector Direction = First.LocalPosition - Current->Points[1].LocalPosition;
    First.LocalPosition += Direction * 0.5;
    First.PointType = ESplinePointType::Curve;
    First.ArriveTangent = FVector::ZeroVector;
    First.LeaveTangent = FVector::ZeroVector;
    Current->Points.Insert(First, 0);
    CommitSplineEdit(0);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ReverseSpline()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->Points.Num() < 2) { return FReply::Handled(); }
    EndViewportPointDrag();
    const int32 OldIndex = SelectedPointIndex;
    const TArray<int32> PreviouslySelected = GetSelectedIndices();
    const FScopedTransaction Transaction(LOCTEXT("ReverseSplineUndo", "Reverse Strand Spline"));
    Current->Modify();
    StrandSplineEditOperations::Reverse(Current->Points, Current->bIsClosedLoop);
    CommitSplineEdit(OldIndex == INDEX_NONE ? INDEX_NONE : Current->Points.Num() - 1 - OldIndex);
    SelectedPointIndices.Reset();
    for (int32 Index : PreviouslySelected) { SelectedPointIndices.Add(Current->Points.Num() - 1 - Index); }
    SelectedPointIndex = OldIndex == INDEX_NONE ? INDEX_NONE : Current->Points.Num() - 1 - OldIndex;
    SyncViewportSelection();
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SetSelectedAsFirst()
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || !Current->bIsClosedLoop || Current->Points.Num() < 2 || SelectedPointIndex == 0)
    {
        return FReply::Handled();
    }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("SetStartSplineUndo", "Set Strand Spline First Point"));
    Current->Modify();
    const TArray<FSplinePointData> Original = Current->Points;
    const int32 Count = Original.Num();
    const int32 NewStart = SelectedPointIndex;
    const TArray<int32> PreviouslySelected = GetSelectedIndices();
    for (int32 I = 0; I < Count; ++I) { Current->Points[I] = Original[(NewStart + I) % Count]; }
    CommitSplineEdit(0);
    SelectedPointIndices.Reset();
    for (int32 Index : PreviouslySelected) { SelectedPointIndices.Add((Index - NewStart + Count) % Count); }
    SelectedPointIndex = 0;
    SyncViewportSelection();
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SnapSelectedPoint(double Step)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || Step <= 0.0 || !FMath::IsFinite(Step)) { return FReply::Handled(); }
    EndViewportPointDrag();
    const TArray<int32> Indices = GetSelectedIndices();
    bool bChanged = false;
    for (int32 Index : Indices)
    {
        const FVector Position = Current->Points[Index].LocalPosition;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (!CanEditAxis(Axis)) { continue; }
            const double Rounded = FMath::RoundHalfFromZero(Position[Axis] / Step) * Step;
            bChanged |= !FMath::IsNearlyEqual(Rounded, Position[Axis], 0.000001);
        }
    }
    if (!bChanged) { return FReply::Handled(); }
    const FScopedTransaction Transaction(LOCTEXT("SnapSplinePointUndo", "Snap Selected Strand Spline Points"));
    Current->Modify();
    for (int32 Index : Indices)
    {
        FVector& Position = Current->Points[Index].LocalPosition;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (CanEditAxis(Axis)) { Position[Axis] = FMath::RoundHalfFromZero(Position[Axis] / Step) * Step; }
        }
    }
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::AlignSelection(int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !CanEditAxis(Axis) || SelectedPointIndices.Num() < 2) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("AlignSelectionUndo", "Align Strand Spline Selection"));
    Current->Modify();
    if (StrandSplineEditOperations::AlignSelected(Current->Points, GetSelectedIndices(), Axis))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::DistributeSelection(int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !CanEditAxis(Axis) || SelectedPointIndices.Num() < 3) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("DistributeSelectionUndo", "Distribute Strand Spline Selection"));
    Current->Modify();
    if (StrandSplineEditOperations::DistributeSelected(Current->Points, GetSelectedIndices(), Axis))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::MatchSelectedTangents(bool bMirror)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || HasMultipleSelectedPoints() || !NativePreview.IsValid()) { return FReply::Handled(); }
    const FSplinePoint Evaluated = NativePreview->GetSplinePointAt(SelectedPointIndex, ESplineCoordinateSpace::Local);
    FSplinePointData Result = Current->Points[SelectedPointIndex];
    if (!StrandSplineEditOperations::MatchTangents(Result, Evaluated.ArriveTangent,
        Evaluated.LeaveTangent, bMirror, SelectedHandle == 1)) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("MatchTangentsUndo", "Match Strand Spline Tangents"));
    Current->Modify();
    Current->Points[SelectedPointIndex] = Result;
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SubdivideSpline()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || Current->Points.Num() < 2 || Current->Points.Num() > 8192)
    {
        return FReply::Handled();
    }
    EndViewportPointDrag();
    const int32 OldIndex = SelectedPointIndex;
    const TArray<int32> PreviouslySelected = GetSelectedIndices();
    const FScopedTransaction Transaction(LOCTEXT("SubdivideSplineUndo", "Subdivide Strand Spline"));
    Current->Modify();
    if (StrandSplineEditOperations::Subdivide(Current->Points, Current->bIsClosedLoop, NativePreview.Get()))
    {
        CommitSplineEdit(OldIndex == INDEX_NONE ? INDEX_NONE : OldIndex * 2);
        SelectedPointIndices.Reset();
        for (int32 Index : PreviouslySelected) { SelectedPointIndices.Add(Index * 2); }
        SelectedPointIndex = OldIndex == INDEX_NONE ? INDEX_NONE : OldIndex * 2;
        SyncViewportSelection();
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::LinearizeSpline()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->Points.IsEmpty()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("LinearSplineUndo", "Linearize Strand Spline"));
    Current->Modify();
    StrandSplineEditOperations::MakeAllLinear(Current->Points);
    CommitSplineEdit(SelectedPointIndex, true);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SmoothSpline()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->Points.IsEmpty()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("SmoothSplineUndo", "Auto-Smooth Strand Spline"));
    Current->Modify();
    StrandSplineEditOperations::MakeAllAuto(Current->Points);
    CommitSplineEdit(SelectedPointIndex, true);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::MirrorSpline(int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->Points.IsEmpty()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("MirrorSplineUndo", "Mirror Strand Spline"));
    Current->Modify();
    StrandSplineEditOperations::Mirror(Current->Points, Axis);
    CommitSplineEdit(SelectedPointIndex, true);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::FlattenSpline(int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->Points.IsEmpty()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FText Warning = LOCTEXT("FlattenSplineWarning", "Flattening projects ALL spline points and tangent handles to the selected plane. This permanently changes the saved spline geometry (but supports Undo). Continue?");
    if (FMessageDialog::Open(EAppMsgType::YesNo, Warning) != EAppReturnType::Yes) { return FReply::Handled(); }
    const FScopedTransaction Transaction(LOCTEXT("FlattenSplineUndo", "Flatten Strand Spline"));
    Current->Modify();
    StrandSplineEditOperations::Flatten(Current->Points, Axis);
    CommitSplineEdit(SelectedPointIndex, true);
    return FReply::Handled();
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeSplineToolsMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto Entry = [&Menu](const FText& Label, const FText& Tip, TFunction<void()> Action)
    {
        const TSharedRef<TFunction<void()>> SharedAction = MakeShared<TFunction<void()>>(MoveTemp(Action));
        Menu.AddMenuEntry(Label, Tip, FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([SharedAction]() { (*SharedAction)(); })));
    };
    Menu.BeginSection(TEXT("SelectionTools"), LOCTEXT("SelectionToolsSection", "Selected Points"));
    Menu.AddSubMenu(LOCTEXT("AlignSubMenu", "Align Points"),
        LOCTEXT("AlignTip", "Move selected anchors to their shared center on one axis. Tangents follow their anchors."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            Sub.AddMenuEntry(LOCTEXT("AlignX", "Align X"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { AlignSelection(0); })));
            Sub.AddMenuEntry(LOCTEXT("AlignY", "Align Y"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { AlignSelection(1); })));
            Sub.AddMenuEntry(LOCTEXT("AlignZ", "Align Z"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { AlignSelection(2); })));
        }));
    Menu.AddSubMenu(LOCTEXT("DistributeSubMenu", "Distribute Points"),
        LOCTEXT("DistributeTip", "Space at least three selected anchors evenly along an axis, ordered by their coordinate."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            Sub.AddMenuEntry(LOCTEXT("DistributeX", "Distribute X"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { DistributeSelection(0); })));
            Sub.AddMenuEntry(LOCTEXT("DistributeY", "Distribute Y"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { DistributeSelection(1); })));
            Sub.AddMenuEntry(LOCTEXT("DistributeZ", "Distribute Z"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { DistributeSelection(2); })));
        }));
    Entry(LOCTEXT("AlignHandles", "Align Primary Tangents"),
        LOCTEXT("AlignHandlesTip", "One-shot tangent alignment preserving each handle length; the selected handle drives the direction."),
        [this]() { MatchSelectedTangents(false); });
    Entry(LOCTEXT("MirrorHandles", "Mirror Primary Tangents"),
        LOCTEXT("MirrorHandlesTip", "One-shot matched tangent directions and lengths. Does not automatically link later edits."),
        [this]() { MatchSelectedTangents(true); });
    Menu.EndSection();
    Menu.BeginSection(TEXT("Build"), LOCTEXT("BuildToolsSection", "Create and Refine"));
    Entry(LOCTEXT("StartPenMenu", "Activate Spline Pen"), LOCTEXT("StartPenTip", "Click to draw splines directly in the viewport."), [this]() { SetPenMode(true); });
    Entry(LOCTEXT("ClearSplineMenu", "Clear All Points"), LOCTEXT("ClearSplineTip", "Remove all points so you can draw a new spline from scratch. Undoable and requires confirmation."), [this]() { ClearSpline(); });
    Entry(LOCTEXT("ExtendStartMenu", "Extend Start"), LOCTEXT("ExtendStartTip", "Add an anchor before the first point of an open spline."), [this]() { ExtendStart(); });
    Entry(LOCTEXT("ExtendEndMenu", "Extend End"), LOCTEXT("ExtendEndTip", "Select the final anchor then Insert to extend an open spline."),
        [this]() { if (USplineDataAsset* Current = Asset.Get()) { if (!Current->bIsClosedLoop && Current->Points.Num() >= 2) { SelectPoint(Current->Points.Num() - 1); AddPointAfterSelection(); } } });
    Entry(LOCTEXT("SubdivideMenu", "Subdivide All Segments"), LOCTEXT("SubdivideTip", "Insert an anchor halfway along every segment, preserving evaluated cubic geometry."), [this]() { SubdivideSpline(); });
    Entry(LOCTEXT("ReverseMenu", "Reverse Direction"), LOCTEXT("ReverseTip", "Reverse point order, with inverted incoming/outgoing handles."), [this]() { ReverseSpline(); });
    Entry(LOCTEXT("FirstPointMenu", "Set Selected as First (Closed)"), LOCTEXT("FirstPointTip", "For closed loops, change the starting anchor without changing the shape."), [this]() { SetSelectedAsFirst(); });
    Menu.AddSubMenu(LOCTEXT("SnapPointMenu", "Snap Selected Points"), LOCTEXT("SnapPointTip", "Snap all selected point positions to a grid increment (cm). Active 2D editing plane is respected."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            Sub.AddMenuEntry(LOCTEXT("Snap1", "1 cm"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SnapSelectedPoint(1.0); })));
            Sub.AddMenuEntry(LOCTEXT("Snap10", "10 cm"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SnapSelectedPoint(10.0); })));
            Sub.AddMenuEntry(LOCTEXT("Snap100", "100 cm"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SnapSelectedPoint(100.0); })));
        }));
    Entry(LOCTEXT("ToggleClosedMenu", "Toggle Closed Loop"), LOCTEXT("ToggleClosedTip", "Join or separate the spline endpoints."), [this]() { ToggleClosedLoop(); });
    Menu.EndSection();
    Menu.BeginSection(TEXT("Shape"), LOCTEXT("ShapeToolsSection", "Shape"));
    Entry(LOCTEXT("LinearAllMenu", "Linearize All"), LOCTEXT("LinearAllTip", "Turn all segments into straight edges. This changes the geometry."), [this]() { LinearizeSpline(); });
    Entry(LOCTEXT("AutoAllMenu", "Auto-Smooth All"), LOCTEXT("AutoAllTip", "Use Unreal's native automatic cubic tangents on every point. This changes the geometry."), [this]() { SmoothSpline(); });
    Menu.AddSubMenu(LOCTEXT("MirrorSubMenu", "Mirror"), LOCTEXT("MirrorTip", "Reflect positions and tangents around the spline's control-point bounds center."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            Sub.AddMenuEntry(LOCTEXT("MirrorX", "Mirror X"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { MirrorSpline(0); })));
            Sub.AddMenuEntry(LOCTEXT("MirrorY", "Mirror Y"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { MirrorSpline(1); })));
            Sub.AddMenuEntry(LOCTEXT("MirrorZ", "Mirror Z"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { MirrorSpline(2); })));
        }));
    Menu.AddSubMenu(LOCTEXT("FlattenSubMenu", "Flatten to Plane"), LOCTEXT("FlattenTip", "Project points AND handles to a 2D plane. Requires confirmation; Undo supported."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            Sub.AddMenuEntry(LOCTEXT("FlattenXY", "Top (XY), discard Z"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { FlattenSpline(2); })));
            Sub.AddMenuEntry(LOCTEXT("FlattenXZ", "Front (XZ), discard Y"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { FlattenSpline(1); })));
            Sub.AddMenuEntry(LOCTEXT("FlattenYZ", "Side (YZ), discard X"), FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { FlattenSpline(0); })));
        }));
    Menu.EndSection();
    return Menu.MakeWidget();
}


void FStrandSplineAssetEditor::SetPenMode(bool bEnabled)
{
    if (!bEnabled) { EndPenStroke(); }
    EndViewportPointDrag();
    if (bEnabled && HasMultipleSelectedPoints()) { SelectPoint(SelectedPointIndex); }
    bPenMode = bEnabled;
    if (PreviewWidget.IsValid())
    {
        PreviewWidget->SetPenMode(bEnabled);
        if (bEnabled) { PreviewWidget->SetShowControlPoints(true); }
    }
}

void FStrandSplineAssetEditor::SetPenSnapStep(double Step)
{
    PenSnapStep = Step;
    if (PreviewWidget.IsValid()) { PreviewWidget->SetPenSnap(Step); }
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakePenSnapMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto Add = [this, &Menu](double Step, const FText& Label)
    {
        Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Step]() { SetPenSnapStep(Step); })));
    };
    Add(0.0, LOCTEXT("PenSnapOff", "No Snapping"));
    Add(1.0, LOCTEXT("PenSnapOne", "1 cm"));
    Add(10.0, LOCTEXT("PenSnapTen", "10 cm"));
    Add(100.0, LOCTEXT("PenSnapHundred", "100 cm"));
    return Menu.MakeWidget();
}

FText FStrandSplineAssetEditor::GetPenSnapLabel() const
{
    return PenSnapStep > 0.0
        ? FText::FromString(FString::Printf(TEXT("Snap: %.0f cm"), PenSnapStep))
        : LOCTEXT("PenSnapNone", "Snap: Off");
}

FText FStrandSplineAssetEditor::GetEditorHint() const
{
    if (bPenMode)
    {
        if (const USplineDataAsset* Current = Asset.Get())
        {
            if (Current->Points.Num() > 4096)
            {
                return LOCTEXT("PenDenseStatus", "Pen  |  Curve segment hit targets disabled above 4096 points  |  Endpoint extension available");
            }
        }
        return LOCTEXT("PenModeStatus", "Pen  |  Click: draw / insert  |  Drag: handles  |  Endpoint: extend / close  |  Esc: Select");
    }
    return LOCTEXT("SelectModeStatus", "Select  |  Click: single  |  Shift-click: range  |  Ctrl-click: toggle  |  Ctrl+A: all  |  Esc: clear");
}

FReply FStrandSplineAssetEditor::ClearSpline()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || Current->Points.IsEmpty()) { return FReply::Handled(); }
    if (FMessageDialog::Open(EAppMsgType::YesNo,
        LOCTEXT("ConfirmClearSpline", "Remove every spline point? The asset will be empty until you draw new points. You can Undo this action."))
        != EAppReturnType::Yes) { return FReply::Handled(); }
    EndPenStroke();
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ClearStrandSplineUndo", "Clear Strand Spline Points"));
    Current->Modify();
    Current->Points.Reset();
    Current->bIsClosedLoop = false;
    CommitSplineEdit(INDEX_NONE);
    if (PreviewWidget.IsValid()) { PreviewWidget->FrameSpline(); }
    return FReply::Handled();
}

bool FStrandSplineAssetEditor::BeginPenStroke(const FStrandPenHit& Hit)
{
    USplineDataAsset* Current = Asset.Get();
    if (!bPenMode || !Current || !NativePreview.IsValid()) { return false; }
    EndViewportPointDrag();
    const int32 Count = Current->Points.Num();
    if (Hit.PointIndex != INDEX_NONE && Current->Points.IsValidIndex(Hit.PointIndex))
    {
        // Click the opposite endpoint to close an open path, or click the same
        // endpoint to choose which end the next pen click should extend.
        const bool bOppositeEnds = Count >= 3 && !Current->bIsClosedLoop &&
            ((SelectedPointIndex == 0 && Hit.PointIndex == Count - 1) ||
             (SelectedPointIndex == Count - 1 && Hit.PointIndex == 0));
        if (bOppositeEnds)
        {
            ToggleClosedLoop();
            SetPenMode(false);
        }
        else { SelectPoint(Hit.PointIndex); }
        return false;
    }
    if (Current->bIsClosedLoop && Hit.SegmentIndex == INDEX_NONE) { return false; }
    if (Count >= 100000) { return false; }
    const bool bPrepend = Count > 1 && SelectedPointIndex == 0;
    PenPreviousPoints = Current->Points;
    bPenPreviousClosed = Current->bIsClosedLoop;
    ActivePenTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("StrandPenStrokeUndo", "Draw Strand Spline Point"));
    Current->Modify();
    if (Hit.SegmentIndex != INDEX_NONE)
    {
        PenEditingPoint = StrandSplineEditOperations::InsertOnSegment(Current->Points,
            Current->bIsClosedLoop, NativePreview.Get(), Hit.SegmentIndex, Hit.SegmentAlpha);
    }
    else
    {
        PenEditingPoint = StrandSplineEditOperations::AppendPenPoint(Current->Points,
            Hit.Position, bPrepend, Current->bIsClosedLoop);
    }
    if (PenEditingPoint == INDEX_NONE)
    {
        ActivePenTransaction->Cancel();
        ActivePenTransaction.Reset();
        PenPreviousPoints.Reset();
        return false;
    }
    PenPointBeforeHandleDrag = Current->Points[PenEditingPoint];
    LastPenHandleOffset = FVector::ZeroVector;
    bPenHandleMoved = false;
    Current->PostEditChange();
    RebuildNativePreview();
    SelectPoint(PenEditingPoint);
    return true;
}

void FStrandSplineAssetEditor::UpdatePenStroke(const FVector& HandleOffset)
{
    USplineDataAsset* Current = Asset.Get();
    if (!ActivePenTransaction || !Current || PenEditingPoint == INDEX_NONE ||
        HandleOffset.ContainsNaN() ||
        (bPenHandleMoved && HandleOffset.Equals(LastPenHandleOffset, 0.01))) { return; }
    if (!bPenHandleMoved && HandleOffset.IsNearlyZero(0.01)) { return; }
    FVector Offset = HandleOffset;
    const int32 LockedAxis = EditPlane == EStrandEditPlane::XY ? 2 : EditPlane == EStrandEditPlane::XZ ? 1
        : EditPlane == EStrandEditPlane::YZ ? 0 : -1;
    if (LockedAxis >= 0) { Offset[LockedAxis] = 0.0; }
    if (Offset.IsNearlyZero(0.01))
    {
        // Returning to the press position must restore an exact inserted
        // segment, rather than destroying its carefully split tangents.
        Current->Points[PenEditingPoint] = PenPointBeforeHandleDrag;
    }
    else if (!StrandSplineEditOperations::SetPenHandles(Current->Points, PenEditingPoint, Offset))
    {
        return;
    }
    LastPenHandleOffset = HandleOffset;
    bPenHandleMoved = true;
    Current->PostEditChange();
    RebuildNativePreview();
}

void FStrandSplineAssetEditor::EndPenStroke()
{
    if (!ActivePenTransaction) { return; }
    if (USplineDataAsset* Current = Asset.Get())
    {
        Current->MarkPackageDirty();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    ActivePenTransaction.Reset();
    PenPreviousPoints.Reset();
    PenEditingPoint = INDEX_NONE;
    bPenHandleMoved = false;
}

void FStrandSplineAssetEditor::CancelPenStroke()
{
    if (!ActivePenTransaction) { return; }
    if (USplineDataAsset* Current = Asset.Get())
    {
        Current->Points = MoveTemp(PenPreviousPoints);
        Current->bIsClosedLoop = bPenPreviousClosed;
        ActivePenTransaction->Cancel();
        ActivePenTransaction.Reset();
        PenEditingPoint = INDEX_NONE;
        bPenHandleMoved = false;
        Current->PostEditChange();
        RebuildNativePreview();
        SelectPoint(INDEX_NONE);
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
}

FText FStrandSplineAssetEditor::GetSummary() const
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current) { return LOCTEXT("NotAvailable", "Spline unavailable"); }
    const double Length = NativePreview.IsValid() ? NativePreview->GetSplineLength() : 0.0;
    return FText::FromString(FString::Printf(TEXT("%d points   |   %.1f cm   |   %s   |   %d selected"),
        Current->Points.Num(), Length, Current->bIsClosedLoop ? TEXT("Closed") : TEXT("Open"), SelectedPointIndices.Num()));
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
