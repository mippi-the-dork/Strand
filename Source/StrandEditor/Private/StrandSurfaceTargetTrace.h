#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// Scene-facing trace helpers. A collision query against the level is preferable
// to calling LineTraceComponent on a collection of arbitrary primitive types.
namespace StrandSurfaceTargetTrace
{
    inline bool RayIntersectsBounds(const FBox& Bounds, const FVector& Start, const FVector& End)
    {
        return Bounds.IsValid && FMath::LineBoxIntersection(Bounds, Start, End, End - Start);
    }

    inline bool FindTargetHit(UWorld* World, const AActor* SourceOwner, const AActor* Target,
        const FVector& Start, const FVector& End, FHitResult& OutHit)
    {
        if (!IsValid(World) || !IsValid(Target)) { return false; }

        FCollisionQueryParams Params(SCENE_QUERY_STAT(StrandSurfaceProjection), true);
        if (IsValid(SourceOwner)) { Params.AddIgnoredActor(SourceOwner); }
        // Query all object types, rather than depending on Visibility/Camera
        // channel responses. This respects trace/query collision enablement.
        TArray<FHitResult> Hits;
        const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
        World->LineTraceMultiByObjectType(Hits, Start, End, Objects, Params);
        bool bFound = false;
        double BestDistance = TNumericLimits<double>::Max();
        for (const FHitResult& Hit : Hits)
        {
            const UPrimitiveComponent* HitComponent = Hit.GetComponent();
            if (!HitComponent || HitComponent->GetOwner() != Target ||
                Hit.ImpactNormal.IsNearlyZero()) { continue; }
            if (Hit.Distance < BestDistance)
            {
                OutHit = Hit;
                BestDistance = Hit.Distance;
                bFound = true;
            }
        }
        // Some primitive implementations expose collision traces through their
        // own component path even if another blocking actor stops the world ray.
        // Keep that as a targeted fallback instead of treating any actor as a hit.
        TArray<UPrimitiveComponent*> Components;
        Target->GetComponents<UPrimitiveComponent>(Components);
        for (UPrimitiveComponent* Component : Components)
        {
            if (!IsValid(Component) || !Component->IsRegistered() ||
                !Component->K2_IsQueryCollisionEnabled()) { continue; }
            FHitResult Hit;
            if (Component->LineTraceComponent(Hit, Start, End, Params) &&
                Hit.bBlockingHit && !Hit.ImpactNormal.IsNearlyZero() &&
                Hit.Distance < BestDistance)
            {
                OutHit = Hit;
                BestDistance = Hit.Distance;
                bFound = true;
            }
        }
        return bFound;
    }
    // A single result contract for single, multi and all-scene query modes.
    // Unlike a Visibility trace, this tests query-enabled object collision.
    // The scene's closest eligible collision is selected deterministically.
    inline bool FindSceneHit(UWorld* World, const AActor* SourceOwner,
        const TArray<AActor*>& AllowedActors, bool bAllScene,
        const FVector& Start, const FVector& End, FHitResult& OutHit)
    {
        if (!IsValid(World)) { return false; }
        FCollisionQueryParams Params(SCENE_QUERY_STAT(StrandProjectionScene), true);
        if (IsValid(SourceOwner)) { Params.AddIgnoredActor(SourceOwner); }
        TArray<FHitResult> Hits;
        World->LineTraceMultiByObjectType(Hits, Start, End,
            FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), Params);
        bool bFound = false;
        double Best = TNumericLimits<double>::Max();
        for (const FHitResult& Hit : Hits)
        {
            const UPrimitiveComponent* Component = Hit.GetComponent();
            const AActor* Owner = Component ? Component->GetOwner() : nullptr;
            if (!IsValid(Owner) || Owner == SourceOwner ||
                (!bAllScene && !AllowedActors.Contains(const_cast<AActor*>(Owner))) ||
                !Hit.bBlockingHit || Hit.ImpactNormal.IsNearlyZero()) { continue; }
            if (Hit.Distance < Best)
            {
                Best = Hit.Distance;
                OutHit = Hit;
                bFound = true;
            }
        }
        // Keep the targeted component fallback for explicit actor scopes. The
        // fallback also handles a selected object behind other blocking objects.
        if (!bAllScene)
        {
            for (AActor* Actor : AllowedActors)
            {
                FHitResult Candidate;
                if (IsValid(Actor) && FindTargetHit(World, SourceOwner, Actor, Start, End, Candidate) &&
                    Candidate.Distance < Best)
                {
                    Best = Candidate.Distance;
                    OutHit = Candidate;
                    bFound = true;
                }
            }
        }
        return bFound;
    }


}
