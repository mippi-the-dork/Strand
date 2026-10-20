#include "SplineEditorCommands.h"
#include "SplineDataAsset.h"
#include "SplineAssetTrackerComponent.h"
#include "SplineDataAssetFactory.h"
#include "SplineLandscapeAdapter.h"
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
    USplineAssetTrackerComponent* FindOrCreateTracker(AActor* Actor)
    {
        if (!IsValid(Actor)) { return nullptr; }
        if (USplineAssetTrackerComponent* Found = Actor->FindComponentByClass<USplineAssetTrackerComponent>())
        {
            return Found;
        }
        Actor->Modify();
        USplineAssetTrackerComponent* Tracker = NewObject<USplineAssetTrackerComponent>(Actor,
            USplineAssetTrackerComponent::StaticClass(), NAME_None, RF_Transactional);
        Actor->AddInstanceComponent(Tracker);
        Tracker->OnComponentCreated();
        Tracker->RegisterComponent();
        return Tracker;
    }
}

void SplineEditorCommands::ImportCurves(bool bAlembic, bool bLegacyFbx)
{
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop) { Message(TEXT("Desktop file dialogs are unavailable.")); return; }
    TArray<FString> Filenames;
    int32 Filter = 0;
    const bool bChosen = Desktop->OpenFileDialog(
        FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        bLegacyFbx ? TEXT("Import Legacy FBX Curves into Strand") : (bAlembic ? TEXT("Import Alembic Curves into Strand") : TEXT("Import SVG Curves into Strand")),
        FPaths::ProjectContentDir(), TEXT(""),
        bLegacyFbx ? TEXT("Legacy FBX Curves (*.fbx)|*.fbx") : (bAlembic ? TEXT("Alembic Curves (*.abc)|*.abc") : TEXT("SVG Vectors (*.svg)|*.svg")),
        EFileDialogFlags::Multiple, Filenames, Filter);
    if (!bChosen || Filenames.IsEmpty()) { return; }

    IContentBrowserSingleton& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(
        "ContentBrowser").Get();
    TArray<FString> Folders;
    Browser.GetSelectedFolders(Folders);
    const FString Destination = Folders.Num() == 1 && Folders[0].StartsWith(TEXT("/Game"))
        ? Folders[0] : TEXT("/Game");
    UFactory* Factory = bLegacyFbx ? static_cast<UFactory*>(NewObject<USplineFbxFactory>()) :
        (bAlembic ? static_cast<UFactory*>(NewObject<UStrandAlembicFactory>())
                  : static_cast<UFactory*>(NewObject<USplineSvgFactory>()));
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
    if (USplineAssetTrackerComponent* Tracker = Actor->FindComponentByClass<USplineAssetTrackerComponent>())
    {
        if (IsValid(Tracker->TrackedSpline) && Tracker->TrackedSpline->GetOwner() == Actor)
        {
            return Tracker->TrackedSpline;
        }
    }
    return Components.Num() == 1 ? Components[0] : nullptr;
}

void SplineEditorCommands::CompareTracker(USplineAssetTrackerComponent* Tracker)
{
    if (!IsValid(Tracker) || !IsValid(Tracker->AssignedSplineAsset) || !IsValid(Tracker->ResolveSpline())) { return; }
    const USplineDataAsset* Asset = Tracker->AssignedSplineAsset;
    const USplineComponent* Spline = Tracker->ResolveSpline();
    const int32 Local = Spline->GetNumberOfSplinePoints();
    const int32 Saved = Asset->Points.Num();
    int32 DifferentPoints = 0;
    double LargestPosDelta = 0;
    int32 FirstDifferentIndex = INDEX_NONE;
    for (int32 I = 0; I < FMath::Min(Local, Saved); ++I)
    {
        const FSplinePoint Current = Spline->GetSplinePointAt(I, ESplineCoordinateSpace::Local);
        const FSplinePointData& Reference = Asset->Points[I];
        const bool bDifferent = !Current.Position.Equals(Reference.LocalPosition, 0.01) ||
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
            FVector::Distance(Current.Position, Reference.LocalPosition));
    }
    const bool bLoopMismatch = Spline->IsClosedLoop() != Asset->bIsClosedLoop;
    Message(FString::Printf(TEXT("Strand comparison\n\nAsset points: %d\nLocal points: %d\nDiffering matched points: %d\nFirst different point index: %s\nLargest position delta: %.3f cm\nClosed loop differs: %s\n\nComparison only. No data was changed."),
        Saved, Local, DifferentPoints,
        FirstDifferentIndex == INDEX_NONE ? TEXT("None") : *FString::FromInt(FirstDifferentIndex),
        LargestPosDelta, bLoopMismatch ? TEXT("Yes") : TEXT("No")));
}

void SplineEditorCommands::BrowseTrackerAsset(USplineAssetTrackerComponent* Tracker)
{
    if (!IsValid(Tracker) || !IsValid(Tracker->AssignedSplineAsset) || !GEditor) { return; }
    TArray<UObject*> Assets { Tracker->AssignedSplineAsset };
    GEditor->SyncBrowserToObjects(Assets);
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
            Message(FString::Printf(TEXT("%s must have exactly one Spline Component. For actors with multiple splines, attach a Spline Asset Tracker and select its Tracked Spline explicitly."), *Actor->GetName()));
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
        USplineAssetTrackerComponent* Tracker = FindOrCreateTracker(Actor);
        if (Tracker)
        {
            Tracker->Modify();
            Tracker->AssignedSplineAsset = Asset;
            Tracker->TrackedSpline = Spline;
            Tracker->MarkPackageDirty();
        }
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
    if (Actors.IsEmpty()) { Message(TEXT("Select a level Actor containing one Spline Component first.")); return; }
    if (!Confirm(LOCTEXT("InjectConfirm", "Replace the selected actors' local spline points with the Data Asset?")))
    { return; }
    const FScopedTransaction Transaction(LOCTEXT("Inject", "Apply Spline Data Asset"));
    for (AActor* Actor : Actors)
    {
        USplineComponent* Spline = FindUnambiguousSpline(Actor);
        if (!Spline) { continue; }
        Actor->Modify();
        Asset->ApplyToSplineComponent(Spline);
        if (USplineAssetTrackerComponent* Tracker = FindOrCreateTracker(Actor))
        {
            Tracker->Modify();
            Tracker->AssignedSplineAsset = Asset;
            Tracker->TrackedSpline = Spline;
        }
        Actor->MarkPackageDirty();
    }
    if (GEditor) { GEditor->RedrawAllViewports(); }
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

void SplineEditorCommands::ResetTracker(USplineAssetTrackerComponent* Tracker)
{
    if (!IsValid(Tracker) || !IsValid(Tracker->AssignedSplineAsset) || !Tracker->ResolveSpline()) { return; }
    if (!Confirm(LOCTEXT("ResetConfirm", "Are you sure you want to revert local modifications to match the Data Asset?")))
    { return; }
    const FScopedTransaction Transaction(LOCTEXT("Reset", "Reset Spline to Spline Data Asset"));
    if (AActor* Actor = Tracker->GetOwner()) { Actor->Modify(); }
    Tracker->AssignedSplineAsset->ApplyToSplineComponent(Tracker->ResolveSpline());
    if (GEditor) { GEditor->RedrawAllViewports(); }
}

void SplineEditorCommands::UpdateTracker(USplineAssetTrackerComponent* Tracker)
{
    if (!IsValid(Tracker) || !IsValid(Tracker->AssignedSplineAsset) || !Tracker->ResolveSpline()) { return; }
    if (!Confirm(LOCTEXT("UpdateConfirm", "This will permanently overwrite the saved asset. Proceed?")))
    { return; }
    USplineDataAsset* Asset = Tracker->AssignedSplineAsset;
    const FScopedTransaction Transaction(LOCTEXT("Update", "Update Spline Data Asset from Level"));
    Asset->Modify();
    Asset->SerializeFromSplineComponent(Tracker->ResolveSpline());
    SaveAsset(Asset);
}

#undef LOCTEXT_NAMESPACE
