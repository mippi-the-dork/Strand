#include "StrandPlacement.h"
#include "StrandSplineActor.h"
#include "StrandMultiSplineActor.h"
#include "StrandSplineUserData.h"
#include "StrandSplineRefresh.h"
#include "SplineDataAsset.h"
#include "Components/SplineComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Subsystems/UnrealEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "StrandPlacement"

USplineComponent* StrandPlacement::AddNativeSpline(AActor* Actor)
{
    if (!IsValid(Actor)) { return nullptr; }
    Actor->Modify();
    USplineComponent* Spline = NewObject<USplineComponent>(Actor, USplineComponent::StaticClass(), NAME_None, RF_Transactional);
    if (!Spline) { return nullptr; }
    if (USceneComponent* Root = Actor->GetRootComponent())
    {
        Spline->SetupAttachment(Root);
    }
    else
    {
        Actor->SetRootComponent(Spline);
    }
    Actor->AddInstanceComponent(Spline);
    Spline->OnComponentCreated();
    Spline->RegisterComponent();
    Actor->MarkPackageDirty();
    return Spline;
}

AActor* StrandPlacement::CreateNativeSplineActor(USplineDataAsset* Asset, FString& OutError)
{
    if (!IsValid(Asset))
    {
        OutError = TEXT("Choose a valid Strand Spline asset.");
        return nullptr;
    }
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!IsValid(World) || !World->GetCurrentLevel() || World->WorldType != EWorldType::Editor)
    {
        OutError = TEXT("Open an editable Level Editor world before creating a spline actor.");
        return nullptr;
    }
    const FScopedTransaction Transaction(LOCTEXT("CreateNative", "Create Native Spline Actor from Strand"));
    World->GetCurrentLevel()->Modify();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags |= RF_Transactional;
    Params.OverrideLevel = World->GetCurrentLevel();
    // Unlike a viewport drop (whose factory already receives the drop location),
    // the Content Browser context menu has no mouse hit location. Prefer an
    // immediately visible point in front of the active Level viewport camera.
    FVector SpawnLocation = FVector::ZeroVector;
    if (GEditor)
    {
        if (UUnrealEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>())
        {
            FVector ViewLocation;
            FRotator ViewRotation;
            if (EditorSubsystem->GetLevelViewportCameraInfo(ViewLocation, ViewRotation))
            {
                SpawnLocation = ViewLocation + ViewRotation.Vector() * 500.0;
            }
        }
    }
    AActor* Actor = World->SpawnActor<AStrandSplineActor>(AStrandSplineActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator, Params);
    if (!IsValid(Actor))
    {
        OutError = TEXT("Unable to spawn an actor in the current editor level.");
        return nullptr;
    }
    Actor->Modify();
    Actor->SetActorLabel(FString::Printf(TEXT("Strand_%s"), *Asset->GetName()));
    // AStrandSplineActor already owns its native spline as its root. Reuse it;
    // adding another component silently made Command-created actors differ from
    // Quick Add and drag/drop actors.
    USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>();
    if (!Spline)
    {
        World->DestroyActor(Actor);
        OutError = TEXT("The native Spline Component could not be created.");
        return nullptr;
    }
    Asset->ApplyToSplineComponentWithPlacement(Spline,
        Asset->DefaultCoordinateMode, Asset->DefaultAnchorMode, Asset->DefaultAnchorPointIndex);
    UStrandSplineUserData::AssignAsset(Spline, Asset);
    StrandSplineRefresh::NotifySplineChanged(Spline);
    Actor->MarkPackageDirty();
    if (GEditor)
    {
        GEditor->SelectNone(false, true, false);
        GEditor->SelectActor(Actor, true, true, true);
        GEditor->RedrawAllViewports();
    }
    return Actor;
}

AActor* StrandPlacement::CreateMultiSplineActor(const TArray<USplineDataAsset*>& Assets, FString& OutError,
                                                const FTransform* RequestedTransform)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!IsValid(World) || World->WorldType != EWorldType::Editor || !World->GetCurrentLevel())
    {
        OutError = TEXT("Open an editable level first.");
        return nullptr;
    }
    for (const USplineDataAsset* Asset : Assets)
    {
        if (!IsValid(Asset)) { OutError = TEXT("A selected Strand asset was invalid."); return nullptr; }
    }
    const FScopedTransaction Transaction(LOCTEXT("CreateMulti", "Create Strand Multi Spline Actor"));
    World->GetCurrentLevel()->Modify();
    FTransform Transform = RequestedTransform ? *RequestedTransform : FTransform::Identity;
    if (!RequestedTransform && GEditor)
    {
        if (UUnrealEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>())
        {
            FVector CameraLocation; FRotator CameraRotation;
            if (Subsystem->GetLevelViewportCameraInfo(CameraLocation, CameraRotation))
            {
                Transform.SetLocation(CameraLocation + CameraRotation.Vector() * 500.0);
            }
        }
    }
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags |= RF_Transactional;
    Params.OverrideLevel = World->GetCurrentLevel();
    AStrandMultiSplineActor* Actor = World->SpawnActor<AStrandMultiSplineActor>(
        AStrandMultiSplineActor::StaticClass(), Transform, Params);
    if (!IsValid(Actor)) { OutError = TEXT("Could not create Strand Multi Spline Actor."); return nullptr; }
    Actor->Modify();
    Actor->SetActorLabel(TEXT("Strand Multi Spline Actor"));
    USplineComponent* RootSpline = Actor->Spline;
    for (int32 I = 0; I < Assets.Num(); ++I)
    {
        USplineComponent* Spline = I == 0 ? RootSpline : StrandPlacement::AddNativeSpline(Actor);
        if (!IsValid(Spline)) { OutError = TEXT("Could not create all spline components."); World->DestroyActor(Actor); return nullptr; }
        Spline->Modify();
        USplineDataAsset* Asset = Assets[I];
        Asset->ApplyToSplineComponentWithPlacement(Spline, Asset->DefaultCoordinateMode,
            Asset->DefaultAnchorMode, Asset->DefaultAnchorPointIndex);
        UStrandSplineUserData::AssignAsset(Spline, Asset);
        StrandSplineRefresh::NotifySplineChanged(Spline);
    }
    Actor->MarkPackageDirty();
    GEditor->SelectNone(false, true, false);
    GEditor->SelectActor(Actor, true, true, true);
    GEditor->RedrawAllViewports();
    return Actor;
}

#undef LOCTEXT_NAMESPACE
