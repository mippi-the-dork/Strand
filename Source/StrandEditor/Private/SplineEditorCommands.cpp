#include "SplineEditorCommands.h"
#include "SplineDataAsset.h"
#include "StrandSplineUserData.h"
#include "SplineDataAssetFactory.h"
#include "SplineLandscapeAdapter.h"
#include "StrandPlacement.h"
#include "StrandSplineRefresh.h"
#include "EngineUtils.h"
#include "Components/SplineComponent.h"
#include "LandscapeSplinesComponent.h"
#include "LandscapeSplineControlPoint.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "UObject/SavePackage.h"
#include "ScopedTransaction.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SplineAssetEditorCommands"

namespace
{
    bool Confirm(const FText& Prompt)
    {
        return FMessageDialog::Open(EAppMsgType::YesNo, Prompt) == EAppReturnType::Yes;
    }
    void Message(const FString& Detail)
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Detail));
    }
    // Intentionally process-local: no ini/config persistence across editor sessions.
    bool bSuppressPlacementDiscardPromptThisSession = false;

    bool ConfirmPlacementDiscard()
    {
        if (bSuppressPlacementDiscardPromptThisSession) { return true; }
        bool bAccepted = false;
        bool bDontShowAgain = false;
        TSharedRef<SWindow> Dialog = SNew(SWindow)
            .Title(LOCTEXT("PlacementDiscardTitle", "Strand Placement"))
            .SizingRule(ESizingRule::Autosized)
            .SupportsMinimize(false).SupportsMaximize(false);
        Dialog->SetContent(
            SNew(SBox).WidthOverride(440).Padding(18)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)
                [SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("PlacementDiscardQuestion",
                    "Changing Strand placement will replace unsaved local spline edits with the asset's points. Continue?"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)
                [ SNew(SCheckBox)
                    .OnCheckStateChanged_Lambda([&bDontShowAgain](ECheckBoxState State) { bDontShowAgain = State == ECheckBoxState::Checked; })
                    [SNew(STextBlock).Text(LOCTEXT("SkipPlacementPrompt", "Don't show this again during this session"))] ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                    [SNew(SButton).Text(LOCTEXT("CancelPlacement", "Cancel"))
                        .OnClicked_Lambda([Dialog]() { Dialog->RequestDestroyWindow(); return FReply::Handled(); })]
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(SButton).Text(LOCTEXT("AcceptPlacement", "Continue"))
                        .OnClicked_Lambda([Dialog, &bAccepted]() { bAccepted = true; Dialog->RequestDestroyWindow(); return FReply::Handled(); })]
                ]
            ]);
        FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
        if (bAccepted && bDontShowAgain) { bSuppressPlacementDiscardPromptThisSession = true; }
        return bAccepted;
    }

    bool MatchesPlacement(const USplineDataAsset* Asset, const USplineComponent* Spline)
    {
        if (!IsValid(Asset) || !IsValid(Spline)) { return false; }
        const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Spline);
        return Data && Data->AssignedSplineAsset == Asset
            ? Data->IsInSync(Spline)
            : Asset->MatchesSplineComponentWithPlacement(Spline,
                Asset->DefaultCoordinateMode, Asset->DefaultAnchorMode, Asset->DefaultAnchorPointIndex);
    }

    void ApplyLinked(USplineDataAsset* Asset, USplineComponent* Spline)
    {
        if (!IsValid(Asset) || !IsValid(Spline)) { return; }
        const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Spline);
        const EStrandCoordinateMode Mode = Data ? Data->CoordinateMode : Asset->DefaultCoordinateMode;
        const EStrandAnchorMode Anchor = Data ? Data->AnchorMode : Asset->DefaultAnchorMode;
        const int32 Index = Data ? Data->AnchorPointIndex : Asset->DefaultAnchorPointIndex;
        Asset->ApplyToSplineComponentWithPlacement(Spline, Mode, Anchor, Index);
        StrandSplineRefresh::NotifySplineChanged(Spline);
    }
    // Transaction is owned by the caller. Explicit target prevents accidental writes
    // to arbitrary spline components when an actor has multiple candidates.
    void ApplyToExplicitComponent(USplineDataAsset* Asset, USplineComponent* Spline)
    {
        AActor* Actor = IsValid(Spline) ? Spline->GetOwner() : nullptr;
        if (!IsValid(Asset) || !IsValid(Actor)) { return; }
        Actor->Modify();
        const bool bAlreadyLinked = UStrandSplineUserData::GetAssignedAsset(Spline) == Asset;
        const bool bMatches = MatchesPlacement(Asset, Spline);
        UStrandSplineUserData::AssignAsset(Spline, Asset);
        if (!bMatches || !bAlreadyLinked)
        {
            if (!MatchesPlacement(Asset, Spline)) { ApplyLinked(Asset, Spline); }
        }
    }
}

void SplineEditorCommands::ImportCurves(bool bAlembic)
{
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop) { Message(TEXT("Desktop file dialogs are unavailable.")); return; }
    TArray<FString> Filenames;
    int32 Filter = 0;
    const bool bChosen = Desktop->OpenFileDialog(
        FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        bAlembic ? TEXT("Import Alembic Curves into Strand") : TEXT("Import SVG Curves into Strand"),
        FPaths::ProjectContentDir(), TEXT(""),
        bAlembic ? TEXT("Alembic Curves (*.abc)|*.abc") : TEXT("SVG Vectors (*.svg)|*.svg"),
        EFileDialogFlags::Multiple, Filenames, Filter);
    if (!bChosen || Filenames.IsEmpty()) { return; }

    IContentBrowserSingleton& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(
        "ContentBrowser").Get();
    TArray<FString> Folders;
    Browser.GetSelectedFolders(Folders);
    const FString Destination = Folders.Num() == 1 && Folders[0].StartsWith(TEXT("/Game"))
        ? Folders[0] : TEXT("/Game");
    UFactory* Factory = bAlembic ? static_cast<UFactory*>(NewObject<UStrandAlembicFactory>())
                                : static_cast<UFactory*>(NewObject<USplineSvgFactory>());
    const TArray<UObject*> Imported = FAssetToolsModule::GetModule().Get().ImportAssets(
        Filenames, Destination, Factory, true, nullptr, false, false);
    if (Imported.IsEmpty())
    {
        Message(TEXT("No curves imported. Check the Output Log and verify that source files contain supported curve geometry."));
    }
}

TArray<AActor*> SplineEditorCommands::SelectedActors()
{
    TArray<AActor*> Actors;
    if (GEditor && GEditor->GetSelectedActors())
    {
        for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
        {
            if (AActor* Actor = Cast<AActor>(*It)) { Actors.Add(Actor); }
        }
    }
    return Actors;
}

USplineComponent* SplineEditorCommands::FindUnambiguousSpline(AActor* Actor)
{
    if (!IsValid(Actor)) { return nullptr; }
    TArray<USplineComponent*> Components;
    Actor->GetComponents<USplineComponent>(Components);
    if (Components.Num() == 1) { return Components[0]; }
    // Never guess which spline to change on actors containing multiple components.
    return nullptr;
}

TArray<USplineDataAsset*> SplineEditorCommands::SelectedSplineAssets()
{
    TArray<FAssetData> Selected;
    FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().GetSelectedAssets(Selected);
    TArray<USplineDataAsset*> Assets;
    for (const FAssetData& Data : Selected)
    {
        if (Data.AssetClassPath != USplineDataAsset::StaticClass()->GetClassPathName()) { continue; }
        if (USplineDataAsset* Asset = Cast<USplineDataAsset>(Data.GetAsset())) { Assets.AddUnique(Asset); }
    }
    return Assets;
}

void SplineEditorCommands::AddSplineComponentsAndAssign(const TArray<USplineDataAsset*>& Assets)
{
    const TArray<AActor*> Actors = SelectedActors();
    if (Actors.IsEmpty() || Assets.IsEmpty())
    {
        Message(TEXT("Select one or more actors and one or more Strand Spline assets.")); return;
    }
    if (Actors.Num() * Assets.Num() > 256)
    {
        Message(TEXT("This operation is limited to 256 new spline components at once.")); return;
    }
    const FString Description = FString::Printf(TEXT("Add %d native spline component(s) to each of %d selected actor(s), assigning the chosen Strand assets?"), Assets.Num(), Actors.Num());
    if (!Confirm(FText::FromString(Description))) { return; }
    const FScopedTransaction Transaction(LOCTEXT("AddAndAssign", "Add Native Splines and Assign Strand Assets"));
    int32 Added = 0;
    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor) || Actor->IsTemplate()) { continue; }
        Actor->Modify();
        for (USplineDataAsset* Asset : Assets)
        {
            if (!IsValid(Asset)) { continue; }
            if (USplineComponent* Component = StrandPlacement::AddNativeSpline(Actor))
            {
                Asset->ApplyToSplineComponentWithPlacement(Component, Asset->DefaultCoordinateMode,
                    Asset->DefaultAnchorMode, Asset->DefaultAnchorPointIndex);
                UStrandSplineUserData::AssignAsset(Component, Asset);
                StrandSplineRefresh::NotifySplineChanged(Component);
                ++Added;
            }
        }
        Actor->MarkPackageDirty();
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
    UE_LOG(LogTemp, Display, TEXT("Strand added %d native spline components and assigned assets."), Added);
}

void SplineEditorCommands::CreateMultiActorFromAssets(const TArray<USplineDataAsset*>& Assets)
{
    FString Error;
    if (!StrandPlacement::CreateMultiSplineActor(Assets, Error)) { Message(Error); }
}

void SplineEditorCommands::ApplyPlacementToComponents(const TArray<TWeakObjectPtr<USplineComponent>>& Splines,
    EStrandCoordinateMode Mode, EStrandAnchorMode AnchorMode, int32 AnchorPointIndex)
{
    if (Splines.IsEmpty()) { return; }
    // Ask once for the whole operation. Only components with an assigned asset are processed.
    bool bDiscarding = false;
    for (const TWeakObjectPtr<USplineComponent>& Weak : Splines)
    {
        const UStrandSplineUserData* Data = UStrandSplineUserData::Get(Weak.Get());
        bDiscarding |= Data && IsValid(Data->AssignedSplineAsset) && !Data->IsInSync(Weak.Get());
    }
    if (bDiscarding && !ConfirmPlacementDiscard()) { return; }
    const FScopedTransaction Transaction(LOCTEXT("BatchPlacement", "Change Strand Placement on Selected Splines"));
    for (const TWeakObjectPtr<USplineComponent>& Weak : Splines)
    {
        USplineComponent* Component = Weak.Get();
        UStrandSplineUserData* Data = UStrandSplineUserData::Get(Component);
        if (!Data || !IsValid(Data->AssignedSplineAsset)) { continue; }
        Component->Modify();
        UStrandSplineUserData::EnsureComponentOwned(Component);
        Data = UStrandSplineUserData::Get(Component);
        Data->Modify();
        if (AActor* Owner = Component->GetOwner()) { Owner->Modify(); }
        Data->CoordinateMode = Mode;
        Data->AnchorMode = AnchorMode;
        Data->AnchorPointIndex = FMath::Clamp(AnchorPointIndex, 0, FMath::Max(0, Data->AssignedSplineAsset->Points.Num() - 1));
        Data->AssignedSplineAsset->ApplyToSplineComponentWithPlacement(Component, Mode, AnchorMode, Data->AnchorPointIndex);
        StrandSplineRefresh::NotifySplineChanged(Component);
    }
}

void SplineEditorCommands::AssignAssetToComponents(const TArray<TWeakObjectPtr<USplineComponent>>& Splines, USplineDataAsset* Asset)
{
    if (!IsValid(Asset) || Splines.IsEmpty()) { return; }
    if (!Confirm(FText::Format(LOCTEXT("BatchAssignConfirm", "Assign and apply {0} to {1} selected spline components? Existing local points will be replaced."),
            FText::FromString(Asset->GetName()), FText::AsNumber(Splines.Num())))) { return; }
    const FScopedTransaction Transaction(LOCTEXT("BatchAssign", "Assign Strand Asset to Selected Splines"));
    for (const TWeakObjectPtr<USplineComponent>& Weak : Splines)
    {
        USplineComponent* Component = Weak.Get();
        if (!IsValid(Component)) { continue; }
        if (AActor* Owner = Component->GetOwner()) { Owner->Modify(); }
        UStrandSplineUserData::AssignAsset(Component, Asset);
        Asset->ApplyToSplineComponentWithPlacement(Component, Asset->DefaultCoordinateMode,
            Asset->DefaultAnchorMode, Asset->DefaultAnchorPointIndex);
        StrandSplineRefresh::NotifySplineChanged(Component);
    }
}

void SplineEditorCommands::CompareComponent(USplineComponent* Spline)
{
    const USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Spline);
    if (!IsValid(Spline) || !IsValid(Asset)) { return; }
    const int32 Local = Spline->GetNumberOfSplinePoints();
    const int32 Saved = Asset->Points.Num();
    const UStrandSplineUserData* Placement = UStrandSplineUserData::Get(Spline);
    const FVector AnchorOffset = Placement && Placement->CoordinateMode == EStrandCoordinateMode::Relative
        ? Asset->GetPlacementAnchor(Placement->AnchorMode, Placement->AnchorPointIndex) : FVector::ZeroVector;
    int32 DifferentPoints = 0;
    double LargestPosDelta = 0;
    int32 FirstDifferentIndex = INDEX_NONE;
    for (int32 I = 0; I < FMath::Min(Local, Saved); ++I)
    {
        const FSplinePoint Current = Spline->GetSplinePointAt(I, ESplineCoordinateSpace::Local);
        const FSplinePointData& Reference = Asset->Points[I];
        const bool bDifferent = !Current.Position.Equals(Reference.LocalPosition - AnchorOffset, 0.01) ||
            !Current.ArriveTangent.Equals(Reference.ArriveTangent, 0.01) ||
            !Current.LeaveTangent.Equals(Reference.LeaveTangent, 0.01) ||
            !Current.Rotation.Equals(Reference.LocalRotation, 0.01) ||
            !Current.Scale.Equals(Reference.LocalScale, 0.01) ||
            Spline->GetSplinePointType(I) != Reference.PointType.GetValue();
        if (bDifferent)
        {
            ++DifferentPoints;
            if (FirstDifferentIndex == INDEX_NONE) { FirstDifferentIndex = I; }
        }
        LargestPosDelta = FMath::Max(LargestPosDelta,
            FVector::Distance(Current.Position, Reference.LocalPosition - AnchorOffset));
    }
    const bool bLoopMismatch = Spline->IsClosedLoop() != Asset->bIsClosedLoop;
    Message(FString::Printf(TEXT("Strand comparison\n\nAsset points: %d\nLocal points: %d\nDiffering matched points: %d\nFirst different point index: %s\nLargest position delta: %.3f cm\nClosed loop differs: %s\n\nComparison only. No data was changed."),
        Saved, Local, DifferentPoints,
        FirstDifferentIndex == INDEX_NONE ? TEXT("None") : *FString::FromInt(FirstDifferentIndex),
        LargestPosDelta, bLoopMismatch ? TEXT("Yes") : TEXT("No")));
}

USplineDataAsset* SplineEditorCommands::SelectedSplineAsset()
{
    TArray<FAssetData> Selected;
    FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().GetSelectedAssets(Selected);
    return Selected.Num() == 1 ? Cast<USplineDataAsset>(Selected[0].GetAsset()) : nullptr;
}

bool SplineEditorCommands::SaveAsset(USplineDataAsset* Asset)
{
    if (!IsValid(Asset)) { return false; }
    UPackage* Package = Asset->GetOutermost();
    if (!Package || !FPackageName::IsValidLongPackageName(Package->GetName())) { return false; }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const bool bSuccess = UPackage::SavePackage(Package, Asset, *Filename, Args);
    if (!bSuccess) { Message(TEXT("Failed to save the spline asset. Check source control and Output Log.")); }
    return bSuccess;
}

void SplineEditorCommands::ConvertSelectedActors()
{
    const TArray<AActor*> Actors = SelectedActors();
    if (Actors.IsEmpty()) { return; }
    IAssetTools& Tools = FAssetToolsModule::GetModule().Get();
    for (AActor* Actor : Actors)
    {
        USplineComponent* Spline = FindUnambiguousSpline(Actor);
        if (!Spline)
        {
            Message(FString::Printf(TEXT("%s must have exactly one Spline Component. For actors with multiple splines, use a named component action or select a specific Spline Component in Details."), *Actor->GetName()));
            continue;
        }
        USplineDataAssetFactory* Factory = NewObject<USplineDataAssetFactory>();
        const FString SuggestedName = TEXT("SD_") + Actor->GetActorNameOrLabel();
        USplineDataAsset* Asset = Cast<USplineDataAsset>(Tools.CreateAssetWithDialog(
            SuggestedName, TEXT("/Game"), USplineDataAsset::StaticClass(), Factory,
            FName(TEXT("Strand")), false));
        if (!Asset) { continue; } // User canceled.
        const FScopedTransaction Transaction(LOCTEXT("Convert", "Convert Spline to Spline Data Asset"));
        Actor->Modify();
        Spline->Modify();
        Asset->Modify();
        Asset->SerializeFromSplineComponent(Spline);
        Asset->DefaultCoordinateMode = EStrandCoordinateMode::Absolute;
        Asset->DefaultAnchorMode = EStrandAnchorMode::PointIndex;
        UStrandSplineUserData::AssignAsset(Spline, Asset);
        SaveAsset(Asset);
        Actor->MarkPackageDirty();
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
}

void SplineEditorCommands::ApplyAssetToSelectedActors(TWeakObjectPtr<USplineDataAsset> WeakAsset)
{
    USplineDataAsset* Asset = WeakAsset.Get();
    if (!Asset) { return; }
    const TArray<AActor*> Actors = SelectedActors();
    if (Actors.IsEmpty()) { Message(TEXT("Select one or more level Actors with native Spline Components.")); return; }
    TArray<USplineComponent*> Targets;
    TArray<FString> Skipped;
    for (AActor* Actor : Actors)
    {
        if (USplineComponent* Spline = FindUnambiguousSpline(Actor))
        {
            Targets.Add(Spline);
        }
        else if (IsValid(Actor))
        {
            Skipped.Add(Actor->GetActorNameOrLabel());
        }
    }
    if (Targets.IsEmpty())
    {
        Message(TEXT("No unambiguous spline targets. For multi-spline actors, right-click the actor and choose an individual named spline component."));
        return;
    }
    const FString Prompt = FString::Printf(
        TEXT("Apply Strand asset '%s' to %d native spline component(s)?\n\nThis replaces their local point data but preserves Actor/Component transforms.\n\n%d actor(s) cannot be targeted automatically and will be skipped."),
        *Asset->GetName(), Targets.Num(), Skipped.Num());
    if (!Confirm(FText::FromString(Prompt))) { return; }
    const FScopedTransaction Transaction(LOCTEXT("Inject", "Apply Strand to Selected Native Splines"));
    for (USplineComponent* Spline : Targets)
    {
        ApplyToExplicitComponent(Asset, Spline);
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
    if (!Skipped.IsEmpty())
    {
        Message(FString::Printf(TEXT("Strand updated %d spline(s). Skipped ambiguous or unsupported actors:\n\n%s"),
            Targets.Num(), *FString::Join(Skipped, TEXT("\n"))));
    }
}

void SplineEditorCommands::ApplySelectedAssetToActors()
{
    USplineDataAsset* Asset = SelectedSplineAsset();
    if (!Asset) { Message(TEXT("Select exactly one Spline Data Asset in the Content Browser.")); return; }
    ApplyAssetToSelectedActors(Asset);
}

void SplineEditorCommands::ApplySelectedAssetToLandscape()
{
    USplineDataAsset* Asset = SelectedSplineAsset();
    if (!Asset) { Message(TEXT("Select exactly one Spline Data Asset in the Content Browser.")); return; }
    if (!Confirm(LOCTEXT("LandscapeConfirm", "Reposition the Landscape spline control points relative to its first point? Existing terrain and spline mesh settings will be preserved.")))
    { return; }
    for (AActor* Actor : SelectedActors())
    {
        if (ULandscapeSplinesComponent* Splines = Actor->FindComponentByClass<ULandscapeSplinesComponent>())
        {
            FString Error;
            if (!SplineLandscapeAdapter::Apply(Asset, Splines, Error)) { Message(Error); }
        }
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
}


void SplineEditorCommands::ConvertSelectedLandscape()
{
    IAssetTools& Tools = FAssetToolsModule::GetModule().Get();
    for (AActor* Actor : SelectedActors())
    {
        ULandscapeSplinesComponent* Landscape = Actor->FindComponentByClass<ULandscapeSplinesComponent>();
        if (!Landscape) { continue; }
        if (Landscape->GetControlPoints().Num() < 2) { continue; }

        // Epic's native conversion is intended for connected paths. Do not silently
        // collapse branched road networks into an arbitrary single spline sequence.
        bool bBranched = false;
        for (const auto& Point : Landscape->GetControlPoints())
        {
            if (!Point || Point->ConnectedSegments.Num() > 2) { bBranched = true; break; }
        }
        if (bBranched)
        {
            Message(TEXT("Cannot export branching Landscape splines as one Spline Data Asset."));
            continue;
        }

        USplineComponent* TemporarySpline = NewObject<USplineComponent>(GetTransientPackage());
        Landscape->CopyToSplineComponent(TemporarySpline);
        if (TemporarySpline->GetNumberOfSplinePoints() < 2)
        {
            Message(TEXT("Landscape spline conversion produced fewer than two points."));
            continue;
        }
        USplineDataAssetFactory* Factory = NewObject<USplineDataAssetFactory>();
        USplineDataAsset* Asset = Cast<USplineDataAsset>(Tools.CreateAssetWithDialog(
            TEXT("SD_") + Actor->GetActorNameOrLabel(), TEXT("/Game"),
            USplineDataAsset::StaticClass(), Factory, FName(TEXT("Strand")), false));
        if (!Asset) { continue; }
        const FScopedTransaction Transaction(LOCTEXT("LandscapeExport", "Convert Landscape Spline to Data Asset"));
        Asset->Modify();
        Asset->SerializeFromSplineComponent(TemporarySpline);
        if (!Asset->Points.IsEmpty())
        {
            const FVector Offset = Asset->Points[0].LocalPosition;
            for (FSplinePointData& P : Asset->Points) { P.LocalPosition -= Offset; }
        }
        SaveAsset(Asset);
    }
}

void SplineEditorCommands::SetComponentPlacement(USplineComponent* Spline,
    EStrandCoordinateMode Mode, EStrandAnchorMode AnchorMode, int32 AnchorPointIndex)
{
    if (!IsValid(Spline)) { return; }
    UStrandSplineUserData* Placement = UStrandSplineUserData::Get(Spline);
    USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Spline);
    if (!Placement || !IsValid(Asset)) { return; }
    const int32 SafeIndex = FMath::Clamp(AnchorPointIndex, 0, FMath::Max(0, Asset->Points.Num() - 1));
    if (Placement->CoordinateMode == Mode && Placement->AnchorMode == AnchorMode &&
        Placement->AnchorPointIndex == SafeIndex) { return; }

    if (!Placement->IsInSync(Spline) && !ConfirmPlacementDiscard())
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("Placement", "Change Strand Spline Placement"));
    if (AActor* Owner = Spline->GetOwner()) { Owner->Modify(); }
    Spline->Modify();
    UStrandSplineUserData::EnsureComponentOwned(Spline);
    Placement = UStrandSplineUserData::Get(Spline);
    Placement->Modify();
    Placement->CoordinateMode = Mode;
    Placement->AnchorMode = AnchorMode;
    Placement->AnchorPointIndex = SafeIndex;
    ApplyLinked(Asset, Spline);
}

void SplineEditorCommands::ResetComponent(USplineComponent* Spline)
{
    USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Spline);
    if (!IsValid(Spline) || !IsValid(Asset)) { return; }
    if (MatchesPlacement(Asset, Spline)) { return; }
    if (!Confirm(LOCTEXT("ResetConfirm", "Are you sure you want to revert local modifications to match the Strand asset?")))
    { return; }
    const FScopedTransaction Transaction(LOCTEXT("Reset", "Apply Strand Asset to Native Spline"));
    if (AActor* Actor = Spline->GetOwner()) { Actor->Modify(); }
    ApplyLinked(Asset, Spline);
}

void SplineEditorCommands::UpdateComponentAsset(USplineComponent* Spline)
{
    USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Spline);
    if (!IsValid(Spline) || !IsValid(Asset)) { return; }
    if (MatchesPlacement(Asset, Spline))
    {
        Message(TEXT("Strand is already in sync. No asset update is necessary."));
        return;
    }
    int32 LoadedActors = 0;
    int32 LinkedComponents = 0;
    int32 OtherDifferingComponents = 0;
    if (GEditor)
    {
        if (UWorld* World = GEditor->GetEditorWorldContext().World())
        {
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                bool bActorLinked = false;
                TArray<USplineComponent*> Components;
                It->GetComponents<USplineComponent>(Components);
                for (USplineComponent* Other : Components)
                {
                    if (UStrandSplineUserData::GetAssignedAsset(Other) != Asset) { continue; }
                    bActorLinked = true;
                    ++LinkedComponents;
                    if (Other != Spline)
                    {
                        const UStrandSplineUserData* OtherData = UStrandSplineUserData::Get(Other);
                        if (!OtherData || !OtherData->IsInSync(Other)) { ++OtherDifferingComponents; }
                    }
                }
                if (bActorLinked) { ++LoadedActors; }
            }
        }
    }
    FString SourceWarning;
#if WITH_EDITORONLY_DATA
    if (!Asset->SourceImportFormat.IsEmpty())
    {
        SourceWarning = TEXT("\nThis asset was imported from a file; reimporting may overwrite your edits.\n");
    }
#endif
    const FString Prompt = FString::Printf(
        TEXT("Update shared Strand asset '%s' with this spline and save it?\n\n%d loaded actor(s) use %d linked spline component(s).\n%d OTHER linked component(s) currently differ from this asset.\n\nUpdating the asset does NOT update any linked level or Blueprint instances automatically. Instances that were previously in sync may show as Modified until you Apply Asset.%s\n\nUndo restores the in-memory asset state. To persist an undone change, save the asset again."),
        *Asset->GetName(), LoadedActors, LinkedComponents, OtherDifferingComponents, *SourceWarning);
    if (!Confirm(FText::FromString(Prompt))) { return; }
    const FScopedTransaction Transaction(LOCTEXT("Update", "Update Strand Asset from Native Spline"));
    Asset->Modify();
    const UStrandSplineUserData* Placement = UStrandSplineUserData::Get(Spline);
    if (Placement)
    {
        Asset->SerializeFromSplineComponentWithPlacement(Spline,
            Placement->CoordinateMode, Placement->AnchorMode, Placement->AnchorPointIndex);
    }
    else { Asset->SerializeFromSplineComponent(Spline); }
    SaveAsset(Asset);
}

void SplineEditorCommands::AssignAssetToComponent(TWeakObjectPtr<USplineComponent> WeakComponent,
                                                 TWeakObjectPtr<USplineDataAsset> WeakAsset)
{
    USplineComponent* Spline = WeakComponent.Get();
    if (!IsValid(Spline)) { return; }
    USplineDataAsset* Asset = WeakAsset.Get();
    const FScopedTransaction Transaction(LOCTEXT("Assign", "Assign Strand Asset to Native Spline"));
    UStrandSplineUserData::AssignAsset(Spline, Asset);
    if (IsValid(Asset) && !MatchesPlacement(Asset, Spline))
    {
        if (Confirm(LOCTEXT("AssignApply", "The assigned Strand asset differs from this spline. Apply its points now?\n\nChoosing No only links the asset; your local points are preserved.")))
        {
            ApplyLinked(Asset, Spline);
        }
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
}

void SplineEditorCommands::CreateNativeActorFromAsset(TWeakObjectPtr<USplineDataAsset> WeakAsset)
{
    FString Error;
    if (!StrandPlacement::CreateNativeSplineActor(WeakAsset.Get(), Error)) { Message(Error); }
}

void SplineEditorCommands::InspectAssetUsage(TWeakObjectPtr<USplineDataAsset> WeakAsset)
{
    const USplineDataAsset* Asset = WeakAsset.Get();
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!IsValid(Asset) || !IsValid(World))
    {
        Message(TEXT("Open a level and choose a valid Strand Spline asset."));
        return;
    }

    int32 ActorCount = 0;
    int32 SplineCount = 0;
    int32 MatchingCount = 0;
    int32 ModifiedCount = 0;
    TArray<FString> Details;
    // Treat each spline component independently. An actor can contain several
    // components linked to the same asset, some modified and some not.
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor)) { continue; }
        TArray<USplineComponent*> Components;
        Actor->GetComponents<USplineComponent>(Components);
        bool bActorUsesAsset = false;
        for (USplineComponent* Spline : Components)
        {
            if (!IsValid(Spline) || UStrandSplineUserData::GetAssignedAsset(Spline) != Asset)
            {
                continue;
            }
            bActorUsesAsset = true;
            ++SplineCount;
            const UStrandSplineUserData* Metadata = UStrandSplineUserData::Get(Spline);
            const bool bMatching = Metadata && Metadata->IsInSync(Spline);
            if (bMatching) { ++MatchingCount; } else { ++ModifiedCount; }
            Details.Add(FString::Printf(TEXT("%s / %s: %s (%d points)"),
                *Actor->GetActorNameOrLabel(), *Spline->GetName(),
                bMatching ? TEXT("In Sync") : TEXT("Different from asset"),
                Spline->GetNumberOfSplinePoints()));
        }
        if (bActorUsesAsset) { ++ActorCount; }
    }
    Details.Sort();
    const int32 DisplayLimit = 25;
    FString Report = FString::Printf(
        TEXT("Strand Usage: %s\n\nLoaded actors: %d\nLinked spline components: %d\nIn sync: %d\nModified locally: %d"),
        *Asset->GetName(), ActorCount, SplineCount, MatchingCount, ModifiedCount);
    if (SplineCount == 0)
    {
        Report += TEXT("\n\nNo loaded level instances use this asset.");
    }
    else
    {
        Report += TEXT("\n\nLinked components:\n");
        for (int32 Index = 0; Index < FMath::Min(Details.Num(), DisplayLimit); ++Index)
        {
            Report += TEXT("\n");
            Report += Details[Index];
        }
        if (Details.Num() > DisplayLimit)
        {
            Report += FString::Printf(TEXT("\n...and %d more."), Details.Num() - DisplayLimit);
        }
    }
    Report += TEXT("\n\nOnly loaded actors in the current editor world are checked. Unloaded World Partition actors and Blueprint class defaults are not included. This report changes nothing.");
    Message(Report);
}

void SplineEditorCommands::SelectActorsUsingAsset(TWeakObjectPtr<USplineDataAsset> WeakAsset)
{
    USplineDataAsset* Asset = WeakAsset.Get();
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!IsValid(Asset) || !IsValid(World)) { Message(TEXT("Open an editable level and choose a Strand asset.")); return; }
    TArray<AActor*> Users;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        TArray<USplineComponent*> Components;
        Actor->GetComponents<USplineComponent>(Components);
        for (USplineComponent* Spline : Components)
        {
            if (UStrandSplineUserData::GetAssignedAsset(Spline) == Asset)
            {
                Users.Add(Actor);
                break;
            }
        }
    }
    if (Users.IsEmpty())
    {
        Message(TEXT("No loaded actors in this editor world reference the Strand asset through native spline components. Unloaded World Partition actors and Blueprint defaults are not included."));
        return;
    }
    GEditor->SelectNone(false, true, false);
    for (AActor* User : Users) { GEditor->SelectActor(User, true, false, true); }
    GEditor->NoteSelectionChange();
    Message(FString::Printf(TEXT("Selected %d loaded actor(s) using '%s'.\n\nThis only checks loaded level instances, not unloaded actors or Blueprint defaults."),
        Users.Num(), *Asset->GetName()));
}

void SplineEditorCommands::ApplyAssetToComponent(TWeakObjectPtr<USplineDataAsset> WeakAsset,
                                                  TWeakObjectPtr<USplineComponent> WeakComponent)
{
    USplineDataAsset* Asset = WeakAsset.Get();
    USplineComponent* Spline = WeakComponent.Get();
    if (!IsValid(Asset) || !IsValid(Spline))
    {
        Message(TEXT("Choose one Strand asset and one valid native spline component."));
        return;
    }
    if (MatchesPlacement(Asset, Spline))
    {
        // Associate even if local geometry already matches.
        const FScopedTransaction Transaction(LOCTEXT("BindEqual", "Link Strand Spline Asset"));
        ApplyToExplicitComponent(Asset, Spline);
        return;
    }
    if (!Confirm(FText::FromString(FString::Printf(
        TEXT("Replace the local spline points on '%s' / '%s' with '%s'?\n\nActor and Component transforms stay unchanged."),
        Spline->GetOwner() ? *Spline->GetOwner()->GetActorNameOrLabel() : TEXT("Blueprint"),
        *Spline->GetName(), *Asset->GetName())))) { return; }
    const FScopedTransaction Transaction(LOCTEXT("ApplyExplicit", "Apply Strand to Native Spline"));
    ApplyToExplicitComponent(Asset, Spline);
    if (GEditor) { GEditor->RedrawAllViewports(); }
}

void SplineEditorCommands::CaptureComponent(TWeakObjectPtr<USplineComponent> WeakComponent)
{
    USplineComponent* Spline = WeakComponent.Get();
    AActor* Actor = IsValid(Spline) ? Spline->GetOwner() : nullptr;
    if (!IsValid(Spline)) { Message(TEXT("The native spline component is no longer valid.")); return; }
    USplineDataAssetFactory* Factory = NewObject<USplineDataAssetFactory>();
    USplineDataAsset* Asset = Cast<USplineDataAsset>(FAssetToolsModule::GetModule().Get().CreateAssetWithDialog(
        TEXT("SD_") + (Actor ? Actor->GetActorNameOrLabel() : TEXT("Blueprint")) + TEXT("_") + Spline->GetName(), TEXT("/Game"),
        USplineDataAsset::StaticClass(), Factory, FName(TEXT("Strand")), false));
    if (!Asset) { return; }
    const FScopedTransaction Transaction(LOCTEXT("CaptureExplicit", "Save Native Spline as Strand Asset"));
    if (Actor) { Actor->Modify(); }
    Asset->Modify();
    Asset->SerializeFromSplineComponent(Spline);
    Asset->DefaultCoordinateMode = EStrandCoordinateMode::Absolute;
    Asset->DefaultAnchorMode = EStrandAnchorMode::PointIndex;
    UStrandSplineUserData::AssignAsset(Spline, Asset);
    SaveAsset(Asset);
    if (Actor) { Actor->MarkPackageDirty(); }
    StrandSplineRefresh::NotifySplineChanged(Spline);
}

void SplineEditorCommands::AddNativeSplineAndApplyToSelected()
{
    USplineDataAsset* Asset = SelectedSplineAsset();
    if (!Asset) { Message(TEXT("Select exactly one Strand Spline in the Content Browser first.")); return; }
    TArray<AActor*> Eligible;
    for (AActor* Actor : SelectedActors())
    {
        TArray<USplineComponent*> Splines;
        Actor->GetComponents<USplineComponent>(Splines);
        if (Splines.IsEmpty()) { Eligible.Add(Actor); }
    }
    if (Eligible.IsEmpty())
    {
        Message(TEXT("Select a level actor without any Spline Component, then try again."));
        return;
    }
    if (!Confirm(FText::FromString(FString::Printf(TEXT("Add a native Spline Component to %d actor(s), using Strand asset '%s'?"),
        Eligible.Num(), *Asset->GetName())))) { return; }
    const FScopedTransaction Transaction(LOCTEXT("AddNative", "Add Native Spline Components from Strand"));
    int32 Applied = 0;
    for (AActor* Actor : Eligible)
    {
        if (USplineComponent* Spline = StrandPlacement::AddNativeSpline(Actor))
        {
            ApplyToExplicitComponent(Asset, Spline);
            ++Applied;
        }
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
    if (Applied != Eligible.Num())
    {
        Message(FString::Printf(TEXT("Created %d of %d requested native splines. Check the Output Log for any actor restrictions."),
            Applied, Eligible.Num()));
    }
}


#undef LOCTEXT_NAMESPACE
