#include "StrandSplineAssetEditor.h"
#include "StrandEditorPreferences.h"
#include "StrandCameraSpaceMath.h"
#include "StrandPrecisionOperations.h"
#include "StrandPerspectiveFlatten.h"
#include "StrandSplineViewport.h"
#include "SplineDataAsset.h"
#include "SplineDataAssetFactory.h"
#include "SplineEditorCommands.h"
#include "StrandSplineExport.h"
#include "StrandSplineEditOperations.h"
#include "StrandSplineOrientationOperations.h"
#include "StrandSplineCleanupOperations.h"
#include "StrandSplineSelectionUtils.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
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
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Math/RotationMatrix.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "Framework/Commands/UIAction.h"
#include "ScopedTransaction.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "StrandSplineAssetEditor"

namespace
{
    const FName PreviewTabId(TEXT("StrandSplineEditor_Preview"));
    const FName DetailsTabId(TEXT("StrandSplineEditor_Details"));
    const FName SourceTabId(TEXT("StrandSplineEditor_Source"));

    // Reuse Strand's existing registered editor style. It is created and
    // unregistered by StrandEditorModule, just like the asset/action icons.
    // Resolving an absent engine brush key produced checkerboards in UE 5.8.
    const FSlateBrush* AuthoringBrush(const FName& Icon)
    {
        if (Icon.ToString().StartsWith(TEXT("Strand.")))
        {
            const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(FName(TEXT("StrandEditorStyle")));
            return Style ? Style->GetBrush(Icon) : FAppStyle::GetBrush(TEXT("Icons.Edit"));
        }
        return FAppStyle::GetBrush(Icon);
    }

    // Top-row icons are correct because they sit in an independent, centered
    // overlay above a blank native control. The left modeling rail must use the
    // SAME layout. Putting art inside an SButton/SCheckBox compresses it through
    // content padding (as verified by comparing rendered buttons at 1:1 scale).
    constexpr float RailIconPixels = 21.0f;
    constexpr float DropdownIconPixels = 16.0f;
    constexpr float DropdownChevronPixels = 9.0f;

    TSharedRef<SWidget> RailIconControl(const TSharedRef<SWidget>& ActionSurface, const FName Icon)
    {
        return SNew(SOverlay)
            + SOverlay::Slot()
            [ActionSurface]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(SBox)
                .WidthOverride(RailIconPixels).HeightOverride(RailIconPixels)
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                .Visibility(EVisibility::HitTestInvisible)
                [SNew(SImage)
                    .Image(AuthoringBrush(Icon))
                    .DesiredSizeOverride(FVector2D(RailIconPixels, RailIconPixels))
                    .ColorAndOpacity_Lambda([ActionSurface]()
                    {
                        return ActionSurface->IsEnabled() ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.45f);
                    })]];
    }

    // A combo button has a native chevron on its right. Position the art at
    // the visual center of the *remaining* left-hand action region, without
    // running it through SComboButton's compressed content padding.
    // Dropdowns deliberately use a smaller icon than ordinary tools.
    // Draw BOTH the glyph and chevron in independent hit-test-invisible
    // layers. Native combo arrow padding varies with UE styles / DPI and
    // previously squeezed the icon or pushed the arrow away from the edge.
    TSharedRef<SWidget> DropdownControl(const TSharedRef<SWidget>& ActionSurface,
        const FName Icon, float GlyphLeft, float ArrowRight)
    {
        const auto Tint = [ActionSurface]() -> FSlateColor
        {
            return ActionSurface->IsEnabled() ? FSlateColor(FLinearColor::White) :
                FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.45f));
        };
        return SNew(SOverlay)
            + SOverlay::Slot()[ActionSurface]
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center)
                .Padding(GlyphLeft, 0.0f, 0.0f, 0.0f)
                [SNew(SBox).WidthOverride(DropdownIconPixels).HeightOverride(DropdownIconPixels)
                    .Visibility(EVisibility::HitTestInvisible)
                    [SNew(SImage).Image(AuthoringBrush(Icon))
                        .DesiredSizeOverride(FVector2D(DropdownIconPixels, DropdownIconPixels))
                        .ColorAndOpacity_Lambda(Tint)]]
            + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, ArrowRight, 0.0f)
                [SNew(SBox).WidthOverride(DropdownChevronPixels).HeightOverride(DropdownChevronPixels)
                    .Visibility(EVisibility::HitTestInvisible)
                    [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.DropdownChevron")))
                        .DesiredSizeOverride(FVector2D(DropdownChevronPixels, DropdownChevronPixels))
                        .ColorAndOpacity_Lambda(Tint)]];
    }

    TSharedRef<SWidget> RailDropdownControl(const TSharedRef<SWidget>& ActionSurface, const FName Icon)
    {
        return DropdownControl(ActionSurface, Icon, 8.0f, 5.0f);
    }

    TSharedRef<SWidget> TopDropdownControl(const TSharedRef<SWidget>& ActionSurface, const FName Icon)
    {
        return DropdownControl(ActionSurface, Icon, 8.0f, 5.0f);
    }

    TSharedRef<SWidget> ActionContent(const FName Icon, const FText& Label)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
              [SNew(SImage).Image(AuthoringBrush(Icon)).DesiredSizeOverride(FVector2D(16.0f, 16.0f))]
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

FStrandSplineAssetEditor::~FStrandSplineAssetEditor()
{
    if (TSharedPtr<SWindow> Window = ActiveSnapWindow.Pin())
    { Window->RequestDestroyWindow(); }
    if (PreviewWidget.IsValid()) { PreviewWidget->SetSnapPreviewSegments({}); }
    if (TSharedPtr<SWindow> Window = ActiveCleanupWindow.Pin())
    { Window->RequestDestroyWindow(); }
    if (TSharedPtr<SWindow> Window = ActiveFlattenWindow.Pin())
    { Window->RequestDestroyWindow(); }
    if (PreviewWidget.IsValid()) { PreviewWidget->SetCleanupPreviewPolyline({}); }
}

void FStrandSplineAssetEditor::Init(USplineDataAsset* InAsset, const TSharedPtr<IToolkitHost>& InHost)
{
    check(InAsset);
    Asset = InAsset;
    // Editor-only per-user setting, deliberately not stored on the shared spline asset.
    GConfig->GetInt(TEXT("StrandSplineEditor"), TEXT("LastResamplePointCount"),
        ResampleTargetCount, GEditorPerProjectIni);
    ResampleTargetCount = FMath::Clamp(ResampleTargetCount, 2, 4096);
    GConfig->GetInt(TEXT("StrandSplineEditor"), TEXT("LastExactSubdivideCuts"), SubdivideCuts, GEditorPerProjectIni);
    SubdivideCuts = FMath::Clamp(SubdivideCuts, 1, 32);
    GConfig->GetDouble(TEXT("StrandSplineEditor"), TEXT("PrecisionSnapToleranceCm"),
        PrecisionSnapToleranceCm, GEditorPerProjectIni);
    PrecisionSnapToleranceCm = FMath::IsFinite(PrecisionSnapToleranceCm)
        ? FMath::Clamp(PrecisionSnapToleranceCm, 0.001, 100000.0) : 100.0;
    GConfig->GetBool(TEXT("StrandSplineEditor"), TEXT("InteractiveAnchorSnap"),
        bInteractiveAnchorSnap, GEditorPerProjectIni);
    GConfig->GetInt(TEXT("StrandSplineEditor"), TEXT("InteractiveSnapTargetMode"),
        InteractiveSnapTargetMode, GEditorPerProjectIni);
    InteractiveSnapTargetMode = FMath::Clamp(InteractiveSnapTargetMode, 0, 2);
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
            // Point geometry and loop state are edited in Spline Details/viewport,
            // not exposed redundantly under the Source & Import tab.
            if (PropertyAndParent.Property.GetFName() == GET_MEMBER_NAME_CHECKED(USplineDataAsset, Points) ||
                PropertyAndParent.Property.GetFName() == GET_MEMBER_NAME_CHECKED(USplineDataAsset, bIsClosedLoop))
            {
                return false;
            }
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

    // UE 5.8 renders the asset toolbar in the standalone toolkit host, not as
    // a dockable tab. GetToolbarTabId() now returns NAME_None and is deprecated.
    // Keep only actual dock tabs in this layout; the standalone host renders
    // the native toolbar separately from the tab manager.
    // v9 replaces the previously persisted v8 layout containing the invalid tab.
    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("StrandSplineEditor_Layout_v9")
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Horizontal)
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.71f)
                    ->AddTab(PreviewTabId, ETabState::OpenedTab)->SetHideTabWell(true))
                ->Split(FTabManager::NewStack()->SetSizeCoefficient(0.29f)
                    ->AddTab(DetailsTabId, ETabState::OpenedTab)
                    ->AddTab(SourceTabId, ETabState::OpenedTab)
                    ->SetForegroundTab(DetailsTabId)
                    ->SetHideTabWell(false)));
    // The UE 5.8 toolkit builds the native toolbar during InitAssetEditor.
    // Strand actions are installed in PostRegenerateMenusAndToolbars(), because
    // the base RegenerateMenusAndToolbars() clears ToolbarWidgets each time.
    TArray<UObject*> ObjectsToEdit;
    ObjectsToEdit.Add(InAsset);
    InitAssetEditor(InHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone,
        InHost, TEXT("StrandSplineEditor"), Layout, true, true, ObjectsToEdit);

    // Refresh once after initialization. Our post-regeneration hook installs
    // the right-aligned Strand actions after the base toolkit clears its widget
    // list, then regenerates the final native toolbar before host attachment.
    RegenerateMenusAndToolbars();
}

void FStrandSplineAssetEditor::PostRegenerateMenusAndToolbars()
{
    // Source verified against UE 5.8 AssetEditorToolkit.cpp:
    // RegenerateMenusAndToolbars() first calls RemoveAllToolbarWidgets(), then
    // GenerateToolbar(), then this hook, and finally host->SetToolbar(Toolbar).
    // The second GenerateToolbar() includes the Strand action group in the
    // dedicated right-aligned MiscWidgets slot beside Save/Browse.
    AddToolbarWidget(BuildAssetActionToolbar());
    GenerateToolbar();
}

void FStrandSplineAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
    InTabManager->RegisterTabSpawner(PreviewTabId, FOnSpawnTab::CreateSP(this, &FStrandSplineAssetEditor::SpawnPreviewTab))
        .SetDisplayName(LOCTEXT("PreviewTab", "Spline Preview"));
    InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FStrandSplineAssetEditor::SpawnDetailsTab))
        .SetDisplayName(LOCTEXT("DetailsTab", "Spline Details"));
    InTabManager->RegisterTabSpawner(SourceTabId, FOnSpawnTab::CreateSP(this, &FStrandSplineAssetEditor::SpawnSourceTab))
        .SetDisplayName(LOCTEXT("SourceTab", "Source & Import"));
}

void FStrandSplineAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    InTabManager->UnregisterTabSpawner(PreviewTabId);
    InTabManager->UnregisterTabSpawner(DetailsTabId);
    InTabManager->UnregisterTabSpawner(SourceTabId);
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

UToolMenu* FStrandSplineAssetEditor::GenerateCommonActionsToolbar(FToolMenuContext& MenuContext)
{
    // Preserve the standard Save/Browse section. Strand actions are installed
    // into the right-hand widget slot during PostRegenerateMenusAndToolbars().
    return FAssetEditorToolkit::GenerateCommonActionsToolbar(MenuContext);
}

TSharedRef<SWidget> FStrandSplineAssetEditor::BuildAssetActionToolbar()
{
    return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton)
                .ToolTipText(LOCTEXT("NewSplineTip", "Create and open an independent, editable Strand Spline asset in the same Content Browser folder."))
                .OnClicked(this, &FStrandSplineAssetEditor::NewSplineAsset)
                [ActionContent(TEXT("Icons.Plus"), LOCTEXT("NewSpline", "New Spline"))]]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SComboButton)
                .ToolTipText(LOCTEXT("PrimitivesTip", "Create a NEW, separate Strand spline asset (Circle, Rectangle, or Helix) in the current Content Browser folder. Does not modify the asset currently open."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakePrimitiveMenu)
                .ButtonContent()[ActionContent(TEXT("Strand.Primitives"), LOCTEXT("PrimitivesLabel", "Primitives"))]]
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
                .ButtonContent()[ActionContent(TEXT("Strand.Actors"), LOCTEXT("ActorsButton", "Actors"))]];
}

TSharedRef<SDockTab> FStrandSplineAssetEditor::SpawnPreviewTab(const FSpawnTabArgs& Args)
{
    TSharedRef<SDockTab> Tab = SNew(SDockTab).Label(LOCTEXT("PreviewTab", "Viewport"))
        .ToolTipText(LOCTEXT("ViewportTabTip", "Edit the Strand spline in a native Unreal 3D preview. RMB+WASD flies the camera; Space cycles transforms."))
    [
        SNew(SVerticalBox)
        // Compact tool strips mirror familiar 3D/curve editors: selection on
        // the upper left, transform/display on upper right, modeling down the
        // left edge. Every icon has a descriptive tooltip and keyboard shortcut.
        + SVerticalBox::Slot().AutoHeight().Padding(10.5f, 3.0f, 7.0f, 3.0f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return !bPenMode && !bMarqueeMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { SetPenMode(false); SetMarqueeMode(false); })
                .ToolTipText(LOCTEXT("DirectSelectionTip", "Select anchors, segments, and tangent handles (V). Shift-click segments to insert; Ctrl-click anchors to toggle."))
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.Select")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return bMarqueeMode && !bLassoMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetMarqueeMode(State == ECheckBoxState::Checked); })
                .ToolTipText(LOCTEXT("BoxSelectIconTip", "Box selection tool (B). Drag to select; Shift adds, Ctrl toggles."))
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.BoxSelect")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                        .IsChecked_Lambda([this]() { return bLassoMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetLassoMode(State == ECheckBoxState::Checked); })
                        .ToolTipText(LOCTEXT("LassoSelectIconTip", "Lasso selection (Shift+B). Draw around anchors; Shift adds, Ctrl toggles."))
                        [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.Lasso")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 4.0f)
            [SNew(SSeparator).Orientation(Orient_Vertical)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("SelectAllIconTip", "Select all control points (Ctrl+A)."))
                .OnClicked(this, &FStrandSplineAssetEditor::SelectAllPoints)
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.SelectAll")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("InvertSelectionIconTip", "Invert anchor selection."))
                .OnClicked(this, &FStrandSplineAssetEditor::InvertPointSelection)
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.Invert")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("ClearSelectionIconTip", "Clear selected anchors (Esc)."))
                .OnClicked(this, &FStrandSplineAssetEditor::ClearPointSelection)
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.ClearSelection")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [SNew(SSpacer)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode && ActiveWidgetMode == 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .IsEnabled_Lambda([this]() { return HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { SetWidgetMode(0); })
                .ToolTipText(LOCTEXT("MoveIconTip", "Move using the native Unreal gizmo (Space cycles Move, Rotate, Scale)."))
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.Move")))
                            .ColorAndOpacity_Lambda([this]() { return (HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode)
                                ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.32f)); })
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode && ActiveWidgetMode == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .IsEnabled_Lambda([this]() { return HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { SetWidgetMode(1); })
                .ToolTipText(LOCTEXT("RotateIconTip", "Rotate using the native Unreal gizmo (Space cycles Move, Rotate, Scale)."))
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.Rotate")))
                            .ColorAndOpacity_Lambda([this]() { return (HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode)
                                ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.32f)); })
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode && ActiveWidgetMode == 2 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .IsEnabled_Lambda([this]() { return HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { SetWidgetMode(2); })
                .ToolTipText(LOCTEXT("ScaleIconTip", "Scale using the native Unreal gizmo (Space cycles Move, Rotate, Scale)."))
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.Scale")))
                            .ColorAndOpacity_Lambda([this]() { return (HasSelectedPoint() && PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints() && !bPenMode && !bMarqueeMode)
                                ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.32f)); })
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 4.0f)
            [SNew(SSeparator).Orientation(Orient_Vertical)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return !bWidgetLocal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { SetWidgetLocal(false); })
                .ToolTipText(LOCTEXT("WorldIconTip", "World axes. Local X follows the selected anchor outgoing tangent."))
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.World")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return bWidgetLocal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { SetWidgetLocal(true); })
                .ToolTipText(LOCTEXT("LocalIconTip", "Local axes. Local X follows the selected anchor outgoing tangent."))
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.Local")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
]
            // Flexible space on both sides centers the transform group while
            // retaining the selection tools on the left and view tools right.
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [SNew(SSpacer)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
            [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [TopDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f)).ToolTipText(LOCTEXT("ProjectionIconTip", "Choose perspective or orthographic viewport."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeProjectionMenu)
                .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.View")))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [TopDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f)).ToolTipText(LOCTEXT("EditPlaneIconTip", "Choose free 3D or XY, XZ, YZ constrained editing."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeEditPlaneMenu)
                .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.Plane")))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [TopDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f)).IsEnabled_Lambda([this]() { return bPenMode; })
                .ToolTipText(LOCTEXT("SnapIconTip", "Pen snapping distance in centimeters."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakePenSnapMenu)
                .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.Snap")))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("FrameAllIconTip", "Center and frame the entire evaluated spline with comfortable viewport margins (F). Keeps the current viewing direction."))
                .OnClicked_Lambda([this]() { if (PreviewWidget.IsValid()) { PreviewWidget->FrameSpline(); } return FReply::Handled(); })
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.FrameAll")))
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("FrameSelectedIconTip", "Frame the primary selected anchor."))
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .OnClicked_Lambda([this]() { if (PreviewWidget.IsValid()) { PreviewWidget->FrameSelectedPoint(); } return FReply::Handled(); })
                [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(21.0f).HeightOverride(21.0f)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).Image(AuthoringBrush(TEXT("Strand.FrameSelected")))
                            .ColorAndOpacity_Lambda([this]() { return (HasSelectedPoint())
                                ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.32f)); })
                            .DesiredSizeOverride(FVector2D(21.0f, 21.0f))]]]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 4.0f)
            [SNew(SSeparator).Orientation(Orient_Vertical)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [TopDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f))
                    .ToolTipText(LOCTEXT("ViewportSettingsTip", "Viewport options: show or hide points, grid, and tangent handles."))
                    .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeViewportOptionsMenu)
                    .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.ViewportSettings")))]]
            ]
        ]
        + SVerticalBox::Slot().FillHeight(1.0f).Padding(6.0f, 0.0f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 5.0f, 5.0f, 0.0f)
            [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(1.0f, 4.0f)
            [SNew(SSeparator)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailIconControl(SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return bPenMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetPenMode(State == ECheckBoxState::Checked); })
                .ToolTipText(LOCTEXT("DrawPenTip", "Pen tool (P). Click to create anchors; drag to create Bezier handles. Esc to finish."))
                [SNullWidget::NullWidget], FName(TEXT("Strand.Pen")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(1.0f, 5.0f)
            [SNew(SSeparator)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailIconControl(SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("StrandInsertTip", "Insert a point after the selected anchor."))
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .OnClicked(this, &FStrandSplineAssetEditor::AddPointAfterSelection)
                [SNullWidget::NullWidget], FName(TEXT("Strand.Insert")))]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailIconControl(SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("StrandDeleteTip", "Delete selected spline anchors (Delete)."))
                .IsEnabled(this, &FStrandSplineAssetEditor::CanDeleteSelection)
                .OnClicked(this, &FStrandSplineAssetEditor::RemoveSelectedPoint)
                [SNullWidget::NullWidget], FName(TEXT("Strand.Delete")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(1.0f, 5.0f)
            [SNew(SSeparator)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailIconControl(SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("StrandSubdivideTip", "Subdivide all spline segments, preserving curve shape."))
                .OnClicked(this, &FStrandSplineAssetEditor::SubdivideSpline)
                [SNullWidget::NullWidget], FName(TEXT("Strand.Subdivide")))]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailIconControl(SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("StrandSmoothTip", "Auto-smooth the spline tangents."))
                .OnClicked(this, &FStrandSplineAssetEditor::SmoothSpline)
                [SNullWidget::NullWidget], FName(TEXT("Strand.Smooth")))]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailIconControl(SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("StrandLinearTip", "Linearize all segments."))
                .OnClicked(this, &FStrandSplineAssetEditor::LinearizeSpline)
                [SNullWidget::NullWidget], FName(TEXT("Strand.Linear")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(1.0f, 5.0f)
            [SNew(SSeparator)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailIconControl(SNew(SButton).ContentPadding(FMargin(2.0f))
                .ToolTipText(LOCTEXT("StrandReverseTip", "Reverse spline point order and tangents."))
                .OnClicked(this, &FStrandSplineAssetEditor::ReverseSpline)
                [SNullWidget::NullWidget], FName(TEXT("Strand.Reverse")))]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(36.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [SNew(SOverlay)
                    + SOverlay::Slot()
                    [SNew(SButton).ContentPadding(FMargin(2.0f))
                        .ToolTipText_Lambda([this]() { return Asset.IsValid() && Asset->bIsClosedLoop
                            ? LOCTEXT("StrandOpenLoopTip", "Open spline: break the connection between the final and first anchor. Undoable.")
                            : LOCTEXT("StrandCloseLoopTip", "Close spline: connect the final anchor to the first. Undoable."); })
                        .OnClicked(this, &FStrandSplineAssetEditor::ToggleClosedLoop)
                        [SNullWidget::NullWidget]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(RailIconPixels).HeightOverride(RailIconPixels)
                        .HAlign(HAlign_Center).VAlign(VAlign_Center)
                        .Visibility(EVisibility::HitTestInvisible)
                        [SNew(SImage).DesiredSizeOverride(FVector2D(RailIconPixels, RailIconPixels))
                            .Image_Lambda([this]() { return AuthoringBrush(Asset.IsValid() && Asset->bIsClosedLoop
                                ? FName(TEXT("Strand.OpenPath")) : FName(TEXT("Strand.ClosedPath"))); })]]]]
            + SVerticalBox::Slot().AutoHeight().Padding(1.0f, 5.0f)
            [SNew(SSeparator)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f))
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .ToolTipText(LOCTEXT("HandleQuickToolTip", "Bezier handles: Free, Aligned, Mirrored, Auto, or Corner. Choose a persistent handle constraint for selected anchors."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeHandleModeMenu)
                .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.Handles")))]]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f))
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .ToolTipText(LOCTEXT("InterpolationQuickToolTip", "Change selected anchor interpolation: Linear, Curve, Curve Clamped, Constant or Custom."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakePointTypeMenu)
                .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.Interpolation")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(1.0f, 5.0f)
            [SNew(SSeparator)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(1.5f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f))
                .ToolTipText(LOCTEXT("ReplacePrimitiveStripTip", "Replace THIS asset's spline with a Circle, Rectangle, or Helix. Supports Undo/Redo; removes any external-source reimport link."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeReplacePrimitiveMenu)
                .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.ReplaceShape")))]]
            + SVerticalBox::Slot().AutoHeight().Padding(4.0f, 8.0f)
            [SNew(SBox).WidthOverride(45.0f).HeightOverride(27.0f)
                .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
                [RailDropdownControl(SNew(SComboButton).HasDownArrow(false).ContentPadding(FMargin(0.0f)).ToolTipText(LOCTEXT("MoreAuthoringTip", "More spline modeling operations, tangent and point tools."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeSplineToolsMenu)
                .ButtonContent()[SNullWidget::NullWidget], FName(TEXT("Strand.More")))]]]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [SAssignNew(PreviewWidget, SStrandSplineViewport)]
        ]
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
        PreviewWidget->SetOnSegmentInsert([this](int32 Segment, double Alpha) { InsertPointOnViewportSegment(Segment, Alpha); });
        PreviewWidget->SetSegmentBendCallbacks(
            [this](int32 Segment, double Alpha) { BeginViewportSegmentBend(Segment, Alpha); },
            [this](const FVector& Delta) { UpdateViewportSegmentBend(Delta); },
            [this]() { EndViewportSegmentBend(); });
        PreviewWidget->SetOnContextMenu([this](int32 Point, int32 Segment, double Alpha)
        {
            return MakeViewportContextMenu(Point, Segment, Alpha);
        });
        PreviewWidget->SetOnNudge([this](const FVector& Delta) { NudgeSelectedPoints(Delta); });
        PreviewWidget->SetOnMarqueeSelected([this](const TArray<int32>& Points, bool bAdd, bool bToggle)
            { SelectMarqueePoints(Points, bAdd, bToggle); });
        PreviewWidget->SetOnMarqueeExit([this]() { SetMarqueeMode(false); });
        PreviewWidget->SetPointRotationCallback([this](const FQuat& Delta) { ApplyViewportRotation(Delta); });
        PreviewWidget->SetTangentRollCallback([this](double Degrees) { ApplyViewportTangentRoll(Degrees); });
        PreviewWidget->SetPointScaleCallback([this](double Factor) { ApplyViewportScale(Factor); });
        PreviewWidget->SetOnWidgetModeChanged([this](int32 Mode) { SetWidgetMode(Mode); });
        PreviewWidget->SetOnToolShortcut([this](int32 Tool)
        {
            if (Tool == 1) { SetPenMode(true); }
            else if (Tool == 2) { SetMarqueeMode(true); }
            else if (Tool == 3) { SetLassoMode(true); }
            else { SetPenMode(false); SetMarqueeMode(false); }
        });
        PreviewWidget->SetOnDeleteShortcut([this]() { RemoveSelectedPoint(); });
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
        if (bLassoMode) { PreviewWidget->SetLassoMode(true); }
        else { PreviewWidget->SetMarqueeMode(bMarqueeMode); }
        PreviewWidget->SetWidgetMode(ActiveWidgetMode);
        PreviewWidget->SetWidgetLocal(bWidgetLocal);
        PreviewWidget->SetCameraAlignedEditing(EditPlane == EStrandEditPlane::CameraPlane);
        PreviewWidget->SetPenSnap(PenSnapStep);
        PreviewWidget->SetPenPlane(EditPlane == EStrandEditPlane::XY ? 2 :
            EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1);
        PreviewWidget->SetPenCustomPlane(EditPlane == EStrandEditPlane::ThreePointPlane,
            CustomPlaneOrigin, CustomPlaneNormal);
    }
    return Tab;
}

TSharedRef<SDockTab> FStrandSplineAssetEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
    check(DetailsView.IsValid());
    return SNew(SDockTab).Label(LOCTEXT("DetailsTab", "Spline Details"))
        .ToolTipText(LOCTEXT("DetailsTabTip", "Point selection, position, authored orientation, Bezier handles, and interpolation."))
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
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [SNew(STextBlock)
                .Text_Lambda([this]() { return bRelativePositionEntry
                    ? LOCTEXT("OffsetCoordinates", "MOVE BY (asset cm, applied on Enter)")
                    : HasMultipleSelectedPoints()
                        ? LOCTEXT("GroupCoordinates", "SELECTION CENTER (asset cm)")
                        : LOCTEXT("SingleCoordinates", "POINT POSITION (asset cm)"); })
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)
            [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return !bRelativePositionEntry ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { bRelativePositionEntry = false; })
                .ToolTipText(LOCTEXT("AbsolutePositionTip", "Set an exact point position or selection-center location in asset coordinates."))
                [SNew(STextBlock).Text(LOCTEXT("AbsolutePosition", "Absolute"))]]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)
            [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([this]() { return bRelativePositionEntry ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState) { bRelativePositionEntry = true; })
                .ToolTipText(LOCTEXT("RelativePositionTip", "Enter an offset to add to every selected point; returns to zero after each commit."))
                [SNew(STextBlock).Text(LOCTEXT("RelativePosition", "Relative"))]]
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
                .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::YZ; })
                .ToolTipText(FText::FromString(TEXT("X position in asset centimeters. Enter commits absolute or relative movement depending on the mode.")))
                .Value_Lambda([this]() { return GetPointCoordinate(0); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 0); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
                .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XZ; })
                .ToolTipText(FText::FromString(TEXT("Y position in asset centimeters. Enter commits absolute or relative movement depending on the mode.")))
                .Value_Lambda([this]() { return GetPointCoordinate(1); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 1); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(3)
                .LabelVAlign(VAlign_Center)
                .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
                .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XY; })
                .ToolTipText(FText::FromString(TEXT("Z position in asset centimeters. Enter commits absolute or relative movement depending on the mode.")))
                .Value_Lambda([this]() { return GetPointCoordinate(2); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetPointCoordinate(Value, 2); })]
        ]
        // Authored anchor rotation is intentionally separate from the curve's
        // tangent direction. Local X follows the outgoing tangent; saved rotation guides up/roll.
        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 2.0f, 8.0f, 3.0f)
        [SNew(STextBlock)
            .Text(LOCTEXT("AuthorOrientationHeader", "ANCHOR ORIENTATION (degrees)"))
            .ToolTipText(LOCTEXT("AuthorOrientationTip", "Saved point orientation, independent of curve tangents. Local gizmo X follows the outgoing tangent; this orientation supplies its up/roll reference."))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 3.0f)
        [SNew(SHorizontalBox).IsEnabled_Lambda([this]() { return HasSelectedPoint() && !HasMultipleSelectedPoints(); })
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(2)
                .Label()[SNew(STextBlock).Text(LOCTEXT("PitchLabel", "P"))]
                .ToolTipText(FText::FromString(TEXT("Pitch: authored anchor rotation in degrees. Does not change spline tangents.")))
                .Value_Lambda([this]() { return GetAnchorRotationCoordinate(0); })
                .OnValueCommitted_Lambda([this](double V, ETextCommit::Type) { SetAnchorRotationCoordinate(V, 0); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(2)
                .Label()[SNew(STextBlock).Text(LOCTEXT("YawLabel", "Y"))]
                .ToolTipText(FText::FromString(TEXT("Yaw: authored anchor rotation in degrees. Does not change spline tangents.")))
                .Value_Lambda([this]() { return GetAnchorRotationCoordinate(1); })
                .OnValueCommitted_Lambda([this](double V, ETextCommit::Type) { SetAnchorRotationCoordinate(V, 1); })]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
            [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(2)
                .Label()[SNew(STextBlock).Text(LOCTEXT("RollLabel", "R"))]
                .ToolTipText(FText::FromString(TEXT("Roll: authored anchor rotation in degrees. Does not change spline tangents.")))
                .Value_Lambda([this]() { return GetAnchorRotationCoordinate(2); })
                .OnValueCommitted_Lambda([this](double V, ETextCommit::Type) { SetAnchorRotationCoordinate(V, 2); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 1.0f, 8.0f, 5.0f)
        [SNew(SHorizontalBox).IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("ResetAnchorOrientation", "Reset Rotation"))
                .ToolTipText(LOCTEXT("ResetAnchorOrientationTip", "Set selected anchor rotations to zero without modifying positions, tangents, or spline interpolation."))
                .OnClicked(this, &FStrandSplineAssetEditor::ResetSelectedOrientations)]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("AlignAnchorToCurve", "Align to Curve"))
                .ToolTipText(LOCTEXT("AlignAnchorToCurveTip", "Explicitly orient selected anchors with X forward along the spline tangent and Z approximately up. Does not alter tangents."))
                .OnClicked(this, &FStrandSplineAssetEditor::AlignSelectedOrientationsToCurve)]
        ]
        // Curve-shaping behavior is a first-class authoring control, not a
        // hidden one-shot command inside Spline Tools. This applies to all
        // selected anchors, including selections made with Box Select.
        + SVerticalBox::Slot().AutoHeight().Padding(9.0f, 6.0f, 8.0f, 3.0f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f, 9.0f, 0.0f)
            [SNew(STextBlock).Text(LOCTEXT("HandleModeHeader", "BEZIER HANDLES"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [SNew(SComboButton)
                .IsEnabled(this, &FStrandSplineAssetEditor::HasSelectedPoint)
                .ToolTipText(LOCTEXT("HandleModeTip", "Stored per-point handle behavior. Free: independent; Aligned: common direction, independent lengths; Mirrored: common direction and length; Auto: Unreal native smoothing; Corner: collapse both handles."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakeHandleModeMenu)
                .ButtonContent()[SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetHandleModeLabel)]]]
        + SVerticalBox::Slot().AutoHeight().Padding(11.0f, 0.0f, 8.0f, 6.0f)
        [SNew(STextBlock).Text(this, &FStrandSplineAssetEditor::GetHandleModeDescription)
            .ColorAndOpacity(FSlateColor::UseSubduedForeground()).AutoWrapText(true)]
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
                    .ToolTipText(FText::FromString(TEXT("Selected incoming/outgoing Bezier tangent X component (centimeters per spline key).")))
                .Value_Lambda([this]() { return GetTangentCoordinate(0); })
                    .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetTangentCoordinate(Value, 0); })]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(3)
                    .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XZ; })
                    .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Y")))]
                    .ToolTipText(FText::FromString(TEXT("Selected incoming/outgoing Bezier tangent Y component (centimeters per spline key).")))
                .Value_Lambda([this]() { return GetTangentCoordinate(1); })
                    .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetTangentCoordinate(Value, 1); })]
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
                [SNew(SNumericEntryBox<double>).MinFractionalDigits(0).MaxFractionalDigits(3)
                    .IsEnabled_Lambda([this]() { return EditPlane != EStrandEditPlane::XY; })
                    .Label()[SNew(STextBlock).Text(FText::FromString(TEXT("Z")))]
                    .ToolTipText(FText::FromString(TEXT("Selected incoming/outgoing Bezier tangent Z component (centimeters per spline key).")))
                .Value_Lambda([this]() { return GetTangentCoordinate(2); })
                    .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { SetTangentCoordinate(Value, 2); })]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 3.0f)
        [SNew(SHorizontalBox)
            .Visibility_Lambda([this]() { return HasSelectedPoint() && !HasMultipleSelectedPoints() ? EVisibility::Visible : EVisibility::Collapsed; })
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("IncomingHandleButton", "Incoming Handle"))
                .ToolTipText(LOCTEXT("IncomingHandleTip", "Select the ORANGE incoming (Arrive) Bezier handle for numeric or gizmo editing. Tangent visibility must be enabled."))
                .OnClicked_Lambda([this]() { SelectTangentHandle(SelectedPointIndex, 1); return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)
            [SNew(SButton).Text(LOCTEXT("OutgoingHandleButton", "Outgoing Handle"))
                .ToolTipText(LOCTEXT("OutgoingHandleTip", "Select the BLUE outgoing (Leave) Bezier handle for numeric or gizmo editing. Tangent visibility must be enabled."))
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
                .ToolTipText(LOCTEXT("InspectorInterpolationTip", "Choose the spline interpolation type for selected anchors. Custom Tangents preserves individual Bezier handles; Constant creates stepped, non-exportable interpolation."))
                .OnGetMenuContent(this, &FStrandSplineAssetEditor::MakePointTypeMenu)
                .ButtonContent()[SNew(STextBlock).Text(LOCTEXT("PointType", "Interpolation"))]]
        ]
        // The right inspector follows the active tool. Showing both numerical
        // Rotate and Scale fields at once blurred the purpose of each mode.
        + SVerticalBox::Slot().AutoHeight().Padding(9.f, 8.f, 8.f, 4.f)
        [SNew(SHorizontalBox)
            .Visibility_Lambda([this]() { return HasSelectedPoint() && ActiveWidgetMode == 1
                ? EVisibility::Visible : EVisibility::Collapsed; })
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 8.f, 0.f)
            [SNew(STextBlock).Text(LOCTEXT("RotateSection", "ROTATE SELECTION"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f)
            [SNew(SNumericEntryBox<double>)
                .Label()[SNew(STextBlock).Text(LOCTEXT("RotateZField", "Z deg"))]
                .ToolTipText(LOCTEXT("RotateZFieldTip", "Type a relative Z rotation in degrees about the selection center. Or rotate using Unreal's native widget in the viewport."))
                .Value_Lambda([]() -> TOptional<double> { return 0.0; })
                .OnValueCommitted_Lambda([this](double Degrees, ETextCommit::Type) { RotateSelection(2, Degrees); })]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(9.f, 8.f, 8.f, 4.f)
        [SNew(SHorizontalBox)
            .Visibility_Lambda([this]() { return HasSelectedPoint() && ActiveWidgetMode == 2
                ? EVisibility::Visible : EVisibility::Collapsed; })
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 8.f, 0.f)
            [SNew(STextBlock).Text(LOCTEXT("UniformScaleSection", "UNIFORM SCALE"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(2.f)
            [SNew(SNumericEntryBox<double>)
                .Label()[SNew(STextBlock).Text(LOCTEXT("ScaleFactorField", "Factor"))]
                .ToolTipText(LOCTEXT("ScaleFactorFieldTip", "Positive uniform scale multiplier about the selection center. Or drag Unreal's native scale widget in the viewport."))
                .Value_Lambda([]() -> TOptional<double> { return 1.0; })
                .OnValueCommitted_Lambda([this](double Factor, ETextCommit::Type) { ScaleSelection(Factor); })]
        ]
        // Persistent context: useful even when no anchor is selected. Native
        // spline length is evaluated, not a sum of straight point distances.
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f, 12.0f, 8.0f, 2.0f)
        [SNew(SSeparator)]
        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 6.0f, 8.0f, 3.0f)
        [SNew(STextBlock).Text(LOCTEXT("SplineOverviewHeading", "SPLINE OVERVIEW"))
            .ToolTipText(LOCTEXT("SplineOverviewHeadingTip", "Live native spline statistics, including evaluated arc length in centimeters."))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 0.0f, 8.0f, 5.0f)
        [SNew(STextBlock)
            .Text_Lambda([this]()
            {
                const USplineDataAsset* Current = Asset.Get();
                const int32 Points = Current ? Current->Points.Num() : 0;
                const bool bClosed = Current && Current->bIsClosedLoop;
                const int32 Segments = FMath::Max(0, Points - (bClosed ? 0 : 1));
                const double Length = NativePreview.IsValid() ? NativePreview->GetSplineLength() : 0.0;
                return FText::FromString(FString::Printf(TEXT("%d anchors  |  %d segments  |  %.1f cm  |  %s"),
                    Points, Segments, Length, bClosed ? TEXT("Closed") : TEXT("Open")));
            })
            .ToolTipText(LOCTEXT("SplineOverviewLengthTip", "Arc length comes from Unreal's evaluated spline. It may differ from the distance between control points."))]
        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 0.0f, 8.0f, 6.0f)
        [SNew(STextBlock)
            .Visibility_Lambda([this]() { return HasSelectedPoint() && !HasMultipleSelectedPoints() ? EVisibility::Visible : EVisibility::Collapsed; })
            .Text_Lambda([this]()
            {
                if (!NativePreview.IsValid() || !HasSelectedPoint() ||
                    SelectedPointIndex >= NativePreview->GetNumberOfSplinePoints()) { return FText::GetEmpty(); }
                const double Distance = NativePreview->GetDistanceAlongSplineAtSplinePoint(SelectedPointIndex);
                const double Length = NativePreview->GetSplineLength();
                const double Percent = Length > KINDA_SMALL_NUMBER ? Distance / Length * 100.0 : 0.0;
                return FText::FromString(FString::Printf(TEXT("Selected anchor: %.1f cm along path (%.1f%%)"), Distance, Percent));
            })
            .ToolTipText(LOCTEXT("SplineOverviewDistanceTip", "Evaluated distance from the spline start to the selected anchor. On a closed spline the first anchor is at 0%."))]
        + SVerticalBox::Slot().FillHeight(1.0f)
        [SNew(SSpacer)]
    ];
}

TSharedRef<SDockTab> FStrandSplineAssetEditor::SpawnSourceTab(const FSpawnTabArgs& Args)
{
    check(DetailsView.IsValid());
    return SNew(SDockTab).Label(LOCTEXT("SourceTabLabel", "Source & Import"))
        .ToolTipText(LOCTEXT("SourceTabTip", "Asset placement defaults, original source, and format-specific reimport settings."))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8.0f)
        [SNew(STextBlock).Text(LOCTEXT("SourceTabHeading", "ASSET / SOURCE / IMPORT"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
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
    return bRelativePositionEntry ? 0.0 : Center[Axis];
}

void FStrandSplineAssetEditor::SetPointCoordinate(double Value, int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || !CanEditAxis(Axis) || !FMath::IsFinite(Value)) { return; }
    const TArray<int32> Indices = GetSelectedIndices();
    const double Old = StrandSplineEditOperations::SelectionCenter(Current->Points, Indices)[Axis];
    const double DeltaValue = StrandSplineEditOperations::PositionEntryDelta(Old, Value, bRelativePositionEntry);
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

TOptional<double> FStrandSplineAssetEditor::GetAnchorRotationCoordinate(int32 Axis) const
{
    if (!HasSelectedPoint() || HasMultipleSelectedPoints() || Axis < 0 || Axis > 2)
    {
        return TOptional<double>();
    }
    const FRotator& Rotation = Asset->Points[SelectedPointIndex].LocalRotation;
    return Axis == 0 ? Rotation.Pitch : Axis == 1 ? Rotation.Yaw : Rotation.Roll;
}

void FStrandSplineAssetEditor::SetAnchorRotationCoordinate(double Value, int32 Axis)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !HasSelectedPoint() || HasMultipleSelectedPoints() ||
        Axis < 0 || Axis > 2 || !FMath::IsFinite(Value)) { return; }
    FRotator Rotation = Current->Points[SelectedPointIndex].LocalRotation;
    double& Component = Axis == 0 ? Rotation.Pitch : Axis == 1 ? Rotation.Yaw : Rotation.Roll;
    if (FMath::IsNearlyEqual(Component, Value, 0.000001)) { return; }
    Component = Value;
    if (Rotation.ContainsNaN()) { return; }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("EditAnchorOrientationUndo", "Edit Strand Anchor Orientation"));
    Current->Modify();
    if (StrandSplineEditOperations::SetSelectedOrientation(Current->Points,
        TArray<int32>{SelectedPointIndex}, Rotation))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
}

FReply FStrandSplineAssetEditor::SetSelectedRoll(double Degrees, bool bAdditive)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !HasSelectedPoint() || !FMath::IsFinite(Degrees)) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("EditSelectedRollUndo", "Edit Strand Spline Roll"));
    Current->Modify();
    const TArray<int32> Indices = GetSelectedIndices();
    const bool bChanged = bAdditive
        ? StrandSplineOrientationOperations::AddRoll(Current->Points, Indices, Degrees)
        : StrandSplineOrientationOperations::SetRoll(Current->Points, Indices, Degrees);
    if (bChanged)
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::DistributeSelectedRoll()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || GetSelectedIndices().Num() < 3) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("DistributeSplineRollUndo", "Distribute Strand Spline Roll"));
    Current->Modify();
    if (StrandSplineOrientationOperations::DistributeRoll(Current->Points, GetSelectedIndices(), NativePreview.Get()))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ResetSelectedOrientations()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !HasSelectedPoint()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ResetAnchorOrientationsUndo", "Reset Strand Anchor Orientations"));
    Current->Modify();
    if (StrandSplineEditOperations::SetSelectedOrientation(Current->Points,
        GetSelectedIndices(), FRotator::ZeroRotator))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::AlignSelectedOrientationsToCurve()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !HasSelectedPoint() || !NativePreview.IsValid()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("AlignAnchorOrientationsUndo", "Align Strand Anchor Orientations to Curve"));
    Current->Modify();
    bool bChanged = false;
    for (int32 Index : GetSelectedIndices())
    {
        if (!Current->Points.IsValidIndex(Index)) { continue; }
        const FVector Forward = NativePreview->GetDirectionAtSplinePoint(Index, ESplineCoordinateSpace::Local);
        if (Forward.IsNearlyZero()) { continue; }
        const FVector ReferenceUp = FMath::Abs(FVector::DotProduct(Forward, FVector::UpVector)) > 0.98
            ? FVector::YAxisVector : FVector::UpVector;
        const FRotator Rotation = FRotationMatrix::MakeFromXZ(Forward, ReferenceUp).Rotator();
        bChanged |= StrandSplineEditOperations::SetSelectedOrientation(Current->Points,
            TArray<int32>{Index}, Rotation);
    }
    if (bChanged)
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
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
    FVector Tangent = SelectedHandle == 1 ? Saved.ArriveTangent : Saved.LeaveTangent;
    Tangent[Axis] = Value;
    StrandSplineEditOperations::EditHandle(Saved, SelectedHandle, Tangent);
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
}

void FStrandSplineAssetEditor::RefreshSoftSelectionVisualization()
{
    if (!PreviewWidget.IsValid()) { return; }
    TArray<double> Weights;
    // Keep the visible falloff identical to the actual frozen gesture weights.
    // Re-evaluating after every geometry rebuild would make the halos drift.
    if (ActivePointDragTransaction && FrozenSoftSelectionWeights.Num() > 0)
    {
        PreviewWidget->SetSoftSelectionWeights(FrozenSoftSelectionWeights);
        return;
    }
    if (SoftSelectionMode != 0 && NativePreview.IsValid())
    {
        if (const USplineDataAsset* Current = Asset.Get())
        {
            const TArray<int32> Sources = GetSelectedIndices();
            Weights.Reserve(Current->Points.Num());
            for (int32 Index = 0; Index < Current->Points.Num(); ++Index)
            {
                Weights.Add(StrandCameraSpace::InfluenceAt(Current->Points, Sources, Index, NativePreview.Get(),
                    SoftSelectionMode == 1 ? StrandCameraSpace::EInfluence::Spatial3D : StrandCameraSpace::EInfluence::AlongSpline,
                    SoftSelectionRadius, static_cast<StrandCameraSpace::EFalloff>(SoftSelectionFalloff)));
            }
        }
    }
    PreviewWidget->SetSoftSelectionWeights(Weights);
}

void FStrandSplineAssetEditor::UpdateInteractiveSnapPreview()
{
    bSnapDuringDragCandidate = false;
    if (!PreviewWidget.IsValid()) { return; }
    PreviewWidget->SetSnapPreviewSegments({});
    USplineDataAsset* Current = Asset.Get();
    if (!bInteractiveAnchorSnap || ActiveSnapWindow.IsValid() || SoftSelectionMode != 0 || ActiveWidgetMode != 0 ||
        SelectedHandle != 0 || !Current || !Current->Points.IsValidIndex(SelectedPointIndex) ||
        !FMath::IsFinite(PrecisionSnapToleranceCm) ||
        FSlateApplication::Get().GetModifierKeys().IsControlDown()) { return; }
    // Explicit initialization is required because anchor lookup can fail before
    // a curve lookup runs; the distance comparison is guarded by bFound.
    FVector Target = FVector::ZeroVector;
    int32 TargetIndex = INDEX_NONE;
    const FVector Position = Current->Points[SelectedPointIndex].LocalPosition;
    const TArray<int32> Selection = GetSelectedIndices();
    bool bFound = false;
    if (InteractiveSnapTargetMode != 1)
    {
        bFound = StrandPrecision::FindLiveAnchorTarget(Current->Points, Selection, Position,
            PrecisionSnapToleranceCm, Target, TargetIndex);
    }
    // Curve targets use native evaluated geometry; any segment touching a
    // selected anchor is excluded so the curve cannot snap to itself.
    if (InteractiveSnapTargetMode != 0 && NativePreview.IsValid())
    {
        FVector CurveTarget;
        int32 CurveSegment = INDEX_NONE;
        const double MaxDistance = bFound
            ? FMath::Min(PrecisionSnapToleranceCm, FVector::Dist(Position, Target))
            : PrecisionSnapToleranceCm;
        if (StrandPrecision::FindNearestOtherSegment(NativePreview.Get(), Selection,
            SelectedPointIndex, MaxDistance, CurveTarget, CurveSegment))
        {
            Target = CurveTarget;
            bFound = true;
        }
    }
    // A snap must not silently break the active editing plane. The target is
    // eligible only if its correction is already in the allowed plane.
    if (bFound)
    {
        const FVector Correction = Target - Position;
        FVector Normal = FVector::ZeroVector;
        if (EditPlane == EStrandEditPlane::XY) { Normal = FVector::ZAxisVector; }
        else if (EditPlane == EStrandEditPlane::XZ) { Normal = FVector::YAxisVector; }
        else if (EditPlane == EStrandEditPlane::YZ) { Normal = FVector::XAxisVector; }
        else if (EditPlane == EStrandEditPlane::CameraPlane) { Normal = LockedCameraPlaneNormal; }
        else if (EditPlane == EStrandEditPlane::ThreePointPlane) { Normal = CustomPlaneNormal; }
        if (!Normal.IsNearlyZero() &&
            FMath::Abs(FVector::DotProduct(Correction, Normal.GetSafeNormal())) > 0.01)
        { bFound = false; }
    }
    if (bFound)
    {
        bSnapDuringDragCandidate = true;
        SnapDuringDragTarget = Target;
        TArray<TPair<FVector, FVector>> Segments;
        Segments.Emplace(Position, Target);
        PreviewWidget->SetSnapPreviewSegments(Segments);
    }
}

void FStrandSplineAssetEditor::BeginViewportPointDrag()
{
    if (ActivePointDragTransaction || !HasSelectedPoint()) { return; }
    if (EditPlane == EStrandEditPlane::CameraPlane && PreviewWidget.IsValid())
    { LockedCameraPlaneNormal = PreviewWidget->GetCameraForward(); }
    ActivePointDragTransaction = MakeUnique<FScopedTransaction>(
        LOCTEXT("DragSplinePoint", "Move Strand Spline Selection"));
    bPointChangedDuringDrag = false;
    bSnapDuringDragCandidate = false;
    FrozenSoftSelectionWeights.Reset();
    FrozenSoftSelectionPivot = StrandSplineEditOperations::SelectionCenter(Asset->Points, GetSelectedIndices());
    if (SoftSelectionMode != 0 && NativePreview.IsValid())
    {
        const TArray<int32> Sources = GetSelectedIndices();
        const USplineDataAsset* Current = Asset.Get();
        if (Current)
        {
            FrozenSoftSelectionWeights.Reserve(Current->Points.Num());
            for (int32 Index = 0; Index < Current->Points.Num(); ++Index)
            {
                FrozenSoftSelectionWeights.Add(StrandCameraSpace::InfluenceAt(Current->Points, Sources, Index,
                    NativePreview.Get(), SoftSelectionMode == 1 ? StrandCameraSpace::EInfluence::Spatial3D : StrandCameraSpace::EInfluence::AlongSpline,
                    SoftSelectionRadius, static_cast<StrandCameraSpace::EFalloff>(SoftSelectionFalloff)));
            }
        }
    }
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
    if (EditPlane == EStrandEditPlane::CameraPlane) { Constrained = StrandCameraSpace::ProjectDelta(Constrained, LockedCameraPlaneNormal); }
    if (EditPlane == EStrandEditPlane::ThreePointPlane) { Constrained = StrandCameraSpace::ProjectDelta(Constrained, CustomPlaneNormal); }
    if (Constrained.IsNearlyZero()) { return; }
    bool bMoved = false;
    if (SoftSelectionMode == 0)
    {
        bMoved = StrandSplineEditOperations::TranslateSelected(Current->Points, GetSelectedIndices(), Constrained);
    }
    else
    {
        bMoved = StrandCameraSpace::ApplyWeightedDelta(Current->Points, FrozenSoftSelectionWeights, Constrained);
    }
    if (bMoved)
    {
        bPointChangedDuringDrag = true;
        Current->PostEditChange();
        RebuildNativePreview();
        UpdateInteractiveSnapPreview();
    }
}

void FStrandSplineAssetEditor::ApplyViewportTangentRoll(double Degrees)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || !HasSelectedPoint() ||
        HasMultipleSelectedPoints() || !FMath::IsFinite(Degrees)) { return; }
    const FVector Forward = NativePreview->GetTangentAtSplinePoint(
        SelectedPointIndex, ESplineCoordinateSpace::Local);
    if (Forward.IsNearlyZero()) { return; }
    if (!ActivePointDragTransaction) { BeginViewportPointDrag(); }
    if (!ActivePointDragTransaction) { return; }
    if (StrandSplineEditOperations::RollAnchorAroundForward(
        Current->Points[SelectedPointIndex], Forward, Degrees))
    {
        bPointChangedDuringDrag = true;
        Current->PostEditChange();
        RebuildNativePreview();
    }
}

void FStrandSplineAssetEditor::ApplyViewportRotation(const FQuat& RotationDelta)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || SelectedPointIndices.IsEmpty() || RotationDelta.ContainsNaN()) { return; }
    const FQuat Delta = RotationDelta.GetNormalized();
    if (Delta.IsIdentity(0.000001)) { return; }
    const int32 PlaneAxis = EditPlane == EStrandEditPlane::XY ? 2 :
        EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1;
    if (PlaneAxis >= 0)
    {
        FVector Axis = FVector::ZeroVector;
        double Angle = 0.0;
        Delta.ToAxisAndAngle(Axis, Angle);
        // A constrained plane permits only rotations about its normal.
        if (FMath::Abs(Axis[PlaneAxis]) < 0.999) { return; }
    }
    if (!ActivePointDragTransaction) { BeginViewportPointDrag(); }
    if (!ActivePointDragTransaction) { return; }
    const bool bChanged = SoftSelectionMode == 0
        ? StrandSplineEditOperations::RotateSelectedQuat(Current->Points, GetSelectedIndices(), Delta)
        : StrandCameraSpace::ApplyWeightedRotation(Current->Points, FrozenSoftSelectionWeights,
            FrozenSoftSelectionPivot, Delta);
    if (bChanged)
    {
        bPointChangedDuringDrag = true;
        Current->PostEditChange();
        RebuildNativePreview();
    }
}

void FStrandSplineAssetEditor::ApplyViewportScale(double Factor)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || SelectedPointIndices.IsEmpty() || !FMath::IsFinite(Factor) || Factor <= 0.0 ||
        FMath::IsNearlyEqual(Factor, 1.0, 0.000001)) { return; }
    if (!ActivePointDragTransaction) { BeginViewportPointDrag(); }
    if (!ActivePointDragTransaction) { return; }
    const bool bChanged = SoftSelectionMode == 0
        ? StrandSplineEditOperations::ScaleSelected(Current->Points, GetSelectedIndices(), Factor)
        : StrandCameraSpace::ApplyWeightedScale(Current->Points, FrozenSoftSelectionWeights,
            FrozenSoftSelectionPivot, Factor);
    if (bChanged)
    {
        bPointChangedDuringDrag = true;
        Current->PostEditChange();
        RebuildNativePreview();
    }
}

void FStrandSplineAssetEditor::EndViewportPointDrag()
{
    if (!ActivePointDragTransaction) { return; }
    // Snap only once, on release, within the same transaction as the drag.
    // This avoids magnetic oscillation and preserves selection spacing.
    USplineDataAsset* Current = Asset.Get();
    if (bSnapDuringDragCandidate && Current && bPointChangedDuringDrag &&
        Current->Points.IsValidIndex(SelectedPointIndex) &&
        !FSlateApplication::Get().GetModifierKeys().IsControlDown())
    {
        const FVector Offset = SnapDuringDragTarget - Current->Points[SelectedPointIndex].LocalPosition;
        if (!Offset.IsNearlyZero() && !Offset.ContainsNaN() &&
            StrandSplineEditOperations::TranslateSelected(Current->Points, GetSelectedIndices(), Offset))
        {
            Current->PostEditChange();
            RebuildNativePreview();
        }
    }
    bSnapDuringDragCandidate = false;
    if (PreviewWidget.IsValid()) { PreviewWidget->SetSnapPreviewSegments({}); }
    if (bPointChangedDuringDrag)
    {
        if (Current) { Current->MarkPackageDirty(); }
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    ActivePointDragTransaction.Reset();
    FrozenSoftSelectionWeights.Reset();
    bPointChangedDuringDrag = false;
    RefreshSoftSelectionVisualization();
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
    // A selected anchor may rotate its point orientation/handles or scale
    // its handles; do not reset the chosen tool merely on single selection.
    if (!HasSelectedPoint() && ActiveWidgetMode != 0)
    {
        ActiveWidgetMode = 0;
        if (PreviewWidget.IsValid()) { PreviewWidget->SetWidgetMode(0); }
    }
    if (PreviewWidget.IsValid())
    {
        PreviewWidget->SetSelectedPoints(GetSelectedIndices(), SelectedPointIndex);
        RefreshSoftSelectionVisualization();
        PreviewWidget->SetSelectedHandle(HasMultipleSelectedPoints() ? 0 : SelectedHandle);
        const USplineDataAsset* Current = Asset.Get();
        PreviewWidget->SetAuthoredAnchorRotation(Current && Current->Points.IsValidIndex(SelectedPointIndex)
            ? Current->Points[SelectedPointIndex].LocalRotation : FRotator::ZeroRotator);
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

FReply FStrandSplineAssetEditor::GrowAnchorSelection()
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current) { return FReply::Handled(); }
    SelectedPointIndices = StrandSplineSelectionUtils::GrowAnchorSelection(
        SelectedPointIndices, Current->Points.Num(), Current->bIsClosedLoop);
    SyncViewportSelection();
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ShrinkAnchorSelection()
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current) { return FReply::Handled(); }
    SelectedPointIndices = StrandSplineSelectionUtils::ShrinkAnchorSelection(
        SelectedPointIndices, Current->Points.Num(), Current->bIsClosedLoop);
    if (!SelectedPointIndices.Contains(SelectedPointIndex))
    {
        const TArray<int32> Sorted = GetSelectedIndices();
        SelectedPointIndex = Sorted.IsEmpty() ? INDEX_NONE : Sorted.Last();
    }
    SyncViewportSelection();
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SelectSharpCorners(double ThresholdDegrees)
{
    if (!NativePreview.IsValid()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const TArray<int32> Found = StrandSplineEditOperations::FindSharpCorners(
        NativePreview.Get(), ThresholdDegrees);
    SelectedPointIndices.Reset();
    for (int32 Index : Found) { SelectedPointIndices.Add(Index); }
    SelectedPointIndex = Found.IsEmpty() ? INDEX_NONE : Found[0];
    SelectedHandle = 0;
    SyncViewportSelection();
    return FReply::Handled();
}

void FStrandSplineAssetEditor::SelectMarqueePoints(const TArray<int32>& Indices, bool bAdd, bool bToggle)
{
    EndViewportPointDrag();
    const USplineDataAsset* Current = Asset.Get();
    StrandSplineEditOperations::ApplySelectionHits(SelectedPointIndices,
        Current ? Current->Points.Num() : 0, Indices, bAdd, bToggle);
    // Keep the old primary if still selected; otherwise use the last point.
    if (!SelectedPointIndices.Contains(SelectedPointIndex))
    {
        const TArray<int32> Sorted = GetSelectedIndices();
        SelectedPointIndex = Sorted.IsEmpty() ? INDEX_NONE : Sorted.Last();
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
    Menu.AddMenuEntry(LOCTEXT("SelectAllMenu", "Select All"), LOCTEXT("SelectAllMenuHelp", "Select every spline anchor (Ctrl+A)."), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { SelectAllPoints(); })));
    Menu.AddMenuEntry(LOCTEXT("InvertSelectMenu", "Invert Selection"), LOCTEXT("InvertSelectMenuHelp", "Select unselected anchors and deselect currently selected anchors."), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { InvertPointSelection(); })));
    Menu.AddMenuEntry(LOCTEXT("ClearSelectMenu", "Clear Selection"), LOCTEXT("ClearSelectMenuHelp", "Deselect all anchors (Esc)."), FSlateIcon(),
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
        if (PreviewWidget.IsValid())
        {
            PreviewWidget->SetSpline(NativePreview.Get());
            PreviewWidget->SetAuthoredAnchorRotation(Asset->Points.IsValidIndex(SelectedPointIndex)
                ? Asset->Points[SelectedPointIndex].LocalRotation : FRotator::ZeroRotator);
        }
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

TSharedRef<SWidget> FStrandSplineAssetEditor::MakePrimitiveMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.AddMenuEntry(LOCTEXT("CirclePrimitive", "Circle"),
        LOCTEXT("CirclePrimitiveTip", "Create a closed 200 cm cubic circle with four editable Bezier anchors."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { CreatePrimitiveAsset(0); })));
    Menu.AddMenuEntry(LOCTEXT("RectanglePrimitive", "Rectangle"),
        LOCTEXT("RectanglePrimitiveTip", "Create a closed 200 x 150 cm rectangle with four linear anchors."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { CreatePrimitiveAsset(1); })));
    Menu.AddMenuEntry(LOCTEXT("HelixPrimitive", "Helix"),
        LOCTEXT("HelixPrimitiveTip", "Create a two-turn, 400 cm tall editable cubic helix."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { CreatePrimitiveAsset(2); })));
    return Menu.MakeWidget();
}

// Asset > Primitives creates another document. This authoring-strip menu edits
// the currently open document instead; its action is transactional and retains
// the asset identity, placement defaults and external actor links.
TSharedRef<SWidget> FStrandSplineAssetEditor::MakeReplacePrimitiveMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.BeginSection("StrandReplacePrimitive", LOCTEXT("ReplacePrimitiveSection", "Replace Current Spline"));
    Menu.AddMenuEntry(LOCTEXT("ReplaceWithCircle", "Circle"),
        LOCTEXT("ReplaceWithCircleTip", "Replace this spline with a four-anchor cubic circle. Undo restores the previous geometry."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ReplaceSplineWithPrimitive(0); })));
    Menu.AddMenuEntry(LOCTEXT("ReplaceWithRectangle", "Rectangle"),
        LOCTEXT("ReplaceWithRectangleTip", "Replace this spline with a four-corner linear rectangle. Undo restores the previous geometry."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ReplaceSplineWithPrimitive(1); })));
    Menu.AddMenuEntry(LOCTEXT("ReplaceWithHelix", "Helix"),
        LOCTEXT("ReplaceWithHelixTip", "Replace this spline with a 25-anchor, two-turn 3D helix. Undo restores the previous geometry."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ReplaceSplineWithPrimitive(2); })));
    Menu.EndSection();
    return Menu.MakeWidget();
}

void FStrandSplineAssetEditor::ReplaceSplineWithPrimitive(int32 Kind)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current) { return; }
    // Invalid kinds leave the asset untouched, without beginning Undo history.
    if (Kind < 0 || Kind > 2) { return; }

    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ReplaceStrandWithPrimitive", "Replace Strand Spline With Primitive"));
    Current->Modify();
#if WITH_EDITORONLY_DATA
    // The new authored geometry is no longer the imported curve. Preserve the
    // source-file metadata under Undo, but prevent a later Reimport from
    // silently replacing the new shape with the old external source.
    Current->SourceCurveId.Reset();
    Current->SourceImportFormat.Reset();
    // Keep the import-data subobject intact so undo can restore the prior
    // source link without depending on that subobject being transactional.
#endif
    if (!StrandSplineEditOperations::ReplacePrimitiveGeometry(*Current, Kind)) { return; }
    // Replacing topology invalidates point/handle indices, even if counts match.
    SelectedPointIndices.Reset();
    SelectedPointIndex = INDEX_NONE;
    SelectedHandle = 0;
    CommitSplineEdit(INDEX_NONE);
}

// Split non-destructively: the source asset and every linked actor keep their
// identity and geometry. The two created assets are independent, unlinked
// authoring assets; their common boundary anchor appears in both halves.
void FStrandSplineAssetEditor::SplitAtSelectedAnchor()
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || HasMultipleSelectedPoints()) { return; }
    TArray<FSplinePointData> BeforePoints, AfterPoints;
    if (!StrandSplineEditOperations::SplitOpenSplineAtAnchor(
            Current->Points, Current->bIsClosedLoop, NativePreview.Get(),
            SelectedPointIndex, BeforePoints, AfterPoints)) { return; }
    const FString ParentFolder = FPackageName::GetLongPackagePath(Current->GetOutermost()->GetName());
    IAssetTools& Tools = FAssetToolsModule::GetModule().Get();
    USplineDataAsset* First = nullptr;
    USplineDataAsset* Second = nullptr;
    const TArray<FSplinePointData>* Parts[2] = { &BeforePoints, &AfterPoints };
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FString BaseName = Current->GetName() + (Side == 0 ? TEXT("_Start") : TEXT("_End"));
        FString PackageName, AssetName;
        Tools.CreateUniqueAssetName(FPaths::Combine(ParentFolder, BaseName), TEXT(""), PackageName, AssetName);
        USplineDataAsset* Created = Cast<USplineDataAsset>(Tools.CreateAsset(
            AssetName, ParentFolder, USplineDataAsset::StaticClass(), NewObject<USplineDataAssetFactory>()));
        if (!Created) { break; }
        Created->Modify();
        Created->Points = *Parts[Side];
        Created->bIsClosedLoop = false;
        Created->DefaultCoordinateMode = Current->DefaultCoordinateMode;
        Created->DefaultAnchorMode = Current->DefaultAnchorMode;
        Created->DefaultAnchorPointIndex = FMath::Clamp(Current->DefaultAnchorPointIndex -
            (Side == 0 ? 0 : SelectedPointIndex), 0, Created->Points.Num() - 1);
        Created->MarkPackageDirty();
        if (Side == 0) { First = Created; } else { Second = Created; }
    }
    // Never modify the source. Both new assets are left visible in the
    // Content Browser; editing the two new paths is an explicit next action.
    if (First && Second && GEditor)
    {
        TArray<UObject*> AssetsToShow = { First, Second };
        GEditor->SyncBrowserToObjects(AssetsToShow);
    }
}

void FStrandSplineAssetEditor::CreatePrimitiveAsset(int32 Kind)
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current) { return; }
    TArray<FSplinePointData> Points;
    bool bClosed = false;
    if (!StrandSplineEditOperations::MakePrimitive(Kind, Points, bClosed)) { return; }
    const FString ParentFolder = FPackageName::GetLongPackagePath(Current->GetOutermost()->GetName());
    const TCHAR* BaseName = Kind == 0 ? TEXT("StrandCircle") : Kind == 1 ? TEXT("StrandRectangle") : TEXT("StrandHelix");
    IAssetTools& Tools = FAssetToolsModule::GetModule().Get();
    FString PackageName, AssetName;
    Tools.CreateUniqueAssetName(FPaths::Combine(ParentFolder, BaseName), TEXT(""), PackageName, AssetName);
    USplineDataAsset* Created = Cast<USplineDataAsset>(Tools.CreateAsset(AssetName, ParentFolder,
        USplineDataAsset::StaticClass(), NewObject<USplineDataAssetFactory>()));
    if (!Created) { return; }
    Created->Modify();
    Created->Points = MoveTemp(Points);
    Created->bIsClosedLoop = bClosed;
    Created->MarkPackageDirty();
    if (GEditor)
    {
        if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            Editors->OpenEditorForAsset(Created);
        }
    }
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
        if (Index > 0 && StrandSplineEditOperations::ExtendOpenEndpoint(Current->Points, NativePreview.Get(), false))
        {
            InsertIndex = Current->Points.Num() - 1;
        }
        else if (Index == 0)
        {
            InsertIndex = StrandSplineEditOperations::AppendPenPoint(Current->Points,
                Current->Points[Index].LocalPosition + FVector(100.0, 0.0, 0.0), false, false);
        }
    }
    else
    {
        InsertIndex = StrandSplineEditOperations::InsertOnSegment(Current->Points,
            Current->bIsClosedLoop, NativePreview.Get(), Index, 0.5);
    }
    if (InsertIndex != INDEX_NONE) { CommitSplineEdit(InsertIndex); }
    return FReply::Handled();
}

void FStrandSplineAssetEditor::InsertPointOnViewportSegment(int32 Segment, double Alpha)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || Current->Points.Num() >= 100000) { return; }
    const int32 Spans = Current->Points.Num() - (Current->bIsClosedLoop ? 0 : 1);
    if (Segment < 0 || Segment >= Spans || !FMath::IsFinite(Alpha)) { return; }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ViewportInsertSegmentUndo", "Insert Strand Spline Point On Curve"));
    Current->Modify();
    const int32 NewIndex = StrandSplineEditOperations::InsertOnSegment(Current->Points,
        Current->bIsClosedLoop, NativePreview.Get(), Segment, FMath::Clamp(Alpha, 0.002, 0.998));
    if (NewIndex != INDEX_NONE) { CommitSplineEdit(NewIndex); }
}

// Direct curve shaping: Ctrl+drag acts on the selected segment's two Bezier
// derivatives, not on its anchor positions. The initial state is immutable for
// the duration of a drag, so pointer movement never accumulates drift.
void FStrandSplineAssetEditor::BeginViewportSegmentBend(int32 Segment, double Alpha)
{
    EndViewportPointDrag();
    EndViewportSegmentBend();
    const USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid()) { return; }
    const int32 SpanCount = Current->Points.Num() - (Current->bIsClosedLoop ? 0 : 1);
    if (Segment < 0 || Segment >= SpanCount || !FMath::IsFinite(Alpha) ||
        Alpha < 0.1 || Alpha > 0.9) { return; }
    BendSegmentIndex = Segment;
    BendSegmentAlpha = Alpha;
    BendBaselinePoints = Current->Points;
    // Capture evaluated native tangents before any automatic point converts to
    // manual derivatives. This is important for Alembic and native Curve points.
    const int32 Next = (Segment + 1) % Current->Points.Num();
    BendEvaluatedStart = NativePreview->GetSplinePointAt(Segment, ESplineCoordinateSpace::Local);
    BendEvaluatedEnd = NativePreview->GetSplinePointAt(Next, ESplineCoordinateSpace::Local);
    bSegmentBendChanged = false;
}

void FStrandSplineAssetEditor::UpdateViewportSegmentBend(const FVector& InDelta)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || BendSegmentIndex == INDEX_NONE || InDelta.ContainsNaN()) { return; }
    FVector Delta = InDelta;
    const int32 LockedAxis = EditPlane == EStrandEditPlane::XY ? 2 :
        EditPlane == EStrandEditPlane::XZ ? 1 : EditPlane == EStrandEditPlane::YZ ? 0 : -1;
    if (LockedAxis >= 0) { Delta[LockedAxis] = 0.0; }
    if (Delta.IsNearlyZero())
    {
        if (bSegmentBendChanged)
        {
            Current->Points = BendBaselinePoints;
            Current->PostEditChange();
            RebuildNativePreview();
        }
        return;
    }
    TArray<FSplinePointData> Result = BendBaselinePoints;
    if (!StrandSplineEditOperations::BendSegment(Result, Current->bIsClosedLoop,
        BendSegmentIndex, BendSegmentAlpha, Delta, BendEvaluatedStart, BendEvaluatedEnd)) { return; }
    if (!ActiveSegmentBendTransaction)
    {
        ActiveSegmentBendTransaction = MakeUnique<FScopedTransaction>(
            LOCTEXT("BendSegmentUndo", "Bend Strand Spline Segment"));
        Current->Modify();
    }
    Current->Points = MoveTemp(Result);
    bSegmentBendChanged = true;
    Current->PostEditChange();
    RebuildNativePreview();
}

void FStrandSplineAssetEditor::EndViewportSegmentBend()
{
    if (ActiveSegmentBendTransaction)
    {
        if (bSegmentBendChanged && Asset.IsValid())
        {
            Asset->MarkPackageDirty();
            if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
        }
        ActiveSegmentBendTransaction.Reset();
    }
    BendSegmentIndex = INDEX_NONE;
    BendSegmentAlpha = 0.5;
    BendBaselinePoints.Reset();
    bSegmentBendChanged = false;
}

// The native viewport retains RMB-drag flight navigation. Only a stationary
// RMB click on an anchor or curve segment requests this contextual menu.
TSharedRef<SWidget> FStrandSplineAssetEditor::MakeViewportContextMenu(int32 Point, int32 Segment, double Alpha)
{
    if (Point != INDEX_NONE && !SelectedPointIndices.Contains(Point))
    {
        SelectPoint(Point);
    }
    FMenuBuilder Menu(true, nullptr);
    if (Point != INDEX_NONE)
    {
        Menu.BeginSection(TEXT("StrandAnchor"), LOCTEXT("ContextAnchor", "Spline Anchor"));
        Menu.AddMenuEntry(LOCTEXT("ContextInsertAfter", "Insert Point After"),
            LOCTEXT("ContextInsertAfterHelp", "Add an anchor after the selected point, preserving the current segment when possible."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
            FUIAction(FExecuteAction::CreateLambda([this]() { AddPointAfterSelection(); })));
        Menu.AddMenuEntry(LOCTEXT("ContextDelete", "Delete Selected Anchors"),
            LOCTEXT("ContextDeleteHelp", "Remove selected anchors in one undo transaction."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
            FUIAction(FExecuteAction::CreateLambda([this]() { RemoveSelectedPoint(); }),
                FCanExecuteAction::CreateSP(this, &FStrandSplineAssetEditor::CanDeleteSelection)));
        Menu.AddMenuEntry(LOCTEXT("ContextDissolveAnchor", "Dissolve Selected (Preview)..."),
            LOCTEXT("ContextDissolveAnchorHelp", "Open Curve Cleanup and use Dissolve Selected to fit replacement curves to the original before removing anchors. Unlike Delete, uses a 3D deviation tolerance."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { OpenCurveCleanupDialog(); })));
        Menu.AddMenuEntry(LOCTEXT("ContextG1", "Smooth Join (G1)"),
            LOCTEXT("ContextG1Tip", "Align incoming/outgoing tangent direction at selected interior anchors, keeping handle lengths."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedContinuity(false); })));
        Menu.AddMenuEntry(LOCTEXT("ContextC1", "Match Tangents (C1)"),
            LOCTEXT("ContextC1Tip", "Match incoming and outgoing native tangent vectors at selected interior anchors."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedContinuity(true); })));
        Menu.AddMenuEntry(LOCTEXT("ContextSplitAtAnchor", "Split into Two Strand Assets"),
            LOCTEXT("ContextSplitAtAnchorTip", "Create two independent open Strand assets split at this internal anchor. The original stays unchanged; both halves preserve evaluated native tangents."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SplitAtSelectedAnchor(); }),
                FCanExecuteAction::CreateLambda([this]()
                {
                    const USplineDataAsset* Current = Asset.Get();
                    return Current && !Current->bIsClosedLoop && !HasMultipleSelectedPoints() &&
                        SelectedPointIndex > 0 && SelectedPointIndex < Current->Points.Num() - 1;
                })));
        Menu.AddMenuSeparator();
        Menu.AddSubMenu(LOCTEXT("ContextBezier", "Bézier Handle Mode"),
            LOCTEXT("ContextBezierHelp", "Change how the selected anchor handles behave."),
            FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
            {
                struct FEntry { EStrandBezierHandleMode Mode; const TCHAR* Name; };
                const FEntry Options[] = {
                    {EStrandBezierHandleMode::Free, TEXT("Free")},
                    {EStrandBezierHandleMode::Aligned, TEXT("Aligned")},
                    {EStrandBezierHandleMode::Mirrored, TEXT("Mirrored")},
                    {EStrandBezierHandleMode::Auto, TEXT("Auto")},
                    {EStrandBezierHandleMode::Corner, TEXT("Corner")}
                };
                for (const FEntry& Option : Options)
                {
                    Sub.AddMenuEntry(FText::FromString(Option.Name), LOCTEXT("ContextHandleModeHelp", "Change selected anchor Bezier tangent constraint. This setting persists in the asset."), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Mode=Option.Mode]() { SetSelectedHandleMode(Mode); })));
                }
            }));
        Menu.AddSubMenu(LOCTEXT("ContextInterp", "Interpolation"),
            LOCTEXT("ContextInterpHelp", "Native spline interpolation for selected points."),
            FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
            {
                Sub.AddMenuEntry(LOCTEXT("ContextSmooth", "Curve (Auto)"), LOCTEXT("ContextSmoothHelp", "Use native Unreal automatic curve tangents for selected anchors."), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedPointType(ESplinePointType::Curve); })));
                Sub.AddMenuEntry(LOCTEXT("ContextCustom", "Curve (Custom Tangents)"), LOCTEXT("ContextCustomHelp", "Use individually editable Bezier tangents for selected anchors."), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedPointType(ESplinePointType::CurveCustomTangent); })));
                Sub.AddMenuEntry(LOCTEXT("ContextLinear", "Linear"), LOCTEXT("ContextLinearHelp", "Create straight interpolation through selected anchors."), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedPointType(ESplinePointType::Linear); })));
            }));
        Menu.AddMenuSeparator();
        Menu.AddMenuEntry(LOCTEXT("ContextAlignCurve", "Align Orientation to Curve"),
            LOCTEXT("ContextAlignCurveHelp", "Align authored rotation to the spline's forward direction, without changing geometry."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { AlignSelectedOrientationsToCurve(); })));
        Menu.AddSubMenu(LOCTEXT("ContextRollTools", "Roll Tools"),
            LOCTEXT("ContextRollToolsHelp", "Set, increment or distribute authored roll. Geometry is unchanged; all operations support Undo."),
            FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
            {
                Sub.AddMenuEntry(LOCTEXT("RollSetZero", "Set Roll: 0 degrees"), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedRoll(0.0, false); })));
                Sub.AddMenuEntry(LOCTEXT("RollAdd15", "Add Roll: +15 degrees"), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedRoll(15.0, true); })));
                Sub.AddMenuEntry(LOCTEXT("RollSubtract15", "Add Roll: -15 degrees"), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]() { SetSelectedRoll(-15.0, true); })));
                Sub.AddMenuEntry(LOCTEXT("RollDistribute", "Distribute Selected Roll by Distance"),
                    LOCTEXT("RollDistributeHelp", "Preserves the first/last selected roll, adjusts selected interior anchors along shortest-angle interpolation by spline distance. Requires 3 selected anchors."), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]() { DistributeSelectedRoll(); }),
                        FCanExecuteAction::CreateLambda([this]() { return GetSelectedIndices().Num() >= 3; })));
            }));
        Menu.AddMenuEntry(LOCTEXT("ContextResetRot", "Reset Authored Orientation"),
            LOCTEXT("ContextResetRotHelp", "Clear authored pitch/yaw/roll without changing the spline."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ResetSelectedOrientations(); })));
        Menu.EndSection();
    }
    else if (Segment != INDEX_NONE)
    {
        Menu.BeginSection(TEXT("StrandSegment"), LOCTEXT("ContextSegment", "Spline Segment"));
        Menu.AddMenuEntry(LOCTEXT("ContextInsertHere", "Insert Anchor Here"),
            LOCTEXT("ContextInsertHereHelp", "Shape-preserving insertion at the chosen position on the segment."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
            FUIAction(FExecuteAction::CreateLambda([this, Segment, Alpha]()
            { InsertPointOnViewportSegment(Segment, Alpha); })));
        // Deterministic subdivisions for repeatable technical-art workflows.
        // Fractions are native spline-input-key fractions, not arc-length percentages.
        Menu.AddSubMenu(LOCTEXT("ContextInsertFraction", "Insert at Segment Fraction"),
            LOCTEXT("ContextInsertFractionHelp", "Insert an anchor at an exact segment parameter (25%, 50%, or 75%). Preserves the original cubic shape; Undo supported."),
            FNewMenuDelegate::CreateLambda([this, Segment](FMenuBuilder& Sub)
            {
                for (const int32 Percent : { 25, 50, 75 })
                {
                    const double Alpha = static_cast<double>(Percent) / 100.0;
                    Sub.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%d%%"), Percent)),
                        LOCTEXT("ContextFractionTip", "Split this native curve segment at the chosen parameter while preserving its geometry. This is parameter fraction, not distance fraction."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Segment, Alpha]()
                        { InsertPointOnViewportSegment(Segment, Alpha); })));
                }
            }));
        Menu.AddMenuEntry(LOCTEXT("ContextSelectEndpoints", "Select Segment Endpoints"),
            LOCTEXT("ContextEndpointsHelp", "Select both anchors defining this curve segment."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Segment]()
            {
                if (const USplineDataAsset* Current = Asset.Get())
                {
                    if (Current->Points.Num() < 2) { return; }
                    SelectPoint(Segment);
                    SelectPoint((Segment + 1) % Current->Points.Num(), false, true);
                }
            })));
        Menu.EndSection();
    }
    return Menu.MakeWidget();
}

void FStrandSplineAssetEditor::NudgeSelectedPoints(const FVector& Delta)
{
    if (!HasSelectedPoint() || Delta.IsNearlyZero()) { return; }
    // A single key press = a single native Undo step. Reuses the same geometry
    // editing pipeline and active 2D plane constraints as viewport dragging.
    EndViewportPointDrag();
    ApplyViewportPointDrag(Delta);
    EndViewportPointDrag();
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
        // Native Auto overrides custom handle rules. A deliberate switch to
        // Custom starts in Free rather than keeping stale linked constraints.
        Saved.HandleMode = (NewType == ESplinePointType::Curve || NewType == ESplinePointType::CurveClamped)
            ? EStrandBezierHandleMode::Auto : EStrandBezierHandleMode::Free;
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
        Menu.AddMenuEntry(Label, LOCTEXT("PointTypeChoiceHelp", "Set selected anchors to this native Unreal spline interpolation type."), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Mode]()
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

FText FStrandSplineAssetEditor::GetHandleModeLabel() const
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current || SelectedPointIndices.IsEmpty()) { return LOCTEXT("HandlesNoSelection", "Select points"); }
    bool bHasMode = false;
    EStrandBezierHandleMode Mode = EStrandBezierHandleMode::Free;
    for (int32 Index : GetSelectedIndices())
    {
        if (!Current->Points.IsValidIndex(Index)) { continue; }
        const EStrandBezierHandleMode Next = StrandSplineEditOperations::EffectiveHandleMode(Current->Points[Index]);
        if (bHasMode && Next != Mode) { return LOCTEXT("HandlesMixed", "Mixed modes"); }
        Mode = Next;
        bHasMode = true;
    }
    if (!bHasMode) { return LOCTEXT("HandlesNoValidSelection", "Select points"); }
    switch (Mode)
    {
    case EStrandBezierHandleMode::Aligned: return LOCTEXT("HandlesAlignedLabel", "Aligned");
    case EStrandBezierHandleMode::Mirrored: return LOCTEXT("HandlesMirroredLabel", "Mirrored");
    case EStrandBezierHandleMode::Auto: return LOCTEXT("HandlesAutoLabel", "Auto (Unreal)");
    case EStrandBezierHandleMode::Corner: return LOCTEXT("HandlesCornerLabel", "Corner");
    default: return LOCTEXT("HandlesFreeLabel", "Free");
    }
}

FText FStrandSplineAssetEditor::GetHandleModeDescription() const
{
    if (!HasSelectedPoint()) { return LOCTEXT("HandlesSelectHelp", "Select one or more anchors to shape their handles."); }
    const USplineDataAsset* Current = Asset.Get();
    EStrandBezierHandleMode Mode = StrandSplineEditOperations::EffectiveHandleMode(Current->Points[SelectedPointIndex]);
    for (int32 Index : GetSelectedIndices())
    {
        if (Current->Points.IsValidIndex(Index) &&
            StrandSplineEditOperations::EffectiveHandleMode(Current->Points[Index]) != Mode)
        {
            return LOCTEXT("HandlesMixedHelp", "Select a mode to apply it to every selected anchor.");
        }
    }
    switch (Mode)
    {
    case EStrandBezierHandleMode::Aligned: return LOCTEXT("HandlesAlignedHelp", "Drag one handle to turn both together. Each keeps its own length.");
    case EStrandBezierHandleMode::Mirrored: return LOCTEXT("HandlesMirrorHelp", "Drag either handle; the other matches its direction and length.");
    case EStrandBezierHandleMode::Auto: return LOCTEXT("HandlesAutoHelp", "Unreal automatically calculates smooth tangents. Drag a handle to switch to Free.");
    case EStrandBezierHandleMode::Corner: return LOCTEXT("HandlesCornerHelp", "Both handles are collapsed to the anchor. Drag to break out into Free.");
    default: return LOCTEXT("HandlesFreeHelp", "Incoming and outgoing handles move independently.");
    }
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeHandleModeMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto Entry = [this, &Menu](EStrandBezierHandleMode Mode, const FText& Label, const FText& Tip)
    {
        Menu.AddMenuEntry(Label, Tip, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Mode]()
        {
            SetSelectedHandleMode(Mode);
        })));
    };
    Entry(EStrandBezierHandleMode::Free, LOCTEXT("FreeMode", "Free"),
        LOCTEXT("FreeModeHelp", "Independent incoming and outgoing tangent handles."));
    Entry(EStrandBezierHandleMode::Aligned, LOCTEXT("AlignedMode", "Aligned"),
        LOCTEXT("AlignedModeHelp", "Linked direction, independent lengths. Persists through later edits."));
    Entry(EStrandBezierHandleMode::Mirrored, LOCTEXT("MirroredMode", "Mirrored"),
        LOCTEXT("MirroredModeHelp", "Linked direction and length. Persists through later edits."));
    Entry(EStrandBezierHandleMode::Auto, LOCTEXT("AutoMode", "Auto (Unreal)"),
        LOCTEXT("AutoModeHelp", "Use Unreal's native automatic spline tangent calculation."));
    Entry(EStrandBezierHandleMode::Corner, LOCTEXT("CornerMode", "Corner"),
        LOCTEXT("CornerModeHelp", "Collapse both cubic handles to zero at this anchor."));
    return Menu.MakeWidget();
}

FReply FStrandSplineAssetEditor::SetSelectedHandleMode(EStrandBezierHandleMode Mode)
{
    USplineDataAsset* Current = Asset.Get();
    if (!HasSelectedPoint() || !NativePreview.IsValid()) { return FReply::Handled(); }
    EndViewportPointDrag();
    const TArray<int32> Indices = GetSelectedIndices();
    // Evaluate once before any edits. A Curve/Clamped key has computed native
    // tangents which must be copied before its type changes to Custom.
    TArray<FSplinePoint> Evaluated;
    Evaluated.Reserve(Indices.Num());
    for (int32 Index : Indices)
    {
        Evaluated.Add(NativePreview->GetSplinePointAt(Index, ESplineCoordinateSpace::Local));
    }
    FScopedTransaction Transaction(LOCTEXT("SetBezierModeUndo", "Change Strand Bezier Handle Mode"));
    Current->Modify();
    bool bChanged = false;
    for (int32 I = 0; I < Indices.Num(); ++I)
    {
        bChanged |= StrandSplineEditOperations::SetHandleMode(Current->Points[Indices[I]],
            Mode, Evaluated[I].ArriveTangent, Evaluated[I].LeaveTangent);
    }
    if (!bChanged) { Transaction.Cancel(); return FReply::Handled(); }
    Current->PostEditChange();
    Current->MarkPackageDirty();
    RebuildNativePreview();
    if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    return FReply::Handled();
}

void FStrandSplineAssetEditor::RotateSelectionAroundCamera(double Degrees)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !PreviewWidget.IsValid() || !FMath::IsFinite(Degrees) || FMath::IsNearlyZero(Degrees)) { return; }
    const FVector CameraForward = PreviewWidget->GetCameraForward();
    if (CameraForward.IsNearlyZero()) { return; }
    TArray<FSplinePointData> Result = Current->Points;
    if (!StrandSplineEditOperations::RotateSelectedQuat(Result, GetSelectedIndices(),
        FQuat(CameraForward, FMath::DegreesToRadians(Degrees)))) { return; }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("RotateScreenSpaceUndo", "Rotate Strand Selection in Screen Space"));
    Current->Modify();
    Current->Points = MoveTemp(Result);
    CommitSplineEdit(SelectedPointIndex, true);
}

void FStrandSplineAssetEditor::OpenPerspectiveFlattenDialog()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || !PreviewWidget.IsValid() || Current->Points.Num() < 2) { return; }
    if (TSharedPtr<SWindow> Snap = ActiveSnapWindow.Pin()) { Snap->RequestDestroyWindow(); }
    if (TSharedPtr<SWindow> OpenWindow = ActiveFlattenWindow.Pin())
    {
        OpenWindow->BringToFront(true);
        return;
    }
    if (TSharedPtr<SWindow> Cleanup = ActiveCleanupWindow.Pin()) { Cleanup->RequestDestroyWindow(); }

    struct FFlattenState
    {
        double TolerancePixels = 1.0;
        StrandPerspectiveFlatten::FCamera Camera;
        TArray<FSplinePointData> Candidate;
        FString Summary;
        bool bValid = false;
        bool bShowOverlay = true;
    };
    TSharedRef<FFlattenState> State = MakeShared<FFlattenState>();
    TSharedRef<SWindow> Dialog = SNew(SWindow)
        .Title(LOCTEXT("CameraFlattenTitle", "Flatten to Camera View — Perspective Preview"))
        .ClientSize(FVector2D(520.0, 300.0))
        .SizingRule(ESizingRule::UserSized)
        .SupportsMaximize(false).SupportsMinimize(false);
    ActiveFlattenWindow = Dialog;
    TWeakPtr<FStrandSplineAssetEditor> WeakOwner = SharedThis(this);
    TWeakPtr<SWindow> WeakWindow = Dialog;
    TWeakPtr<SStrandSplineViewport> WeakViewport = PreviewWidget;
    Dialog->SetOnWindowClosed(FOnWindowClosed::CreateLambda([WeakOwner, WeakViewport](const TSharedRef<SWindow>&)
    {
        if (TSharedPtr<SStrandSplineViewport> Viewport = WeakViewport.Pin())
        { Viewport->SetCleanupPreviewPolyline({}); }
        if (TSharedPtr<FStrandSplineAssetEditor> Owner = WeakOwner.Pin()) { Owner->ActiveFlattenWindow.Reset(); }
    }));
    auto CaptureCamera = [WeakViewport, State]()
    {
        if (TSharedPtr<SStrandSplineViewport> Viewport = WeakViewport.Pin())
        {
            State->Camera.Location = Viewport->GetCameraLocation();
            State->Camera.Forward = Viewport->GetCameraForward();
            State->Camera.HorizontalFOVDegrees = Viewport->GetCameraHorizontalFOV();
            State->Camera.ViewportWidth = Viewport->GetCameraViewportWidth();
        }
    };
    auto Refresh = [WeakOwner, WeakViewport, State]()
    {
        TSharedPtr<FStrandSplineAssetEditor> Owner = WeakOwner.Pin();
        USplineDataAsset* AssetNow = Owner.IsValid() ? Owner->Asset.Get() : nullptr;
        State->bValid = false;
        State->Candidate.Reset();
        if (!AssetNow || !Owner->NativePreview.IsValid())
        { State->Summary = TEXT("Source spline unavailable."); return; }
        State->bValid = StrandPerspectiveFlatten::Build(Owner->NativePreview.Get(), AssetNow->Points,
            AssetNow->bIsClosedLoop, State->Camera, State->TolerancePixels,
            State->Candidate, State->Summary);
        if (TSharedPtr<SStrandSplineViewport> Viewport = WeakViewport.Pin())
        {
            TArray<FVector> Sampled;
            if (State->bValid && State->bShowOverlay)
            {
                USplineComponent* Proposed = NewObject<USplineComponent>(GetTransientPackage(), NAME_None, RF_Transient);
                Proposed->ClearSplinePoints(false);
                for (int32 I = 0; I < State->Candidate.Num(); ++I)
                { Proposed->AddPoint(State->Candidate[I].ToSplinePoint(I), false); }
                Proposed->SetClosedLoop(AssetNow->bIsClosedLoop, false);
                Proposed->UpdateSpline();
                const int32 Segments = FMath::Clamp(State->Candidate.Num() * 12, 128, 2048);
                const double Length = Proposed->GetSplineLength();
                for (int32 I = 0; I <= Segments; ++I)
                { Sampled.Add(Proposed->GetLocationAtDistanceAlongSpline(Length * I / Segments, ESplineCoordinateSpace::Local)); }
            }
            if (State->bValid)
            {
                State->Summary = FString::Printf(TEXT("%d -> %d anchors | Target deviation <= %.2f px (sampled) | Camera captured; refresh to recapture view."),
                    AssetNow->Points.Num(), State->Candidate.Num(), State->TolerancePixels);
            }
            Viewport->SetCleanupPreviewPolyline(Sampled);
        }
    };
    CaptureCamera();
    Dialog->SetContent(SNew(SBorder).Padding(FMargin(16.0f))
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("FlattenIntro", "Orange is the proposed flattened curve; cyan is unchanged. The entire spline is projected through the captured perspective camera onto one plane. It should keep the same silhouette from that view; orbit afterward to see the flattened result."))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(STextBlock).Text(LOCTEXT("FlattenToleranceLabel", "Maximum screen deviation (pixels)"))]
            + SHorizontalBox::Slot().AutoWidth()
            [SNew(SBox).WidthOverride(130)
                [SNew(SNumericEntryBox<double>)
                    .MinValue(0.25).MaxValue(16.0)
                    .MinSliderValue(0.25).MaxSliderValue(8.0)
                    .AllowSpin(true)
                    .Value_Lambda([State]() -> TOptional<double> { return State->TolerancePixels; })
                    .OnValueChanged_Lambda([State, Refresh](double Value)
                    { State->TolerancePixels = FMath::Clamp(Value, 0.25, 16.0); Refresh(); })]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SCheckBox)
            .IsChecked_Lambda([State]() { return State->bShowOverlay ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State, Refresh](ECheckBoxState Value)
            { State->bShowOverlay = Value == ECheckBoxState::Checked; Refresh(); })
            [SNew(STextBlock).Text(LOCTEXT("FlattenShowPreview", "Show proposed curve overlay"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SButton).Text(LOCTEXT("FlattenRefresh", "Capture Current View and Refresh Preview"))
            .OnClicked_Lambda([CaptureCamera, Refresh]() { CaptureCamera(); Refresh(); return FReply::Handled(); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [SNew(STextBlock).AutoWrapText(true).Text_Lambda([State]() { return FText::FromString(State->Summary); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("FlattenCaveat", "The preview approximates the captured image-plane curve within a sampled pixel tolerance. It may insert anchors and convert tangent modes. Original point rotations/scales are retained at source anchors. Cancel leaves the asset untouched."))]
        + SVerticalBox::Slot().FillHeight(1) [SNullWidget::NullWidget]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1) [SNullWidget::NullWidget]
            + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
            [SNew(SButton).Text(LOCTEXT("FlattenCancel", "Cancel"))
                .OnClicked_Lambda([WeakWindow]()
                { if (TSharedPtr<SWindow> Window = WeakWindow.Pin()) { Window->RequestDestroyWindow(); } return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth()
            [SNew(SButton).Text(LOCTEXT("FlattenApply", "Apply Flatten"))
                .IsEnabled_Lambda([State]() { return State->bValid; })
                .OnClicked_Lambda([WeakOwner, WeakWindow, WeakViewport, State, Refresh]()
                {
                    Refresh(); // Prevent a modeless dialog committing a stale source curve.
                    TSharedPtr<FStrandSplineAssetEditor> Owner = WeakOwner.Pin();
                    USplineDataAsset* AssetNow = Owner.IsValid() ? Owner->Asset.Get() : nullptr;
                    if (!AssetNow || !State->bValid) { return FReply::Handled(); }
                    Owner->EndViewportPointDrag();
                    const FScopedTransaction Transaction(LOCTEXT("PerspectiveFlattenUndo", "Flatten Strand to Captured Camera View"));
                    AssetNow->Modify();
                    AssetNow->Points = State->Candidate;
                    if (TSharedPtr<SStrandSplineViewport> Viewport = WeakViewport.Pin()) { Viewport->SetCleanupPreviewPolyline({}); }
                    Owner->CommitSplineEdit(INDEX_NONE);
                    if (TSharedPtr<SWindow> Window = WeakWindow.Pin()) { Window->RequestDestroyWindow(); }
                    return FReply::Handled();
                })]]]);
    Refresh();
    FSlateApplication::Get().AddWindow(Dialog);
}

FText FStrandSplineAssetEditor::GetEditPlaneLabel() const
{
    switch (EditPlane)
    {
    case EStrandEditPlane::XY: return LOCTEXT("EditXYLabel", "Edit: XY");
    case EStrandEditPlane::XZ: return LOCTEXT("EditXZLabel", "Edit: XZ");
    case EStrandEditPlane::YZ: return LOCTEXT("EditYZLabel", "Edit: YZ");
    case EStrandEditPlane::CameraPlane: return LOCTEXT("EditCameraLabel", "Edit: Camera");
    case EStrandEditPlane::ThreePointPlane: return LOCTEXT("Edit3PointLabel", "Edit: 3-Point");
    default: return LOCTEXT("Edit3DLabel", "Edit: 3D");
    }
}

void FStrandSplineAssetEditor::SetEditPlane(EStrandEditPlane NewPlane)
{
    EndViewportPointDrag();
    EditPlane = NewPlane;
    if (NewPlane == EStrandEditPlane::CameraPlane && PreviewWidget.IsValid())
    {
        LockedCameraPlaneNormal = PreviewWidget->GetCameraForward();
    }
    if (PreviewWidget.IsValid())
    {
        PreviewWidget->SetCameraAlignedEditing(NewPlane == EStrandEditPlane::CameraPlane);
        PreviewWidget->SetPenPlane(NewPlane == EStrandEditPlane::XY ? 2 :
            NewPlane == EStrandEditPlane::XZ ? 1 : NewPlane == EStrandEditPlane::YZ ? 0 : -1);
        PreviewWidget->SetPenCustomPlane(NewPlane == EStrandEditPlane::ThreePointPlane,
            CustomPlaneOrigin, CustomPlaneNormal);
    }
    // A working plane never changes the camera projection. View and edit constraints
    // are independent; framing and view selection are explicit user operations.
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeEditPlaneMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto Entry = [this, &Menu](EStrandEditPlane Plane, const FText& Label, const FText& Description)
    {
        Menu.AddMenuEntry(Label, Description, FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Plane]() { SetEditPlane(Plane); }),
                FCanExecuteAction(), FIsActionChecked::CreateLambda([this, Plane]() { return EditPlane == Plane; })),
            NAME_None, EUserInterfaceActionType::RadioButton);
    };
    Entry(EStrandEditPlane::Free3D, LOCTEXT("Edit3D", "Free 3D"), LOCTEXT("Edit3DHelp", "Unrestricted X, Y, Z movement."));
    Entry(EStrandEditPlane::XY, LOCTEXT("EditXY", "World XY Plane"), LOCTEXT("EditXYHelp", "Edit only X/Y; leave existing Z coordinates untouched."));
    Entry(EStrandEditPlane::XZ, LOCTEXT("EditXZ", "World XZ Plane"), LOCTEXT("EditXZHelp", "Edit only X/Z; leave existing Y coordinates untouched."));
    Entry(EStrandEditPlane::YZ, LOCTEXT("EditYZ", "World YZ Plane"), LOCTEXT("EditYZHelp", "Edit only Y/Z; leave existing X coordinates untouched."));
    Entry(EStrandEditPlane::CameraPlane, LOCTEXT("EditCameraPlane", "Camera-Aligned 2D Editing"),
        LOCTEXT("EditCameraPlaneHelp", "Keep perspective camera navigation. Each new drag captures the current camera-aligned plane; original depth is preserved. This does not flatten the spline."));
    Menu.AddMenuEntry(LOCTEXT("CaptureThreePointPlane", "Working Plane from 3 Selected Anchors"),
        LOCTEXT("CaptureThreePointPlaneTip", "Select exactly three non-collinear anchors. Capture their plane for constrained movement. No geometry changes; remains fixed while orbiting the camera."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FStrandSplineAssetEditor::SetPlaneFromThreeSelectedAnchors),
            FCanExecuteAction::CreateLambda([this]() { return GetSelectedIndices().Num() == 3; })));
    Menu.AddMenuSeparator();
    Menu.AddMenuEntry(LOCTEXT("LiveAnchorSnapToggle", "Snap to Other Anchors While Moving"),
        LOCTEXT("LiveAnchorSnapTooltip", "Opt-in interactive snap while moving selected anchors. Uses the primary anchor as the reference, and moves the whole selection by the same final offset. The magenta preview is not committed until release. Disabled while soft selection is active. Hold Ctrl during a drag to temporarily suppress snapping."),
        FSlateIcon(), FUIAction(
            FExecuteAction::CreateLambda([this]()
            {
                bInteractiveAnchorSnap = !bInteractiveAnchorSnap;
                GConfig->SetBool(TEXT("StrandSplineEditor"), TEXT("InteractiveAnchorSnap"),
                    bInteractiveAnchorSnap, GEditorPerProjectIni);
                GConfig->Flush(false, GEditorPerProjectIni);
                if (!bInteractiveAnchorSnap && PreviewWidget.IsValid())
                { PreviewWidget->SetSnapPreviewSegments({}); }
            }), FCanExecuteAction(),
            FIsActionChecked::CreateLambda([this]() { return bInteractiveAnchorSnap; })),
        NAME_None, EUserInterfaceActionType::ToggleButton);
    Menu.AddSubMenu(LOCTEXT("LiveSnapTargetsLabel", "Snap During Drag: Target Priority"),
        LOCTEXT("LiveSnapTargetsHelp", "Choose whether interactive snapping targets unselected anchors, non-adjacent spline segments, or whichever is closest. For performance, curve evaluation is used only when enabled."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            struct FSnapModeOption { int32 Mode; const TCHAR* Name; };
            const FSnapModeOption Options[] =
            {
                {0, TEXT("Other Anchors")},
                {1, TEXT("Other Curve Segments")},
                {2, TEXT("Nearest of Anchors and Segments")}
            };
            for (const auto& Option : Options)
            {
                Sub.AddMenuEntry(FText::FromString(Option.Name), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, Mode=Option.Mode]()
                    {
                        InteractiveSnapTargetMode = Mode;
                        GConfig->SetInt(TEXT("StrandSplineEditor"), TEXT("InteractiveSnapTargetMode"),
                            Mode, GEditorPerProjectIni);
                        GConfig->Flush(false, GEditorPerProjectIni);
                    }), FCanExecuteAction(),
                    FIsActionChecked::CreateLambda([this, Mode=Option.Mode]()
                    { return InteractiveSnapTargetMode == Mode; })),
                    NAME_None, EUserInterfaceActionType::RadioButton);
            }
        }));
    Menu.AddMenuSeparator();
    // The removed "Perspective (Navigable Camera-Aligned Editing)" entry
    // merely reset viewport projection; it was not a separate edit mode.
    // Camera navigation remains available through the normal viewport controls.
    Menu.AddSubMenu(LOCTEXT("ScreenSpaceRotate", "Rotate Around Camera (Numeric)"),
        LOCTEXT("ScreenSpaceRotateHelp", "Undoable screen-space rotation about the camera forward axis. Relative to the current selection pivot."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            for (const double Degrees : {-90.0, -15.0, 15.0, 90.0})
            {
                Sub.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%+g degrees"), Degrees)), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, Degrees]() { RotateSelectionAroundCamera(Degrees); })));
            }
            Sub.AddWidget(SNew(SNumericEntryBox<double>)
                .Value(TOptional<double>())
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { RotateSelectionAroundCamera(Value); }),
                LOCTEXT("ScreenRotationCustom", "Custom Delta (deg)"), true);
        }));
    Menu.AddMenuSeparator();
    Menu.AddMenuEntry(LOCTEXT("FlattenCameraPlane", "Flatten Entire Spline to Camera View..."),
        LOCTEXT("FlattenCameraPlaneHelp", "Preview the complete spline projected along perspective rays onto one camera-aligned plane. Preserves its on-screen silhouette, with adaptive curves and a user-selected pixel tolerance. Apply/Cancel; Undo supported."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { OpenPerspectiveFlattenDialog(); }),
            FCanExecuteAction::CreateLambda([this]() { return Asset.IsValid() && Asset->Points.Num() >= 2; })));
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
    if (EditPlane == EStrandEditPlane::CameraPlane && PreviewWidget.IsValid())
    { Constrained = StrandCameraSpace::ProjectDelta(Constrained, PreviewWidget->GetCameraForward()); }
    if (EditPlane == EStrandEditPlane::ThreePointPlane)
    { Constrained = StrandCameraSpace::ProjectDelta(Constrained, CustomPlaneNormal); }
    if (Constrained.IsNearlyZero()) { return; }
    if (!ActivePointDragTransaction) { BeginViewportPointDrag(); }
    if (!ActivePointDragTransaction) { return; }
    // A handle drag is a shape edit, not an anchor translation. When soft
    // selection is active, move the corresponding tangent of each influenced
    // anchor by a frozen proportion of the same handle delta. Do not move
    // anchor positions or change the untouched anchors' handle modes.
    bool bEdited = false;
    if (SoftSelectionMode != 0 && FrozenSoftSelectionWeights.Num() == Current->Points.Num())
    {
        // Snapshot evaluated Auto tangents before modifying any data. A rebuild
        // must never change the input to another anchor in the same gesture.
        TArray<FSplinePoint> EvaluatedPoints;
        EvaluatedPoints.Reserve(Current->Points.Num());
        for (int32 Index = 0; Index < Current->Points.Num(); ++Index)
        {
            EvaluatedPoints.Add(NativePreview->GetSplinePointAt(Index, ESplineCoordinateSpace::Local));
        }
        bEdited = StrandCameraSpace::ApplyWeightedTangentDelta(Current->Points,
            EvaluatedPoints, FrozenSoftSelectionWeights, Handle, Constrained * 3.0);
    }
    else
    {
        FSplinePointData& Point = Current->Points[SelectedPointIndex];
        if (Point.PointType != ESplinePointType::CurveCustomTangent)
        {
            const FSplinePoint Evaluated = NativePreview->GetSplinePointAt(SelectedPointIndex, ESplineCoordinateSpace::Local);
            Point.ArriveTangent = Evaluated.ArriveTangent;
            Point.LeaveTangent = Evaluated.LeaveTangent;
            Point.PointType = ESplinePointType::CurveCustomTangent;
        }
        const FVector Target = Handle == 1 ? Point.ArriveTangent - Constrained * 3.0
            : Point.LeaveTangent + Constrained * 3.0;
        bEdited = StrandSplineEditOperations::EditHandle(Point, Handle, Target);
    }
    if (!bEdited) { return; }
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
    if (StrandSplineEditOperations::ExtendOpenEndpoint(Current->Points, NativePreview.Get(), true))
    {
        CommitSplineEdit(0);
    }
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

FReply FStrandSplineAssetEditor::SnapSelectionToOtherGeometry(bool bSegmentTarget)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || !PreviewWidget.IsValid() || SelectedPointIndices.IsEmpty())
    { return FReply::Handled(); }
    if (TSharedPtr<SWindow> Existing = ActiveSnapWindow.Pin())
    { Existing->BringToFront(true); return FReply::Handled(); }
    if (TSharedPtr<SWindow> Flatten = ActiveFlattenWindow.Pin()) { Flatten->RequestDestroyWindow(); }
    if (TSharedPtr<SWindow> Cleanup = ActiveCleanupWindow.Pin()) { Cleanup->RequestDestroyWindow(); }
    EndViewportPointDrag();

    struct FSnapState
    {
        TArray<TPair<int32, FVector>> Changes;
        FString Summary;
        bool bShowPreview = true;
        double Tolerance = 100.0;
    };
    TSharedRef<FSnapState> State = MakeShared<FSnapState>();
    State->Tolerance = PrecisionSnapToleranceCm;
    const TWeakPtr<FStrandSplineAssetEditor> WeakOwner = SharedThis(this);
    const TWeakPtr<SStrandSplineViewport> WeakViewport = PreviewWidget;
    TSharedRef<SWindow> Dialog = SNew(SWindow)
        .Title(bSegmentTarget ? LOCTEXT("SnapSegmentPreviewTitle", "Strand - Snap to Curve Preview")
            : LOCTEXT("SnapAnchorPreviewTitle", "Strand - Snap to Anchor Preview"))
        .ClientSize(FVector2D(480.0, 255.0)).SizingRule(ESizingRule::UserSized)
        .SupportsMaximize(false).SupportsMinimize(false);
    const TWeakPtr<SWindow> WeakWindow = Dialog;
    ActiveSnapWindow = Dialog;
    Dialog->SetOnWindowClosed(FOnWindowClosed::CreateLambda([WeakOwner, WeakViewport](const TSharedRef<SWindow>&)
    {
        if (TSharedPtr<SStrandSplineViewport> Viewport = WeakViewport.Pin())
        { Viewport->SetSnapPreviewSegments({}); }
        if (TSharedPtr<FStrandSplineAssetEditor> Owner = WeakOwner.Pin())
        { Owner->ActiveSnapWindow.Reset(); }
    }));

    auto Refresh = [WeakOwner, WeakViewport, State, bSegmentTarget]()
    {
        TSharedPtr<FStrandSplineAssetEditor> Owner = WeakOwner.Pin();
        USplineDataAsset* Source = Owner.IsValid() ? Owner->Asset.Get() : nullptr;
        State->Changes.Reset();
        TArray<TPair<FVector, FVector>> PreviewLines;
        if (Source && Owner->NativePreview.IsValid() && FMath::IsFinite(State->Tolerance) && State->Tolerance > 0.0)
        {
            const TArray<int32> Selection = Owner->GetSelectedIndices();
            StrandPrecision::BuildSnapPlan(Source->Points, Owner->NativePreview.Get(), Selection,
                bSegmentTarget, State->Tolerance, State->Changes);
            for (const TPair<int32, FVector>& Change : State->Changes)
            { PreviewLines.Emplace(Source->Points[Change.Key].LocalPosition, Change.Value); }
            State->Summary = State->Changes.IsEmpty()
                ? TEXT("No eligible targets within the maximum distance. Nothing will change.")
                : FString::Printf(TEXT("%d of %d selected anchors will move. Magenta shows target positions; original curve stays unchanged until Apply."),
                    State->Changes.Num(), Selection.Num());
        }
        else { State->Summary = TEXT("Source unavailable or invalid snap distance."); }
        if (TSharedPtr<SStrandSplineViewport> Viewport = WeakViewport.Pin())
        { Viewport->SetSnapPreviewSegments(State->bShowPreview ? PreviewLines : TArray<TPair<FVector, FVector>>()); }
    };

    Dialog->SetContent(SNew(SBorder).Padding(FMargin(16.0f))
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("SnapPreviewIntro", "Preview the anchor positions before snapping. The magenta target dots and connecting lines are temporary. Drag the window aside to inspect the spline."))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(STextBlock).Text(LOCTEXT("SnapPreviewTolerance", "Maximum snap distance (cm)"))]
            + SHorizontalBox::Slot().AutoWidth()
            [SNew(SBox).WidthOverride(125)
                [SNew(SNumericEntryBox<double>)
                    .MinValue(0.001).MaxValue(100000.0).AllowSpin(false)
                    .Value_Lambda([State]() -> TOptional<double> { return State->Tolerance; })
                    .OnValueCommitted_Lambda([State, Refresh](double Value, ETextCommit::Type)
                    { if (FMath::IsFinite(Value)) { State->Tolerance = FMath::Clamp(Value, 0.001, 100000.0); Refresh(); } })]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SCheckBox)
            .IsChecked_Lambda([State]() { return State->bShowPreview ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([State, Refresh](ECheckBoxState Value)
            { State->bShowPreview = Value == ECheckBoxState::Checked; Refresh(); })
            [SNew(STextBlock).Text(LOCTEXT("SnapPreviewToggle", "Show snap targets in viewport"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SButton).Text(LOCTEXT("SnapPreviewRefresh", "Refresh Targets"))
            .OnClicked_Lambda([Refresh]() { Refresh(); return FReply::Handled(); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [SNew(STextBlock).AutoWrapText(true).Text_Lambda([State]() { return FText::FromString(State->Summary); })]
        + SVerticalBox::Slot().FillHeight(1) [SNullWidget::NullWidget]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1) [SNullWidget::NullWidget]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [SNew(SButton).Text(LOCTEXT("SnapPreviewCancel", "Cancel"))
                .OnClicked_Lambda([WeakWindow]()
                { if (TSharedPtr<SWindow> Window = WeakWindow.Pin()) { Window->RequestDestroyWindow(); } return FReply::Handled(); })]
            + SHorizontalBox::Slot().AutoWidth()
            [SNew(SButton).Text(LOCTEXT("SnapPreviewApply", "Apply Snap"))
                .IsEnabled_Lambda([State]() { return !State->Changes.IsEmpty(); })
                .OnClicked_Lambda([WeakOwner, WeakWindow, WeakViewport, State, Refresh, bSegmentTarget]()
                {
                    Refresh(); // A modeless dialog must not commit stale target positions.
                    TSharedPtr<FStrandSplineAssetEditor> Owner = WeakOwner.Pin();
                    USplineDataAsset* Source = Owner.IsValid() ? Owner->Asset.Get() : nullptr;
                    if (!Source || State->Changes.IsEmpty()) { return FReply::Handled(); }
                    Owner->EndViewportPointDrag();
                    const FScopedTransaction Transaction(bSegmentTarget
                        ? LOCTEXT("SnapCurvePreviewUndo", "Snap Strand Anchors to Other Curve Segments")
                        : LOCTEXT("SnapAnchorPreviewUndo", "Snap Strand Anchors to Other Anchors"));
                    Source->Modify();
                    for (const TPair<int32, FVector>& Change : State->Changes)
                    { if (Source->Points.IsValidIndex(Change.Key)) { Source->Points[Change.Key].LocalPosition = Change.Value; } }
                    Owner->PrecisionSnapToleranceCm = State->Tolerance;
                    GConfig->SetDouble(TEXT("StrandSplineEditor"), TEXT("PrecisionSnapToleranceCm"),
                        State->Tolerance, GEditorPerProjectIni);
                    GConfig->Flush(false, GEditorPerProjectIni);
                    Source->PostEditChange();
                    Source->MarkPackageDirty();
                    Owner->RebuildNativePreview();
                    if (Owner->DetailsView.IsValid()) { Owner->DetailsView->ForceRefresh(); }
                    if (TSharedPtr<SStrandSplineViewport> Viewport = WeakViewport.Pin()) { Viewport->SetSnapPreviewSegments({}); }
                    if (TSharedPtr<SWindow> Window = WeakWindow.Pin()) { Window->RequestDestroyWindow(); }
                    return FReply::Handled();
                })]]]);
    Refresh();
    FSlateApplication::Get().AddWindow(Dialog);
    return FReply::Handled();
}

void FStrandSplineAssetEditor::SetPlaneFromThreeSelectedAnchors()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current) { return; }
    FVector Origin, Normal;
    if (!StrandPrecision::PlaneFromThreePoints(Current->Points, GetSelectedIndices(), Origin, Normal)) { return; }
    CustomPlaneOrigin = Origin;
    CustomPlaneNormal = Normal;
    SetEditPlane(EStrandEditPlane::ThreePointPlane);
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

FReply FStrandSplineAssetEditor::RotateSelection(int32 Axis, double Degrees)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || SelectedPointIndices.IsEmpty() || Axis < 0 || Axis > 2 ||
        !FMath::IsFinite(Degrees) || FMath::IsNearlyZero(Degrees)) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("RotateSelectedUndo", "Rotate Strand Spline Selection"));
    Current->Modify();
    if (StrandSplineEditOperations::RotateSelected(Current->Points, GetSelectedIndices(), Axis, Degrees))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::ScaleSelection(double Factor)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || SelectedPointIndices.IsEmpty() || !FMath::IsFinite(Factor) || Factor <= 0.0 ||
        FMath::IsNearlyEqual(Factor, 1.0)) { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ScaleSelectedUndo", "Scale Strand Spline Selection"));
    Current->Modify();
    if (StrandSplineEditOperations::ScaleSelected(Current->Points, GetSelectedIndices(), Factor))
    {
        Current->PostEditChange();
        Current->MarkPackageDirty();
        RebuildNativePreview();
        if (DetailsView.IsValid()) { DetailsView->ForceRefresh(); }
    }
    return FReply::Handled();
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeTransformMenu()
{
    FMenuBuilder Menu(true, nullptr);
    Menu.BeginSection(TEXT("Rotate"), LOCTEXT("RotateSelectionSection", "Rotate Selection Around Center"));
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const TCHAR* AxisName = Axis == 0 ? TEXT("X") : Axis == 1 ? TEXT("Y") : TEXT("Z");
        const FText AxisLabel = FText::FromString(FString::Printf(TEXT("Rotate %s +15 degrees"), AxisName));
        const FText ReverseLabel = FText::FromString(FString::Printf(TEXT("Rotate %s -15 degrees"), AxisName));
        Menu.AddMenuEntry(AxisLabel, LOCTEXT("RotateStepTip", "Rotate only selected anchors and tangents about their center. Undoable."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Axis]() { RotateSelection(Axis, 15.0); })));
        Menu.AddMenuEntry(ReverseLabel, FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Axis]() { RotateSelection(Axis, -15.0); })));
    }
    Menu.EndSection();
    Menu.BeginSection(TEXT("Scale"), LOCTEXT("ScaleSelectionSection", "Uniform Scale Around Center"));
    Menu.AddMenuEntry(LOCTEXT("HalfSize", "Scale 50%"), FText::GetEmpty(), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { ScaleSelection(0.5); })));
    Menu.AddMenuEntry(LOCTEXT("DoubleSize", "Scale 200%"), FText::GetEmpty(), FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this]() { ScaleSelection(2.0); })));
    Menu.EndSection();
    return Menu.MakeWidget();
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

FReply FStrandSplineAssetEditor::ResampleSpline(int32 TargetPoints)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || Current->Points.Num() < 2 ||
        TargetPoints < 2 || TargetPoints > 4096)
    {
        return FReply::Handled();
    }
    TArray<FSplinePointData> NewPoints;
    if (!StrandSplineEditOperations::ResampleByArcLength(NativePreview.Get(),
        Current->Points, Current->bIsClosedLoop, TargetPoints, NewPoints))
    {
        return FReply::Handled();
    }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ResampleSplineUndo", "Resample Strand Spline"));
    Current->Modify();
    Current->Points = MoveTemp(NewPoints);
    CommitSplineEdit(INDEX_NONE);
    ResampleTargetCount = TargetPoints;
    GConfig->SetInt(TEXT("StrandSplineEditor"), TEXT("LastResamplePointCount"),
        ResampleTargetCount, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SubdivideWithCuts(int32 Cuts, bool bDistanceUniform)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || Cuts < 1 || Cuts > 32) { return FReply::Handled(); }
    TArray<FSplinePointData> Result;
    if (!StrandSplineCleanup::SubdivideExact(Current->Points, NativePreview.Get(), Cuts, bDistanceUniform, Result))
    { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("ExactSubdivideTransaction", "Exact Strand Curve Subdivision"));
    Current->Modify();
    Current->Points = MoveTemp(Result);
    CommitSplineEdit(INDEX_NONE);
    SubdivideCuts = Cuts;
    GConfig->SetInt(TEXT("StrandSplineEditor"), TEXT("LastExactSubdivideCuts"), Cuts, GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SetSelectedContinuity(bool bC1)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || SelectedPointIndices.IsEmpty()) { return FReply::Handled(); }
    TArray<FSplinePointData> Result = Current->Points;
    if (!StrandSplineCleanup::SetContinuity(Result, NativePreview.Get(), GetSelectedIndices(), bC1))
    { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(bC1 ? LOCTEXT("C1ContinuityTransaction", "Set Strand C1 Continuity")
        : LOCTEXT("G1ContinuityTransaction", "Set Strand G1 Continuity"));
    Current->Modify();
    Current->Points = MoveTemp(Result);
    CommitSplineEdit(SelectedPointIndex, true);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::NormalizeTangents(double LengthCm)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || SelectedPointIndices.IsEmpty()) { return FReply::Handled(); }
    TArray<FSplinePointData> Result = Current->Points;
    if (!StrandSplineCleanup::NormalizeSelectedTangents(Result, NativePreview.Get(), GetSelectedIndices(), LengthCm))
    { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("NormalizeTangentsTransaction", "Normalize Strand Tangent Lengths"));
    Current->Modify();
    Current->Points = MoveTemp(Result);
    CommitSplineEdit(SelectedPointIndex, true);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::RecalculateTangents()
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || SelectedPointIndices.IsEmpty()) { return FReply::Handled(); }
    TArray<FSplinePointData> Result = Current->Points;
    if (!StrandSplineCleanup::RecalculateSelectedTangents(Result, NativePreview.Get(), GetSelectedIndices()))
    { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("RecalculateTangentsTransaction", "Recalculate Strand Tangents"));
    Current->Modify();
    Current->Points = MoveTemp(Result);
    CommitSplineEdit(SelectedPointIndex, true);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::WeldEndpoints(double DistanceCm)
{
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid()) { return FReply::Handled(); }
    TArray<FSplinePointData> Result = Current->Points;
    bool bClosed = Current->bIsClosedLoop;
    if (!StrandSplineCleanup::WeldLoopEndpoints(Result, bClosed, NativePreview.Get(), DistanceCm))
    { return FReply::Handled(); }
    EndViewportPointDrag();
    const FScopedTransaction Transaction(LOCTEXT("WeldEndpointsTransaction", "Weld Strand Loop Endpoints"));
    Current->Modify();
    Current->Points = MoveTemp(Result);
    Current->bIsClosedLoop = bClosed;
    CommitSplineEdit(INDEX_NONE);
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SelectDuplicateAnchors(double DistanceCm)
{
    const USplineDataAsset* Current = Asset.Get();
    if (!Current) { return FReply::Handled(); }
    const TArray<int32> Duplicates = StrandSplineCleanup::FindDuplicateAnchors(Current->Points,
        Current->bIsClosedLoop, DistanceCm);
    SelectedPointIndices.Reset();
    for (int32 I : Duplicates) { SelectedPointIndices.Add(I); }
    SelectedPointIndex = Duplicates.IsEmpty() ? INDEX_NONE : Duplicates[0];
    SyncViewportSelection();
    return FReply::Handled();
}

FReply FStrandSplineAssetEditor::SelectBySegmentLength(double ThresholdCm, bool bShorter)
{
    if (!NativePreview.IsValid()) { return FReply::Handled(); }
    const TArray<int32> Found = StrandSplineCleanup::SelectBySegmentLength(NativePreview.Get(), ThresholdCm, bShorter);
    SelectedPointIndices.Reset();
    for (int32 I : Found) { SelectedPointIndices.Add(I); }
    SelectedPointIndex = Found.IsEmpty() ? INDEX_NONE : Found[0];
    SyncViewportSelection();
    return FReply::Handled();
}

namespace
{
    struct FStrandCleanupDialogState
    {
        float ToleranceCm = 1.0f;
        bool bProtectCorners = true;
        bool bShowPreview = true;
        int32 Mode = 0; // 0: simplify, 1: dissolve, 2: degenerate repair
        StrandSplineCleanup::FResult Result;
        bool bCanApply = false;
        FString Summary;
    };
}

void FStrandSplineAssetEditor::OpenCurveCleanupDialog()
{
    if (TSharedPtr<SWindow> Snap = ActiveSnapWindow.Pin()) { Snap->RequestDestroyWindow(); }
    USplineDataAsset* Current = Asset.Get();
    if (!Current || !NativePreview.IsValid() || Current->Points.Num() < 3) { return; }
    if (TSharedPtr<SWindow> Flatten = ActiveFlattenWindow.Pin()) { Flatten->RequestDestroyWindow(); }
    if (TSharedPtr<SWindow> OpenWindow = ActiveCleanupWindow.Pin())
    {
        OpenWindow->BringToFront(true);
        return; // Avoid competing previews.
    }
    TSharedRef<FStrandCleanupDialogState> State = MakeShared<FStrandCleanupDialogState>();
    GConfig->GetFloat(TEXT("StrandSplineEditor"), TEXT("LastCleanupToleranceCm"), State->ToleranceCm, GEditorPerProjectIni);
    State->ToleranceCm = FMath::Clamp(State->ToleranceCm, 0.01f, 10000.0f);
    TSharedRef<SWindow> Dialog = SNew(SWindow)
        .Title(LOCTEXT("CleanupWindowTitle", "Strand - Curve Cleanup"))
        .ClientSize(FVector2D(525.0f, 405.0f))
        .SupportsMaximize(false)
        .SupportsMinimize(false);
    ActiveCleanupWindow = Dialog;
    const TWeakPtr<SWindow> WeakCleanupWindow = Dialog;
    const TWeakPtr<FStrandSplineAssetEditor> WeakCleanupEditor =
        StaticCastSharedRef<FStrandSplineAssetEditor>(AsShared());
    const TWeakPtr<SStrandSplineViewport> WeakCleanupViewport = PreviewWidget;
    Dialog->SetOnWindowClosed(FOnWindowClosed::CreateLambda(
        [WeakCleanupViewport, WeakCleanupEditor](const TSharedRef<SWindow>&)
        {
            if (TSharedPtr<FStrandSplineAssetEditor> Owner = WeakCleanupEditor.Pin())
            { Owner->ActiveCleanupWindow.Reset(); }
            if (TSharedPtr<SStrandSplineViewport> Viewport = WeakCleanupViewport.Pin())
            { Viewport->SetCleanupPreviewPolyline({}); }
        }));

    // Preview is evaluated from an independent transient native spline. Neither
    // sliders nor operation switches modify the asset or the editing component.
    const auto RefreshPreview = [WeakCleanupEditor, State]()
    {
        TSharedPtr<FStrandSplineAssetEditor> Owner = WeakCleanupEditor.Pin();
        if (!Owner.IsValid()) { return; }
        USplineDataAsset* Active = Owner->Asset.Get();
        State->bCanApply = false;
        State->Result = StrandSplineCleanup::FResult();
        if (!Active || !Owner->NativePreview.IsValid()) { return; }
        if (Active->Points.Num() > 512)
        {
            State->Summary = TEXT("Cleanup preview supports up to 512 anchors. Resample the input first.");
            if (Owner->PreviewWidget.IsValid()) { Owner->PreviewWidget->SetCleanupPreviewPolyline({}); }
            return;
        }
        StrandSplineCleanup::FResult Candidate;
        const bool bValid = State->Mode == 0
            ? StrandSplineCleanup::Simplify(Active->Points, Owner->NativePreview.Get(), State->ToleranceCm,
                State->bProtectCorners, nullptr, Candidate)
            : State->Mode == 1
            ? StrandSplineCleanup::Dissolve(Active->Points, Owner->NativePreview.Get(), Owner->GetSelectedIndices(),
                State->ToleranceCm, Candidate)
            : StrandSplineCleanup::RemoveDegenerate(Active->Points, Owner->NativePreview.Get(),
                0.1, State->ToleranceCm, Candidate);
        if (bValid && Candidate.Removed > 0)
        {
            State->Result = MoveTemp(Candidate);
            State->bCanApply = true;
            State->Summary = FString::Printf(TEXT("%d -> %d anchors  |  Removed %d  |  Sampled deviation %.4f cm"),
                Active->Points.Num(), State->Result.Points.Num(), State->Result.Removed,
                State->Result.SampledMaxDeviationCm);
            USplineComponent* Preview = NewObject<USplineComponent>(GetTransientPackage(), NAME_None, RF_Transient);
            Preview->ClearSplinePoints(false);
            for (int32 I = 0; I < State->Result.Points.Num(); ++I)
            { Preview->AddPoint(State->Result.Points[I].ToSplinePoint(I), false); }
            Preview->SetClosedLoop(Active->bIsClosedLoop, false);
            Preview->UpdateSpline();
            const int32 Count = FMath::Clamp(State->Result.Points.Num()*20, 96, 1024);
            const double Length = Preview->GetSplineLength();
            TArray<FVector> Sampled;
            Sampled.Reserve(Count+1);
            for (int32 I = 0; I <= Count; ++I)
            {
                Sampled.Add(Preview->GetLocationAtDistanceAlongSpline(Length*I/Count,
                    ESplineCoordinateSpace::Local));
            }
            if (Owner->PreviewWidget.IsValid()) { Owner->PreviewWidget->SetCleanupPreviewPolyline(State->bShowPreview ? Sampled : TArray<FVector>()); }
        }
        else
        {
            State->Summary = State->Mode == 1
                ? TEXT("No proposed result. Select interior anchors and/or raise the deviation tolerance.")
                : TEXT("No proposed result at this tolerance. Increase the tolerance to allow at least one anchor to be removed.");
            if (Owner->PreviewWidget.IsValid()) { Owner->PreviewWidget->SetCleanupPreviewPolyline({}); }
        }
    };

    // The model is generated once per user interaction, never during viewport Draw.
    Dialog->SetContent(
        SNew(SBorder).Padding(FMargin(16.0f))
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [SNew(STextBlock).AutoWrapText(true)
            .Text(LOCTEXT("CleanupIntro", "Orange = proposed curve; cyan = original. Orange appears only when the operation finds a valid change. Adjust the tolerance to preview a result."))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([State]() { return State->Mode == 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .ToolTipText(LOCTEXT("CleanupSimplifyTip", "Remove as many unnecessary anchors as the sampled deviation tolerance allows."))
                .OnCheckStateChanged_Lambda([State, RefreshPreview](ECheckBoxState) { State->Mode=0; RefreshPreview(); })
                [SNew(STextBlock).Text(LOCTEXT("CleanupSimplify", "Simplify"))]]
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([State]() { return State->Mode == 1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .ToolTipText(LOCTEXT("CleanupDissolveTip", "Remove selected interior anchors only if every dissolve fits within the tolerance."))
                .OnCheckStateChanged_Lambda([State, RefreshPreview](ECheckBoxState) { State->Mode=1; RefreshPreview(); })
                [SNew(STextBlock).Text(LOCTEXT("CleanupDissolve", "Dissolve Selected"))]]
            + SHorizontalBox::Slot().FillWidth(1)
            [SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                .IsChecked_Lambda([State]() { return State->Mode == 2 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .ToolTipText(LOCTEXT("CleanupRepairTip", "Attempt to remove near-duplicate anchors within 0.1 cm without exceeding curve tolerance."))
                .OnCheckStateChanged_Lambda([State, RefreshPreview](ECheckBoxState) { State->Mode=2; RefreshPreview(); })
                [SNew(STextBlock).Text(LOCTEXT("CleanupRepair", "Repair Degenerates"))]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().VAlign(VAlign_Center).FillWidth(1)
                [SNew(STextBlock).Text(LOCTEXT("CleanupToleranceLabel", "Maximum sampled deviation (cm)"))]
            + SHorizontalBox::Slot().AutoWidth()
                [SNew(SBox).WidthOverride(125)
                    [SNew(SNumericEntryBox<float>)
                        .MinValue(0.01f).MaxValue(10000.0f)
                        .MinSliderValue(0.01f).MaxSliderValue(10.0f)
                        .AllowSpin(true)
                        .Value_Lambda([State]() -> TOptional<float> { return State->ToleranceCm; })
                        .OnValueChanged_Lambda([State, RefreshPreview](float Value)
                        { State->ToleranceCm=FMath::Clamp(Value, 0.01f, 10000.0f); RefreshPreview(); })]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [SNew(SCheckBox).IsEnabled_Lambda([State]() { return State->Mode == 0; })
                .IsChecked_Lambda([State]() { return State->bProtectCorners ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([State, RefreshPreview](ECheckBoxState Value)
                { State->bProtectCorners = Value == ECheckBoxState::Checked; RefreshPreview(); })
                .ToolTipText(LOCTEXT("CleanupProtectCornersTip", "Protect sharp turns (30 degrees or more), constant interpolation points and the seam. Simplify only."))
                [SNew(STextBlock).Text(LOCTEXT("CleanupProtectCorners", "Protect corners and path endpoints (Simplify)"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [SNew(SCheckBox)
                .IsChecked_Lambda([State]() { return State->bShowPreview ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([State, RefreshPreview](ECheckBoxState Value)
                { State->bShowPreview = Value == ECheckBoxState::Checked; RefreshPreview(); })
                .ToolTipText(LOCTEXT("CleanupOverlayTip", "Show the proposed geometry in orange over the unchanged original spline in the viewport."))
                [SNew(STextBlock).Text(LOCTEXT("CleanupOverlay", "Show proposed curve overlay"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [SNew(SButton).Text(LOCTEXT("CleanupRefreshPreview", "Refresh Preview"))
                .ToolTipText(LOCTEXT("CleanupRefreshPreviewTip", "Recalculate the proposed curve using the current spline and cleanup settings. Useful if you edited the spline while this window is open."))
                .OnClicked_Lambda([RefreshPreview]() { RefreshPreview(); return FReply::Handled(); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [SNew(STextBlock).AutoWrapText(true)
                .Text_Lambda([State]() { return FText::FromString(State->Summary); })]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [SNew(STextBlock).AutoWrapText(true)
                .Text(LOCTEXT("CleanupSampleNotice", "Deviation is evaluated on sampled source geometry, not a mathematically certified continuous maximum. Undo restores the original points and attributes."))]
        + SVerticalBox::Slot().FillHeight(1) [SNullWidget::NullWidget]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1) [SNullWidget::NullWidget]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
            [SNew(SButton).Text(LOCTEXT("CleanupCancel", "Cancel"))
                .OnClicked_Lambda([WeakCleanupWindow, WeakCleanupViewport]()
                {
                    if (TSharedPtr<SStrandSplineViewport> Viewport = WeakCleanupViewport.Pin())
                    { Viewport->SetCleanupPreviewPolyline({}); }
                    if (TSharedPtr<SWindow> Window = WeakCleanupWindow.Pin()) { Window->RequestDestroyWindow(); }
                    return FReply::Handled();
                })]
            + SHorizontalBox::Slot().AutoWidth()
            [SNew(SButton).Text(LOCTEXT("CleanupApply", "Apply Cleanup"))
                .IsEnabled_Lambda([State]() { return State->bCanApply; })
                .OnClicked_Lambda([WeakCleanupEditor, State, WeakCleanupWindow, RefreshPreview]()
                {
                    // The preview window is modeless: another editor action
                    // could have changed the asset while it was open. Rebuild
                    // the candidate from the current native geometry before
                    // committing, so stale results cannot overwrite edits.
                    RefreshPreview();
                    TSharedPtr<FStrandSplineAssetEditor> Owner = WeakCleanupEditor.Pin();
                    USplineDataAsset* Active = Owner.IsValid() ? Owner->Asset.Get() : nullptr;
                    if (!Active || !State->bCanApply) { return FReply::Handled(); }
                    Owner->EndViewportPointDrag();
                    const FScopedTransaction Transaction(LOCTEXT("CleanupApplyUndo", "Clean Up Strand Spline"));
                    Active->Modify();
                    Active->Points = State->Result.Points;
                    if (Owner->PreviewWidget.IsValid()) { Owner->PreviewWidget->SetCleanupPreviewPolyline({}); }
                    Owner->CommitSplineEdit(INDEX_NONE);
                    GConfig->SetFloat(TEXT("StrandSplineEditor"), TEXT("LastCleanupToleranceCm"), State->ToleranceCm, GEditorPerProjectIni);
                    GConfig->Flush(false, GEditorPerProjectIni);
                    if (TSharedPtr<SWindow> Window = WeakCleanupWindow.Pin()) { Window->RequestDestroyWindow(); }
                    return FReply::Handled();
                })]]]);
    RefreshPreview();
    // A modal dialog prevents the editor viewport from repainting while
    // settings are adjusted, hiding the very preview it is meant to show.
    // Keep the window modeless and close it explicitly on Apply/Cancel.
    if (TSharedPtr<SWindow> Parent = FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr))
    { FSlateApplication::Get().AddWindowAsNativeChild(Dialog, Parent.ToSharedRef(), true); }
    else { FSlateApplication::Get().AddWindow(Dialog); }
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
    // A short command taxonomy replaces the old ~20-item root menu. The
    // active editing modes remain visible on the authoring strip; these are
    // less frequent operations grouped by what the designer is doing.
    FMenuBuilder Menu(true, nullptr);
    Menu.AddMenuEntry(LOCTEXT("CurveCleanupRoot", "Curve Cleanup..."),
        LOCTEXT("CurveCleanupRootTip", "Non-destructive preview for simplifying the spline, dissolving selected anchors and repairing duplicate/degenerate anchors. Uses a sampled 3D deviation tolerance and one Undo transaction."),
        FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &FStrandSplineAssetEditor::OpenCurveCleanupDialog)));
    Menu.AddSubMenu(LOCTEXT("ExactSubdivideMenu", "Exact Subdivision"),
        LOCTEXT("ExactSubdivideTip", "Insert an exact number of cuts per spline segment using de Casteljau splitting. Cubic curve geometry is preserved."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            Sub.AddWidget(SNew(SBox).WidthOverride(190)
                [SNew(SNumericEntryBox<int32>)
                    .Value_Lambda([this]() { return TOptional<int32>(SubdivideCuts); })
                    .MinValue(1).MaxValue(32).AllowSpin(false)
                    .ToolTipText(LOCTEXT("ExactSubdivideCutCountTip", "1-32 exact cuts per original segment. Remembers your last successful count."))
                    .OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type)
                    { SubdivideCuts = FMath::Clamp(Value, 1, 32); })],
                LOCTEXT("ExactSubdivideCutCount", "Cuts per segment"), true);
            Sub.AddMenuEntry(LOCTEXT("ExactSubdivideParam", "Apply: Equal Parameter"),
                LOCTEXT("ExactSubdivideParamTip", "Split each cubic at equal parameter intervals. Preserves the exact cubic geometry."),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]()
                { SubdivideWithCuts(SubdivideCuts, false); })));
            Sub.AddMenuEntry(LOCTEXT("ExactSubdivideArc", "Apply: Equal Distance"),
                LOCTEXT("ExactSubdivideArcTip", "Place cuts approximately equally by arc distance along each segment while preserving cubic geometry."),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]()
                { SubdivideWithCuts(SubdivideCuts, true); })));
        }));
    Menu.AddSubMenu(LOCTEXT("ResampleMenu", "Resample by Arc Length"),
        LOCTEXT("ResampleMenuTip", "Enter an exact target count of 2 to 4096 equally spaced spline anchors. The count is remembered per user, starting at 16. This approximates the source shape and is undoable."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Samples)
        {
            Samples.AddWidget(
                SNew(SBox).WidthOverride(190.0f)
                [SNew(SNumericEntryBox<int32>)
                    .Value_Lambda([this]() { return TOptional<int32>(ResampleTargetCount); })
                    .MinValue(2).MaxValue(4096)
                    .AllowSpin(false)
                    .ToolTipText(LOCTEXT("ResampleExactCountTip", "Exact target control point count, 2 to 4096. Last successfully applied count is remembered for this editor user."))
                    .OnValueCommitted_Lambda([this](int32 Value, ETextCommit::Type) { ResampleTargetCount = FMath::Clamp(Value, 2, 4096); })],
                LOCTEXT("ResamplePointsLabel", "Target points"), true);
            Samples.AddMenuEntry(LOCTEXT("ResampleApply", "Apply Resampling"),
                LOCTEXT("ResampleApplyTip", "Rebuild the current spline with the specified point count. Undo restores the original point geometry."),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ResampleSpline(ResampleTargetCount); })));
        }));
    auto AddAction = [](FMenuBuilder& Target, const FText& Label, const FText& Tip,
        TFunction<void()> Action)
    {
        const TSharedRef<TFunction<void()>> Shared = MakeShared<TFunction<void()>>(MoveTemp(Action));
        Target.AddMenuEntry(Label, Tip, FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([Shared]() { (*Shared)(); })));
    };
    Menu.AddSubMenu(LOCTEXT("PointMenu", "Selected Points"),
        LOCTEXT("PointMenuHelp", "Align, distribute, snap or adjust selected control points."),
        FNewMenuDelegate::CreateLambda([this, AddAction](FMenuBuilder& Sub)
        {
            AddAction(Sub, LOCTEXT("GrowAnchorSelection", "Grow Selection"),
                LOCTEXT("GrowAnchorSelectionTip", "Include adjacent spline anchors in the current selection. Works on open and closed splines."),
                [this]() { GrowAnchorSelection(); });
            AddAction(Sub, LOCTEXT("ShrinkAnchorSelection", "Shrink Selection"),
                LOCTEXT("ShrinkAnchorSelectionTip", "Remove boundary anchors from the selection. Open-path endpoints remain when their only neighbor is selected."),
                [this]() { ShrinkAnchorSelection(); });
            Sub.AddSubMenu(LOCTEXT("SelectSharpCornersMenu", "Select Sharp Corners"),
                LOCTEXT("SelectSharpCornersTip", "Select anchors where incoming and outgoing directions differ by the chosen angle. Uses evaluated spline tangents and does not change geometry."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Angles)
                {
                    for (const int32 Degrees : {15, 30, 45, 60, 90})
                    {
                        const FText Label = FText::FromString(FString::Printf(TEXT("%d degrees or sharper"), Degrees));
                        Angles.AddMenuEntry(Label,
                            LOCTEXT("SelectSharpAnglesTip", "Select open-path interior anchors or closed-path anchors with a turn at least this sharp. Replaces the current selection."),
                            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Degrees]()
                            { SelectSharpCorners(static_cast<double>(Degrees)); })));
                    }
                }));
            Sub.AddSubMenu(LOCTEXT("SelectDuplicatesMenu", "Select Duplicate Anchors"),
                LOCTEXT("SelectDuplicatesTip", "Select adjacent coincident or nearly coincident anchors without changing geometry."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Tolerances)
                {
                    for (const double Tolerance : {0.01, 0.1, 1.0})
                    {
                        const FText Label = FText::FromString(FString::Printf(TEXT("Within %.2f cm"), Tolerance));
                        Tolerances.AddMenuEntry(Label, LOCTEXT("DuplicateDistanceTip", "Select near-duplicate anchors in asset space."),
                            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Tolerance]()
                            { SelectDuplicateAnchors(Tolerance); })));
                    }
                }));
            Sub.AddSubMenu(LOCTEXT("SelectSegmentLengthMenu", "Select by Segment Length"),
                LOCTEXT("SelectSegmentLengthTip", "Select the endpoints of evaluated segments shorter or longer than a distance threshold."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Lengths)
                {
                    for (const double Threshold : {10.0, 50.0, 100.0, 500.0})
                    {
                        for (bool bShort : {true, false})
                        {
                            const FText Label = FText::FromString(FString::Printf(TEXT("%s %.0f cm"),
                                bShort ? TEXT("Shorter than") : TEXT("Longer than"), Threshold));
                            Lengths.AddMenuEntry(Label, LOCTEXT("SegmentLengthSelectTip", "Select anchor endpoints of matching spline segments. Does not modify geometry."),
                                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Threshold, bShort]()
                                { SelectBySegmentLength(Threshold, bShort); })));
                        }
                    }
                }));
            Sub.AddMenuSeparator();
            Sub.AddSubMenu(LOCTEXT("AlignSubMenu", "Align"),
                LOCTEXT("AlignTip", "Align selected anchors to their shared center along an axis."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Axis)
                {
                    for (int32 I = 0; I < 3; ++I)
                    {
                        const FText Label = I == 0 ? LOCTEXT("AlignX", "Align X")
                            : I == 1 ? LOCTEXT("AlignY", "Align Y") : LOCTEXT("AlignZ", "Align Z");
                        Axis.AddMenuEntry(Label, LOCTEXT("AxisEditingTip", "Apply this operation to selected anchors along the named asset-space axis. Undoable."), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, I]() { AlignSelection(I); })));
                    }
                }));
            Sub.AddSubMenu(LOCTEXT("DistributeSubMenu", "Distribute"),
                LOCTEXT("DistributeTip", "Space at least three selected anchors evenly along an axis."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Axis)
                {
                    for (int32 I = 0; I < 3; ++I)
                    {
                        const FText Label = I == 0 ? LOCTEXT("DistributeX", "Distribute X")
                            : I == 1 ? LOCTEXT("DistributeY", "Distribute Y") : LOCTEXT("DistributeZ", "Distribute Z");
                        Axis.AddMenuEntry(Label, LOCTEXT("AxisEditingTip", "Apply this operation to selected anchors along the named asset-space axis. Undoable."), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, I]() { DistributeSelection(I); })));
                    }
                }));
            Sub.AddSubMenu(LOCTEXT("SnapPointMenu", "Snap Anchors"),
                LOCTEXT("SnapPointTip", "Round selected positions to the chosen centimeter increment."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Steps)
                {
                    for (const double Step : { 1.0, 10.0, 100.0 })
                    {
                        const FText Label = FText::FromString(FString::Printf(TEXT("%.0f cm"), Step));
                        Steps.AddMenuEntry(Label, LOCTEXT("SnapStepsTip", "Round selected anchor positions to this centimeter step. Undoable."), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Step]() { SnapSelectedPoint(Step); })));
                    }
                }));
            Sub.AddSubMenu(LOCTEXT("SnapToGeometry", "Snap to Existing Spline Geometry"),
                LOCTEXT("SnapToGeometryTip", "Preview target positions before applying a snap to other anchors or non-adjacent curve segments; tangents and roll stay unchanged."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Snap)
                {
                    Snap.AddMenuEntry(LOCTEXT("SnapToOtherAnchor", "Preview Snap to Unselected Anchor..."),
                        LOCTEXT("SnapToOtherAnchorTip", "Snap selected anchor positions to the nearest unselected anchor within range; no change if none qualifies."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SnapSelectionToOtherGeometry(false); })));
                    Snap.AddMenuEntry(LOCTEXT("SnapToOtherSegment", "Preview Snap to Curve Segment..."),
                        LOCTEXT("SnapToOtherSegmentTip", "Snap to evaluated curve geometry, excluding segments adjoining selected anchors. Does not alter point rotation or tangent vectors."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SnapSelectionToOtherGeometry(true); })));
                }));
            Sub.AddMenuSeparator();
            AddAction(Sub, LOCTEXT("AlignHandles", "Align Tangents Once"),
                LOCTEXT("AlignHandlesTip", "One-time alignment. For linked editing use inspector > Bezier Handles > Aligned."),
                [this]() { MatchSelectedTangents(false); });
            AddAction(Sub, LOCTEXT("G1Continuity", "Smooth Join: G1 Tangent Direction"),
                LOCTEXT("G1ContinuityTip", "Align incoming/outgoing tangent directions for selected interior anchors while retaining each handle length. Undoable."),
                [this]() { SetSelectedContinuity(false); });
            AddAction(Sub, LOCTEXT("RecalculateSelectedTangents", "Recalculate Selected Tangents"),
                LOCTEXT("RecalculateSelectedTangentsTip", "Rebuild selected handle directions and lengths from neighboring positions without moving anchors."),
                [this]() { RecalculateTangents(); });
            AddAction(Sub, LOCTEXT("C1Continuity", "Smooth Join: C1 Matching Tangents"),
                LOCTEXT("C1ContinuityTip", "Give selected interior anchors matching incoming and outgoing tangent vectors for first-derivative continuity."),
                [this]() { SetSelectedContinuity(true); });
            Sub.AddSubMenu(LOCTEXT("NormalizeSelectedHandles", "Normalize Selected Handle Lengths"),
                LOCTEXT("NormalizeSelectedHandlesTip", "Set nonzero incoming/outgoing handle magnitudes to a common length, preserving directions."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Lengths)
                {
                    for (const double LengthCm : {25.0, 50.0, 100.0, 200.0})
                    {
                        const FText Label = FText::FromString(FString::Printf(TEXT("%.0f cm"), LengthCm));
                        Lengths.AddMenuEntry(Label, LOCTEXT("NormalizeSelectedHandleTip", "Apply uniform tangent magnitudes to the selected anchors. Undoable."),
                            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, LengthCm]()
                            { NormalizeTangents(LengthCm); })));
                    }
                }));
            AddAction(Sub, LOCTEXT("MirrorHandles", "Mirror Tangents Once"),
                LOCTEXT("MirrorHandlesTip", "One-time mirroring. For linked editing use inspector > Bezier Handles > Mirrored."),
                [this]() { MatchSelectedTangents(true); });
        }));
    Menu.AddSubMenu(LOCTEXT("PathMenu", "Create and Path"),
        LOCTEXT("PathMenuHelp", "Draw, extend, close, reverse, or clear a spline."),
        FNewMenuDelegate::CreateLambda([this, AddAction](FMenuBuilder& Sub)
        {
            AddAction(Sub, LOCTEXT("StartPenMenu", "Activate Spline Pen"),
                LOCTEXT("StartPenTip", "Click to create or extend a spline directly in the viewport."),
                [this]() { SetPenMode(true); });
            AddAction(Sub, LOCTEXT("ExtendStartMenu", "Extend Start"),
                LOCTEXT("ExtendStartTip", "Add an anchor before the first point of an open spline."),
                [this]() { ExtendStart(); });
            AddAction(Sub, LOCTEXT("ExtendEndMenu", "Extend End"),
                LOCTEXT("ExtendEndTip", "Append an endpoint to an open spline."),
                [this]() { if (USplineDataAsset* Current = Asset.Get())
                { if (!Current->bIsClosedLoop && Current->Points.Num() >= 2)
                { SelectPoint(Current->Points.Num() - 1); AddPointAfterSelection(); } } });
            AddAction(Sub, LOCTEXT("ReverseMenu", "Reverse Direction"),
                LOCTEXT("ReverseTip", "Reverse point sequence, with inverted incoming/outgoing tangents."),
                [this]() { ReverseSpline(); });
            AddAction(Sub, LOCTEXT("SplitSplineMenu", "Split at Selected Anchor into Two Assets"),
                LOCTEXT("SplitSplineMenuTip", "Create separate left/right Strand assets from an open spline at one selected internal anchor. The original is preserved; reimport links are not copied."),
                [this]() { SplitAtSelectedAnchor(); });
            AddAction(Sub, LOCTEXT("FirstPointMenu", "Set Selected as First (Closed)"),
                LOCTEXT("FirstPointTip", "Change the start point of a closed spline without moving geometry."),
                [this]() { SetSelectedAsFirst(); });
            Sub.AddSubMenu(LOCTEXT("WeldEndpointsMenu", "Weld Start and End (Close Loop)"),
                LOCTEXT("WeldEndpointsTip", "When an open path's start and end are within the chosen distance, merge them into a single closed seam. The first anchor is retained; Undoable."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Distances)
                {
                    for (const double Threshold : {0.01, 0.1, 1.0, 10.0})
                    {
                        const FText Label = FText::FromString(FString::Printf(TEXT("Maximum gap %.2f cm"), Threshold));
                        Distances.AddMenuEntry(Label, LOCTEXT("WeldEndpointsThresholdTip", "Weld the nearby endpoints into a closed loop, preserving the first anchor's position. Reject gaps larger than this threshold."),
                            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Threshold]() { WeldEndpoints(Threshold); })));
                    }
                }));
            AddAction(Sub, LOCTEXT("ToggleClosedMenu", "Toggle Closed Loop"),
                LOCTEXT("ToggleClosedTip", "Join or separate the spline endpoints."),
                [this]() { ToggleClosedLoop(); });
            Sub.AddMenuSeparator();
            AddAction(Sub, LOCTEXT("ClearSplineMenu", "Clear All Points..."),
                LOCTEXT("ClearSplineTip", "Clear the entire asset to draw a new path; asks for confirmation."),
                [this]() { ClearSpline(); });
        }));
    Menu.AddSubMenu(LOCTEXT("RefineMenu", "Refine and Shape"),
        LOCTEXT("RefineMenuHelp", "Subdivide, linearize, smooth, mirror, or flatten the entire spline."),
        FNewMenuDelegate::CreateLambda([this, AddAction](FMenuBuilder& Sub)
        {
            AddAction(Sub, LOCTEXT("RefineExactCuts", "Exact Subdivision (Last Cut Count)"),
                LOCTEXT("RefineExactCutsTip", "Apply your remembered exact cut count to every segment. Use the main Exact Subdivision menu for cut-count and distance options."),
                [this]() { SubdivideWithCuts(SubdivideCuts, false); });
            AddAction(Sub, LOCTEXT("SubdivideMenu", "Subdivide All Segments"),
                LOCTEXT("SubdivideTip", "Insert an anchor at each segment midpoint, preserving cubic geometry."),
                [this]() { SubdivideSpline(); });
            AddAction(Sub, LOCTEXT("LinearAllMenu", "Linearize All"),
                LOCTEXT("LinearAllTip", "Turn every segment into straight edges; changes the curve."),
                [this]() { LinearizeSpline(); });
            AddAction(Sub, LOCTEXT("AutoAllMenu", "Auto-Smooth All"),
                LOCTEXT("AutoAllTip", "Apply Unreal native automatic cubic tangents to every point."),
                [this]() { SmoothSpline(); });
            Sub.AddSubMenu(LOCTEXT("MirrorSubMenu", "Mirror Spline"),
                LOCTEXT("MirrorTip", "Reflect all positions and tangents around the spline bounds center."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Axes)
                {
                    for (int32 Axis = 0; Axis < 3; ++Axis)
                    {
                        const FText Label = Axis == 0 ? LOCTEXT("MirrorX", "Mirror X")
                            : Axis == 1 ? LOCTEXT("MirrorY", "Mirror Y") : LOCTEXT("MirrorZ", "Mirror Z");
                        Axes.AddMenuEntry(Label, LOCTEXT("AxisShapeTip", "Apply this modeling operation to the entire spline in asset space. Undoable."), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Axis]() { MirrorSpline(Axis); })));
                    }
                }));
            Sub.AddSubMenu(LOCTEXT("FlattenSubMenu", "Flatten to Plane..."),
                LOCTEXT("FlattenTip", "Discard one dimension from positions and handles; confirmation required."),
                FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Axes)
                {
                    for (int32 Axis = 0; Axis < 3; ++Axis)
                    {
                        const FText Label = Axis == 2 ? LOCTEXT("FlattenXY", "Top (XY), discard Z")
                            : Axis == 1 ? LOCTEXT("FlattenXZ", "Front (XZ), discard Y")
                            : LOCTEXT("FlattenYZ", "Side (YZ), discard X");
                        Axes.AddMenuEntry(Label, LOCTEXT("AxisShapeTip", "Apply this modeling operation to the entire spline in asset space. Undoable."), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this, Axis]() { FlattenSpline(Axis); })));
                    }
                }));
        }));
    return Menu.MakeWidget();
}


void FStrandSplineAssetEditor::SetWidgetMode(int32 Mode)
{
    EndViewportPointDrag();
    ActiveWidgetMode = FMath::Clamp(Mode, 0, 2);
    if (ActiveWidgetMode != 0 && SelectedHandle != 0)
    {
        // Rotating/scaling shapes the anchor, not one isolated handle.
        SelectedHandle = 0;
        if (PreviewWidget.IsValid()) { PreviewWidget->SetSelectedHandle(0); }
    }
    if (PreviewWidget.IsValid()) { PreviewWidget->SetWidgetMode(ActiveWidgetMode); }
}

void FStrandSplineAssetEditor::SetWidgetLocal(bool bLocal)
{
    EndViewportPointDrag();
    bWidgetLocal = bLocal;
    if (PreviewWidget.IsValid()) { PreviewWidget->SetWidgetLocal(bLocal); }
}

void FStrandSplineAssetEditor::SetMarqueeMode(bool bEnabled)
{
    if (bEnabled && bPenMode) { SetPenMode(false); }
    EndViewportPointDrag();
    bLassoMode = false;
    bMarqueeMode = bEnabled;
    if (PreviewWidget.IsValid()) { PreviewWidget->SetMarqueeMode(bEnabled); }
}

void FStrandSplineAssetEditor::SetLassoMode(bool bEnabled)
{
    if (bEnabled && bPenMode) { SetPenMode(false); }
    EndViewportPointDrag();
    bLassoMode = bEnabled;
    bMarqueeMode = bEnabled;
    if (PreviewWidget.IsValid()) { PreviewWidget->SetLassoMode(bEnabled); }
}

void FStrandSplineAssetEditor::SetPenMode(bool bEnabled)
{
    if (!bEnabled) { EndPenStroke(); }
    EndViewportPointDrag();
    if (bEnabled && HasMultipleSelectedPoints()) { SelectPoint(SelectedPointIndex); }
    if (bEnabled && bMarqueeMode) { SetMarqueeMode(false); }
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

TSharedRef<SWidget> FStrandSplineAssetEditor::MakeViewportOptionsMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto AddOption = [&Menu](const FText& Label, const FText& Help,
        TFunction<void()> Toggle, TFunction<bool()> IsChecked)
    {
        const TSharedRef<TFunction<void()>> Action = MakeShared<TFunction<void()>>(MoveTemp(Toggle));
        const TSharedRef<TFunction<bool()>> Checked = MakeShared<TFunction<bool()>>(MoveTemp(IsChecked));
        Menu.AddMenuEntry(Label, Help, FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([Action]() { (*Action)(); }),
                FCanExecuteAction(), FIsActionChecked::CreateLambda([Checked]() { return (*Checked)(); })),
            NAME_None, EUserInterfaceActionType::ToggleButton);
    };
    AddOption(LOCTEXT("ViewportShowAnchors", "Show Control Points"),
        LOCTEXT("ViewportShowAnchorsHelp", "Toggle spline anchor markers and native transform gizmo."),
        [this]() { if (PreviewWidget.IsValid()) { PreviewWidget->SetShowControlPoints(!PreviewWidget->GetShowControlPoints()); } },
        [this]() { return PreviewWidget.IsValid() && PreviewWidget->GetShowControlPoints(); });
    AddOption(LOCTEXT("ViewportShowGrid", "Show Grid"),
        LOCTEXT("ViewportShowGridHelp", "Toggle the local preview reference grid."),
        [this]() { if (PreviewWidget.IsValid()) { PreviewWidget->SetShowGrid(!PreviewWidget->GetShowGrid()); } },
        [this]() { return PreviewWidget.IsValid() && PreviewWidget->GetShowGrid(); });
    AddOption(LOCTEXT("ViewportShowTangents", "Show Tangent Handles"),
        LOCTEXT("ViewportShowTangentsHelp", "Toggle incoming (orange) and outgoing (blue) handles."),
        [this]() { if (PreviewWidget.IsValid()) { PreviewWidget->SetShowTangents(!PreviewWidget->GetShowTangents()); } },
        [this]() { return PreviewWidget.IsValid() && PreviewWidget->GetShowTangents(); });
    Menu.BeginSection(TEXT("StrandOrientation"), LOCTEXT("OrientationVisualsSection", "Spline Orientation"));
    const FText Names[] = {
        LOCTEXT("ShowLocalUp", "Local Up (Z / Blue)"),
        LOCTEXT("ShowLocalRight", "Local Right (Y / Green)"),
        LOCTEXT("ShowFrames", "Local XYZ Frames"),
        LOCTEXT("ShowRollRibbon", "Roll Ribbon")
    };
    const FText Tips[] = {
        LOCTEXT("ShowLocalUpTip", "Sample the evaluated native spline up direction along the entire curve."),
        LOCTEXT("ShowLocalRightTip", "Sample the evaluated native spline right direction along the entire curve."),
        LOCTEXT("ShowFramesTip", "Show native evaluated X-forward, Y-right and Z-up orientation frames."),
        LOCTEXT("ShowRollRibbonTip", "Draw the right-side ribbon edge along the evaluated spline to visualize banking and twists.")
    };
    for (int32 Mode = 0; Mode < 4; ++Mode)
    {
        AddOption(Names[Mode], Tips[Mode],
            [this, Mode]() { if (PreviewWidget.IsValid()) { PreviewWidget->SetOrientationVisual(Mode, !PreviewWidget->GetOrientationVisual(Mode)); } },
            [this, Mode]() { return PreviewWidget.IsValid() && PreviewWidget->GetOrientationVisual(Mode); });
    }
    // Presets are edited in Editor Preferences > Plugins > Strand Editor.
    // Custom values are always available; selecting one saves the last-used value.
    Menu.AddSubMenu(LOCTEXT("OrientationSampleSpacing", "Sample Spacing"),
        LOCTEXT("OrientationSpacingHelp", "Desired distance between visualization samples in centimeters; up to 2,048 sampled intervals."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            const UStrandEditorPreferences* Pref = GetDefault<UStrandEditorPreferences>();
            for (const double Step : Pref->OrientationSpacingPresets)
            {
                if (!FMath::IsFinite(Step) || Step <= 0.0) { continue; }
                Sub.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%g cm"), Step)), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, Step]() { if (PreviewWidget.IsValid()) { PreviewWidget->SetOrientationSpacing(Step); } }),
                    FCanExecuteAction(), FIsActionChecked::CreateLambda([this, Step]() { return PreviewWidget.IsValid() && FMath::IsNearlyEqual(PreviewWidget->GetOrientationSpacing(), Step); })),
                    NAME_None, EUserInterfaceActionType::RadioButton);
            }
            Sub.AddWidget(SNew(SNumericEntryBox<double>)
                .MinValue(0.01).MaxValue(100000.0)
                .Value_Lambda([this]() -> TOptional<double> { return PreviewWidget.IsValid() ? TOptional<double>(PreviewWidget->GetOrientationSpacing()) : TOptional<double>(); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { if (PreviewWidget.IsValid()) { PreviewWidget->SetOrientationSpacing(Value); } }),
                LOCTEXT("CustomOrientationSpacing", "Custom (cm)"), true);
        }));
    Menu.AddSubMenu(LOCTEXT("OrientationDisplaySize", "Marker Size"),
        LOCTEXT("OrientationSizeHelp", "Length of orientation arrows and width of roll ribbon, in centimeters."),
        FNewMenuDelegate::CreateLambda([this](FMenuBuilder& Sub)
        {
            const UStrandEditorPreferences* Pref = GetDefault<UStrandEditorPreferences>();
            for (const double Size : Pref->OrientationSizePresets)
            {
                if (!FMath::IsFinite(Size) || Size <= 0.0) { continue; }
                Sub.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%g cm"), Size)), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, Size]() { if (PreviewWidget.IsValid()) { PreviewWidget->SetOrientationSize(Size); } }),
                    FCanExecuteAction(), FIsActionChecked::CreateLambda([this, Size]() { return PreviewWidget.IsValid() && FMath::IsNearlyEqual(PreviewWidget->GetOrientationSize(), Size); })),
                    NAME_None, EUserInterfaceActionType::RadioButton);
            }
            Sub.AddWidget(SNew(SNumericEntryBox<double>)
                .MinValue(0.01).MaxValue(10000.0)
                .Value_Lambda([this]() -> TOptional<double> { return PreviewWidget.IsValid() ? TOptional<double>(PreviewWidget->GetOrientationSize()) : TOptional<double>(); })
                .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { if (PreviewWidget.IsValid()) { PreviewWidget->SetOrientationSize(Value); } }),
                LOCTEXT("CustomOrientationSize", "Custom (cm)"), true);
        }));
    Menu.EndSection();
    Menu.BeginSection(TEXT("StrandSoftSelection"), LOCTEXT("SoftSelectionSection", "Soft Selection"));
    const FText ModeLabels[] = {LOCTEXT("SoftOff", "Off"), LOCTEXT("Soft3D", "3D Distance"), LOCTEXT("SoftAlong", "Spline Distance")};
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        Menu.AddMenuEntry(ModeLabels[Mode], LOCTEXT("SoftModeTip", "Choose full selection only, 3D radial influence, or arc-length influence along the spline."), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Mode]() { EndViewportPointDrag(); SoftSelectionMode = Mode; RefreshSoftSelectionVisualization(); }), FCanExecuteAction(),
                FIsActionChecked::CreateLambda([this, Mode]() { return SoftSelectionMode == Mode; })),
            NAME_None, EUserInterfaceActionType::RadioButton);
    }
    Menu.AddWidget(SNew(SNumericEntryBox<double>)
        .MinValue(0.01).MaxValue(100000.0)
        .Value_Lambda([this]() -> TOptional<double> { return SoftSelectionRadius; })
        .OnValueCommitted_Lambda([this](double Value, ETextCommit::Type) { if (FMath::IsFinite(Value) && Value > 0.0) { EndViewportPointDrag(); SoftSelectionRadius = Value; RefreshSoftSelectionVisualization(); } }),
        LOCTEXT("SoftRadiusLabel", "Falloff Radius (cm)"), true);
    const FText FalloffLabels[] = { LOCTEXT("FalloffLinear", "Linear"), LOCTEXT("FalloffSmooth", "Smooth"), LOCTEXT("FalloffSharp", "Sharp") };
    for (int32 Profile = 0; Profile < 3; ++Profile)
    {
        Menu.AddMenuEntry(FalloffLabels[Profile], FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Profile]() { EndViewportPointDrag(); SoftSelectionFalloff = Profile; RefreshSoftSelectionVisualization(); }), FCanExecuteAction(),
                FIsActionChecked::CreateLambda([this, Profile]() { return SoftSelectionFalloff == Profile; })),
            NAME_None, EUserInterfaceActionType::RadioButton);
    }
    Menu.EndSection();
    return Menu.MakeWidget();
}

TSharedRef<SWidget> FStrandSplineAssetEditor::MakePenSnapMenu()
{
    FMenuBuilder Menu(true, nullptr);
    auto Add = [this, &Menu](double Step, const FText& Label)
    {
        Menu.AddMenuEntry(Label, LOCTEXT("PenSnapChoiceTip", "Snap Pen-created anchors and handles to the specified centimeter grid. Off preserves freehand placement."), FSlateIcon(),
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
    if (PreviewWidget.IsValid() && !bMarqueeMode && !bPenMode)
    {
        const FText HoverHint = PreviewWidget->GetInteractionHint();
        if (!HoverHint.IsEmpty()) { return HoverHint; }
    }
    if (bLassoMode)
    {
        return LOCTEXT("LassoStatus", "Lasso Select  |  Freehand outline around anchors  |  Shift: add  |  Ctrl: toggle  |  Esc: exit");
    }
    if (bMarqueeMode)
    {
        return LOCTEXT("MarqueeStatus", "Box Select  |  Drag rectangle to select anchors  |  Shift: add  |  Ctrl: toggle  |  Alt+LMB: orbit");
    }
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
    if (HasSelectedPoint())
    {
        if (ActiveWidgetMode == 1)
        {
            return HasMultipleSelectedPoints()
                ? LOCTEXT("RotateStatus", "ROTATE  |  Native Unreal rotation widget  |  Group pivot: selection center  |  Space: Scale  |  Shift+click curve: insert  |  RMB+WASD: fly")
                : LOCTEXT("RotateSingleStatus", "ROTATE  |  Rotate selected anchor tangents/orientation  |  Space: Scale  |  Shift+click curve: insert  |  RMB+WASD: fly");
        }
        if (ActiveWidgetMode == 2)
        {
            return HasMultipleSelectedPoints()
                ? LOCTEXT("ScaleStatus", "UNIFORM SCALE  |  Native Unreal scale widget  |  Group pivot: selection center  |  Space: Move  |  Shift+click curve: insert  |  RMB+WASD: fly")
                : LOCTEXT("ScaleSingleStatus", "UNIFORM SCALE  |  Scale selected anchor's tangents  |  Space: Move  |  Shift+click curve: insert  |  RMB+WASD: fly");
        }
        return bWidgetLocal
            ? LOCTEXT("MoveLocalStatus", "MOVE / LOCAL  |  X follows outgoing tangent  |  Space: Rotate  |  Shift-hover: insert preview  |  RMB-click: actions")
            : LOCTEXT("MoveStatus", "MOVE / WORLD  |  Asset axes  |  Hover a widget axis to highlight it  |  Ctrl+Shift+I/J/K/L/U/O: 1 cm nudge  |  RMB-click: actions");
    }
    return LOCTEXT("SelectModeStatus", "SELECT  |  Hover: contextual cursor  |  Shift-hover curve: insertion preview  |  RMB click: actions  |  Ctrl+Shift+I/J/K/L/U/O: nudge XYZ");
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
