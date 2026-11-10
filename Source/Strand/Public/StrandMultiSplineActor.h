#pragma once

#include "CoreMinimal.h"
#include "StrandSplineActor.h"
#include "StrandMultiSplineActor.generated.h"

/** A native spline container. All paths remain ordinary USplineComponents. */
UCLASS(Blueprintable, ClassGroup=Strand, meta=(DisplayName="Strand Multi Spline Actor"))
class STRAND_API AStrandMultiSplineActor : public AStrandSplineActor
{
    GENERATED_BODY()
public:
    /** Adds one additional native spline; does not replace the inherited spline root. */
    // Invoked from StrandEditor custom Details with a scoped Undo transaction.
    // Intentionally not CallInEditor: the native actor details button would
    // otherwise bypass the transaction owned by StrandEditor.
    void AddSplineComponent();
};
