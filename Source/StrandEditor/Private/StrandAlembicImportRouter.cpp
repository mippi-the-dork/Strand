#include "SplineDataAssetFactory.h"
#include "SplineDataAsset.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "StrandAlembicImportRouter"
DEFINE_LOG_CATEGORY_STATIC(LogStrandAlembicRoute, Log, All);

namespace
{
    enum class EImportChoice : uint8 { Cancel, Strand, Unreal };

    EImportChoice AskHowToImportAlembic(const FString& Filename)
    {
        if (!FSlateApplication::IsInitialized())
        {
            // Never assume an unattended environment can display a modal.
            return EImportChoice::Cancel;
        }
        EImportChoice Choice = EImportChoice::Cancel;
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT("SelectTitle", "Import Alembic"))
            .SizingRule(ESizingRule::Autosized)
            .SupportsMinimize(false)
            .SupportsMaximize(false);
        Window->SetContent(
            SNew(SBorder).Padding(18)
            [
                SNew(SBox).WidthOverride(520)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                    [SNew(STextBlock)
                        .Text(FText::Format(LOCTEXT("Intro", "How would you like to import {0}?"),
                            FText::FromString(FPaths::GetCleanFilename(Filename))))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 15)
                    [SNew(STextBlock).AutoWrapText(true)
                        .Text(LOCTEXT("Explanation", "Strand creates editable native spline data assets from Alembic curves. Unreal's Alembic importer handles its normal mesh and geometry cache workflows. Choose one importer for this file."))]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                        [SNew(SButton)
                            .Text(LOCTEXT("Cancel", "Cancel"))
                            .OnClicked_Lambda([&Choice, Window]() { Choice = EImportChoice::Cancel; Window->RequestDestroyWindow(); return FReply::Handled(); })]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
                        [SNew(SButton)
                            .Text(LOCTEXT("UnrealDefault", "Unreal Alembic Importer"))
                            .OnClicked_Lambda([&Choice, Window]() { Choice = EImportChoice::Unreal; Window->RequestDestroyWindow(); return FReply::Handled(); })]
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SButton)
                            .Text(LOCTEXT("Strand", "Strand Spline Importer"))
                            .OnClicked_Lambda([&Choice, Window]() { Choice = EImportChoice::Strand; Window->RequestDestroyWindow(); return FReply::Handled(); })]
                    ]
                ]
            ]);
        FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());
        return Choice;
    }

    /**
     * Use the installed Unreal Alembic import factory via the UFactory virtual interface.
     * Do not put a second .abc task into AssetTools: doing so while a factory is already
     * running would recursively re-enter the import picker.
     */
    UObject* ImportWithUnreal(UObject* InParent, FName InName, EObjectFlags Flags,
                             const FString& Filename, const TCHAR* Parms,
                             FFeedbackContext* Warn, bool& bOutOperationCanceled)
    {
        if (!FModuleManager::Get().LoadModule(FName(TEXT("AlembicImporter"))))
        {
            UE_LOG(LogStrandAlembicRoute, Error,
                TEXT("Could not load Unreal's AlembicImporter module. No assets were imported."));
            return nullptr;
        }
        UClass* NativeFactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/AlembicImporter.AlembicImportFactory"));
        if (!NativeFactoryClass || !NativeFactoryClass->IsChildOf(UFactory::StaticClass()))
        {
            UE_LOG(LogStrandAlembicRoute, Error,
                TEXT("Unreal's Alembic importer is unavailable. Enable the AlembicImporter plugin; no assets were imported."));
            return nullptr;
        }
        UFactory* NativeFactory = NewObject<UFactory>(GetTransientPackage(), NativeFactoryClass);
        if (!NativeFactory || !NativeFactory->FactoryCanImport(Filename))
        {
            UE_LOG(LogStrandAlembicRoute, Error,
                TEXT("Unreal's Alembic factory rejected %s. No assets were imported."), *Filename);
            return nullptr;
        }
        UClass* NativeAssetClass = NativeFactory->ResolveSupportedClass();
        return NativeFactory->FactoryCreateFile(NativeAssetClass, InParent, InName,
                                                Flags, Filename, Parms, Warn, bOutOperationCanceled);
    }
}

UStrandAlembicImportRouterFactory::UStrandAlembicImportRouterFactory()
{
    SupportedClass = USplineDataAsset::StaticClass();
    bCreateNew = false;
    bEditorImport = true;
    bText = false;
    Formats.Add(TEXT("abc;Alembic (Strand or Unreal)"));
    // Higher than the dedicated curve importer and Unreal's standard .abc importer:
    // only interactive file drop / generic Import workflows should reach this chooser.
    ImportPriority = 10000;
}

bool UStrandAlembicImportRouterFactory::ShouldRouteAlembic(
    const FString& Filename, const bool bAutomatedImport)
{
    // File type and import context are independent: tests can validate both
    // without relying on how the Automation Controller configures factories.
    return !bAutomatedImport &&
        FPaths::GetExtension(Filename).Equals(TEXT("abc"), ESearchCase::IgnoreCase);
}

bool UStrandAlembicImportRouterFactory::FactoryCanImport(const FString& Filename)
{
    // Automated imports must remain non-interactive; Unreal's standard Alembic
    // factory can handle them, while scripts can request Strand's factory explicitly.
    return ShouldRouteAlembic(Filename, IsAutomatedImport());
}

UObject* UStrandAlembicImportRouterFactory::FactoryCreateFile(
    UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
    const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn,
    bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;
    if (!ShouldRouteAlembic(Filename, IsAutomatedImport()))
    {
        // Defense in depth: an explicitly invoked router must never open a
        // modal during an unattended import, even if FactoryCanImport was skipped.
        bOutOperationCanceled = true;
        return nullptr;
    }
    const EImportChoice Choice = AskHowToImportAlembic(Filename);
    switch (Choice)
    {
    case EImportChoice::Strand:
    {
        UStrandAlembicFactory* StrandFactory = NewObject<UStrandAlembicFactory>();
        UObject* Imported = StrandFactory->FactoryCreateFile(USplineDataAsset::StaticClass(),
            InParent, InName, Flags, Filename, Parms, Warn, bOutOperationCanceled);
        if (!Imported) { bOutOperationCanceled = true; } // No silent fallback to another importer.
        return Imported;
    }
    case EImportChoice::Unreal:
    {
        UObject* Imported = ImportWithUnreal(InParent, InName, Flags, Filename,
                                               Parms, Warn, bOutOperationCanceled);
        if (!Imported) { bOutOperationCanceled = true; } // Preserve the selected path.
        return Imported;
    }
    case EImportChoice::Cancel:
    default:
        bOutOperationCanceled = true;
        return nullptr;
    }
}
#undef LOCTEXT_NAMESPACE
