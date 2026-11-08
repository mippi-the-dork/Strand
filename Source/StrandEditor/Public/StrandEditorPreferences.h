// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StrandEditorPreferences.generated.h"

/** Per-user Strand viewport controls. Registered with the editor Settings module. */
UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="Strand Editor"))
class STRANDEDITOR_API UStrandEditorPreferences : public UObject
{
    GENERATED_BODY()
public:
    UStrandEditorPreferences()
    {
        OrientationSpacingPresets = {25.0, 50.0, 100.0, 200.0, 500.0};
        OrientationSizePresets = {15.0, 35.0, 70.0, 140.0};
    }

    /** Any positive finite spacing, in centimeters, may appear in the viewport presets. */
    UPROPERTY(EditAnywhere, Config, Category="Orientation Visualization", meta=(DisplayName="Sample Spacing Presets (cm)"))
    TArray<double> OrientationSpacingPresets;

    /** Any positive finite marker size in centimeters. */
    UPROPERTY(EditAnywhere, Config, Category="Orientation Visualization", meta=(DisplayName="Marker Size Presets (cm)"))
    TArray<double> OrientationSizePresets;

    UPROPERTY(EditAnywhere, Config, Category="Orientation Visualization", meta=(ClampMin="0.01", UIMin="1.0", DisplayName="Last Sample Spacing (cm)"))
    double OrientationSpacing = 100.0;

    UPROPERTY(EditAnywhere, Config, Category="Orientation Visualization", meta=(ClampMin="0.01", UIMin="1.0", DisplayName="Last Marker Size (cm)"))
    double OrientationSize = 35.0;
};
