#include "StrandSplineActor.h"
#include "Components/SplineComponent.h"

#if WITH_EDITORONLY_DATA
#include "Components/BillboardComponent.h"
#endif

#if WITH_EDITOR
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
    // BillboardComponent requires a UTexture2D; the plugin's original SVG remains the
    // Content Browser artwork. The PNG in Resources is a lossless raster of THAT SVG.
    // Load on demand and keep the transient texture rooted while the editor is running.
    UTexture2D* GetStrandBillboardTexture()
    {
        static TStrongObjectPtr<UTexture2D> CachedTexture;
        if (CachedTexture.IsValid())
        {
            return CachedTexture.Get();
        }

        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
        if (!Plugin.IsValid())
        {
            return nullptr;
        }
        const FString PngPath = FPaths::Combine(Plugin->GetBaseDir(),
            TEXT("Resources"), TEXT("StrandSplineAsset_Billboard.png"));
        if (!FPaths::FileExists(PngPath))
        {
            return nullptr;
        }
        UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(PngPath);
        if (IsValid(Texture))
        {
            Texture->SetFlags(RF_Transient);
            CachedTexture.Reset(Texture);
        }
        return CachedTexture.Get();
    }
}
#endif

AStrandSplineActor::AStrandSplineActor()
{
    PrimaryActorTick.bCanEverTick = false;
    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
    SetRootComponent(Spline);

#if WITH_EDITORONLY_DATA
    StrandBillboard = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("StrandBillboard"));
    if (StrandBillboard)
    {
        StrandBillboard->SetupAttachment(Spline);
        StrandBillboard->SetIsVisualizationComponent(true);
        StrandBillboard->SetHiddenInGame(true);
        StrandBillboard->bUseInEditorScaling = true;
        // Stock editor sprites (Player Start, lights, volumes) stay legible as
        // the editor camera moves.  ScreenSize scales the sprite in the view,
        // NOT the spline's world geometry or component transform.
        StrandBillboard->bIsScreenSizeScaled = true;
        StrandBillboard->ScreenSize = 0.006f * StrandBillboardSize;
        StrandBillboard->SetRelativeScale3D(FVector::OneVector);
        StrandBillboard->bEditableWhenInherited = false;
        StrandBillboard->SetVisibility(bShowStrandBillboard);
    }
#endif
}

#if WITH_EDITOR
void AStrandSplineActor::RefreshStrandBillboard()
{
#if WITH_EDITORONLY_DATA
    if (!StrandBillboard || HasAnyFlags(RF_ClassDefaultObject))
    {
        return;
    }
    if (UTexture2D* Sprite = GetStrandBillboardTexture())
    {
        if (StrandBillboard->Sprite != Sprite)
        {
            // BillboardComponent is MinimalAPI on some UE versions. Directly set
            // the public sprite property and dirty render state below rather
            // than depending on a non-exported SetSprite implementation.
            StrandBillboard->Sprite = Sprite;
        }
    }
    // Reassert native editor-sprite behavior on older level instances and
    // whenever the actor's exposed Size property changes.
    StrandBillboard->bIsScreenSizeScaled = true;
    StrandBillboard->bUseInEditorScaling = true;
    StrandBillboard->ScreenSize = 0.006f * FMath::Clamp(StrandBillboardSize, 0.25f, 4.0f);
    StrandBillboard->SetRelativeScale3D(FVector::OneVector);
    StrandBillboard->SetVisibility(bShowStrandBillboard);
    StrandBillboard->MarkRenderStateDirty();
#endif
}

void AStrandSplineActor::PostLoad()
{
    Super::PostLoad();
    RefreshStrandBillboard();
}

void AStrandSplineActor::PostActorCreated()
{
    Super::PostActorCreated();
    RefreshStrandBillboard();
}

void AStrandSplineActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshStrandBillboard();
}

void AStrandSplineActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RefreshStrandBillboard();
}
#endif
