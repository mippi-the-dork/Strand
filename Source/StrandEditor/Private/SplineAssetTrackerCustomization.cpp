#include "SplineAssetTrackerCustomization.h"
#include "SplineAssetTrackerComponent.h"
#include "SplineDataAsset.h"
#include "SplineEditorCommands.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SVerticalBox.h"
#include "Widgets/Layout/SHorizontalBox.h"

#define LOCTEXT_NAMESPACE "SplineAssetTrackerDetails"

TSharedRef<IDetailCustomization> FSplineAssetTrackerCustomization::MakeInstance()
{
    return MakeShared<FSplineAssetTrackerCustomization>();
}

void FSplineAssetTrackerCustomization::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    Builder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1) { return; }
    Tracker = Cast<USplineAssetTrackerComponent>(Objects[0].Get());
    if (!Tracker.IsValid()) { return; }

    IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Strand"));
    Category.AddCustomRow(LOCTEXT("ActionSearch", "Spline Sync Actions"))
    .WholeRowContent()
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
        [
            SNew(STextBlock)
            .Text_Lambda([Weak = Tracker]() -> FText
            {
                const USplineAssetTrackerComponent* T = Weak.Get();
                if (!T || !T->AssignedSplineAsset) { return LOCTEXT("None", "Assign a Spline Data Asset to enable synchronization."); }
                if (!T->ResolveSpline()) { return LOCTEXT("NoSpline", "Spline target is ambiguous or missing. Set Tracked Spline to a component on this actor."); }
                return T->HasLocalChanges() ? LOCTEXT("Modified", "Local spline differs from the asset.")
                                            : LOCTEXT("InSync", "Local spline matches the asset.");
            })
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("Compare", "Compare Local vs Asset"))
                .ToolTipText(LOCTEXT("CompareTip", "Read-only summary of point count, value differences, and maximum position displacement."))
                .IsEnabled_Lambda([Weak = Tracker]()
                {
                    const auto* T = Weak.Get();
                    return T && IsValid(T->AssignedSplineAsset) && IsValid(T->ResolveSpline());
                })
                .OnClicked_Lambda([Weak = Tracker]() -> FReply
                {
                    SplineEditorCommands::CompareTracker(Weak.Get());
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("FindAsset", "Browse to Data Asset"))
                .IsEnabled_Lambda([Weak = Tracker]()
                {
                    const auto* T = Weak.Get();
                    return T && IsValid(T->AssignedSplineAsset);
                })
                .OnClicked_Lambda([Weak = Tracker]() -> FReply
                {
                    SplineEditorCommands::BrowseTrackerAsset(Weak.Get());
                    return FReply::Handled();
                })
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("Reset", "Reset to Spline Data Asset"))
                .IsEnabled_Lambda([Weak = Tracker]()
                {
                    const auto* T = Weak.Get();
                    return T && IsValid(T->AssignedSplineAsset) && IsValid(T->ResolveSpline());
                })
                .OnClicked_Lambda([Weak = Tracker]() -> FReply
                {
                    SplineEditorCommands::ResetTracker(Weak.Get());
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
            [
                SNew(SButton)
                .Text(LOCTEXT("Update", "Update Spline Data Asset"))
                .IsEnabled_Lambda([Weak = Tracker]()
                {
                    const auto* T = Weak.Get();
                    return T && IsValid(T->AssignedSplineAsset) && IsValid(T->ResolveSpline());
                })
                .OnClicked_Lambda([Weak = Tracker]() -> FReply
                {
                    SplineEditorCommands::UpdateTracker(Weak.Get());
                    return FReply::Handled();
                })
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
