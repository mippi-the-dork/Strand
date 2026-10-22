#include "StrandSplineActor.h"
#include "Components/SplineComponent.h"

AStrandSplineActor::AStrandSplineActor()
{
    PrimaryActorTick.bCanEverTick = false;
    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
    SetRootComponent(Spline);
}
