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
#include "StrandSplineAssetEditor.h"
#include "StrandSplineExport.h"
#include "StrandPlacement.h"
#include "StrandDropLogic.h"
#include "StrandSplineActor.h"
#include "StrandSplineUserData.h"
#include "StrandMultiSplineActor.h"
#include "Subsystems/PlacementSubsystem.h"
#include "Factories/AssetFactoryInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "StrandSplineComponentCustomization.h"
#include "StrandSplineMeshCustomization.h"
#include "StrandActorDetails.h"
#include "IPlacementModeModule.h"
#include "Components/SplineMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "SplineEditorCommands.h"
#include "StrandCurveTestGenerator.h"
#include "Misc/MessageDialog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "ScopedTransaction.h"
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
    virtual void OpenAssetEditor(const TArray<UObject*>& InObjects,
        const EAssetTypeActivationOpenedMethod OpenedMethod,
        TSharedPtr<IToolkitHost> EditWithinLevelEditor) override
    {
        OpenAssetEditor(InObjects, EditWithinLevelEditor);
    }
    virtual void OpenAssetEditor(const TArray<UObject*>& InObjects,
        TSharedPtr<IToolkitHost> EditWithinLevelEditor) override
    {
        for (UObject* Object : InObjects)
        {
            if (USplineDataAsset* SplineAsset = Cast<USplineDataAsset>(Object))
            {
                TSharedRef<FStrandSplineAssetEditor> Editor = MakeShared<FStrandSplineAssetEditor>();
                Editor->Init(SplineAsset, EditWithinLevelEditor);
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
        PropertyEditor.RegisterCustomClassLayout(AStrandSplineActor::StaticClass()->GetFName(),
            FOnGetDetailCustomizationInstance::CreateStatic(&FStrandActorDetails::MakeInstance));
        RegisteredDetailClasses.Add(AStrandSplineActor::StaticClass()->GetFName());
        PropertyEditor.RegisterCustomClassLayout(AStrandMultiSplineActor::StaticClass()->GetFName(),
            FOnGetDetailCustomizationInstance::CreateStatic(&FStrandActorDetails::MakeInstance));
        RegisteredDetailClasses.Add(AStrandMultiSplineActor::StaticClass()->GetFName());
        PropertyEditor.NotifyCustomizationModuleChanged();

        // Native Content Browser drag/drop calls this factory at the viewport drop location.
        // Keep it registered for the lifetime of StrandEditor only.
        if (GEditor)
        {
            ActorFactory.Reset(NewObject<UStrandActorFactory>(GetTransientPackage(), UStrandActorFactory::StaticClass(),
                NAME_None, RF_Transient));
            GEditor->ActorFactories.Add(ActorFactory.Get());
            MultiActorFactory.Reset(NewObject<UStrandMultiActorFactory>(GetTransientPackage(),
                UStrandMultiActorFactory::StaticClass(), NAME_None, RF_Transient));
            GEditor->ActorFactories.Add(MultiActorFactory.Get());
            // UE 5.5+ viewport drops use the PlacementSubsystem's registry, which is
            // independent of GEditor->ActorFactories. Register with BOTH.
            if (UPlacementSubsystem* Placement = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
            {
                TScriptInterface<IAssetFactoryInterface> FactoryInterface;
                FactoryInterface.SetObject(ActorFactory.Get());
                FactoryInterface.SetInterface(static_cast<IAssetFactoryInterface*>(ActorFactory.Get()));
                Placement->RegisterAssetFactory(FactoryInterface);
                TScriptInterface<IAssetFactoryInterface> MultiInterface;
                MultiInterface.SetObject(MultiActorFactory.Get());
                MultiInterface.SetInterface(static_cast<IAssetFactoryInterface*>(MultiActorFactory.Get()));
                Placement->RegisterAssetFactory(MultiInterface);
            }
            // Add to Place Actors > Basic as well as the top-left Quickly Add menu.
            if (FModuleManager::Get().ModuleExists(TEXT("PlacementMode"), nullptr))
            {
                IPlacementModeModule& PlaceActors = FModuleManager::LoadModuleChecked<IPlacementModeModule>(TEXT("PlacementMode"));
                PlacementItemId = PlaceActors.RegisterPlaceableItem(FBuiltInPlacementCategories::Basic(),
                    MakeShared<FPlaceableItem>(ActorFactory.Get(), FAssetData(), TOptional<int32>(75)));
                MultiPlacementItemId = PlaceActors.RegisterPlaceableItem(FBuiltInPlacementCategories::Basic(),
                    MakeShared<FPlaceableItem>(MultiActorFactory.Get(), FAssetData(), TOptional<int32>(76)));
            }
        }
        // This editor event is emitted for actual Content Browser viewport drops.
        // Unlike ActorFactory::PostSpawnActor / EndPlacement, it does not include
        // the temporary actors made and discarded while hovering the mouse.
        ActorsDroppedHandle = FEditorDelegates::OnNewActorsDropped.AddRaw(
            this, &FStrandEditorModule::OnNewActorsDropped);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FStrandEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        if (ActorsDroppedHandle.IsValid())
        {
            FEditorDelegates::OnNewActorsDropped.Remove(ActorsDroppedHandle);
            ActorsDroppedHandle.Reset();
        }
        if (GEditor && ActorFactory.IsValid())
        {
            if (UPlacementSubsystem* Placement = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
            {
                TScriptInterface<IAssetFactoryInterface> FactoryInterface;
                FactoryInterface.SetObject(ActorFactory.Get());
                FactoryInterface.SetInterface(static_cast<IAssetFactoryInterface*>(ActorFactory.Get()));
                Placement->UnregisterAssetFactory(FactoryInterface);
            }
            if (MultiActorFactory.IsValid())
            {
                if (UPlacementSubsystem* Placement = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
                {
                    TScriptInterface<IAssetFactoryInterface> MultiInterface;
                    MultiInterface.SetObject(MultiActorFactory.Get());
                    MultiInterface.SetInterface(static_cast<IAssetFactoryInterface*>(MultiActorFactory.Get()));
                    Placement->UnregisterAssetFactory(MultiInterface);
                }
                GEditor->ActorFactories.Remove(MultiActorFactory.Get());
            }
            GEditor->ActorFactories.Remove(ActorFactory.Get());
        }
        if (PlacementItemId.IsSet() && FModuleManager::Get().IsModuleLoaded(TEXT("PlacementMode")))
        {
            IPlacementModeModule::Get().UnregisterPlaceableItem(PlacementItemId.GetValue());
        }
        if (MultiPlacementItemId.IsSet() && FModuleManager::Get().IsModuleLoaded(TEXT("PlacementMode")))
        { IPlacementModeModule::Get().UnregisterPlaceableItem(MultiPlacementItemId.GetValue()); }
        PlacementItemId.Reset();
        MultiPlacementItemId.Reset();
        MultiActorFactory.Reset();
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
    FDelegateHandle ActorsDroppedHandle;

    void OnNewActorsDropped(const TArray<UObject*>& DroppedObjects,
                            const TArray<AActor*>& DroppedActors)
    {
        // This is a notification for a COMPLETED mouse drop. For one Strand
        // asset do nothing: keep Unreal's existing regular actor and location.
        // Mixed file types or preview actors must never be grouped/deleted.
        if (!GEditor || !StrandDropLogic::HasMultipleDistinctStrandAssets(DroppedObjects) ||
            DroppedActors.Num() != DroppedObjects.Num() || !ActorFactory.IsValid())
        {
            return;
        }

        TArray<USplineDataAsset*> Assets;
        TMap<USplineDataAsset*, int32> RemainingAssets;
        for (UObject* DroppedObject : DroppedObjects)
        {
            USplineDataAsset* Asset = Cast<USplineDataAsset>(DroppedObject);
            if (!IsValid(Asset)) { return; }
            Assets.Add(Asset);
            RemainingAssets.FindOrAdd(Asset)++;
        }
        if (RemainingAssets.Num() < 2) { return; } // Repeated preview of a single asset is not multi-placement.

        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!IsValid(World) || World->WorldType != EWorldType::Editor) { return; }
        const FTransform DropTransform = DroppedActors[0] ?
            DroppedActors[0]->GetActorTransform() : FTransform::Identity;
        for (AActor* DroppedActor : DroppedActors)
        {
            // Never mistake preview actors (which may be marked for replacement
            // or destruction) for actors actually placed in the level.
            if (!IsValid(DroppedActor) ||
                !DroppedActor->IsA<AStrandSplineActor>() ||
                DroppedActor->IsA<AStrandMultiSplineActor>() ||
                DroppedActor->GetWorld() != World ||
                !DroppedActor->GetLevel() ||
                DroppedActor->HasAnyFlags(RF_Transient | RF_NewerVersionExists |
                    RF_BeginDestroyed | RF_FinishDestroyed))
            {
                return;
            }
            USplineDataAsset* Linked = Cast<USplineDataAsset>(
                ActorFactory->GetAssetFromActorInstance(DroppedActor));
            int32* RemainingCount = RemainingAssets.Find(Linked);
            if (!RemainingCount || *RemainingCount <= 0) { return; }
            --(*RemainingCount);
        }
        for (const TPair<USplineDataAsset*, int32>& Entry : RemainingAssets)
        {
            if (Entry.Value != 0) { return; }
        }

        if (FMessageDialog::Open(EAppMsgType::YesNo,
            LOCTEXT("GroupFinishedDropPrompt",
                "You dropped multiple different Strand Spline assets.\n\n"
                "Combine them into one Strand Multi Spline Actor?\n\n"
                "Yes: group these newly placed splines.\n"
                "No: keep the separate actors at their dropped locations.")) != EAppReturnType::Yes)
        {
            return;
        }

        const FScopedTransaction Transaction(LOCTEXT("GroupDroppedActors", "Group Dropped Strand Splines"));
        FString Error;
        AActor* Multi = StrandPlacement::CreateMultiSplineActor(Assets, Error, &DropTransform);
        if (!IsValid(Multi))
        {
            UE_LOG(LogTemp, Warning, TEXT("Strand: Could not group dropped splines: %s"), *Error);
            return; // Leave every original placed actor untouched on failure.
        }

        for (AActor* Original : DroppedActors)
        {
            Original->Modify();
            Original->GetLevel()->Modify();
            World->DestroyActor(Original);
        }
        GEditor->SelectNone(false, true, false);
        GEditor->SelectActor(Multi, true, true, true);
        GEditor->RedrawAllViewports();
    }

    TStrongObjectPtr<UStrandActorFactory> ActorFactory;
    TStrongObjectPtr<UStrandMultiActorFactory> MultiActorFactory;
    TOptional<FPlacementModeID> MultiPlacementItemId;
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
            AssetStyle->Set(TEXT("ClassIcon.StrandMultiSplineActor"),
                new FSlateVectorImageBrush(ActorSvgPath, FVector2D(16.0f, 16.0f),
                    FLinearColor::White, ESlateBrushTileType::NoTile));
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
                    if (!Context || Context->SelectedAssets.IsEmpty()) { return; }
                    for (const FAssetData& Data : Context->SelectedAssets)
                    {
                        if (Data.AssetClassPath != USplineDataAsset::StaticClass()->GetClassPathName()) { return; }
                    }
                    TArray<FSoftObjectPath> SourceAssets;
                    for (const FAssetData& Data : Context->SelectedAssets) { SourceAssets.Add(Data.ToSoftObjectPath()); }
                    const auto ResolveAssets = [SourceAssets]()
                    {
                        TArray<USplineDataAsset*> Result;
                        for (const FSoftObjectPath& Path : SourceAssets)
                        {
                            if (USplineDataAsset* Asset = Cast<USplineDataAsset>(Path.TryLoad())) { Result.AddUnique(Asset); }
                        }
                        return Result;
                    };
                    if (SourceAssets.Num() > 1)
                    {
                        DynamicSection.AddMenuEntry(TEXT("Strand_CreateMultiActor"),
                            LOCTEXT("CreateMulti", "Create Strand Multi Spline Actor"),
                            LOCTEXT("CreateMultiTip", "Create one actor with one native Spline Component per selected Strand asset."),
                            FSlateIcon(StrandEditorStyle::StyleSetName, TEXT("ClassIcon.StrandMultiSplineActor")),
                            FUIAction(FExecuteAction::CreateLambda([ResolveAssets]()
                            {
                                SplineEditorCommands::CreateMultiActorFromAssets(ResolveAssets());
                            })));
                    }
                    DynamicSection.AddMenuEntry(TEXT("Strand_AddAndAssign"),
                        LOCTEXT("AddAndAssign", "Add Spline Component and Assign"),
                        LOCTEXT("AddAndAssignTip", "Add one native Spline Component per selected Strand asset to EACH selected level actor. Existing components and assets are preserved."),
                        FSlateIcon(StrandEditorStyle::StyleSetName, TEXT("Strand.Action.New")),
                        FUIAction(FExecuteAction::CreateLambda([ResolveAssets]()
                        {
                            SplineEditorCommands::AddSplineComponentsAndAssign(ResolveAssets());
                        }), FCanExecuteAction::CreateLambda([]()
                        {
                            return !SplineEditorCommands::SelectedActors().IsEmpty();
                        })));
                    if (SourceAssets.Num() != 1) { return; }
                    // Do not load the selected asset to build a context menu.
                    // Loading occurs only when the designer clicks an action.
                    const FSoftObjectPath AssetPath = Context->SelectedAssets[0].ToSoftObjectPath();
                    const auto ResolveAsset = [AssetPath]() -> USplineDataAsset*
                    {
                        return Cast<USplineDataAsset>(AssetPath.TryLoad());
                    };
                    DynamicSection.AddSubMenu(
                        TEXT("Strand_Export"),
                        LOCTEXT("ExportStrand", "Export Strand Spline"),
                        LOCTEXT("ExportStrandTip", "Export spline geometry as a 3D Alembic curve or projected 2D SVG without modifying the asset."),
                        FNewToolMenuDelegate::CreateLambda([ResolveAsset](UToolMenu* SubMenu)
                        {
                            FToolMenuSection& ExportSection = SubMenu->AddSection(TEXT("StrandExportFormats"));
                            ExportSection.AddMenuEntry(TEXT("Strand_ExportABC"),
                                LOCTEXT("ExportABC", "Alembic (.abc)  |  3D"),
                                LOCTEXT("ExportABCTip", "Export native evaluated 3D curve geometry."),
                                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([ResolveAsset]()
                                {
                                    StrandSplineExport::ExportInteractive(ResolveAsset(), true);
                                })));
                            ExportSection.AddMenuEntry(TEXT("Strand_ExportSVG"),
                                LOCTEXT("ExportSVG", "SVG (.svg)  |  2D"),
                                LOCTEXT("ExportSVGTip", "Export projected 2D curve geometry with explicit projection when needed."),
                                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([ResolveAsset]()
                                {
                                    StrandSplineExport::ExportInteractive(ResolveAsset(), false);
                                })));
                        }), false);
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
        ImportSection.AddMenuEntry(TEXT("StrandGenerateCurveTests"),
            LOCTEXT("GenerateTests", "Strand: Generate Helix and Braid Test Curves..."),
            LOCTEXT("GenerateTestsTip", "Write two Alembic ICurves test fixtures to Project/Saved/Strand/CurveImportTests."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]()
            {
                FString Directory, Error;
                if (StrandCurveTestGenerator::Generate(Directory, Error))
                {
                    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString(TEXT("Generated Helix.abc and TripleBraid.abc in:\n")) + Directory));
                }
                else
                {
                    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString(TEXT("Strand fixture generation failed: ")) + Error));
                }
            })));
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
    }
};

IMPLEMENT_MODULE(FStrandEditorModule, StrandEditor)
#undef LOCTEXT_NAMESPACE
