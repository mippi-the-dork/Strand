#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StrandProjectSettings.generated.h"

/** Which point of an SVG's source viewBox maps to local (0,0) on import. */
UENUM(BlueprintType)
enum class EStrandSvgCanvasOrigin : uint8
{
    TopLeft UMETA(DisplayName="Top Left"),
    TopRight UMETA(DisplayName="Top Right"),
    BottomLeft UMETA(DisplayName="Bottom Left"),
    BottomRight UMETA(DisplayName="Bottom Right"),
    Center UMETA(DisplayName="Center")
};

/** Project Settings > Plugins > Strand. Changing these settings affects future imports/reimports only. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Strand"))
class STRAND_API UStrandProjectSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Config, Category="SVG Import", meta=(DisplayName="SVG Canvas Origin"))
    EStrandSvgCanvasOrigin SvgCanvasOrigin = EStrandSvgCanvasOrigin::TopLeft;

    /** Default centimeters per SVG user unit for new SVG imports. Each asset stores its own scale for reimport. */
    UPROPERTY(EditAnywhere, Config, Category="SVG Import", meta=(DisplayName="Default SVG Import Scale", ClampMin="0.000001", UIMin="0.01", UIMax="100"))
    float DefaultSvgImportScale = 1.0f;

    virtual FName GetContainerName() const override { return TEXT("Project"); }
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
    virtual FName GetSectionName() const override { return TEXT("Strand"); }
};
