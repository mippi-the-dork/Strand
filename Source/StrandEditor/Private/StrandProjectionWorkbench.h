#pragma once

#include "CoreMinimal.h"

class USplineComponent;
class SWindow;
struct FStrandProjectionOptions;

/** Opens the level-instance projection workbench from the Strand asset editor.
 * Does not edit the source asset or the component until the shared ghost preview's Apply action. */
namespace StrandProjectionWorkbench
{
    /** Central mode predicates used by disabled workbench controls and preflight checks. */
    bool UsesWorldDirection(const FStrandProjectionOptions& Options);
    bool UsesContinuousSettings(const FStrandProjectionOptions& Options);

    /** Validate option combinations before allocating a preview or making collision queries. */
    bool ValidateOptions(const FStrandProjectionOptions& Options, int32 AnchorCount, FString& OutReason);
    TSharedPtr<SWindow> Open(USplineComponent* Source, const FStrandProjectionOptions& InitialOptions,
        TFunction<void(USplineComponent*, const TArray<int32>&, bool)> OnApplied = {}, TFunction<void()> OnClosed = {});
}
