#include "StrandActorDetails.h"
#include "StrandSplineActor.h"
#include "StrandMultiSplineActor.h"
#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "SplineEditorCommands.h"
#include "StrandProjectionWorkbench.h"
#include "StrandPlacement.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "Components/SplineComponent.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "StrandActorDetails"

TSharedRef<IDetailCustomization> FStrandActorDetails::MakeInstance()
{
    return MakeShared<FStrandActorDetails>();
}

void FStrandActorDetails::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
    TArray<TWeakObjectPtr<UObject>> RawObjects;
    Builder.GetObjectsBeingCustomized(RawObjects);
    TArray<TWeakObjectPtr<USplineComponent>> Splines;
    TArray<TWeakObjectPtr<AStrandSplineActor>> Actors;
    for (const auto& Weak : RawObjects)
    {
        if (AStrandSplineActor* Actor = Cast<AStrandSplineActor>(Weak.Get()))
        {
            Actors.Add(Actor);
            TArray<USplineComponent*> Components;
            Actor->GetComponents<USplineComponent>(Components);
            for (USplineComponent* Component : Components) { Splines.Add(Component); }
        }
    }
    if (Actors.IsEmpty()) { return; }

    // The native Spline component is already accessible in the Components tree.
    // Exposing the inherited component property here duplicates the asset controls.
    Builder.HideProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, Spline));

    const bool bSingleActor = Actors.Num() == 1;
    const bool bMulti = bSingleActor && Cast<AStrandMultiSplineActor>(Actors[0].Get()) != nullptr;
    IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Strand"),
        LOCTEXT("Strand", "Strand"), ECategoryPriority::Important);

    // Keep *all* Strand-only actor properties inside the Strand category. The
    // inherited category path in UPROPERTY remains stable for serialized data;
    // the Details UI relocates its property handles into actor-level groups.
    Builder.HideCategory(TEXT("Strand|Viewport"));
    Builder.HideCategory(TEXT("Strand|Surface Projection"));

    Category.AddCustomRow(LOCTEXT("Summary", "Strand Selection Summary"))
    .WholeRowContent()
    [SNew(STextBlock).Text_Lambda([Actors, Splines]() -> FText
    {
        int32 Linked = 0, Synced = 0, Different = 0;
        for (const auto& Weak : Splines)
        {
            USplineComponent* Spline = Weak.Get();
            const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Spline);
            if (Data && IsValid(Data->AssignedSplineAsset))
            {
                ++Linked;
                Data->IsInSync(Spline) ? ++Synced : ++Different;
            }
        }
        return FText::FromString(FString::Printf(TEXT("%d spline(s) | %d linked | %d in sync | %d modified"),
            Splines.Num(), Linked, Synced, Different));
    })];

    const TSharedPtr<IPropertyUtilities> PropertyUtilities = Builder.GetPropertyUtilities();

    if (bMulti)
    {
        Category.AddCustomRow(LOCTEXT("Actions", "All Spline Actions"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("ActionsLabel", "All Splines"))]
    .ValueContent().MinDesiredWidth(280)
    [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
        [SNew(SButton).Text(LOCTEXT("Apply", "Apply All Assets"))
            .ToolTipText(LOCTEXT("ApplyTip", "Apply each spline's own assigned Strand asset. Does not change shared assets."))
            .OnClicked_Lambda([Splines]()
            {
                for (const auto& Weak : Splines) { SplineEditorCommands::ResetComponent(Weak.Get()); }
                return FReply::Handled();
            })]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
        [SNew(SButton).Text(LOCTEXT("Add", "Add Spline"))
            .ToolTipText(LOCTEXT("AddTip", "Add one additional native Spline Component to each selected Strand Multi Spline Actor."))
            .OnClicked_Lambda([Actors, PropertyUtilities]()
            {
                const FScopedTransaction Transaction(LOCTEXT("AddMultiSplineTransaction", "Add Strand Native Spline Component"));
                for (const auto& Weak : Actors)
                {
                    if (AStrandMultiSplineActor* Multi = Cast<AStrandMultiSplineActor>(Weak.Get()))
                    {
                        Multi->AddSplineComponent();
                    }
                }
                if (PropertyUtilities.IsValid())
                {
                    PropertyUtilities->ForceRefresh();
                }
                return FReply::Handled();
            })]
    ];
    }

    // Never duplicate a component's asset picker and command rows in both the
    // overview and a second unstructured "Strand Asset Links" group.
    if (bSingleActor)
    {
        for (int32 I = 0; I < Splines.Num(); ++I)
        {
            const TWeakObjectPtr<USplineComponent> Weak = Splines[I];
            const FString ComponentName = Weak.IsValid() ? Weak->GetName() :
                FString::Printf(TEXT("Spline %d"), I + 1);
            const FText ComponentLabel = FText::FromString(ComponentName);
            const FText Title = FText::Format(
                LOCTEXT("SplineSectionTitle", "Spline: {0}"), ComponentLabel);

            // A single Strand Spline Actor already has a natural active spline.
            // Multi-spline actors instead get one clearly named, collapsible
            // group per native component. All callbacks bind that component.
            IDetailGroup* SplineGroup = bMulti
                ? &Category.AddGroup(FName(*FString::Printf(TEXT("StrandSpline_%d"), I)), Title, false, I == 0)
                : nullptr;
            auto AddSplineRow = [&Category, SplineGroup](const FText& Filter) -> FDetailWidgetRow&
            {
                return SplineGroup ? SplineGroup->AddWidgetRow() : Category.AddCustomRow(Filter);
            };

            AddSplineRow(FText::Format(LOCTEXT("LinkSearch", "{0} Asset Link"), ComponentLabel))
            .NameContent()
            [SNew(STextBlock).Text(bMulti ? LOCTEXT("AssetShort", "Asset") : LOCTEXT("AssetFull", "Spline Asset"))
                .ToolTipText(LOCTEXT("AssetPickerTip", "Strand asset assigned to this native spline component. Changing it replaces this component's linked geometry."))]
            .ValueContent().MinDesiredWidth(240.f)
            [SNew(SObjectPropertyEntryBox)
                .AllowedClass(USplineDataAsset::StaticClass())
                .ToolTipText(LOCTEXT("SplineAssetTip", "Assign or clear the Strand asset linked to this spline. Each component has its own independent link."))
                .ObjectPath_Lambda([Weak]() -> FString
                {
                    const USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Weak.Get());
                    return Asset ? Asset->GetPathName() : FString();
                })
                .OnObjectChanged_Lambda([Weak](const FAssetData& Selection)
                {
                    SplineEditorCommands::AssignAssetToComponent(Weak,
                        Cast<USplineDataAsset>(Selection.GetAsset()));
                })
                .DisplayBrowse(true).DisplayUseSelected(true).AllowClear(true)];

            AddSplineRow(FText::Format(LOCTEXT("SyncSearch", "{0} Sync Status"), ComponentLabel))
            .NameContent()[SNew(STextBlock).Text(LOCTEXT("StatusLabel", "Status"))]
            .ValueContent()[SNew(STextBlock).Text_Lambda([Weak]() -> FText
            {
                const USplineComponent* Component = Weak.Get();
                if (!IsValid(Component)) { return LOCTEXT("ComponentMissing", "Unavailable"); }
                const UStrandSplineUserData* Link = UStrandSplineUserData::Get(Component);
                if (!Link || !IsValid(Link->AssignedSplineAsset.Get())) { return LOCTEXT("Unlinked", "Unlinked"); }
                return Link->IsInSync(Component) ? LOCTEXT("Synced", "In sync")
                    : LOCTEXT("LocalChanges", "Modified locally");
            })];

            AddSplineRow(FText::Format(LOCTEXT("AssetActionsSearch", "{0} Asset Actions"), ComponentLabel))
            .WholeRowContent()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                [SNew(SButton).Text(LOCTEXT("ApplyOne", "Apply Asset"))
                    .ToolTipText(LOCTEXT("ApplyOneTip", "Replace this component's geometry with its linked asset. The asset is not changed."))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::ResetComponent(Weak.Get()); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                [SNew(SButton).Text(LOCTEXT("UpdateOne", "Update Asset"))
                    .ToolTipText(LOCTEXT("UpdateOneTip", "Write the edited native spline back to its linked asset after confirmation. Other instances are not refreshed automatically."))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::UpdateComponentAsset(Weak.Get()); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                [SNew(SButton).Text(LOCTEXT("SaveOne", "Save as New"))
                    .ToolTipText(LOCTEXT("SaveOneTip", "Capture this component's native spline as a new Strand asset."))
                    .IsEnabled_Lambda([Weak]() { return Weak.IsValid(); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::CaptureComponent(Weak); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(SButton).Text(LOCTEXT("CompareOne", "Compare"))
                    .ToolTipText(LOCTEXT("CompareOneTip", "Compare this component's current spline geometry with its linked asset."))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::CompareComponent(Weak.Get()); return FReply::Handled(); })]
            ];

            AddSplineRow(FText::Format(LOCTEXT("ProjectionSearch", "{0} Projection Actions"), ComponentLabel))
            .WholeRowContent()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
                [SNew(SButton).Text(LOCTEXT("ProjectionWorkbench", "Projection Tool"))
                    .HAlign(HAlign_Center)
                    .ToolTipText(LOCTEXT("ProjectionWorkbenchTip", "Open the Projection workbench to configure and review scene projection for this native spline."))
                    .IsEnabled_Lambda([Weak]() { return Weak.IsValid(); })
                    .OnClicked_Lambda([Weak, Actors]()
                    {
                        const AStrandSplineActor* Owner = Actors[0].Get();
                        if (IsValid(Owner) && Weak.IsValid())
                        {
                            FStrandProjectionOptions Options;
                            Options.TargetScope = Owner->StrandProjectionTargetScope;
                            Options.RayMode = Owner->StrandProjectionRayMode;
                            Options.Target = Owner->StrandProjectionTarget;
                            for (AActor* Candidate : Owner->StrandProjectionTargets) { Options.Targets.Add(Candidate); }
                            Options.WorldDirection = Owner->StrandProjectionDirection;
                            Options.MaxDistance = Owner->StrandProjectionMaxDistance;
                            Options.Clearance = Owner->StrandProjectionClearance;
                            Options.Strength = Owner->StrandProjectionStrength;
                            Options.FirstAnchor = Owner->StrandProjectionFirstAnchor;
                            Options.LastAnchor = Owner->StrandProjectionLastAnchor;
                            Options.bAlignNormals = Owner->bStrandProjectionAlignNormals;
                            StrandProjectionWorkbench::Open(Weak.Get(), Options);
                        }
                        return FReply::Handled();
                    })]
                + SHorizontalBox::Slot().FillWidth(1)
                [SNew(SButton).Text(LOCTEXT("QuickProjection", "Quick Project"))
                    .HAlign(HAlign_Center)
                    .ToolTipText(LOCTEXT("QuickProjectionTip", "Review projection using the actor's Surface Projection properties. Applying changes this native spline, not its linked asset."))
                    .IsEnabled_Lambda([Weak]() { return Weak.IsValid(); })
                    .OnClicked_Lambda([Weak, Actors]()
                    {
                        const AStrandSplineActor* Owner = Actors[0].Get();
                        if (IsValid(Owner) && Weak.IsValid())
                        {
                            FStrandProjectionOptions Options;
                            Options.TargetScope = Owner->StrandProjectionTargetScope;
                            Options.RayMode = Owner->StrandProjectionRayMode;
                            Options.Target = Owner->StrandProjectionTarget;
                            for (AActor* Candidate : Owner->StrandProjectionTargets) { Options.Targets.Add(Candidate); }
                            Options.WorldDirection = Owner->StrandProjectionDirection;
                            Options.MaxDistance = Owner->StrandProjectionMaxDistance;
                            Options.Clearance = Owner->StrandProjectionClearance;
                            Options.Strength = Owner->StrandProjectionStrength;
                            Options.FirstAnchor = Owner->StrandProjectionFirstAnchor;
                            Options.LastAnchor = Owner->StrandProjectionLastAnchor;
                            Options.bAlignNormals = Owner->bStrandProjectionAlignNormals;
                            SplineEditorCommands::ProjectComponentOntoScene(Weak.Get(), Options);
                        }
                        return FReply::Handled();
                    })]
            ];
        }
    }

    // Multi-spline placement is an explicit bulk operation. A Details group
    // inside Strand, not an unrelated top-level category, makes that scope clear.
    IDetailGroup* PlacementGroup = bMulti
        ? &Category.AddGroup(TEXT("StrandAllSplinePlacement"),
            LOCTEXT("AllSplinePlacement", "All Splines: Placement"), false, false)
        : nullptr;
    auto AddPlacementRow = [&Category, PlacementGroup](const FText& Filter) -> FDetailWidgetRow&
    {
        return PlacementGroup ? PlacementGroup->AddWidgetRow() : Category.AddCustomRow(Filter);
    };

    AddPlacementRow(LOCTEXT("Mode", "Common Coordinate Mode"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("ModeLabel", "Coordinate Mode"))]
    .ValueContent()
    [SNew(SCheckBox)
        .ToolTipText(LOCTEXT("ModeTip", "Checked: Relative; unchecked: Absolute; dash: mixed values. Editing applies to all linked splines."))
        .IsChecked_Lambda([Splines]()
        {
            TOptional<EStrandCoordinateMode> Mode;
            for (const auto& Weak : Splines)
            {
                const auto* Data = UStrandSplineUserData::Get(Weak.Get());
                if (!Data || !Data->AssignedSplineAsset) { continue; }
                if (Mode.IsSet() && Mode.GetValue() != Data->CoordinateMode) { return ECheckBoxState::Undetermined; }
                Mode = Data->CoordinateMode;
            }
            return Mode.IsSet() && Mode.GetValue() == EStrandCoordinateMode::Relative
                ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda([Splines](ECheckBoxState State)
        {
            if (State == ECheckBoxState::Undetermined) { return; }
            EStrandAnchorMode Anchor = EStrandAnchorMode::PointCenter;
            int32 Index = 0;
            for (const auto& Weak : Splines)
            {
                if (const auto* Data = UStrandSplineUserData::Get(Weak.Get()))
                { Anchor = Data->AnchorMode; Index = Data->AnchorPointIndex; break; }
            }
            SplineEditorCommands::ApplyPlacementToComponents(Splines,
                State == ECheckBoxState::Checked ? EStrandCoordinateMode::Relative : EStrandCoordinateMode::Absolute,
                Anchor, Index);
        })
        [SNew(STextBlock).Text(LOCTEXT("RelativeCheckbox", "Relative (off: Absolute)"))]];

    AddPlacementRow(LOCTEXT("Anchor", "Center Anchor"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("AnchorLabel", "Use Center of Spline Points"))]
    .ValueContent()
    [SNew(SCheckBox)
        .ToolTipText(LOCTEXT("CenterTip", "Checked: center anchor; unchecked: point-index anchor. Editing applies to all linked splines."))
        .IsEnabled_Lambda([Splines]()
        {
            bool bAny = false;
            for (const auto& Weak : Splines)
            {
                const auto* Data = UStrandSplineUserData::Get(Weak.Get());
                if (!Data || !Data->AssignedSplineAsset) { continue; }
                bAny = true;
                if (Data->CoordinateMode != EStrandCoordinateMode::Relative) { return false; }
            }
            return bAny;
        })
        .IsChecked_Lambda([Splines]()
        {
            TOptional<EStrandAnchorMode> Anchor;
            for (const auto& Weak : Splines)
            {
                const auto* Data = UStrandSplineUserData::Get(Weak.Get());
                if (!Data || !Data->AssignedSplineAsset) { continue; }
                if (Anchor.IsSet() && Anchor.GetValue() != Data->AnchorMode) { return ECheckBoxState::Undetermined; }
                Anchor = Data->AnchorMode;
            }
            return Anchor.IsSet() && Anchor.GetValue() == EStrandAnchorMode::PointCenter
                ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
        })
        .OnCheckStateChanged_Lambda([Splines](ECheckBoxState State)
        {
            if (State == ECheckBoxState::Undetermined) { return; }
            EStrandCoordinateMode Mode = EStrandCoordinateMode::Relative;
            int32 Index = 0;
            for (const auto& Weak : Splines)
            {
                if (const auto* Data = UStrandSplineUserData::Get(Weak.Get()))
                { Mode = Data->CoordinateMode; Index = Data->AnchorPointIndex; break; }
            }
            SplineEditorCommands::ApplyPlacementToComponents(Splines, Mode,
                State == ECheckBoxState::Checked ? EStrandAnchorMode::PointCenter : EStrandAnchorMode::PointIndex,
                Index);
        })];

    AddPlacementRow(LOCTEXT("Index", "Common Anchor Point Index"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("IndexLabel", "Anchor Point Index"))]
    .ValueContent()
    [SNew(SNumericEntryBox<int32>).MinValue(0)
        .ToolTipText(LOCTEXT("IndexTip", "When Relative placement uses a point anchor, this is its zero-based spline point index. On multi-spline actors this setting applies to all linked splines."))
        .IsEnabled_Lambda([Splines]()
        {
            bool bAny = false;
            for (const auto& Weak : Splines)
            {
                const auto* Data = UStrandSplineUserData::Get(Weak.Get());
                if (!Data || !Data->AssignedSplineAsset) { continue; }
                bAny = true;
                if (Data->CoordinateMode != EStrandCoordinateMode::Relative ||
                    Data->AnchorMode != EStrandAnchorMode::PointIndex) { return false; }
            }
            return bAny;
        })
        .Value_Lambda([Splines]() -> TOptional<int32>
        {
            TOptional<int32> Index;
            for (const auto& Weak : Splines)
            {
                const auto* Data = UStrandSplineUserData::Get(Weak.Get());
                if (!Data || !Data->AssignedSplineAsset) { continue; }
                if (Index.IsSet() && Index.GetValue() != Data->AnchorPointIndex) { return TOptional<int32>(); }
                Index = Data->AnchorPointIndex;
            }
            return Index;
        })
        .OnValueCommitted_Lambda([Splines](int32 Index, ETextCommit::Type)
        {
            EStrandCoordinateMode Mode = EStrandCoordinateMode::Relative;
            EStrandAnchorMode Anchor = EStrandAnchorMode::PointIndex;
            for (const auto& Weak : Splines)
            {
                if (const auto* Data = UStrandSplineUserData::Get(Weak.Get()))
                { Mode = Data->CoordinateMode; Anchor = Data->AnchorMode; break; }
            }
            SplineEditorCommands::ApplyPlacementToComponents(Splines, Mode, Anchor, Index);
        })];

    // Actor-only visualization and Quick Project defaults must not spill into
    // other top-level Details categories. Preserve native property handles,
    // edit conditions, reset-to-default, transactions, and multiselect behavior.
    IDetailGroup& ViewportGroup = Category.AddGroup(TEXT("StrandViewport"),
        LOCTEXT("ViewportGroup", "Viewport"), false, false);
    ViewportGroup.AddPropertyRow(Builder.GetProperty(
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, bShowStrandBillboard)));
    ViewportGroup.AddPropertyRow(Builder.GetProperty(
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandBillboardSize)));

    IDetailGroup& SurfaceGroup = Category.AddGroup(TEXT("StrandSurfaceProjection"),
        LOCTEXT("SurfaceGroup", "Surface Projection"), false, false);
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionTargetScope)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionTargets)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionRayMode)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionTarget)));

    // Native vector expansion repeats X/Y/Z under the compact inline editor.
    // Bind the three inputs to the actual property children, not custom actor
    // mutations, to retain undo/redo, multi-selection, and change notifications.
    const TSharedRef<IPropertyHandle> Direction = Builder.GetProperty(
        GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionDirection));
    // This vector is rendered by the compact color-coded row below.  Hiding
    // its automatically generated property row is essential: otherwise the
    // original expandable FVector is also added to its UPROPERTY category,
    // causing a second top-level Surface Projection section in Details.
    // The custom row still uses the live native child handles for editing.
    Builder.HideProperty(Direction);
    const FLinearColor AxisColors[] = {
        FLinearColor::FromSRGBColor(FColor(0xCB, 0x26, 0x00)),
        FLinearColor::FromSRGBColor(FColor(0x67, 0xA9, 0x03)),
        FLinearColor::FromSRGBColor(FColor(0x2B, 0x7E, 0xEC))
    };
    const FName AxisNames[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };
    TSharedRef<SHorizontalBox> Axes = SNew(SHorizontalBox);
    for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
    {
        const TSharedPtr<IPropertyHandle> Axis = Direction->GetChildHandle(AxisNames[AxisIndex]);
        const FText AxisTooltip = FText::Format(
            LOCTEXT("ProjectionAxisTip", "World-space projection direction {0} component. Active when Ray Mode is World Direction."),
            FText::FromName(AxisNames[AxisIndex]));
        Axes->AddSlot().FillWidth(1.f).Padding(AxisIndex > 0 ? 3.f : 0.f, 0.f, 0.f, 0.f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 1.f, 3.f, 1.f)
            [SNew(SBox).WidthOverride(4.f)
                [SNew(SBorder)
                    .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(AxisColors[AxisIndex])]]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [SNew(SNumericEntryBox<double>)
                .ToolTipText(AxisTooltip)
                .MinDesiredValueWidth(48.f)
                .Value_Lambda([Axis]() -> TOptional<double>
                {
                    double Value = 0.0;
                    return Axis.IsValid() && Axis->GetValue(Value) == FPropertyAccess::Success
                        ? TOptional<double>(Value) : TOptional<double>();
                })
                .OnValueCommitted_Lambda([Axis](double Value, ETextCommit::Type)
                {
                    if (Axis.IsValid()) { Axis->SetValue(Value); }
                })]];
    }
    SurfaceGroup.AddWidgetRow()
        .NameContent()[SNew(STextBlock).Text(LOCTEXT("ProjectionDirectionLabel", "Projection Direction"))
            .ToolTipText(LOCTEXT("ProjectionDirectionTip", "Direction of parallel world-space projection rays. Not used in either camera ray mode."))]
        .ValueContent().MinDesiredWidth(240.f)[Axes]
        .Visibility(TAttribute<EVisibility>::CreateLambda([Actors]()
        {
            for (const auto& Weak : Actors)
            {
                const AStrandSplineActor* Actor = Weak.Get();
                if (IsValid(Actor) && Actor->StrandProjectionRayMode == EStrandProjectionRayMode::WorldDirection)
                    { return EVisibility::Visible; }
            }
            return EVisibility::Collapsed;
        }));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionMaxDistance)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionClearance)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionStrength)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionFirstAnchor)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, StrandProjectionLastAnchor)));
    SurfaceGroup.AddPropertyRow(Builder.GetProperty(GET_MEMBER_NAME_CHECKED(AStrandSplineActor, bStrandProjectionAlignNormals)));



}
#undef LOCTEXT_NAMESPACE
