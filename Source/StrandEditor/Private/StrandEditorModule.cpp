#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "AssetTypeActions_Base.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "PropertyEditorModule.h"
#include "SplineDataAsset.h"
#include "SplineAssetTrackerComponent.h"
#include "SplineAssetTrackerCustomization.h"
#include "SplineEditorCommands.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "LandscapeSplinesComponent.h"

#define LOCTEXT_NAMESPACE "StrandEditor"

class FSplineDataAssetActions final : public FAssetTypeActions_Base
{
public:
    explicit FSplineDataAssetActions(EAssetTypeCategories::Type InCategory) : Category(InCategory) {}
    virtual FText GetName() const override { return LOCTEXT("AssetName", "Spline Data Asset"); }
    virtual FColor GetTypeColor() const override { return FColor(76, 178, 230); }
    virtual UClass* GetSupportedClass() const override { return USplineDataAsset::StaticClass(); }
    virtual uint32 GetCategories() override { return static_cast<uint32>(Category); }
    virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return !InObjects.IsEmpty(); }
    virtual void GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section) override
    {
        for (UObject* Object : InObjects)
        {
            if (USplineDataAsset* Asset = Cast<USplineDataAsset>(Object))
            {
                const TWeakObjectPtr<USplineDataAsset> Weak(Asset);
                Section.AddMenuEntry(
                    TEXT("SplineAsset_ApplyToSelectedActors"),
                    LOCTEXT("ApplyAsset", "Apply to Selected Level Spline Actor(s)"),
                    LOCTEXT("ApplyAssetTip", "Replace the points on selected level actors containing one Spline Component."),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([Weak]()
                    {
                        SplineEditorCommands::ApplyAssetToSelectedActors(Weak);
                    })));
            }
        }
    }
private:
    EAssetTypeCategories::Type Category;
};

class FStrandEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
        const EAssetTypeCategories::Type SplinesCategory = AssetTools.RegisterAdvancedAssetCategory(
            TEXT("Strand"), LOCTEXT("SplinesCategory", "Splines"));
        AssetActions = MakeShared<FSplineDataAssetActions>(SplinesCategory);
        AssetTools.RegisterAssetTypeActions(AssetActions.ToSharedRef());

        FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyEditor.RegisterCustomClassLayout(USplineAssetTrackerComponent::StaticClass()->GetFName(),
            FOnGetDetailCustomizationInstance::CreateStatic(&FSplineAssetTrackerCustomization::MakeInstance));
        PropertyEditor.NotifyCustomizationModuleChanged();
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FStrandEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
        if (UToolMenus::IsToolMenuUIEnabled()) { UToolMenus::UnregisterOwner(this); }
        if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
        {
            FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
            PropertyEditor.UnregisterCustomClassLayout(USplineAssetTrackerComponent::StaticClass()->GetFName());
            PropertyEditor.NotifyCustomizationModuleChanged();
        }
        if (AssetActions.IsValid() && FModuleManager::Get().IsModuleLoaded("AssetTools"))
        {
            FAssetToolsModule::GetModule().Get().UnregisterAssetTypeActions(AssetActions.ToSharedRef());
            AssetActions.Reset();
        }
    }

private:
    TSharedPtr<IAssetTypeActions> AssetActions;

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);
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
                        LOCTEXT("ConvertTip", "Create an asset, capture local points, and attach a Tracker to the level actor."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ConvertSelectedActors)));
                    Subsection.AddMenuEntry(TEXT("SplineAssetInject"),
                        LOCTEXT("Inject", "Apply Selected Spline Data Asset"),
                        LOCTEXT("InjectTip", "Read the asset selected in the Content Browser and overwrite actor spline points."),
                        FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ApplySelectedAssetToActors)));
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
