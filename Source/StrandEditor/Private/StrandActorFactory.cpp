#include "StrandActorFactory.h"
#include "StrandPlacement.h"
#include "StrandSplineActor.h"
#include "StrandMultiSplineActor.h"
#include "StrandSplineRefresh.h"
#include "StrandSplineUserData.h"
#include "SplineDataAsset.h"
#include "AssetRegistry/AssetData.h"
#include "Components/SplineComponent.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "StrandActorFactory"

UStrandActorFactory::UStrandActorFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DisplayName = LOCTEXT("DisplayName", "Strand Spline Actor");
    NewActorClass = AStrandSplineActor::StaticClass();
    bUseSurfaceOrientation = false;
    bShowInEditorQuickMenu = true;
    MenuPriority = 1;
    // We add this factory to the editor's registry explicitly at PostEngineInit.
    bShouldAutoRegister = false;
}

bool UStrandActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
    if (!AssetData.IsValid() || AssetData.AssetClassPath == USplineDataAsset::StaticClass()->GetClassPathName())
    {
        return true;
    }
    OutErrorMsg = LOCTEXT("WrongAsset", "Select a Strand Spline asset.");
    return false;
}

bool UStrandActorFactory::CanPlaceElementsFromAssetData(const FAssetData& AssetData)
{
    FText Error;
    // Empty data is a valid native Strand actor placement through Quick Add.
    return CanCreateActorFrom(AssetData, Error);
}

UClass* UStrandActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
    // The editor may ask for factories that can create arbitrary AActors using empty
    // asset data. Decline those requests rather than claiming every generic actor.
    FText Error;
    return CanCreateActorFrom(AssetData, Error) ? AStrandSplineActor::StaticClass() : nullptr;
}

UObject* UStrandActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
    if (!IsValid(ActorInstance)) { return nullptr; }
    TArray<USplineComponent*> Components;
    ActorInstance->GetComponents<USplineComponent>(Components);
    USplineDataAsset* OnlyAsset = nullptr;
    for (USplineComponent* Component : Components)
    {
        USplineDataAsset* Asset = UStrandSplineUserData::GetAssignedAsset(Component);
        if (!Asset) { continue; }
        if (OnlyAsset && OnlyAsset != Asset) { return nullptr; }
        OnlyAsset = Asset;
    }
    return OnlyAsset;
}

void UStrandActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
    USplineDataAsset* Strand = Cast<USplineDataAsset>(Asset);
    if (!IsValid(NewActor)) { return; }
    // Quick Add produces the same actor class, but starts with an unlinked native spline.
    if (!IsValid(Strand)) { return; }
    // No custom spline representation: this is a plain actor and native spline component.
    TArray<USplineComponent*> Splines;
    NewActor->GetComponents<USplineComponent>(Splines);
    USplineComponent* Spline = Cast<USplineComponent>(NewActor->GetRootComponent());
    if (!Spline)
    {
        // Only for non-Strand actors that explicitly use this factory.
        Spline = Splines.IsEmpty() ? StrandPlacement::AddNativeSpline(NewActor) : Splines[0];
    }
    if (!Spline) { return; }
    Strand->ApplyToSplineComponentWithPlacement(Spline,
        Strand->DefaultCoordinateMode, Strand->DefaultAnchorMode, Strand->DefaultAnchorPointIndex);
    UStrandSplineUserData::AssignAsset(Spline, Strand);
    StrandSplineRefresh::NotifySplineChanged(Spline);
    // Editor viewport drag previews are also spawned through this callback.
    // Never retain, delete, group or dirty preview actors from the factory.
    // The editor finalizes placements and broadcasts OnNewActorsDropped later.
    if (!NewActor->HasAnyFlags(RF_Transient | RF_NewerVersionExists | RF_BeginDestroyed))
    {
        NewActor->MarkPackageDirty();
    }
}

FString UStrandActorFactory::GetDefaultActorLabel(UObject* Asset) const
{
    return IsValid(Asset) ? FString::Printf(TEXT("Strand_%s"), *Asset->GetName()) : TEXT("Strand Spline Actor");
}

UStrandMultiActorFactory::UStrandMultiActorFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DisplayName = LOCTEXT("MultiDisplay", "Strand Multi Spline Actor");
    NewActorClass = AStrandMultiSplineActor::StaticClass();
    bUseSurfaceOrientation = false;
    bShowInEditorQuickMenu = true;
    MenuPriority = 2;
    bShouldAutoRegister = false;
}

bool UStrandMultiActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutError)
{
    if (!AssetData.IsValid()) { return true; }
    OutError = LOCTEXT("MultiNoAsset", "Use a Strand Spline asset for individual placement or the grouped drag option.");
    return false;
}

bool UStrandMultiActorFactory::CanPlaceElementsFromAssetData(const FAssetData& AssetData)
{
    return !AssetData.IsValid();
}

UClass* UStrandMultiActorFactory::GetDefaultActorClass(const FAssetData& AssetData)
{
    return !AssetData.IsValid() ? AStrandMultiSplineActor::StaticClass() : nullptr;
}

#undef LOCTEXT_NAMESPACE
