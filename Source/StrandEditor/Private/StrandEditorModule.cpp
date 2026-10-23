#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "AssetTypeActions_Base.h"
#include "ContentBrowserMenuContexts.h"
#include "UObject/SoftObjectPath.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "PropertyEditorModule.h"
#include "SplineDataAsset.h"
#include "StrandActorFactory.h"
#include "Subsystems/PlacementSubsystem.h"
#include "Factories/AssetFactoryInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "StrandSplineComponentCustomization.h"
#include "StrandSplineMeshCustomization.h"
#include "IPlacementModeModule.h"
#include "Components/SplineMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "SplineEditorCommands.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "LandscapeSplinesComponent.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "StrandEditor"

namespace StrandEditorStyle
{
    const FName StyleSetName(TEXT("StrandEditorStyle"));
    const FName ClassIconName(TEXT("ClassIcon.SplineDataAsset"));
    const FName ClassThumbnailName(TEXT("ClassThumbnail.SplineDataAsset"));

    const FSlateBrush* FindBrush(const FName BrushName)
    {
        const ISlateStyle* RegisteredStyle = FSlateStyleRegistry::FindSlateStyle(StyleSetName);
        return RegisteredStyle ? RegisteredStyle->GetBrush(BrushName) : nullptr;
    }
}

class FSplineDataAssetActions final : public FAssetTypeActions_Base
{
public:
    explicit FSplineDataAssetActions(EAssetTypeCategories::Type InCategory) : Category(InCategory) {}
    virtual FText GetName() const override { return LOCTEXT("AssetName", "Strand Spline"); }
    virtual FColor GetTypeColor() const override { return FColor(0x00, 0xB4, 0xA7); }
    virtual const FSlateBrush* GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const override
    {
        return StrandEditorStyle::FindBrush(StrandEditorStyle::ClassIconName);
    }
    virtual const FSlateBrush* GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const override
    {
        return StrandEditorStyle::FindBrush(StrandEditorStyle::ClassThumbnailName);
    }
    virtual UClass* GetSupportedClass() const override { return USplineDataAsset::StaticClass(); }
    virtual uint32 GetCategories() override { return static_cast<uint32>(Category); }
    // Actions live in their own ToolMenus section below. Keeping the legacy
    // asset type actions strictly for icon, label, and color avoids folding
    // engine-provided Data Asset commands into a misleading Strand group.
    virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return false; }
private:
    EAssetTypeCategories::Type Category;
};

class FStrandEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        RegisterAssetStyle();

        IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
        const EAssetTypeCategories::Type SplinesCategory = AssetTools.RegisterAdvancedAssetCategory(
            TEXT("Strand"), LOCTEXT("SplinesCategory", "Splines"));
        AssetActions = MakeShared<FSplineDataAssetActions>(SplinesCategory);
        AssetTools.RegisterAssetTypeActions(AssetActions.ToSharedRef());

        FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        // Register native spline variants explicitly: class layouts on some derived
        // components are not guaranteed to inherit the base customization.
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* Class = *It;
            if (!Class || Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)) { continue; }
            if (Class->IsChildOf(USplineComponent::StaticClass()))
            {
                PropertyEditor.RegisterCustomClassLayout(Class->GetFName(),
                    FOnGetDetailCustomizationInstance::CreateStatic(&FStrandSplineComponentCustomization::MakeInstance));
                RegisteredDetailClasses.Add(Class->GetFName());
            }
            else if (Class->IsChildOf(USplineMeshComponent::StaticClass()))
            {
                PropertyEditor.RegisterCustomClassLayout(Class->GetFName(),
                    FOnGetDetailCustomizationInstance::CreateStatic(&FStrandSplineMeshCustomization::MakeInstance));
                RegisteredDetailClasses.Add(Class->GetFName());
            }
        }
        PropertyEditor.NotifyCustomizationModuleChanged();

        // Native Content Browser drag/drop calls this factory at the viewport drop location.
        // Keep it registered for the lifetime of StrandEditor only.
        if (GEditor)
        {
            ActorFactory.Reset(NewObject<UStrandActorFactory>(GetTransientPackage(), UStrandActorFactory::StaticClass(),
                NAME_None, RF_Transient));
            GEditor->ActorFactories.Add(ActorFactory.Get());
            // UE 5.5+ viewport drops use the PlacementSubsystem's registry, which is
            // independent of GEditor->ActorFactories. Register with BOTH.
            if (UPlacementSubsystem* Placement = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
            {
                TScriptInterface<IAssetFactoryInterface> FactoryInterface;
                FactoryInterface.SetObject(ActorFactory.Get());
                FactoryInterface.SetInterface(static_cast<IAssetFactoryInterface*>(ActorFactory.Get()));
                Placement->RegisterAssetFactory(FactoryInterface);
            }
            // Add to Place Actors > Basic as well as the top-left Quickly Add menu.
            if (FModuleManager::Get().ModuleExists(TEXT("PlacementMode"), nullptr))
            {
                IPlacementModeModule& PlaceActors = FModuleManager::LoadModuleChecked<IPlacementModeModule>(TEXT("PlacementMode"));
                PlacementItemId = PlaceActors.RegisterPlaceableItem(FBuiltInPlacementCategories::Basic(),
                    MakeShared<FPlaceableItem>(ActorFactory.Get(), FAssetData(), TOptional<int32>(75)));
            }
        }
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FStrandEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        if (GEditor && ActorFactory.IsValid())
        {
            if (UPlacementSubsystem* Placement = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
            {
                TScriptInterface<IAssetFactoryInterface> FactoryInterface;
                FactoryInterface.SetObject(ActorFactory.Get());
                FactoryInterface.SetInterface(static_cast<IAssetFactoryInterface*>(ActorFactory.Get()));
                Placement->UnregisterAssetFactory(FactoryInterface);
            }
            GEditor->ActorFactories.Remove(ActorFactory.Get());
        }
        if (PlacementItemId.IsSet() && FModuleManager::Get().IsModuleLoaded(TEXT("PlacementMode")))
        {
            IPlacementModeModule::Get().UnregisterPlaceableItem(PlacementItemId.GetValue());
        }
        PlacementItemId.Reset();
        ActorFactory.Reset();
        UToolMenus::UnRegisterStartupCallback(this);
        if (UToolMenus::IsToolMenuUIEnabled()) { UToolMenus::UnregisterOwner(this); }
        if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
        {
            FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
            for (const FName Name : RegisteredDetailClasses) { PropertyEditor.UnregisterCustomClassLayout(Name); }
            RegisteredDetailClasses.Reset();
            PropertyEditor.NotifyCustomizationModuleChanged();
        }
        if (AssetActions.IsValid() && FModuleManager::Get().IsModuleLoaded("AssetTools"))
        {
            FAssetToolsModule::GetModule().Get().UnregisterAssetTypeActions(AssetActions.ToSharedRef());
        }
        AssetActions.Reset();

        if (AssetStyle.IsValid())
        {
            FSlateStyleRegistry::UnRegisterSlateStyle(*AssetStyle);
            AssetStyle.Reset();
        }
    }

private:
    TStrongObjectPtr<UStrandActorFactory> ActorFactory;
    TOptional<FPlacementModeID> PlacementItemId;
    TArray<FName> RegisteredDetailClasses;
    TSharedPtr<IAssetTypeActions> AssetActions;
    TSharedPtr<FSlateStyleSet> AssetStyle;

    void RegisterAssetStyle()
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Strand"));
        if (!Plugin.IsValid())
        {
            UE_LOG(LogTemp, Warning, TEXT("Strand: Cannot locate plugin directory for spline asset icons."));
            return;
        }

        const FString ResourceDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"));
        const FString SvgPath = FPaths::Combine(ResourceDirectory, TEXT("StrandSplineAsset_64.svg"));
        if (!FPaths::FileExists(SvgPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("Strand: Spline asset icon not found: %s"), *SvgPath);
            return;
        }

        const FString ActorSvgPath = FPaths::Combine(ResourceDirectory, TEXT("StrandSplineActor_16.svg"));
        if (!FPaths::FileExists(ActorSvgPath))
        {
            UE_LOG(LogTemp, Warning, TEXT("Strand: Actor icon not found: %s"), *ActorSvgPath);
        }

        AssetStyle = MakeShared<FSlateStyleSet>(StrandEditorStyle::StyleSetName);
        AssetStyle->SetContentRoot(ResourceDirectory);
        AssetStyle->Set(StrandEditorStyle::ClassIconName,
            new FSlateVectorImageBrush(SvgPath, FVector2D(16.0f, 16.0f),
                FLinearColor::White, ESlateBrushTileType::NoTile));
        // The actor has its OWN white 16px artwork for Outliner and placement menus.
        // The Content Browser asset icon/thumbnail continue to use the original SVG.
        if (FPaths::FileExists(ActorSvgPath))
        {
            AssetStyle->Set(TEXT("ClassIcon.StrandSplineActor"),
                new FSlateVectorImageBrush(ActorSvgPath, FVector2D(16.0f, 16.0f),
                    FLinearColor::White, ESlateBrushTileType::NoTile));
            AssetStyle->Set(TEXT("ClassThumbnail.StrandSplineActor"),
                new FSlateVectorImageBrush(ActorSvgPath, FVector2D(64.0f, 64.0f),
                    FLinearColor::White, ESlateBrushTileType::NoTile));
        }
        AssetStyle->Set(StrandEditorStyle::ClassThumbnailName,
            new FSlateVectorImageBrush(SvgPath, FVector2D(64.0f, 64.0f),
                FLinearColor::White, ESlateBrushTileType::NoTile));
        // Plugin-owned 16px vector action icons avoid relying on engine-version-specific
        // Slate brush names (which can render as a checkerboard when missing).
        AssetStyle->Set(TEXT("Strand.Action.Apply"),
            new FSlateVectorImageBrush(FPaths::Combine(ResourceDirectory, TEXT("StrandActionApply.svg")), FVector2D(16, 16)));
        AssetStyle->Set(TEXT("Strand.Action.Update"),
            new FSlateVectorImageBrush(FPaths::Combine(ResourceDirectory, TEXT("StrandActionUpdate.svg")), FVector2D(16, 16)));
        AssetStyle->Set(TEXT("Strand.Action.New"),
            new FSlateVectorImageBrush(FPaths::Combine(ResourceDirectory, TEXT("StrandActionNew.svg")), FVector2D(16, 16)));
        AssetStyle->Set(TEXT("Strand.Action.Compare"),
            new FSlateVectorImageBrush(FPaths::Combine(ResourceDirectory, TEXT("StrandActionCompare.svg")), FVector2D(16, 16)));
        FSlateStyleRegistry::RegisterSlateStyle(*AssetStyle);
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);

        // UE 5.5+ Content Browser actions are registered against the asset class,
        // not the legacy GetActions section. Keep native Data Asset operations
        // (including "Convert to Different Data Asset Type") in their own
        // general-purpose category. Only Strand commands go in "Strand".
        if (UToolMenu* AssetMenu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(USplineDataAsset::StaticClass()))
        {
            FToolMenuSection& GeneralAssetActions = AssetMenu->FindOrAddSection(TEXT("GetAssetActions"));
            GeneralAssetActions.Label = LOCTEXT("GeneralAssetActions", "Asset Actions");

            FToolMenuSection& StrandActions = AssetMenu->AddSection(
                TEXT("StrandAssetActions"), LOCTEXT("StrandAssetMenuTitle", "Strand"),
                FToolMenuInsert(TEXT("GetAssetActions"), EToolMenuInsertType::Before));
            StrandActions.AddDynamicEntry(TEXT("StrandAssetMenuEntries"),
                FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& DynamicSection)
                {
                    const UContentBrowserAssetContextMenuContext* Context =
                        UContentBrowserAssetContextMenuContext::FindContextWithAssets(DynamicSection);
                    if (!Context || Context->SelectedAssets.Num() != 1 ||
                        Context->SelectedAssets[0].AssetClassPath != USplineDataAsset::StaticClass()->GetClassPathName())
                    {
                        return;
                    }
                    // Do not load the selected asset to build a context menu.
                    // Loading occurs only when the designer clicks an action.
                    const FSoftObjectPath AssetPath = Context->SelectedAssets[0].ToSoftObjectPath();
                    const auto ResolveAsset = [AssetPath]() -> USplineDataAsset*
                    {
                        return Cast<USplineDataAsset>(AssetPath.TryLoad());
                    };
                    DynamicSection.AddMenuEntry(
                        TEXT("Strand_CreateActor"),
                        LOCTEXT("CreateActor", "Create Strand Spline Actor"),
                        LOCTEXT("CreateActorTip", "Place a native Strand Spline Actor in front of the active level viewport camera. Drag this asset into the viewport for precise placement."),
                        FSlateIcon(StrandEditorStyle::StyleSetName, TEXT("ClassIcon.StrandSplineActor")),
                        FUIAction(FExecuteAction::CreateLambda([ResolveAsset]()
                        {
                            SplineEditorCommands::CreateNativeActorFromAsset(ResolveAsset());
                        }), FCanExecuteAction::CreateLambda([]()
                        {
                            return GEditor && GEditor->GetEditorWorldContext().World();
                        })));
                    DynamicSection.AddMenuEntry(
                        TEXT("Strand_ApplyToSelection"),
                        LOCTEXT("ApplyToSelection", "Apply to Selected Splines"),
                        LOCTEXT("ApplyToSelectionTip", "Replace point data on selected actors that have exactly one native Spline Component. Actors containing multiple splines are skipped."),
                        FSlateIcon(StrandEditorStyle::StyleSetName, TEXT("Strand.Action.Apply")),
                        FUIAction(FExecuteAction::CreateLambda([ResolveAsset]()
                        {
                            SplineEditorCommands::ApplyAssetToSelectedActors(ResolveAsset());
                        }), FCanExecuteAction::CreateLambda([]()
                        {
                            for (AActor* Actor : SplineEditorCommands::SelectedActors())
                            {
                                if (IsValid(SplineEditorCommands::FindUnambiguousSpline(Actor))) { return true; }
                            }
                            return false;
                        })));
                    DynamicSection.AddMenuEntry(
                        TEXT("Strand_SelectActorsUsingAsset"),
                        LOCTEXT("SelectUsers", "Select Actors Using This Asset"),
                        LOCTEXT("SelectUsersTip", "Select loaded level actors that have native spline components linked to this asset."),
                        FSlateIcon(StrandEditorStyle::StyleSetName, TEXT("Strand.Action.Compare")),
                        FUIAction(FExecuteAction::CreateLambda([ResolveAsset]()
                        {
                            SplineEditorCommands::SelectActorsUsingAsset(ResolveAsset());
                        }), FCanExecuteAction::CreateLambda([]()
                        {
                            return GEditor && GEditor->GetEditorWorldContext().World();
                        })));
                    DynamicSection.AddMenuEntry(
                        TEXT("Strand_InspectUsage"),
                        LOCTEXT("InspectUsage", "Inspect Strand Usage..."),
                        LOCTEXT("InspectUsageTip", "Report loaded spline components using this asset, including synchronized and locally modified instances. Does not modify or select anything."),
                        FSlateIcon(StrandEditorStyle::StyleSetName, TEXT("Strand.Action.Compare")),
                        FUIAction(FExecuteAction::CreateLambda([ResolveAsset]()
                        {
                            SplineEditorCommands::InspectAssetUsage(ResolveAsset());
                        }), FCanExecuteAction::CreateLambda([]()
                        {
                            return GEditor && GEditor->GetEditorWorldContext().World();
                        })));
                }));
        }

        UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        FToolMenuSection& ImportSection = ToolsMenu->FindOrAddSection(TEXT("StrandImport"));
        ImportSection.AddMenuEntry(TEXT("StrandImportAlembic"),
            LOCTEXT("ImportAbc", "Strand: Import Alembic Curves..."),
            LOCTEXT("ImportAbcTip", "Import native Alembic curve geometry as separate Strand spline assets. Does not intercept Unreal's mesh/groom workflow."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]()
            {
                SplineEditorCommands::ImportCurves(true);
            })));
        ImportSection.AddMenuEntry(TEXT("StrandImportSvg"),
            LOCTEXT("ImportSvg", "Strand: Import SVG Curves..."),
            LOCTEXT("ImportSvgTip", "Import SVG vector paths as Strand spline assets in the selected Content Browser folder."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]()
            {
                SplineEditorCommands::ImportCurves(false);
            })));

        ImportSection.AddMenuEntry(TEXT("StrandImportLegacyFBX"),
            LOCTEXT("ImportLegacyFbx", "Strand: Import Legacy FBX Curves..."),
            LOCTEXT("ImportLegacyFbxTip", "Explicitly import FBX curve nodes as Strand assets, without overriding native FBX mesh/skeleton import."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]()
            {
                SplineEditorCommands::ImportCurves(false, true);
            })));
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.ActorContextMenu"));
        FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("Strand"));
        Section.AddDynamicEntry(TEXT("StrandActorActions"),
            FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Subsection)
            {
                bool bHasSpline = false, bHasLandscape = false;
                for (AActor* Actor : SplineEditorCommands::SelectedActors())
                {
                    bHasSpline |= IsValid(SplineEditorCommands::FindUnambiguousSpline(Actor));
                    bHasLandscape |= IsValid(Actor->FindComponentByClass<ULandscapeSplinesComponent>());
                }
                if (bHasSpline)
                {
                    Subsection.AddMenuEntry(TEXT("SplineAssetConvert"),
                        LOCTEXT("Convert", "Convert Spline to Spline Data Asset"),
                        LOCTEXT("ConvertTip", "Create an asset, capture local points, and link it directly to the native spline component."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ConvertSelectedActors)));
                    Subsection.AddMenuEntry(TEXT("SplineAssetInject"),
                        LOCTEXT("Inject", "Apply Selected Spline Data Asset"),
                        LOCTEXT("InjectTip", "Read the asset selected in the Content Browser and overwrite actor spline points."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ApplySelectedAssetToActors)));
                }
                const TArray<AActor*> Actors = SplineEditorCommands::SelectedActors();
                if (Actors.Num() == 1 && IsValid(Actors[0]))
                {
                    TArray<USplineComponent*> Components;
                    Actors[0]->GetComponents<USplineComponent>(Components);
                    if (Components.IsEmpty())
                    {
                        Subsection.AddMenuEntry(TEXT("StrandAddNativeComponent"),
                            LOCTEXT("AddNativeComponent", "Add Native Spline Component from Selected Strand"),
                            LOCTEXT("AddNativeComponentTip", "Attach an ordinary, editable Spline Component and apply the Strand asset currently selected in the Content Browser."),
                            FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::AddNativeSplineAndApplyToSelected)));
                    }
                    else if (Components.Num() > 1)
                    {
                        // Explicitly name every destination when the actor owns multiple
                        // splines; never route all of them to the first component.
                        for (int32 Index = 0; Index < Components.Num(); ++Index)
                        {
                            USplineComponent* Component = Components[Index];
                            if (!IsValid(Component)) { continue; }
                            const TWeakObjectPtr<USplineComponent> WeakComponent(Component);
                            const FText ComponentName = FText::FromString(Component->GetName());
                            Subsection.AddMenuEntry(FName(*FString::Printf(TEXT("StrandCapture_%d"), Index)),
                                FText::Format(LOCTEXT("CaptureNamed", "Save {0} as Strand Spline"), ComponentName),
                                LOCTEXT("CaptureNamedTip", "Save only this specific native spline component and link it to the new asset."),
                                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakComponent]()
                                {
                                    SplineEditorCommands::CaptureComponent(WeakComponent);
                                })));
                            Subsection.AddMenuEntry(FName(*FString::Printf(TEXT("StrandApply_%d"), Index)),
                                FText::Format(LOCTEXT("ApplyNamed", "Apply Selected Strand to {0}"), ComponentName),
                                LOCTEXT("ApplyNamedTip", "Apply the selected Content Browser Strand asset to this specific component."),
                                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakComponent]()
                                {
                                    USplineDataAsset* Asset = SplineEditorCommands::SelectedSplineAsset();
                                    SplineEditorCommands::ApplyAssetToComponent(Asset, WeakComponent);
                                })));
                        }
                    }
                }
                if (bHasLandscape)
                {
                    Subsection.AddMenuEntry(TEXT("SplineAssetLandscapeExport"),
                        LOCTEXT("LandscapeExport", "Convert Landscape Path to Spline Data Asset"),
                        LOCTEXT("LandscapeExportTip", "Extract the connected Landscape curve into a new reusable Spline Data Asset."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ConvertSelectedLandscape)));
                    Subsection.AddMenuEntry(TEXT("SplineAssetLandscapeInject"),
                        LOCTEXT("LandscapeInject", "Apply Selected Spline Asset to Landscape Path"),
                        LOCTEXT("LandscapeInjectTip", "Topology-safe Landscape spline snap, preserving the first control point."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ApplySelectedAssetToLandscape)));
                }
            }));
    }
};

IMPLEMENT_MODULE(FStrandEditorModule, StrandEditor)
#undef LOCTEXT_NAMESPACE
