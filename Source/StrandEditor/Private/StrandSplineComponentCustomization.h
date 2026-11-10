#pragma once

#include "IDetailCustomization.h"

class USplineComponent;

/** Adds Strand actions to native Spline Component Details without creating a tracker component. */
class FStrandSplineComponentCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
private:
    TWeakObjectPtr<USplineComponent> Spline;
};
