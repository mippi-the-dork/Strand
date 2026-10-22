#include "StrandSplineMeshCustomization.h"
#include "StrandSplineMeshAdapter.h"
#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "SplineEditorCommands.h"
#include "SplineDataAssetFactory.h"
#include "Components/SplineMeshComponent.h"
#include "GameFramework/Actor.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetData.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "StrandSplineMeshDetails"

namespace
{
    const FSlateBrush* Icon(FName Name)
    {
        const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(TEXT("StrandEditorStyle"));
        return Style ? Style->GetBrush(Name) : nullptr;
    }
    bool Confirm(const FText& Text)
    { return FMessageDialog::Open(EAppMsgType::YesNo, Text) == EAppReturnType::Yes; }
    void Notify(const FText& Text)
    { FMessageDialog::Open(EAppMsgType::Ok, Text); }

    void SetAsset(USplineMeshComponent* Mesh, USplineDataAsset* Asset)
    {
        if (!IsValid(Mesh)) { return; }
        if (IsValid(Asset) && !StrandSplineMeshAdapter::CanApply(Asset))
        {
            Notify(LOCTEXT("Unsupported", "Spline Mesh Components support a single two-point open curve only. Choose a two-point Strand Spline or use a regular Spline Component for multipoint and closed curves."));
            return;
        }
        const FScopedTransaction Transaction(LOCTEXT("Assign", "Assign Strand to Spline Mesh"));
        UStrandSplineUserData::AssignAssetToSplineMesh(Mesh, Asset);
        if (IsValid(Asset) && !StrandSplineMeshAdapter::Matches(Mesh, Asset) && Confirm(
            LOCTEXT("ApplyOnAssign", "This Strand asset differs from the spline mesh. Apply its start/end positions and tangents? Choosing No only links the asset.")))
        { StrandSplineMeshAdapter::Apply(Mesh, Asset); }
    }
    void Apply(USplineMeshComponent* Mesh)
    {
        USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh);
        if (!IsValid(Mesh) || !StrandSplineMeshAdapter::CanApply(Asset)) { return; }
        if (StrandSplineMeshAdapter::Matches(Mesh, Asset)) { return; }
        if (!Confirm(LOCTEXT("ApplyConfirm", "Replace this spline mesh's start/end positions and tangents from its Strand asset?"))) { return; }
        const FScopedTransaction Transaction(LOCTEXT("ApplyTransaction", "Apply Strand to Spline Mesh"));
        StrandSplineMeshAdapter::Apply(Mesh, Asset);
    }
    void Update(USplineMeshComponent* Mesh)
    {
        USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh);
        if (!IsValid(Mesh) || !StrandSplineMeshAdapter::CanApply(Asset)) { return; }
        if (StrandSplineMeshAdapter::Matches(Mesh, Asset)) { return; }
        if (!Confirm(LOCTEXT("UpdateConfirm", "Overwrite the two-point Strand asset from this spline mesh? Other instances will not be updated automatically. Original point rotation, scale and other unsupported mesh parameters will be replaced with defaults. Proceed?"))) { return; }
        const FScopedTransaction Transaction(LOCTEXT("UpdateTransaction", "Update Strand From Spline Mesh"));
        StrandSplineMeshAdapter::Capture(Mesh, Asset, true);
        SplineEditorCommands::SaveAsset(Asset);
    }
    void SaveNew(USplineMeshComponent* Mesh)
    {
        if (!IsValid(Mesh)) { return; }
        USplineDataAssetFactory* Factory = NewObject<USplineDataAssetFactory>();
        const FString SuggestedName = FString::Printf(TEXT("SD_%s"), *Mesh->GetName());
        USplineDataAsset* Asset = Cast<USplineDataAsset>(FAssetToolsModule::GetModule().Get().CreateAssetWithDialog(
            SuggestedName, TEXT("/Game"), USplineDataAsset::StaticClass(), Factory, FName(TEXT("Strand")), false));
        if (!Asset) { return; }
        const FScopedTransaction Transaction(LOCTEXT("SaveNewTransaction", "Save Spline Mesh as Strand"));
        StrandSplineMeshAdapter::Capture(Mesh, Asset, false);
        Asset->DefaultCoordinateMode = EStrandCoordinateMode::Absolute;
        UStrandSplineUserData::AssignAssetToSplineMesh(Mesh, Asset);
        SplineEditorCommands::SaveAsset(Asset);
    }
    void Compare(USplineMeshComponent* Mesh)
    {
        USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh);
        if (!IsValid(Asset) || !IsValid(Mesh)) { return; }
        Notify(StrandSplineMeshAdapter::Matches(Mesh, Asset)
            ? LOCTEXT("Matches", "The spline mesh's endpoint positions and tangents match the Strand asset.")
            : LOCTEXT("Different", "The spline mesh's endpoint positions or tangents differ from the Strand asset. Comparison is limited to the one segment supported by Spline Mesh Components."));
    }
}

TSharedRef<IDetailCustomization> FStrandSplineMeshCustomization::MakeInstance()
{ return MakeShared<FStrandSplineMeshCustomization>(); }

void FStrandSplineMeshCustomization::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    Builder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1) { return; }
    TWeakObjectPtr<USplineMeshComponent> Mesh(Cast<USplineMeshComponent>(Objects[0].Get()));
    if (!Mesh.IsValid()) { return; }
    IDetailCategoryBuilder& Category = Builder.EditCategory(TEXT("Strand"), LOCTEXT("Category", "Strand"), ECategoryPriority::Important);
    Category.AddCustomRow(LOCTEXT("Asset", "Strand Spline Asset"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("AssetLabel", "Strand Spline Asset"))]
    .ValueContent().MinDesiredWidth(260.f)
    [
        SNew(SObjectPropertyEntryBox)
        .AllowedClass(USplineDataAsset::StaticClass())
        .ObjectPath_Lambda([Mesh]() -> FString
        {
            USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh.Get());
            return Asset ? Asset->GetPathName() : FString();
        })
        .OnObjectChanged_Lambda([Mesh](const FAssetData& Data)
        { SetAsset(Mesh.Get(), Cast<USplineDataAsset>(Data.GetAsset())); })
        .DisplayUseSelected(true).DisplayBrowse(true).AllowClear(true)
    ];
    Category.AddCustomRow(LOCTEXT("Status", "Strand Status"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("StatusLabel", "Strand Status"))]
    .ValueContent()
    [ SNew(STextBlock).Text_Lambda([Mesh]() -> FText
    {
        USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh.Get());
        if (!Asset) { return LOCTEXT("Unassigned", "No asset assigned"); }
        return StrandSplineMeshAdapter::Matches(Mesh.Get(), Asset)
            ? LOCTEXT("Synced", "In sync (2-point segment)") : LOCTEXT("Modified", "Modified (2-point segment)");
    }) ];
    Category.AddCustomRow(LOCTEXT("LimitedSupport", "Spline Mesh Limitations"))
    .WholeRowContent()
    [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Limits", "Spline Mesh: one open segment. Strand controls positions and tangents only; mesh, material, collision, roll and scale remain native settings.")) ];

    Category.AddCustomRow(LOCTEXT("Actions", "Strand Actions"))
    .NameContent()[SNew(STextBlock).Text(LOCTEXT("ActionsLabel", "Strand Actions"))]
    .ValueContent().MinDesiredWidth(260.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
        [ SNew(SButton).ToolTipText(LOCTEXT("ApplyTip", "Apply Strand asset to the mesh segment"))
            .IsEnabled_Lambda([Mesh]() { return StrandSplineMeshAdapter::CanApply(UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh.Get())); })
            .OnClicked_Lambda([Mesh]() { Apply(Mesh.Get()); return FReply::Handled(); })
            [ SNew(SImage).Image(Icon(TEXT("Strand.Action.Apply"))).ColorAndOpacity(FSlateColor::UseForeground()) ] ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
        [ SNew(SButton).ToolTipText(LOCTEXT("UpdateTip", "Update linked two-point Strand asset from mesh segment"))
            .IsEnabled_Lambda([Mesh]() { return StrandSplineMeshAdapter::CanApply(UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh.Get())); })
            .OnClicked_Lambda([Mesh]() { Update(Mesh.Get()); return FReply::Handled(); })
            [ SNew(SImage).Image(Icon(TEXT("Strand.Action.Update"))).ColorAndOpacity(FSlateColor::UseForeground()) ] ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
        [ SNew(SButton).ToolTipText(LOCTEXT("NewTip", "Save this spline mesh segment as a new Strand asset"))
            .OnClicked_Lambda([Mesh]() { SaveNew(Mesh.Get()); return FReply::Handled(); })
            [ SNew(SImage).Image(Icon(TEXT("Strand.Action.New"))).ColorAndOpacity(FSlateColor::UseForeground()) ] ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
        [ SNew(SButton).ToolTipText(LOCTEXT("CompareTip", "Compare segment endpoints and tangents to Strand asset"))
            .IsEnabled_Lambda([Mesh]() { return StrandSplineMeshAdapter::CanApply(UStrandSplineUserData::GetAssignedAssetOnSplineMesh(Mesh.Get())); })
            .OnClicked_Lambda([Mesh]() { Compare(Mesh.Get()); return FReply::Handled(); })
            [ SNew(SImage).Image(Icon(TEXT("Strand.Action.Compare"))).ColorAndOpacity(FSlateColor::UseForeground()) ] ]
    ];
}

#undef LOCTEXT_NAMESPACE
