#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"
#include "SplineDataAssetFactory.generated.h"

UCLASS()
class USplineDataAssetFactory : public UFactory
{
    GENERATED_BODY()
public:
    USplineDataAssetFactory();
    virtual uint32 GetMenuCategories() const override;
    virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName,
        EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class USplineSvgFactory : public UFactory
{
    GENERATED_BODY()
public:
    USplineSvgFactory();
    virtual bool FactoryCanImport(const FString& Filename) override;
    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
        EObjectFlags Flags, const FString& Filename, const TCHAR* Parms,
        FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

/**
 * Interactive Alembic import selector for editor file-drop and Import workflows.
 * Only routes interactive .abc imports; explicitly selected Strand and automated
 * import factories bypass the chooser.
 */
UCLASS()
class UStrandAlembicImportRouterFactory : public UFactory
{
    GENERATED_BODY()
public:
    UStrandAlembicImportRouterFactory();
    // Pure routing decision, independent of the editor's current import context.
    // Exposed for deterministic tests of interactive versus automated imports.
    static bool ShouldRouteAlembic(const FString& Filename, bool bAutomatedImport);
    virtual bool FactoryCanImport(const FString& Filename) override;
    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
        EObjectFlags Flags, const FString& Filename, const TCHAR* Parms,
        FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

/** Dedicated curve-targeted Alembic factory; low priority so native mesh/groom importers retain .abc ownership. */
UCLASS()
class UStrandAlembicFactory : public UFactory
{
    GENERATED_BODY()
public:
    UStrandAlembicFactory();
    virtual bool FactoryCanImport(const FString& Filename) override;
    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
        EObjectFlags Flags, const FString& Filename, const TCHAR* Parms,
        FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
};

/** Source-aware, transactional per-curve reimport for SVG and Alembic assets. */
UCLASS()
class UStrandReimportFactory : public UFactory, public FReimportHandler
{
    GENERATED_BODY()
public:
    UStrandReimportFactory();
    virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
    virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
    virtual EReimportResult::Type Reimport(UObject* Obj) override;
    virtual int32 GetPriority() const override { return 250; }
};
