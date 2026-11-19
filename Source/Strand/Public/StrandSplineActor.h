#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StrandSplineActor.generated.h"

class USplineComponent;
class UBillboardComponent;
struct FPropertyChangedEvent;

/** Which scene geometry is eligible for a Strand projection trace. */
UENUM(BlueprintType)
enum class EStrandProjectionTargetScope : uint8
{
    SingleActor UMETA(DisplayName="Single Actor"),
    ActorSet UMETA(DisplayName="Actor Set"),
    EntireLevel UMETA(DisplayName="All Scene Collision")
};

/** Ray construction from the level viewport or a user-authored world vector. */
UENUM(BlueprintType)
enum class EStrandProjectionRayMode : uint8
{
    WorldDirection UMETA(DisplayName="World Direction"),
    CameraDirection UMETA(DisplayName="Camera Direction (Parallel)"),
    CameraProjection UMETA(DisplayName="Camera Projection (Perspective)")
};

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

    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(DisplayName="Target Scope"))
    EStrandProjectionTargetScope StrandProjectionTargetScope = EStrandProjectionTargetScope::SingleActor;

    /** Only used when target scope is Actor Set. Other scene actors are excluded. */
    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(DisplayName="Surface Targets", EditCondition="StrandProjectionTargetScope == EStrandProjectionTargetScope::ActorSet", EditConditionHides))
    TArray<TObjectPtr<AActor>> StrandProjectionTargets;

    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(DisplayName="Ray Mode"))
    EStrandProjectionRayMode StrandProjectionRayMode = EStrandProjectionRayMode::WorldDirection;

    /** Explicit scene actor used by the level-only surface projection command.
     *  Never used to project curves in the standalone asset preview scene. */
    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(DisplayName="Surface Target", EditCondition="StrandProjectionTargetScope == EStrandProjectionTargetScope::SingleActor", EditConditionHides))
    TObjectPtr<AActor> StrandProjectionTarget = nullptr;

    /** World-space projection direction; defaults to downward world Z. */
    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(DisplayName="Projection Direction", EditCondition="StrandProjectionRayMode == EStrandProjectionRayMode::WorldDirection", EditConditionHides))
    FVector StrandProjectionDirection = FVector(0, 0, -1);

    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(ClampMin="1.0", UIMin="10", UIMax="100000", DisplayName="Max Trace Distance (cm)"))
    float StrandProjectionMaxDistance = 10000.0f;

    /** Signed offset along the collision hit normal. */
    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(UIMin="-100", UIMax="100", DisplayName="Surface Clearance (cm)"))
    float StrandProjectionClearance = 0.0f;

    /** 0 keeps the existing point, 1 reaches the hit surface plus clearance. */
    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0", DisplayName="Projection Strength"))
    float StrandProjectionStrength = 1.0f;

    /** Inclusive spline point range; -1 for End means the last anchor. */
    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(ClampMin="0", DisplayName="First Anchor Index"))
    int32 StrandProjectionFirstAnchor = 0;

    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(ClampMin="-1", DisplayName="Last Anchor Index (-1 = All)"))
    int32 StrandProjectionLastAnchor = -1;

    /** Reorient the spline point's up vector to the collision normal, without changing its tangents. */
    UPROPERTY(EditInstanceOnly, Category="Strand|Surface Projection", meta=(DisplayName="Align Up Vectors to Surface Normals"))
    bool bStrandProjectionAlignNormals = false;

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
