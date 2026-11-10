#pragma once

#include "CoreMinimal.h"

// Pure color mapping shared by viewport drawing and automation coverage.
// A zero-weight anchor is white; full influence is vivid magenta.
namespace StrandSoftSelectionVisuals
{
    inline FLinearColor GetAnchorColor(double InWeight)
    {
        const float Weight = FMath::IsFinite(InWeight)
            ? static_cast<float>(FMath::Clamp(InWeight, 0.0, 1.0)) : 0.0f;
        return FMath::Lerp(FLinearColor::White, FLinearColor(1.0f, 0.0f, 1.0f), Weight);
    }

    // Centralize selection precedence so selected anchors don't accidentally
    // bypass the falloff gradient in any of the viewport drawing paths.
    inline FLinearColor GetDisplayedAnchorColor(bool bSoftSelectionActive, bool bSelected,
        double Weight, const FLinearColor& NormalColor, const FLinearColor& SelectedColor)
    {
        if (bSoftSelectionActive)
        {
            return bSelected ? GetAnchorColor(1.0) : GetAnchorColor(Weight);
        }
        return bSelected ? SelectedColor : NormalColor;
    }
}
