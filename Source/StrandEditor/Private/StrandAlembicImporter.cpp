#include "SplineDataAssetFactory.h"
#include "SplineImportHelpers.h"
#include "SplineDataAsset.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Containers/StringConv.h"
#include <exception>

THIRD_PARTY_INCLUDES_START
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreFactory/All.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogStrandAlembic, Log, All);

namespace
{
    namespace Abc = Alembic::Abc;
    namespace G = Alembic::AbcGeom;

    static constexpr int32 MaxCurves = 4096;
    static constexpr int32 MaxSourceVertices = 100000;
    static constexpr int32 MaxGeneratedPoints = 8192;

    struct FReadSettings
    {
        double Scale = 1.0;
        bool bConvertYUp = true;
        double Time = 0.0;
    };

    FVector Convert(const Imath::V3f& V, const FReadSettings& Settings)
    {
        const double S = Settings.Scale;
        // Alembic is unitless. Default assumes 1 source unit = 1 cm.
        return Settings.bConvertYUp
            ? FVector(V.x * S, V.z * S, V.y * S)
            : FVector(V.x * S, V.y * S, V.z * S);
    }

    void AddLinearOrSmooth(FSplineImportedCurve& Curve, const TArray<FVector>& Vertices,
                           bool bClosed, bool bLinear)
    {
        if (Vertices.Num() < 2) { return; }
        TArray<FVector> P = Vertices;
        if (bClosed && P.Num() > 2 && P.Last().Equals(P[0], 0.0001))
        {
            P.Pop();
        }
        if (P.Num() < 2) { return; }
        Curve.bClosedLoop = bClosed && P.Num() > 2;
        Curve.Points.Reserve(P.Num());
        const FVector Offset = P[0];
        for (int32 I = 0; I < P.Num(); ++I)
        {
            FSplinePointData& Pt = Curve.Points.AddDefaulted_GetRef();
            Pt.LocalPosition = P[I] - Offset;
            Pt.PointType = bLinear ? ESplinePointType::Linear : ESplinePointType::CurveCustomTangent;
        }
        if (bLinear) { return; }
        for (int32 I = 0, Count = P.Num(); I < Count; ++I)
        {
            const FVector Before = P[I ? I - 1 : (Curve.bClosedLoop ? Count - 1 : 0)];
            const FVector After = P[I + 1 < Count ? I + 1 : (Curve.bClosedLoop ? 0 : Count - 1)];
            const FVector Direction = (After - Before) * ((Curve.bClosedLoop || (I > 0 && I < Count - 1)) ? 0.5 : 1.0);
            Curve.Points[I].ArriveTangent = Direction;
            Curve.Points[I].LeaveTangent = Direction;
        }
    }

    bool ReadBezier(FSplineImportedCurve& Curve, const TArray<FVector>& CV, bool bClosed)
    {
        // Alembic's ordinary cubic Bezier curves encode anchor / two handles / anchor.
        // For a periodic curve the final anchor is implicit (3N control points).
        const int32 N = CV.Num();
        const bool bPeriodicShape = bClosed && N >= 6 && N % 3 == 0;
        if ((!bPeriodicShape && (N < 4 || (N - 1) % 3 != 0))) { return false; }
        const int32 Segments = bPeriodicShape ? N / 3 : (N - 1) / 3;
        const int32 Anchors = bPeriodicShape ? Segments : Segments + 1;
        if (Anchors < 2) { return false; }
        Curve.bClosedLoop = bPeriodicShape;
        Curve.Points.SetNum(Anchors);
        const FVector Origin = CV[0];
        for (int32 I = 0; I < Anchors; ++I)
        {
            Curve.Points[I].LocalPosition = CV[I * 3] - Origin;
            Curve.Points[I].PointType = ESplinePointType::CurveCustomTangent;
        }
        for (int32 S = 0; S < Segments; ++S)
        {
            const int32 Next = (S + 1) % Anchors;
            Curve.Points[S].LeaveTangent = (CV[3 * S + 1] - CV[3 * S]) * 3.0;
            Curve.Points[Next].ArriveTangent = (CV[(3 * S + 3) % N] - CV[3 * S + 2]) * 3.0;
        }
        if (!bPeriodicShape && bClosed && Curve.Points.Num() > 2 &&
            Curve.Points.Last().LocalPosition.Equals(Curve.Points[0].LocalPosition, 0.0001))
        {
            const FSplinePointData ClosingAnchor = Curve.Points.Pop();
            Curve.Points[0].ArriveTangent = ClosingAnchor.ArriveTangent;
            Curve.bClosedLoop = true;
        }
        if (!Curve.bClosedLoop)
        {
            // Native splines allow independent incoming/outgoing handles at each anchor.
            Curve.Points[0].ArriveTangent = Curve.Points[0].LeaveTangent;
            Curve.Points.Last().LeaveTangent = Curve.Points.Last().ArriveTangent;
        }
        return true;
    }

    // Samples a cubic uniform B-spline using its actual basis, rather than using
    // control vertices as interpolated points. Unreal stores the result as Hermite.
    void SampleCubicBSpline(const TArray<FVector>& CV, bool bClosed, TArray<FVector>& Out)
    {
        const int32 N = CV.Num();
        if (N < 4) { return; }
        const int32 Segments = bClosed ? N : N - 3;
        constexpr int32 SamplesPerSpan = 8;
        Out.Reserve(Segments * SamplesPerSpan + 1);
        for (int32 I = 0; I < Segments; ++I)
        {
            auto P = [&](int32 J) { return CV[bClosed ? (J + N) % N : FMath::Clamp(J, 0, N - 1)]; };
            for (int32 S = 0; S < SamplesPerSpan; ++S)
            {
                const double T = double(S) / SamplesPerSpan;
                const double T2 = T * T, T3 = T2 * T;
                Out.Add((P(I) * (1 - 3*T + 3*T2 - T3)
                       + P(I+1) * (4 - 6*T2 + 3*T3)
                       + P(I+2) * (1 + 3*T + 3*T2 - 3*T3)
                       + P(I+3) * T3) / 6.0);
            }
        }
        if (!bClosed)
        {
            const int32 I = Segments - 1;
            Out.Add((CV[I+1] + CV[I+2] * 4.0 + CV[I+3]) / 6.0);
        }
    }

    struct FWeighted { double X = 0, Y = 0, Z = 0, W = 1; };

    /** Evaluate one rational NURBS curve with its individual knot vector and weights. */
    bool SampleVariableOrder(const TArray<FVector>& CV, const TArray<double>& InputKnots,
                             const TArray<double>& Weights, int32 Order, TArray<FVector>& Samples)
    {
        const int32 N = CV.Num(), Degree = Order - 1;
        if (Order < 2 || Order > 16 || N < Order || N > MaxSourceVertices) { return false; }
        TArray<double> Knots = InputKnots;
        // Some exporters omit the redundant exterior knot pair.
        if (Knots.Num() == N + Order - 2 && !Knots.IsEmpty())
        {
            Knots.Insert(Knots[0], 0);
            Knots.Add(Knots.Last());
        }
        if (Knots.Num() != N + Order) { return false; }
        for (int32 I = 1; I < Knots.Num(); ++I)
        {
            if (!FMath::IsFinite(Knots[I]) || Knots[I] < Knots[I-1]) { return false; }
        }
        const double Min = Knots[Degree], Max = Knots[N];
        if (!(Max > Min)) { return false; }
        int32 Intervals = 0;
        for (int32 I = Degree; I < N; ++I) { Intervals += (Knots[I+1] > Knots[I]); }
        const int32 Count = FMath::Clamp(Intervals * 12 + 1, 2, MaxGeneratedPoints);
        for (int32 K = 0; K < Count; ++K)
        {
            const double T = Min + (Max - Min) * double(K) / (Count - 1);
            int32 Span = N - 1;
            for (int32 I = Degree; I < N && T < Max; ++I)
            {
                if (T >= Knots[I] && T < Knots[I+1]) { Span = I; break; }
            }
            TArray<FWeighted> D;
            D.Reserve(Order);
            for (int32 J = 0; J <= Degree; ++J)
            {
                const int32 At = Span - Degree + J;
                const double W = Weights.IsValidIndex(At) ? Weights[At] : 1.0;
                if (!FMath::IsFinite(W)) { return false; }
                D.Add({CV[At].X * W, CV[At].Y * W, CV[At].Z * W, W});
            }
            for (int32 R = 1; R <= Degree; ++R)
            {
                for (int32 J = Degree; J >= R; --J)
                {
                    const int32 I = Span - Degree + J;
                    const double Den = Knots[I + Degree - R + 1] - Knots[I];
                    const double A = Den > 1e-12 ? (T - Knots[I]) / Den : 0.0;
                    const FWeighted& P = D[J-1];
                    FWeighted& Q = D[J];
                    Q = {FMath::Lerp(P.X, Q.X, A), FMath::Lerp(P.Y, Q.Y, A),
                         FMath::Lerp(P.Z, Q.Z, A), FMath::Lerp(P.W, Q.W, A)};
                }
            }
            const FWeighted& P = D[Degree];
            if (FMath::Abs(P.W) < 1e-12) { return false; }
            const FVector V(P.X/P.W, P.Y/P.W, P.Z/P.W);
            if (V.ContainsNaN()) { return false; }
            if (Samples.IsEmpty() || !Samples.Last().Equals(V, 0.00001)) { Samples.Add(V); }
        }
        return Samples.Num() >= 2;
    }

    bool ParseSchema(const G::ICurves& Curves, const FString& ObjectName,
                     const FReadSettings& Settings, TArray<FSplineImportedCurve>& Out)
    {
        G::ICurvesSchema::Sample Sample;
        const Abc::ISampleSelector Selector(Settings.Time);
        Curves.getSchema().get(Sample, Selector);
        const auto Positions = Sample.getPositions();
        const auto Counts = Sample.getCurvesNumVertices();
        if (!Positions || !Counts || Positions->size() > MaxSourceVertices * MaxCurves ||
            Counts->size() > MaxCurves)
        {
            UE_LOG(LogStrandAlembic, Warning, TEXT("Invalid/oversized ICurves data in %s"), *ObjectName);
            return false;
        }
        const auto AllKnots = Sample.getKnots();
        const auto AllOrders = Sample.getOrders();
        const auto AllWeights = Sample.getPositionWeights();
        size_t Offset = 0, KnotOffset = 0;
        for (size_t CurveIndex = 0; CurveIndex < Counts->size(); ++CurveIndex)
        {
            const int32 N = (*Counts)[CurveIndex];
            if (N < 2 || N > MaxSourceVertices || Offset + N > Positions->size())
            {
                UE_LOG(LogStrandAlembic, Warning, TEXT("Skipping malformed curve %llu in %s"),
                    static_cast<unsigned long long>(CurveIndex), *ObjectName);
                return false; // Cannot safely resynchronize offsets after a malformed count.
            }
            TArray<FVector> CV;
            CV.Reserve(N);
            for (int32 I = 0; I < N; ++I) { CV.Add(Convert((*Positions)[Offset+I], Settings)); }
            FSplineImportedCurve Curve;
            Curve.Name = ObjectName + FString::Printf(TEXT("_%04d"), int32(CurveIndex));
            Curve.SourceId = ObjectName + FString::Printf(TEXT("#%d"), int32(CurveIndex));
            const bool bClosed = Sample.getWrap() == G::kPeriodic;
            bool bSupported = true;
            if (Sample.getType() == G::kLinear)
            {
                AddLinearOrSmooth(Curve, CV, bClosed, true);
            }
            else if (Sample.getType() == G::kCubic && Sample.getBasis() == G::kBezierBasis)
            {
                bSupported = ReadBezier(Curve, CV, bClosed);
            }
            else if (Sample.getType() == G::kCubic && Sample.getBasis() == G::kCatmullromBasis)
            {
                AddLinearOrSmooth(Curve, CV, bClosed, false);
            }
            else if (Sample.getType() == G::kCubic && Sample.getBasis() == G::kBsplineBasis)
            {
                TArray<FVector> Evaluated;
                SampleCubicBSpline(CV, bClosed, Evaluated);
                AddLinearOrSmooth(Curve, Evaluated, bClosed, false);
                UE_LOG(LogStrandAlembic, Display, TEXT("Approximated B-spline curve %s with sampled Hermite points"), *Curve.SourceId);
            }
            else if (Sample.getType() == G::kVariableOrder && AllOrders && CurveIndex < AllOrders->size() && AllKnots)
            {
                const int32 Order = (*AllOrders)[CurveIndex];
                // Determine whole-file knot layout before consuming any individual curve.
                // Mixed or ambiguous knot layouts are refused, never silently shifted.
                size_t FullTotal = 0, TrimmedTotal = 0;
                for (size_t C = 0; C < Counts->size() && C < AllOrders->size(); ++C)
                {
                    FullTotal += (*Counts)[C] + (*AllOrders)[C];
                    TrimmedTotal += (*Counts)[C] + (*AllOrders)[C] - 2;
                }
                const bool bFull = AllOrders->size() == Counts->size() && AllKnots->size() == FullTotal;
                const bool bTrimmed = AllOrders->size() == Counts->size() && AllKnots->size() == TrimmedTotal;
                const size_t ThisCount = bFull ? size_t(N + Order) :
                    (bTrimmed ? size_t(N + Order - 2) : 0);
                TArray<double> Knots, Weights;
                for (size_t I = 0; I < ThisCount; ++I) { Knots.Add((*AllKnots)[KnotOffset+I]); }
                KnotOffset += ThisCount;
                if (AllWeights && AllWeights->size() >= Offset + N)
                {
                    for (int32 I = 0; I < N; ++I) { Weights.Add((*AllWeights)[Offset+I]); }
                }
                TArray<FVector> Evaluated;
                bSupported = ThisCount > 0 && SampleVariableOrder(CV, Knots, Weights, Order, Evaluated);
                if (bSupported) { AddLinearOrSmooth(Curve, Evaluated, bClosed, false); }
                UE_LOG(LogStrandAlembic, Display, TEXT("NURBS %s evaluated as sampled Unreal Hermite spline"), *Curve.SourceId);
            }
            else
            {
                bSupported = false; // Do not silently reinterpret a non-equivalent interpolation basis.
            }
            if (!bSupported || Curve.Points.Num() < 2)
            {
                UE_LOG(LogStrandAlembic, Warning, TEXT("Unsupported or malformed interpolation in %s. Curve left out."), *Curve.SourceId);
            }
            else { Out.Add(MoveTemp(Curve)); }
            Offset += N;
        }
        return !Out.IsEmpty();
    }

    void Walk(const Abc::IObject& Object, const FReadSettings& Settings,
              TArray<FSplineImportedCurve>& Out)
    {
        if (!Object.valid() || Out.Num() >= MaxCurves) { return; }
        if (G::ICurves::matches(Object.getHeader()))
        {
            try
            {
                const G::ICurves Curves(Object, G::kWrapExisting);
                const FString Name = UTF8_TO_TCHAR(Object.getFullName().c_str());
                ParseSchema(Curves, Name, Settings, Out);
            }
            catch (const std::exception& E)
            {
                UE_LOG(LogStrandAlembic, Warning, TEXT("Cannot read Alembic curves: %s"), UTF8_TO_TCHAR(E.what()));
            }
        }
        for (size_t I = 0; I < Object.getNumChildren() && Out.Num() < MaxCurves; ++I)
        {
            Walk(Object.getChild(I), Settings, Out);
        }
    }
}

bool SplineAssetImport::LoadAlembicCurves(const FString& Filename,
    const USplineDataAsset* AssetSettings, TArray<FSplineImportedCurve>& Curves)
{
    const int64 Size = IFileManager::Get().FileSize(*Filename);
    if (Size < 0 || Size > (int64(1024) * 1024 * 1024))
    {
        UE_LOG(LogStrandAlembic, Error, TEXT("Alembic file unreadable or exceeds 1 GiB safety limit: %s"), *Filename);
        return false;
    }
    FReadSettings Settings;
#if WITH_EDITORONLY_DATA
    if (AssetSettings)
    {
        Settings.Scale = AssetSettings->SourceUnitsToCentimeters;
        Settings.bConvertYUp = AssetSettings->bAlembicYUp;
        Settings.Time = AssetSettings->ImportSampleSeconds;
    }
#endif
    try
    {
        Alembic::AbcCoreFactory::IFactory Factory;
        const FTCHARToUTF8 Path(*Filename);
        const Abc::IArchive Archive = Factory.getArchive(Path.Get());
        if (!Archive.valid()) { return false; }
        Walk(Archive.getTop(), Settings, Curves);
    }
    catch (const std::exception& E)
    {
        UE_LOG(LogStrandAlembic, Error, TEXT("Alembic read failed (%s): %s"), *Filename, UTF8_TO_TCHAR(E.what()));
        return false;
    }
    return !Curves.IsEmpty();
}

UObject* UStrandAlembicFactory::FactoryCreateFile(UClass* InClass, UObject* InParent,
    FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms,
    FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;
    TArray<FSplineImportedCurve> Curves;
    if (!SplineAssetImport::LoadAlembicCurves(Filename, nullptr, Curves)) { return nullptr; }
    return SplineAssetImport::CommitCurves(InParent, InName, Flags, Curves, Filename);
}
