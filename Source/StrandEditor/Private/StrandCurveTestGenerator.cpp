#include "StrandCurveTestGenerator.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Containers/StringConv.h"
#include <vector>
#include <exception>

THIRD_PARTY_INCLUDES_START
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreOgawa/All.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogStrandCurveFixtures, Log, All);
namespace
{
    struct FSourceCurve
    {
        FString Name;
        TArray<FVector> Positions;
    };

    TArray<FSourceCurve> BuildCurves(bool bBraided)
    {
        TArray<FSourceCurve> Curves;
        const int32 Count = bBraided ? 161 : 129;
        const int32 Paths = bBraided ? 3 : 1;
        for (int32 CurveIndex = 0; CurveIndex < Paths; ++CurveIndex)
        {
            FSourceCurve& Curve = Curves.AddDefaulted_GetRef();
            Curve.Name = bBraided ? FString::Printf(TEXT("Braid_%d"), CurveIndex + 1) : TEXT("Helix");
            const double Phase = 2.0 * UE_PI * CurveIndex / 3.0;
            for (int32 I = 0; I < Count; ++I)
            {
                const double T = double(I) / double(Count - 1);
                const double Angle = 2.0 * UE_PI * (bBraided ? 5.0 : 3.0) * T + Phase;
                const double Radius = bBraided ? 75.0 : 135.0;
                Curve.Positions.Add(FVector(Radius * FMath::Cos(Angle),
                                            Radius * FMath::Sin(Angle),
                                            (bBraided ? 650.0 : 600.0) * T));
            }
        }
        return Curves;
    }

    bool WriteAlembic(const FString& Path, const TArray<FSourceCurve>& Curves, FString& OutError)
    {
        namespace Abc = Alembic::Abc;
        namespace Geom = Alembic::AbcGeom;
        try
        {
            const FTCHARToUTF8 Utf8Filename(*Path);
            Abc::OArchive Archive(Alembic::AbcCoreOgawa::WriteArchive(), Utf8Filename.Get());
            for (const FSourceCurve& Curve : Curves)
            {
                const FTCHARToUTF8 Utf8Name(*Curve.Name);
                Geom::OCurves Object(Abc::OObject(Archive, Abc::kTop), Utf8Name.Get());
                std::vector<Imath::V3f> Positions;
                Positions.reserve(Curve.Positions.Num());
                for (const FVector& P : Curve.Positions)
                {
                    // These fixtures are authored Z-up in centimeters. Strand's
                    // Alembic default Y-up conversion can be disabled when importing.
                    Positions.emplace_back(float(P.X), float(P.Y), float(P.Z));
                }
                const int32 VertCount = Curve.Positions.Num();
                const Geom::OCurvesSchema::Sample Sample(
                    Abc::P3fArraySample(Positions.data(), Positions.size()),
                    Abc::Int32ArraySample(&VertCount, 1),
                    Geom::kLinear, Geom::kNonPeriodic);
                Object.getSchema().set(Sample);
            }
        }
        catch (const std::exception& E)
        {
            OutError = UTF8_TO_TCHAR(E.what());
            return false;
        }
        return true;
    }
}

bool StrandCurveTestGenerator::Generate(FString& OutDirectory, FString& OutError)
{
    OutDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Strand"), TEXT("CurveImportTests"));
    IFileManager::Get().MakeDirectory(*OutDirectory, true);
    for (int32 ShapeIndex = 0; ShapeIndex < 2; ++ShapeIndex)
    {
        const bool bBraid = ShapeIndex == 1;
        const FString Base = bBraid ? TEXT("TripleBraid") : TEXT("Helix");
        const TArray<FSourceCurve> Curves = BuildCurves(bBraid);
        if (!WriteAlembic(FPaths::Combine(OutDirectory, Base + TEXT(".abc")), Curves, OutError)) { return false; }
    }
    UE_LOG(LogStrandCurveFixtures, Display, TEXT("Strand generated two genuine Alembic curve fixtures at %s"), *OutDirectory);
    return true;
}
