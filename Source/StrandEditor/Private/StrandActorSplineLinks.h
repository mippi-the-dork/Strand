#pragma once
#include "ActorDetailsDelegates.h"

class IDetailLayoutBuilder;

/** Additive Strand tools on Actor Details; never replaces stock spline-component customization or visualizers. */
namespace StrandActorSplineLinks
{
    void ExtendActorDetails(IDetailLayoutBuilder& Builder, const FGetSelectedActors& SelectedActors);
}
