#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StrandSplineActor.generated.h"

class USplineComponent;
class UBillboardComponent;
struct FPropertyChangedEvent;

/** Placeable Strand actor using Unreal's native USplineComponent for every spline operation.
 *  The optional viewport icon is editor-only and is not gameplay geometry. */
UCLASS(Blueprintable, ClassGroup=Strand, meta=(DisplayName="Strand Spline Actor"))
class STRAND_API AStrandSplineActor : public AActor
{
    GENERATED_BODY()
public:
    AStrandSplineActor();

    /** This is a stock Unreal Spline Component, not a Strand-specific spline type. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Strand")
    TObjectPtr<USplineComponent> Spline;

#if WITH_EDITORONLY_DATA
    /** Editor-only origin marker. Uses the Strand Spline asset artwork, not the actor class icon. */
    // Internal visualization only. Do not expose a second component editor in Actor Details.
    // The user-facing controls below are the only supported billboard options.
    UPROPERTY(Transient)
    TObjectPtr<UBillboardComponent> StrandBillboard;

    /** Display the editor billboard even while the spline is not selected. */
    UPROPERTY(EditAnywhere, Category="Strand|Viewport", meta=(DisplayName="Show Strand Billboard"))
    bool bShowStrandBillboard = true;

    /** Screen-space sprite size multiplier. 1.0 matches Strand's default editor icon size.
     *  Does not change spline/actor scale or spline coordinates. */
    UPROPERTY(EditAnywhere, Category="Strand|Viewport", meta=(ClampMin="0.25", ClampMax="4.00", UIMin="0.25", UIMax="3.00", DisplayName="Billboard Size"))
    float StrandBillboardSize = 1.0f;
#endif

#if WITH_EDITOR
    virtual void PostLoad() override;
    virtual void PostActorCreated() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

    /** Rehydrate the texture after level/Blueprint load. The texture itself is never a Strand asset dependency. */
    void RefreshStrandBillboard();
#endif
};
