#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StrandSplineActor.generated.h"

class USplineComponent;

/** An ordinary placeable Actor whose only geometry is a native Unreal USplineComponent.
 *  Strand assets can be assigned later, or provided when dragging the asset into the viewport.
 *  No custom spline evaluation or rendering is introduced. */
UCLASS(Blueprintable, ClassGroup=Strand, meta=(DisplayName="Strand Spline Actor"))
class STRAND_API AStrandSplineActor : public AActor
{
    GENERATED_BODY()
public:
    AStrandSplineActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Strand")
    TObjectPtr<USplineComponent> Spline;
};
