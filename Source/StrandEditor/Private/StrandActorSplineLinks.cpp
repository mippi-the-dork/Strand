#include "StrandActorSplineLinks.h"
#include "ActorDetailsDelegates.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetRegistry/AssetData.h"
#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "SplineEditorCommands.h"
#include "StrandSplineMeshAdapter.h"
#include "StrandSplineActor.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "ScopedTransaction.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "StrandActorSplineLinks"

namespace StrandActorSplineLinks
{
    void ExtendActorDetails(IDetailLayoutBuilder& Builder, const FGetSelectedActors& SelectedActors)
    {
        if (!SelectedActors.IsBound()) { return; }
        const TArray<TWeakObjectPtr<AActor>>& Actors = SelectedActors.Execute();
        // A component picker for a multi-actor selection would silently edit only
        // one actor. Never present an ambiguous or misleading single-object picker.
        if (Actors.Num() != 1) { return; }
        AActor* Actor = Actors[0].Get();
        if (!IsValid(Actor) || Actor->IsTemplate() || Actor->IsA<AStrandSplineActor>()) { return; }

        TArray<USplineComponent*> Splines;
        TArray<USplineMeshComponent*> Meshes;
        Actor->GetComponents<USplineComponent>(Splines);
        Actor->GetComponents<USplineMeshComponent>(Meshes);
        if (Splines.IsEmpty() && Meshes.IsEmpty()) { return; }

        // This extension runs inside ACTOR details, independent of the native
        // USplineComponent/USplineMeshComponent class layouts. UE's Selected
        // Points widget and FSplineComponentVisualizer remain registered.
        IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Strand Links"),
            LOCTEXT("Category", "Strand Links"), ECategoryPriority::Default);
        Category.InitiallyCollapsed(true);
        Category.AddCustomRow(LOCTEXT("NativeHintSearch", "Strand links Native spline editing"))
        .WholeRowContent()[SNew(STextBlock)
            .Text(LOCTEXT("NativeHint", "Asset links for this actor. Select the component for Unreal's native spline-point tools."))
            .AutoWrapText(true)];

        for (USplineComponent* Spline : Splines)
        {
            const TWeakObjectPtr<USplineComponent> Weak(Spline);
            const FText Name = FText::FromString(Spline->GetName());
            Category.AddCustomRow(Name)
            .NameContent()[SNew(STextBlock).Text(Name)]
            .ValueContent().MinDesiredWidth(250.f)
            [SNew(SObjectPropertyEntryBox)
                .AllowedClass(USplineDataAsset::StaticClass())
                .ObjectPath_Lambda([Weak]() -> FString
                {
                    const USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Weak.Get());
                    return Asset ? Asset->GetPathName() : FString();
                })
                .OnObjectChanged_Lambda([Weak](const FAssetData& Selection)
                {
                    SplineEditorCommands::AssignAssetToComponent(Weak, Cast<USplineDataAsset>(Selection.GetAsset()));
                })
                .DisplayBrowse(true).DisplayUseSelected(true).AllowClear(true)];
            Category.AddCustomRow(FText::Format(LOCTEXT("SplineCommands", "{0} Strand Actions"), Name))
            .WholeRowContent()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                [SNew(SButton).Text(LOCTEXT("Apply", "Apply Asset"))
                    .ToolTipText(LOCTEXT("ApplyTip", "Replace this instance's geometry with the linked asset. Other native settings are untouched."))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::ResetComponent(Weak.Get()); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                [SNew(SButton).Text(LOCTEXT("Update", "Update Asset"))
                    .ToolTipText(LOCTEXT("UpdateTip", "Capture changes from the component back to the linked asset, after confirmation."))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::UpdateComponentAsset(Weak.Get()); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                [SNew(SButton).Text(LOCTEXT("Capture", "Save as New"))
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::CaptureComponent(Weak); return FReply::Handled(); })]
                + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                [SNew(SButton).Text(LOCTEXT("Compare", "Compare"))
                    .IsEnabled_Lambda([Weak]() { return IsValid(UStrandSplineUserData::GetAssignedAsset(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() { SplineEditorCommands::CompareComponent(Weak.Get()); return FReply::Handled(); })]
            ];
            Category.AddCustomRow(FText::Format(LOCTEXT("SplineState", "{0} linked state"), Name))
            .WholeRowContent()[SNew(STextBlock).Text_Lambda([Weak]() -> FText
            {
                USplineComponent* Component = Weak.Get();
                if (!IsValid(Component)) { return FText::GetEmpty(); }
                const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Component);
                if (!Data || !IsValid(Data->AssignedSplineAsset)) { return LOCTEXT("Unlinked", "Unlinked (native spline editing is unchanged)"); }
                return Data->IsInSync(Component) ? LOCTEXT("LinkedSynced", "Linked / In sync")
                    : LOCTEXT("LinkedModified", "Linked / Modified locally");
            })];
        }
        for (USplineMeshComponent* Mesh : Meshes)
        {
            const TWeakObjectPtr<USplineMeshComponent> Weak(Mesh);
            const FText Name = FText::FromString(Mesh->GetName());
            Category.AddCustomRow(Name)
            .NameContent()[SNew(STextBlock).Text(Name)]
            .ValueContent().MinDesiredWidth(250.f)
            [SNew(SObjectPropertyEntryBox)
                .AllowedClass(USplineDataAsset::StaticClass())
                .ObjectPath_Lambda([Weak]() -> FString
                {
                    const USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Weak.Get());
                    return Asset ? Asset->GetPathName() : FString();
                })
                .OnObjectChanged_Lambda([Weak](const FAssetData& Selection)
                {
                    USplineMeshComponent* Component = Weak.Get();
                    USplineDataAsset* Asset = Cast<USplineDataAsset>(Selection.GetAsset());
                    if (!IsValid(Component)) { return; }
                    if (Asset && !StrandSplineMeshAdapter::CanApply(Asset))
                    {
                        FMessageDialog::Open(EAppMsgType::Ok,
                            LOCTEXT("MeshLimit", "Spline Mesh Components support only an open two-point Strand asset."));
                        return;
                    }
                    const FScopedTransaction Transaction(LOCTEXT("AssignMesh", "Assign Strand Spline Mesh Asset"));
                    UStrandSplineUserData::AssignAssetToSplineMesh(Component, Asset);
                })
                .DisplayBrowse(true).DisplayUseSelected(true).AllowClear(true)];
            Category.AddCustomRow(FText::Format(LOCTEXT("MeshActions", "{0} Actions"), Name))
            .WholeRowContent()[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(2.f)
                [SNew(SButton).Text(LOCTEXT("ApplyMesh", "Apply 2-Point Asset"))
                    .IsEnabled_Lambda([Weak]() { return StrandSplineMeshAdapter::CanApply(
                        UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Weak.Get())); })
                    .OnClicked_Lambda([Weak]() -> FReply
                    {
                        USplineMeshComponent* Component = Weak.Get();
                        USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Component);
                        if (IsValid(Component) && StrandSplineMeshAdapter::CanApply(Asset) &&
                            FMessageDialog::Open(EAppMsgType::YesNo,
                                LOCTEXT("MeshConfirm", "Replace the spline mesh's positions and tangents with its linked asset?")) == EAppReturnType::Yes)
                        {
                            const FScopedTransaction Transaction(LOCTEXT("ApplyMeshTx", "Apply Strand Spline Mesh Asset"));
                            StrandSplineMeshAdapter::Apply(Component, Asset);
                        }
                        return FReply::Handled();
                    })]
            ];
        }
    }
}

#undef LOCTEXT_NAMESPACE
