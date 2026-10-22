#include "StrandPlacement.h"
#include "StrandSplineActor.h"
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
    AActor* Actor = World->SpawnActor<AStrandSplineActor>(AStrandSplineActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (!IsValid(Actor))
    {
        OutError = TEXT("Unable to spawn an actor in the current editor level.");
        return nullptr;
    }
    Actor->Modify();
    Actor->SetActorLabel(FString::Printf(TEXT("Strand_%s"), *Asset->GetName()));
    USplineComponent* Spline = AddNativeSpline(Actor);
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

#undef LOCTEXT_NAMESPACE
