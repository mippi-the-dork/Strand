#pragma once

class USplineComponent;

namespace StrandSplineRefresh
{
    /** Rebuild native spline visuals and refresh Blueprint preview instances without compiling a Blueprint. */
    void NotifySplineChanged(USplineComponent* Component);
}
