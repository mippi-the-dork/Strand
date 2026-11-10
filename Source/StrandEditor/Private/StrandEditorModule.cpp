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
#include "SplineDataAssetFactory.h"
#include "StrandSplineEditOperations.h"
#include "Misc/PackageName.h"
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
#include "StrandActorSplineLinks.h"
#include "ActorDetailsDelegates.h"
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
#include "EngineUtils.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectSaveContext.h"
#include "HAL/IConsoleManager.h"
#include "ScopedTransaction.h"
#include "Components/SplineComponent.h"
#include "LandscapeSplinesComponent.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ISettingsModule.h"
#include "StrandEditorPreferences.h"

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

// Non-destructive Content Browser asset join. The operation only creates a
// new asset, so neither source nor existing actor references are modified.
namespace StrandAssetJoining
{
    static void JoinSelectedAssets(const TArray<USplineDataAsset*>& InAssets)
    {
        if (InAssets.Num() != 2 || !IsValid(InAssets[0]) || !IsValid(InAssets[1]) ||
            InAssets[0] == InAssets[1]) { return; }
        TArray<USplineDataAsset*> Assets = InAssets;
        Assets.Sort([](const USplineDataAsset& A, const USplineDataAsset& B)
        {
            return A.GetPathName() < B.GetPathName();
        });
        USplineComponent* FirstNative = NewObject<USplineComponent>(GetTransientPackage());
        USplineComponent* SecondNative = NewObject<USplineComponent>(GetTransientPackage());
        Assets[0]->ApplyToSplineComponent(FirstNative);
        Assets[1]->ApplyToSplineComponent(SecondNative);
        TArray<FSplinePointData> Joined;
        double Gap = 0.0;
        bool bWelded = false;
        if (!StrandSplineEditOperations::JoinOpenSplineAssets(Assets[0]->Points,
            Assets[0]->bIsClosedLoop, FirstNative, Assets[1]->Points,
            Assets[1]->bIsClosedLoop, SecondNative, Joined, Gap, bWelded))
        {
            FMessageDialog::Open(EAppMsgType::Ok,
                LOCTEXT("JoinReject", "Select exactly two open Strand spline assets, each with at least two valid anchors."));
            return;
        }
        if (!bWelded)
        {
            const FText Prompt = FText::Format(LOCTEXT("JoinGapPrompt",
                "These splines have endpoints {0} cm apart in asset-local coordinates. "
                "Join them with a new straight segment? The originals will remain unchanged."),
                FText::AsNumber(Gap));
            if (FMessageDialog::Open(EAppMsgType::YesNo, Prompt) != EAppReturnType::Yes) { return; }
        }
        const FString ParentFolder = FPackageName::GetLongPackagePath(Assets[0]->GetOutermost()->GetName());
        IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
        FString PackageName, AssetName;
        AssetTools.CreateUniqueAssetName(FPaths::Combine(ParentFolder,
            Assets[0]->GetName() + TEXT("_") + Assets[1]->GetName() + TEXT("_Joined")),
            TEXT(""), PackageName, AssetName);
        USplineDataAsset* NewAsset = Cast<USplineDataAsset>(AssetTools.CreateAsset(
            AssetName, ParentFolder, USplineDataAsset::StaticClass(), NewObject<USplineDataAssetFactory>()));
        if (!NewAsset) { return; }
        NewAsset->Modify();
        NewAsset->Points = MoveTemp(Joined);
        NewAsset->bIsClosedLoop = false;
        NewAsset->DefaultCoordinateMode = Assets[0]->DefaultCoordinateMode;
        NewAsset->DefaultAnchorMode = Assets[0]->DefaultAnchorMode;
        NewAsset->DefaultAnchorPointIndex = FMath::Clamp(Assets[0]->DefaultAnchorPointIndex,
            0, NewAsset->Points.Num() - 1);
        // Deliberately do not inherit import association from either source.
        NewAsset->MarkPackageDirty();
        if (GEditor)
        {
            TArray<UObject*> AssetsToShow = { NewAsset };
            GEditor->SyncBrowserToObjects(AssetsToShow);
        }
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
        // UDeveloperSettings is MinimalAPI in UE 5.8; use a normal config UObject
        // with explicit Editor Settings registration to avoid non-exported vtable symbols.
        if (ISettingsModule* Settings = FModuleManager::LoadModulePtr<ISettingsModule>(TEXT("Settings")))
        {
            Settings->RegisterSettings(TEXT("Editor"), TEXT("Plugins"), TEXT("Strand Editor"),
                LOCTEXT("StrandPreferencesName", "Strand Editor"),
                LOCTEXT("StrandPreferencesDescription", "Strand viewport numeric presets and visualization preferences."),
                GetMutableDefault<UStrandEditorPreferences>());
        }
        RegisterAssetStyle();

        IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
        const EAssetTypeCategories::Type SplinesCategory = AssetTools.RegisterAdvancedAssetCategory(
            TEXT("Strand"), LOCTEXT("SplinesCategory", "Splines"));
        AssetActions = MakeShared<FSplineDataAssetActions>(SplinesCategory);
        AssetTools.RegisterAssetTypeActions(AssetActions.ToSharedRef());

        FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        // Never replace Unreal's native USplineComponent / USplineMeshComponent
        // Details customizations. In UE 5.8, replacing them hides stock spline
        // point selection and can interfere with component visualizer controls.
        // Strand link controls extend the actor's details independently instead.
        ActorDetailsExtensionHandle = OnExtendActorDetails.AddStatic(
            &StrandActorSplineLinks::ExtendActorDetails);
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
        // Fix metadata on existing Blueprint instances before OFPA serialization.
        // This is necessary because an inherited AssetUserData pointer can still
        // refer to the Blueprint's *private* component-template subobject.
        PreSaveExternalActorsHandle = FEditorDelegates::PreSaveExternalActors.AddRaw(
            this, &FStrandEditorModule::RepairWorldBeforeSave);
        ObjectPreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(
            this, &FStrandEditorModule::OnObjectPreSave);
        RepairCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("Strand.RepairUserData"),
            TEXT("Repair Strand asset user data on native spline components in the active editor world."),
            FConsoleCommandDelegate::CreateRaw(this, &FStrandEditorModule::RepairCurrentEditorWorld),
            ECVF_Default);
        AuditCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("Strand.AuditUserData"),
            TEXT("Read-only inventory of Strand metadata ownership and archetype references in loaded editor actors."),
            FConsoleCommandDelegate::CreateRaw(this, &FStrandEditorModule::AuditCurrentEditorWorld),
            ECVF_Default);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FStrandEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        if (ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
        {
            Settings->UnregisterSettings(TEXT("Editor"), TEXT("Plugins"), TEXT("Strand Editor"));
        }

        if (ActorDetailsExtensionHandle.IsValid())
        {
            OnExtendActorDetails.Remove(ActorDetailsExtensionHandle);
            ActorDetailsExtensionHandle.Reset();
        }
        if (AuditCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(AuditCommand);
            AuditCommand = nullptr;
        }
        if (RepairCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(RepairCommand);
            RepairCommand = nullptr;
        }
        if (ObjectPreSaveHandle.IsValid())
        {
            FCoreUObjectDelegates::OnObjectPreSave.Remove(ObjectPreSaveHandle);
            ObjectPreSaveHandle.Reset();
        }
        if (PreSaveExternalActorsHandle.IsValid())
        {
            FEditorDelegates::PreSaveExternalActors.Remove(PreSaveExternalActorsHandle);
            PreSaveExternalActorsHandle.Reset();
        }
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
    FDelegateHandle ActorDetailsExtensionHandle;
    FDelegateHandle ActorsDroppedHandle;
    FDelegateHandle PreSaveExternalActorsHandle;
    FDelegateHandle ObjectPreSaveHandle;
    IConsoleObject* RepairCommand = nullptr;
    IConsoleObject* AuditCommand = nullptr;

    static int32 RepairActor(AActor* Actor)
    {
        if (!IsValid(Actor) || Actor->IsTemplate()) { return 0; }
        int32 Repaired = 0;
        TArray<USplineComponent*> Splines;
        Actor->GetComponents<USplineComponent>(Splines);
        for (USplineComponent* Component : Splines)
        {
            Repaired += UStrandSplineUserData::EnsureComponentOwned(Component) ? 1 : 0;
        }
        TArray<USplineMeshComponent*> Meshes;
        Actor->GetComponents<USplineMeshComponent>(Meshes);
        for (USplineMeshComponent* Component : Meshes)
        {
            Repaired += UStrandSplineUserData::EnsureComponentOwned(Component) ? 1 : 0;
        }
        return Repaired;
    }

    void RepairWorldBeforeSave(UWorld* World)
    {
        if (!IsValid(World) || World->WorldType != EWorldType::Editor) { return; }
        int32 Repaired = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            Repaired += RepairActor(*It);
        }
        if (Repaired > 0)
        {
            UE_LOG(LogTemp, Display,
                TEXT("Strand: repaired %d Blueprint-instanced spline user-data owners before external actor save."),
                Repaired);
        }
    }

    void OnObjectPreSave(UObject* Object, FObjectPreSaveContext /*SaveContext*/)
    {
        // Supplement the early PreSaveExternalActors repair for save paths that
        // serialize actors/components individually. New exports generated from
        // within a PreSave callback may not receive PreSave themselves, so
        // PreSaveExternalActors and the explicit command remain the preferred
        // repair points for level external-actor packages.
        if (AActor* Actor = Cast<AActor>(Object))
        {
            RepairActor(Actor);
        }
        else if (USplineComponent* Spline = Cast<USplineComponent>(Object))
        {
            if (AActor* Owner = Spline->GetOwner())
            {
                if (!Owner->IsTemplate()) { UStrandSplineUserData::EnsureComponentOwned(Spline); }
            }
        }
        else if (USplineMeshComponent* Mesh = Cast<USplineMeshComponent>(Object))
        {
            if (AActor* Owner = Mesh->GetOwner())
            {
                if (!Owner->IsTemplate()) { UStrandSplineUserData::EnsureComponentOwned(Mesh); }
            }
        }
    }

    struct FStrandUserDataAudit
    {
        int32 Actors = 0;
        int32 Components = 0;
        int32 Metadata = 0;
        int32 Foreign = 0;
        int32 Linked = 0;
    };

    static void AuditComponent(UActorComponent* Component, UStrandSplineUserData* Data,
        FStrandUserDataAudit& Result)
    {
        ++Result.Components;
        if (!IsValid(Data)) { return; }
        ++Result.Metadata;
        if (IsValid(Data->AssignedSplineAsset)) { ++Result.Linked; }
        const bool bForeignOwner = Data->GetOuter() != Component;
        const bool bForeignArchetype = Data->GetArchetype() !=
            UStrandSplineUserData::StaticClass()->GetDefaultObject();
        if (bForeignOwner || bForeignArchetype)
        {
            ++Result.Foreign;
            UE_LOG(LogTemp, Warning,
                TEXT("Strand metadata: component '%s' has %s owner / %s archetype; metadata '%s', archetype '%s'."),
                *Component->GetPathName(),
                bForeignOwner ? TEXT("FOREIGN") : TEXT("local"),
                bForeignArchetype ? TEXT("FOREIGN") : TEXT("CDO"),
                *Data->GetPathName(), *GetPathNameSafe(Data->GetArchetype()));
        }
    }

    static FStrandUserDataAudit AuditWorld(UWorld* World)
    {
        FStrandUserDataAudit Result;
        if (!IsValid(World) || World->WorldType != EWorldType::Editor) { return Result; }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (!IsValid(Actor) || Actor->IsTemplate()) { continue; }
            ++Result.Actors;
            TArray<USplineComponent*> Splines;
            Actor->GetComponents<USplineComponent>(Splines);
            for (USplineComponent* Component : Splines)
            {
                if (IsValid(Component))
                {
                    AuditComponent(Component, UStrandSplineUserData::Get(Component), Result);
                }
            }
            TArray<USplineMeshComponent*> Meshes;
            Actor->GetComponents<USplineMeshComponent>(Meshes);
            for (USplineMeshComponent* Component : Meshes)
            {
                if (IsValid(Component))
                {
                    AuditComponent(Component, UStrandSplineUserData::GetOnSplineMesh(Component), Result);
                }
            }
        }
        UE_LOG(LogTemp, Display,
            TEXT("Strand metadata audit: %d actors, %d native spline/spline-mesh components, %d metadata entries (%d linked), %d unsafe owners/archetypes. Loaded editor world only."),
            Result.Actors, Result.Components, Result.Metadata, Result.Linked, Result.Foreign);
        return Result;
    }

    void AuditCurrentEditorWorld()
    {
        if (!GEditor)
        {
            UE_LOG(LogTemp, Warning, TEXT("Strand.AuditUserData: no editor instance."));
            return;
        }
        AuditWorld(GEditor->GetEditorWorldContext().World());
    }

    void RepairCurrentEditorWorld()
    {
        if (!GEditor)
        {
            UE_LOG(LogTemp, Warning, TEXT("Strand.RepairUserData: no Unreal Editor instance."));
            return;
        }
        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!IsValid(World) || World->WorldType != EWorldType::Editor)
        {
            UE_LOG(LogTemp, Warning, TEXT("Strand.RepairUserData: no active editor world."));
            return;
        }
        const FStrandUserDataAudit Before = AuditWorld(World);
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            Count += RepairActor(*It);
        }
        UE_LOG(LogTemp, Display,
            TEXT("Strand.RepairUserData: repaired %d / %d unsafe metadata references across %d scanned components. Save the level to persist repairs; 0 repairs means no unsafe entries were detected in loaded actors."),
            Count, Before.Foreign, Before.Components);
        if (Count > 0) { AuditWorld(World); }
    }

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
        // Tool icons use the SAME registered style and shutdown lifecycle as
        // the Strand Content Browser and actor icons, not a second style set.
        struct FToolBrush { const TCHAR* Key; const TCHAR* Filename; };
        const FToolBrush ToolIcons[] = {
            { TEXT("Strand.Actors"), TEXT("StrandToolActors.svg") },
            { TEXT("Strand.DropdownChevron"), TEXT("StrandToolDropdownChevron.svg") },
            { TEXT("Strand.Select"), TEXT("StrandToolSelect.svg") },
            { TEXT("Strand.BoxSelect"), TEXT("StrandToolBoxSelect.svg") },
            { TEXT("Strand.Lasso"), TEXT("StrandToolLasso.svg") },
            { TEXT("Strand.Pen"), TEXT("StrandToolPen.svg") },
            { TEXT("Strand.Move"), TEXT("StrandToolMove.svg") },
            { TEXT("Strand.Rotate"), TEXT("StrandToolRotate.svg") },
            { TEXT("Strand.Scale"), TEXT("StrandToolScale.svg") },
            { TEXT("Strand.SelectAll"), TEXT("StrandToolSelectAll.svg") },
            { TEXT("Strand.Invert"), TEXT("StrandToolInvert.svg") },
            { TEXT("Strand.ClearSelection"), TEXT("StrandToolClearSelection.svg") },
            { TEXT("Strand.World"), TEXT("StrandToolWorld.svg") },
            { TEXT("Strand.Local"), TEXT("StrandToolLocal.svg") },
            { TEXT("Strand.Selection"), TEXT("StrandToolSelection.svg") },
            { TEXT("Strand.View"), TEXT("StrandToolView.svg") },
            { TEXT("Strand.Plane"), TEXT("StrandToolPlane.svg") },
            { TEXT("Strand.Snap"), TEXT("StrandToolSnap.svg") },
            { TEXT("Strand.FrameAll"), TEXT("StrandToolFrameAll.svg") },
            { TEXT("Strand.FrameSelected"), TEXT("StrandToolFrameSelected.svg") },
            { TEXT("Strand.Points"), TEXT("StrandToolPoints.svg") },
            { TEXT("Strand.Grid"), TEXT("StrandToolGrid.svg") },
            { TEXT("Strand.ViewportSettings"), TEXT("StrandToolViewportSettings.svg") },
            { TEXT("Strand.Tangents"), TEXT("StrandToolTangents.svg") },
            { TEXT("Strand.Insert"), TEXT("StrandToolInsert.svg") },
            { TEXT("Strand.Delete"), TEXT("StrandToolDelete.svg") },
            { TEXT("Strand.Subdivide"), TEXT("StrandToolSubdivide.svg") },
            { TEXT("Strand.Smooth"), TEXT("StrandToolSmooth.svg") },
            { TEXT("Strand.Linear"), TEXT("StrandToolLinear.svg") },
            { TEXT("Strand.Reverse"), TEXT("StrandToolReverse.svg") },
            { TEXT("Strand.Loop"), TEXT("StrandToolLoop.svg") },
            { TEXT("Strand.OpenPath"), TEXT("StrandToolOpenPath.svg") },
            { TEXT("Strand.ClosedPath"), TEXT("StrandToolClosedPath.svg") },
            { TEXT("Strand.Primitives"), TEXT("StrandToolPrimitives.svg") },
            { TEXT("Strand.ReplaceShape"), TEXT("StrandToolReplaceShape.svg") },
            { TEXT("Strand.Absolute"), TEXT("StrandToolAbsolute.svg") },
            { TEXT("Strand.Relative"), TEXT("StrandToolRelative.svg") },
            { TEXT("Strand.More"), TEXT("StrandToolMore.svg") },
            { TEXT("Strand.Handles"), TEXT("StrandToolHandles.svg") },
            { TEXT("Strand.Interpolation"), TEXT("StrandToolInterpolation.svg") },
        };
        for (const FToolBrush& Icon : ToolIcons)
        {
            const FString Path = FPaths::Combine(ResourceDirectory, Icon.Filename);
            if (FPaths::FileExists(Path))
            {
                AssetStyle->Set(Icon.Key, new FSlateVectorImageBrush(Path,
                    FVector2D(28.0f, 28.0f), FLinearColor::White, ESlateBrushTileType::NoTile));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Strand: Missing authoring toolbar icon %s"), *Path);
            }
        }
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
                    if (SourceAssets.Num() == 2)
                    {
                        DynamicSection.AddMenuEntry(TEXT("Strand_JoinTwoAssets"),
                            LOCTEXT("JoinTwo", "Join Two Strand Spline Assets..."),
                            LOCTEXT("JoinTwoTip", "Create a NEW spline asset from two selected open Strand assets. "
                                "Automatically connects their nearest endpoints, welding coincident ends or "
                                "confirming a linear gap bridge. Originals and linked actors are untouched."),
                            FSlateIcon(StrandEditorStyle::StyleSetName, TEXT("ClassIcon.SplineDataAsset")),
                            FUIAction(FExecuteAction::CreateLambda([ResolveAssets]()
                            {
                                StrandAssetJoining::JoinSelectedAssets(ResolveAssets());
                            })));
                    }
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

        // Native spline conversion is useful outside the Strand asset editor.
        // Reuse the existing safe transaction/asset capture implementation.
        // An ambiguous actor with multiple spline components is rejected by
        // ConvertSelectedActors; its individual components can be captured via
        // Strand's actor Details link UI.
        if (UToolMenu* ActorMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.ActorContextMenu")))
        {
            FToolMenuSection& NativeSplineActions = ActorMenu->FindOrAddSection(TEXT("StrandNativeSplineActions"));
            NativeSplineActions.AddMenuEntry(TEXT("StrandConvertSelectedNativeSpline"),
                LOCTEXT("ConvertNativeSplineLabel", "Strand: Save Native Spline as Asset..."),
                LOCTEXT("ConvertNativeSplineTip", "Capture the selected level Actor's existing native Spline Component into a new editable Strand asset and link it without changing its points or Actor transform. For multiple spline components, capture one explicitly in Strand's actor Details panel."),
                FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ConvertSelectedActors),
                    FCanExecuteAction::CreateLambda([]()
                    {
                        for (AActor* Actor : SplineEditorCommands::SelectedActors())
                        {
                            if (IsValid(Actor) && SplineEditorCommands::FindUnambiguousSpline(Actor)) { return true; }
                        }
                        return false;
                    })));
        }

        UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        // Explicit section label makes Strand its own first-class Tools group.
        // Previously FindOrAddSection without a label visually grouped these
        // commands beneath the preceding plugin's heading.
        FToolMenuSection& ImportSection = ToolsMenu->FindOrAddSection(TEXT("StrandImport"));
        ImportSection.Label = LOCTEXT("StrandToolsCategory", "Strand");
        ImportSection.AddMenuEntry(TEXT("StrandConvertNativeSplineFromTools"),
            LOCTEXT("ConvertSelectedNativeSplineTools", "Strand: Save Selected Actor Spline as Asset..."),
            LOCTEXT("ConvertSelectedNativeSplineToolsTip", "Capture existing native Spline Components from selected level actors as new Strand Spline assets; preserves local spline geometry and Actor transforms."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateStatic(&SplineEditorCommands::ConvertSelectedActors),
                FCanExecuteAction::CreateLambda([]()
                {
                    for (AActor* Actor : SplineEditorCommands::SelectedActors())
                    {
                        if (IsValid(Actor) && SplineEditorCommands::FindUnambiguousSpline(Actor)) { return true; }
                    }
                    return false;
                })));
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
