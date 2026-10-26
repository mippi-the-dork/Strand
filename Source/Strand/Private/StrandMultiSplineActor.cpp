#include "StrandMultiSplineActor.h"
#include "Components/SplineComponent.h"
#include "Components/SceneComponent.h"

void AStrandMultiSplineActor::AddSplineComponent()
{
#if WITH_EDITOR
    // The editor Details action owns FScopedTransaction (StrandEditor).
    // Runtime classes must never link directly against UnrealEd.
    Modify();
#endif
    USplineComponent* Component = NewObject<USplineComponent>(this, USplineComponent::StaticClass(), NAME_None, RF_Transactional);
    if (!Component) { return; }
    Component->SetupAttachment(GetRootComponent());
    AddInstanceComponent(Component);
    Component->OnComponentCreated();
    if (GetWorld() && GetWorld()->IsGameWorld()) { Component->RegisterComponent(); }
#if WITH_EDITOR
    else if (GetWorld()) { Component->RegisterComponent(); }
    MarkPackageDirty();
#endif
}
