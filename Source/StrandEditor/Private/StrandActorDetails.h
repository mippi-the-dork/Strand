#pragma once
#include "IDetailCustomization.h"

/** Native actor-level summary and batch editing for any number of Strand actors. */
class FStrandActorDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
};
