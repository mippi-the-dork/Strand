#pragma once

#include "IDetailCustomization.h"

/** Strand's 2-point, non-destructive adapter for Unreal's native USplineMeshComponent. */
class FStrandSplineMeshCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
};
