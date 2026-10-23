#include "StrandSplineComponentCustomization.h"
#include "SplineEditorCommands.h"
#include "SplineDataAsset.h"
#include "StrandSplineUserData.h"
#include "Components/SplineComponent.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "StrandSplineComponentDetails"

namespace StrandActionIcons
{
    const FSlateBrush* Get(const FName IconName)
    {
        const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(FName(TEXT("StrandEditorStyle")));
        return Style ? Style->GetBrush(IconName) : nullptr;
    }
}


TSharedRef<IDetailCustomization> FStrandSplineComponentCustomization::MakeInstance()
{
    return MakeShared<FStrandSplineComponentCustomization>();
}

void FStrandSplineComponentCustomization::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    Builder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1) { return; }
    Spline = Cast<USplineComponent>(Objects[0].Get());
    if (!Spline.IsValid()) { return; }

    IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Strand"),
        LOCTEXT("Category", "Strand"), ECategoryPriority::Important);

    Category.AddCustomRow(LOCTEXT("AssetSearch", "Strand Spline Asset"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("AssetLabel", "Strand Spline Asset"))]
    .ValueContent().MinDesiredWidth(260.0f)
    [
        SNew(SObjectPropertyEntryBox)
        .AllowedClass(USplineDataAsset::StaticClass())
        .ObjectPath_Lambda([Weak = Spline]() -> FString
        {
            const USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Weak.Get());
            return IsValid(Asset) ? Asset->GetPathName() : FString();
        })
        .OnObjectChanged_Lambda([Weak = Spline](const FAssetData& InAsset)
        {
            SplineEditorCommands::AssignAssetToComponent(Weak, Cast<USplineDataAsset>(InAsset.GetAsset()));
        })
        .DisplayUseSelected(true)
        .DisplayBrowse(true)
        .AllowClear(true)
    ];

    Category.AddCustomRow(LOCTEXT("StatusSearch", "Strand Sync Status"))
    .WholeRowContent()
    [
        SNew(STextBlock)
        .Text_Lambda([Weak = Spline]() -> FText
        {
            USplineComponent* Component = Weak.Get();
            if (!IsValid(Component)) { return FText::GetEmpty(); }
            const USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Component);
            if (!Asset) { return LOCTEXT("NotAssigned", "No Strand asset assigned"); }
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Component);
            return Data && Data->IsInSync(Component)
                ? FText::Format(LOCTEXT("InSync", "In sync | {0} points"), FText::AsNumber(Component->GetNumberOfSplinePoints()))
                : FText::Format(LOCTEXT("Different", "Different from asset | {0} local / {1} asset points"),
                    FText::AsNumber(Component->GetNumberOfSplinePoints()), FText::AsNumber(Asset->Points.Num()));
        })
    ];

    // The saved asset's closure flag and the native instance's closure flag
    // are independent when the source asset has been reimported. Show both
    // values so stale actors are distinguishable from import problems.
    Category.AddCustomRow(LOCTEXT("TopologySearch", "Strand Spline Topology Closed Open"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("TopologyLabel", "Spline Closure"))]
    .ValueContent().MinDesiredWidth(260.0f)
    [
        SNew(STextBlock)
        .Text_Lambda([Weak = Spline]() -> FText
        {
            const USplineComponent* Component = Weak.Get();
            if (!IsValid(Component)) { return FText::GetEmpty(); }
            const USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Component);
            if (!IsValid(Asset))
            {
                return Component->IsClosedLoop()
                    ? LOCTEXT("OnlyNativeClosed", "Native: Closed | No asset")
                    : LOCTEXT("OnlyNativeOpen", "Native: Open | No asset");
            }
            return FText::FromString(FString::Printf(TEXT("Asset: %s | Native: %s"),
                Asset->bIsClosedLoop ? TEXT("Closed") : TEXT("Open"),
                Component->IsClosedLoop() ? TEXT("Closed") : TEXT("Open")));
        })
        .ToolTipText(LOCTEXT("TopologyTooltip", "These are the stored Strand asset closure state and the actual native USplineComponent closed-loop state. Reimport changes the asset but deliberately does not overwrite already placed instances. Apply Asset to update an instance."))
    ];

    // Placement changes affect only local point positions. These choices are stored on
    // the native component as Asset User Data, never on a new actor component.
    Category.AddCustomRow(LOCTEXT("CoordinateSearch", "Coordinate Mode Absolute Relative"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("PlacementMode", "Coordinate Mode"))]
    .ValueContent()
    [
        SNew(SCheckBox)
        .ToolTipText(LOCTEXT("ModeTooltip", "Off: preserve source-local coordinates (Absolute). On: align the selected point or point center with the spline component origin (Relative). Neither changes the actor transform."))
        .IsEnabled_Lambda([Weak = Spline]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
        .IsChecked_Lambda([Weak = Spline]()
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            return Data && Data->CoordinateMode == EStrandCoordinateMode::Relative ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda([Weak = Spline](ECheckBoxState State)
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            if (Data) { SplineEditorCommands::SetComponentPlacement(Weak.Get(), State == ECheckBoxState::Checked
                ? EStrandCoordinateMode::Relative : EStrandCoordinateMode::Absolute, Data->AnchorMode, Data->AnchorPointIndex); }
        })
        [ SNew(STextBlock).Text_Lambda([Weak = Spline]()
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            return Data && Data->CoordinateMode == EStrandCoordinateMode::Relative
                ? LOCTEXT("Relative", "Relative") : LOCTEXT("Absolute", "Absolute");
        }) ]
    ];

    Category.AddCustomRow(LOCTEXT("CenterSearch", "Use Center of Spline Points"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("CenterLabel", "Use Center of Spline Points"))]
    .ValueContent()
    [
        SNew(SCheckBox)
        .ToolTipText(LOCTEXT("CenterTip", "Use the arithmetic mean of all spline point positions as the local placement anchor."))
        .IsEnabled_Lambda([Weak = Spline]()
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            return Data && Data->AssignedSplineAsset && Data->CoordinateMode == EStrandCoordinateMode::Relative;
        })
        .IsChecked_Lambda([Weak = Spline]()
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            return Data && Data->AnchorMode == EStrandAnchorMode::PointCenter ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda([Weak = Spline](ECheckBoxState State)
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            if (Data) { SplineEditorCommands::SetComponentPlacement(Weak.Get(), Data->CoordinateMode,
                State == ECheckBoxState::Checked ? EStrandAnchorMode::PointCenter : EStrandAnchorMode::PointIndex,
                Data->AnchorPointIndex); }
        })
    ];

    Category.AddCustomRow(LOCTEXT("PointSearch", "Anchor Point Index"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("PointLabel", "Anchor Point Index"))]
    .ValueContent()
    [
        SNew(SNumericEntryBox<int32>)
        .MinValue(0)
        .MinSliderValue(0)
        .ToolTipText(LOCTEXT("PointTip", "Zero-based point index that occupies local (0, 0, 0) in Relative mode. Index is clamped to available points."))
        .IsEnabled_Lambda([Weak = Spline]()
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            return Data && Data->AssignedSplineAsset && Data->CoordinateMode == EStrandCoordinateMode::Relative
                && Data->AnchorMode == EStrandAnchorMode::PointIndex;
        })
        .Value_Lambda([Weak = Spline]() -> TOptional<int32>
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            return Data ? TOptional<int32>(Data->AnchorPointIndex) : TOptional<int32>();
        })
        .OnValueCommitted_Lambda([Weak = Spline](int32 Index, ETextCommit::Type)
        {
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
            if (Data) { SplineEditorCommands::SetComponentPlacement(Weak.Get(), Data->CoordinateMode,
                Data->AnchorMode, Index); }
        })
    ];

    Category.AddCustomRow(LOCTEXT("ActionsSearch", "Strand Actions"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("ActionsLabel", "Strand Actions"))]
    .ValueContent().MinDesiredWidth(260.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("ApplyTooltip", "Apply Asset: Reset native spline points to the linked Strand asset, preserving actor/component transforms."))
            .IsEnabled_Lambda([Weak = Spline]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
            .OnClicked_Lambda([Weak = Spline]() { SplineEditorCommands::ResetComponent(Weak.Get()); return FReply::Handled(); })
            [ SNew(SImage).Image(StrandActionIcons::Get(TEXT("Strand.Action.Apply"))).ColorAndOpacity(FSlateColor::UseForeground()) ]
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("UpdateTooltip", "Update Asset: Save native spline changes back to the linked Strand asset after confirmation."))
            .IsEnabled_Lambda([Weak = Spline]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
            .OnClicked_Lambda([Weak = Spline]() { SplineEditorCommands::UpdateComponentAsset(Weak.Get()); return FReply::Handled(); })
            [ SNew(SImage).Image(StrandActionIcons::Get(TEXT("Strand.Action.Update"))).ColorAndOpacity(FSlateColor::UseForeground()) ]
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("SaveNewTooltip", "Save as New: Create a new Strand Spline asset from these native spline points."))
            .OnClicked_Lambda([Weak = Spline]() { SplineEditorCommands::CaptureComponent(Weak); return FReply::Handled(); })
            [ SNew(SImage).Image(StrandActionIcons::Get(TEXT("Strand.Action.New"))).ColorAndOpacity(FSlateColor::UseForeground()) ]
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("CompareTooltip", "Compare: Report differences between the native spline and the assigned Strand asset."))
            .IsEnabled_Lambda([Weak = Spline]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
            .OnClicked_Lambda([Weak = Spline]() { SplineEditorCommands::CompareComponent(Weak.Get()); return FReply::Handled(); })
            [ SNew(SImage).Image(StrandActionIcons::Get(TEXT("Strand.Action.Compare"))).ColorAndOpacity(FSlateColor::UseForeground()) ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
