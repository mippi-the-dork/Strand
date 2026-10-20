#pragma once

#include "IDetailCustomization.h"

class USplineAssetTrackerComponent;

class FSplineAssetTrackerCustomization : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& Builder) override;
private:
    TWeakObjectPtr<USplineAssetTrackerComponent> Tracker;
};
