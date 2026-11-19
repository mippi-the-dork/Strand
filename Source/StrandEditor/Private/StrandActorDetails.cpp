#include "StrandActorDetails.h"
#include "StrandSplineActor.h"
#include "StrandMultiSplineActor.h"
#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "SplineEditorCommands.h"
#include "StrandPlacement.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
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

    IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Strand"), LOCTEXT("Strand", "Strand"), ECategoryPriority::Important);
    if (Actors.Num() == 1 && Cast<AStrandMultiSplineActor>(Actors[0].Get()))
    {
        // Native component list. No custom spline hierarchy or duplicate asset
        // properties: every picker edits the component's Asset User Data.
        for (int32 I = 0; I < Splines.Num(); ++I)
        {
            const TWeakObjectPtr<USplineComponent> Weak = Splines[I];
            const FString Label = Weak.IsValid() ? Weak->GetName() : FString::Printf(TEXT("Spline %d"), I + 1);
            Category.AddCustomRow(FText::FromString(Label))
            .NameContent()[SNew(STextBlock).Text(FText::FromString(Label))]
            .ValueContent().MinDesiredWidth(250)
            [SNew(SObjectPropertyEntryBox)
                .AllowedClass(USplineDataAsset::StaticClass())
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
        }
    }

    // Keep Strand-native actors at parity with arbitrary actors containing native
    // USplineComponents. These controls operate on a named component, never an
    // ambiguous "first spline". Both the single and multi actor use this path.
    if (Actors.Num() == 1)
    {
        IDetailCategoryBuilder& Links = Builder.EditCategory(TEXT("Strand Asset Links"),
            LOCTEXT("LinksCategory", "Strand Asset Links"), ECategoryPriority::Important);
        for (const TWeakObjectPtr<USplineComponent>& Weak : Splines)
        {
            const FString ComponentName = Weak.IsValid() ? Weak->GetName() : TEXT("Missing Spline");
            const FText Name = FText::FromString(ComponentName);
            Links.AddCustomRow(FText::FromString(ComponentName + TEXT(" Asset Commands")))
            .WholeRowContent()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [SNew(SButton).Text(LOCTEXT("ApplyOne", "Apply Asset"))
                    .ToolTipText(FText::Format(LOCTEXT("ApplyOneTip", "Replace the native spline '{0}' with its linked asset. Does not update the shared asset."), Name))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::ResetComponent(Weak.Get()); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [SNew(SButton).Text(LOCTEXT("UpdateOne", "Update Asset"))
                    .ToolTipText(FText::Format(LOCTEXT("UpdateOneTip", "Save the edited native spline '{0}' back into its linked shared Strand asset after confirmation. Other instances are not automatically updated."), Name))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::UpdateComponentAsset(Weak.Get()); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [SNew(SButton).Text(LOCTEXT("SaveOne", "Save as New"))
                    .ToolTipText(LOCTEXT("SaveOneTip", "Capture this native spline as a separate Strand asset."))
                    .IsEnabled_Lambda([Weak]() { return Weak.IsValid(); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::CaptureComponent(Weak); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(2)
                [SNew(SButton).Text(LOCTEXT("CompareOne", "Compare"))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::CompareComponent(Weak.Get()); return FReply::Handled(); })]
            ];
            // Actor-level projection is explicit. The target and its parameters
            // live on the placed Strand actor and never in a standalone asset.
            Links.AddCustomRow(FText::FromString(ComponentName + TEXT(" Surface Projection")))
            .WholeRowContent()
            [SNew(SButton).Text(FText::Format(LOCTEXT("ProjectOne", "Review Surface Projection: {0}"), Name))
                .ToolTipText(LOCTEXT("ProjectTip", "Project this native spline's anchors onto the Surface Target assigned on the actor. First review hit/miss counts, then confirm or cancel. Uses the configured target scope and ray mode; does not automatically update the linked Strand asset."))
                .IsEnabled_Lambda([Weak, Actors]()
                {
                    const AStrandSplineActor* Owner = Actors[0].Get();
                    return Weak.IsValid() && IsValid(Owner);
                })
                .OnClicked_Lambda([Weak, Actors]()
                {
                    const AStrandSplineActor* Owner = Actors[0].Get();
                    if (IsValid(Owner))
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
                })];
            Links.AddCustomRow(FText::FromString(ComponentName + TEXT(" Sync")))
            .WholeRowContent()[SNew(STextBlock).Text_Lambda([Weak, ComponentName]() -> FText
            {
                const USplineComponent* Component = Weak.Get();
                if (!IsValid(Component)) { return LOCTEXT("Missing", "Spline component unavailable"); }
                const UStrandSplineUserData* Link = UStrandSplineUserData::Get(Component);
                const FString State = !Link || !IsValid(Link->AssignedSplineAsset.Get())
                    ? TEXT("Unlinked") : Link->IsInSync(Component) ? TEXT("In sync") : TEXT("Modified locally");
                return FText::FromString(ComponentName + TEXT(": ") + State);
            })];
        }
    }

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
        return FText::FromString(FString::Printf(TEXT("%d actor(s) | %d native spline(s) | %d linked | %d in sync | %d different"),
            Actors.Num(), Splines.Num(), Linked, Synced, Different));
    })];

    Category.AddCustomRow(LOCTEXT("Asset", "Shared Strand Asset"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("AssetLabel", "Strand Spline Asset"))]
    .ValueContent().MinDesiredWidth(250)
    [SNew(SObjectPropertyEntryBox)
        .AllowedClass(USplineDataAsset::StaticClass())
        .ObjectPath_Lambda([Splines]() -> FString
        {
            USplineDataAsset* Common = nullptr;
            bool bFirst = true;
            for (const auto& Weak : Splines)
            {
                USplineDataAsset* Candidate = UStrandSplineUserData::GetAssignedAsset(Weak.Get());
                if (bFirst) { Common = Candidate; bFirst = false; }
                else if (Candidate != Common) { return FString(); }
            }
            return IsValid(Common) ? Common->GetPathName() : FString();
        })
        .OnObjectChanged_Lambda([Splines](const FAssetData& Selection)
        {
            if (USplineDataAsset* Asset = Cast<USplineDataAsset>(Selection.GetAsset()))
            {
                SplineEditorCommands::AssignAssetToComponents(Splines, Asset);
            }
        })
        .DisplayBrowse(true).DisplayUseSelected(true).AllowClear(false)];

    Category.AddCustomRow(LOCTEXT("Mode", "Common Coordinate Mode"))
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
        [SNew(STextBlock).Text(LOCTEXT("RelativeCheckbox", "Relative (unchecked: Absolute)"))]];

    Category.AddCustomRow(LOCTEXT("Anchor", "Center Anchor"))
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

    Category.AddCustomRow(LOCTEXT("Index", "Common Anchor Point Index"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("IndexLabel", "Anchor Point Index"))]
    .ValueContent()
    [SNew(SNumericEntryBox<int32>).MinValue(0)
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

    const TSharedPtr<IPropertyUtilities> PropertyUtilities = Builder.GetPropertyUtilities();

    Category.AddCustomRow(LOCTEXT("Actions", "Strand Selection Actions"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("ActionsLabel", "Strand Actions"))]
    .ValueContent().MinDesiredWidth(280)
    [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
        [SNew(SButton).Text(LOCTEXT("Apply", "Apply All"))
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
#undef LOCTEXT_NAMESPACE
