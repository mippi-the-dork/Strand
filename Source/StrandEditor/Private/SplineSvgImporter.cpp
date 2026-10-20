#include "SplineDataAssetFactory.h"
#include "SplineImportHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "XmlFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSplineSvg, Log, All);

namespace
{
    struct FNumScanner
    {
        FString Source;
        int32 At = 0;
        explicit FNumScanner(const FString& Text) : Source(Text) {}
        void Skip()
        {
            while (At < Source.Len() && (FChar::IsWhitespace(Source[At]) || Source[At] == ',')) { ++At; }
        }
        bool HasNumber()
        {
            Skip();
            return At < Source.Len() &&
                   (FChar::IsDigit(Source[At]) || Source[At] == '+' || Source[At] == '-' || Source[At] == '.');
        }
        bool Number(double& Result)
        {
            if (!HasNumber()) { return false; }
            const int32 Start = At;
            if (Source[At] == '-' || Source[At] == '+') { ++At; }
            bool HasDigits = false;
            while (At < Source.Len() && FChar::IsDigit(Source[At])) { HasDigits = true; ++At; }
            if (At < Source.Len() && Source[At] == '.')
            {
                ++At;
                while (At < Source.Len() && FChar::IsDigit(Source[At])) { HasDigits = true; ++At; }
            }
            if (!HasDigits) { At = Start; return false; }
            if (At < Source.Len() && (Source[At] == 'e' || Source[At] == 'E'))
            {
                const int32 ExponentStart = At++;
                if (At < Source.Len() && (Source[At] == '+' || Source[At] == '-')) { ++At; }
                const int32 DigitsAt = At;
                while (At < Source.Len() && FChar::IsDigit(Source[At])) { ++At; }
                if (DigitsAt == At) { At = ExponentStart; }
            }
            Result = FCString::Atod(*Source.Mid(Start, At - Start));
            return FMath::IsFinite(Result);
        }
        bool Pair(FVector2D& V)
        {
            double X, Y;
            if (!Number(X) || !Number(Y)) { return false; }
            V = FVector2D(X, Y);
            return true;
        }
    };

    struct FAffine
    {
        double A = 1, B = 0, C = 0, D = 1, E = 0, F = 0;
        FVector2D Apply(FVector2D P) const { return FVector2D(A * P.X + C * P.Y + E, B * P.X + D * P.Y + F); }
        FAffine operator*(const FAffine& R) const
        {
            return {A * R.A + C * R.B, B * R.A + D * R.B,
                    A * R.C + C * R.D, B * R.C + D * R.D,
                    A * R.E + C * R.F + E, B * R.E + D * R.F + F};
        }
    };

    FAffine ReadTransform(const FString& Value)
    {
        FAffine Total;
        int32 At = 0;
        while (At < Value.Len())
        {
            while (At < Value.Len() && !FChar::IsAlpha(Value[At])) { ++At; }
            const int32 Begin = At;
            while (At < Value.Len() && FChar::IsAlpha(Value[At])) { ++At; }
            if (Begin == At) { break; }
            const FString Name = Value.Mid(Begin, At - Begin).ToLower();
            while (At < Value.Len() && FChar::IsWhitespace(Value[At])) { ++At; }
            if (At == Value.Len() || Value[At++] != '(') { break; }
            const int32 ParamsStart = At;
            while (At < Value.Len() && Value[At] != ')') { ++At; }
            if (At == Value.Len()) { break; }
            FNumScanner Scanner(Value.Mid(ParamsStart, At - ParamsStart));
            ++At;
            TArray<double> V;
            double N;
            while (Scanner.HasNumber() && V.Num() < 6 && Scanner.Number(N)) { V.Add(N); }
            FAffine M;
            if (Name == TEXT("matrix") && V.Num() == 6) { M = {V[0], V[1], V[2], V[3], V[4], V[5]}; }
            else if (Name == TEXT("translate") && V.Num() >= 1)
            { M.E = V[0]; M.F = V.Num() > 1 ? V[1] : 0; }
            else if (Name == TEXT("scale") && V.Num() >= 1)
            { M.A = V[0]; M.D = V.Num() > 1 ? V[1] : V[0]; }
            else if (Name == TEXT("rotate") && V.Num() >= 1)
            {
                const double Angle = FMath::DegreesToRadians(V[0]);
                const double S = FMath::Sin(Angle), C = FMath::Cos(Angle);
                FAffine R = {C, S, -S, C, 0, 0};
                if (V.Num() == 3)
                {
                    FAffine T, Inv;
                    T.E = V[1]; T.F = V[2]; Inv.E = -V[1]; Inv.F = -V[2];
                    R = T * R * Inv;
                }
                M = R;
            }
            else if (Name == TEXT("skewx") && V.Num() == 1) { M.C = FMath::Tan(FMath::DegreesToRadians(V[0])); }
            else if (Name == TEXT("skewy") && V.Num() == 1) { M.B = FMath::Tan(FMath::DegreesToRadians(V[0])); }
            Total = Total * M;
        }
        return Total;
    }

    struct FContour
    {
        FAffine Transform;
        FString Name;
        FVector2D Current = FVector2D::ZeroVector;
        FVector2D Begin = FVector2D::ZeroVector;
        FVector2D CubicControl = FVector2D::ZeroVector;
        FVector2D QuadControl = FVector2D::ZeroVector;
        bool bCubic = false, bQuad = false;
        bool bClosed = false;
        TArray<FSplinePointData> Points;

        FVector Position(FVector2D P) const
        {
            const FVector2D Q = Transform.Apply(P);
            // SVG is y-down. Convert to local right-handed XY (z-up).
            return FVector(Q.X, -Q.Y, 0.0);
        }
        void BeginAt(FVector2D P)
        {
            Begin = Current = P;
            Points.Reset(); bClosed = false; bCubic = bQuad = false;
            FSplinePointData& Pt = Points.AddDefaulted_GetRef();
            Pt.LocalPosition = Position(P);
            Pt.PointType = ESplinePointType::Linear;
        }
        void Line(FVector2D End)
        {
            if (Points.IsEmpty()) { BeginAt(End); return; }
            Points.Last().PointType = ESplinePointType::Linear;
            FSplinePointData& Pt = Points.AddDefaulted_GetRef();
            Pt.LocalPosition = Position(End);
            Pt.PointType = ESplinePointType::Linear;
            Current = End;
            bCubic = bQuad = false;
        }
        void Cubic(FVector2D C1, FVector2D C2, FVector2D End)
        {
            if (Points.IsEmpty()) { BeginAt(End); return; }
            const FVector P0 = Points.Last().LocalPosition;
            const FVector EndPosition = Position(End);
            Points.Last().LeaveTangent = 3.0 * (Position(C1) - P0);
            Points.Last().PointType = ESplinePointType::CurveCustomTangent;
            FSplinePointData& Pt = Points.AddDefaulted_GetRef();
            Pt.LocalPosition = EndPosition;
            Pt.ArriveTangent = 3.0 * (EndPosition - Position(C2));
            Pt.PointType = ESplinePointType::CurveCustomTangent;
            CubicControl = C2;
            Current = End;
            bCubic = true; bQuad = false;
        }
        void Quadratic(FVector2D Q, FVector2D End)
        {
            const FVector2D C1 = Current + (Q - Current) * (2.0 / 3.0);
            const FVector2D C2 = End + (Q - End) * (2.0 / 3.0);
            Cubic(C1, C2, End);
            QuadControl = Q;
            bQuad = true; bCubic = false;
        }
        void Close()
        {
            if (Points.Num() < 2) { return; }
            if (!Current.Equals(Begin, 0.0001)) { Line(Begin); }
            if (Points.Num() > 2 && Points.Last().LocalPosition.Equals(Points[0].LocalPosition, 0.001))
            {
                const FSplinePointData Last = Points.Pop(EAllowShrinking::No);
                Points[0].ArriveTangent = Last.ArriveTangent;
            }
            bClosed = Points.Num() > 2;
            Current = Begin;
            bCubic = bQuad = false;
        }
        void Emit(TArray<FSplineImportedCurve>& Out)
        {
            if (Points.Num() >= 2 && Points.Num() <= 100000)
            {
                FSplineImportedCurve& Curve = Out.AddDefaulted_GetRef();
                Curve.Name = Name;
                Curve.Points = MoveTemp(Points);
                Curve.bClosedLoop = bClosed;
            }
            Points.Reset(); bClosed = false;
        }
    };

    // SVG elliptical arcs are represented as cubic Béziers in segments <= 90 degrees.
    void Arc(FContour& Contour, double Rx, double Ry, double Degrees,
             bool Large, bool Sweep, FVector2D End)
    {
        FVector2D Start = Contour.Current;
        Rx = FMath::Abs(Rx); Ry = FMath::Abs(Ry);
        if (Rx < 1e-8 || Ry < 1e-8 || Start.Equals(End, 1e-8))
        { Contour.Line(End); return; }
        const double Phi = FMath::DegreesToRadians(FMath::Fmod(Degrees, 360.0));
        const double Co = FMath::Cos(Phi), Si = FMath::Sin(Phi);
        const double Dx = (Start.X - End.X) * 0.5, Dy = (Start.Y - End.Y) * 0.5;
        const double X1 = Co * Dx + Si * Dy, Y1 = -Si * Dx + Co * Dy;
        const double Lambda = (X1 * X1) / (Rx * Rx) + (Y1 * Y1) / (Ry * Ry);
        if (Lambda > 1) { const double S = FMath::Sqrt(Lambda); Rx *= S; Ry *= S; }
        const double Rxsq = Rx * Rx, Rysq = Ry * Ry;
        const double Numerator = FMath::Max(0.0, Rxsq * Rysq - Rxsq * Y1 * Y1 - Rysq * X1 * X1);
        const double Denominator = Rxsq * Y1 * Y1 + Rysq * X1 * X1;
        if (Denominator < 1e-12) { Contour.Line(End); return; }
        const double Sign = Large == Sweep ? -1.0 : 1.0;
        const double S = Sign * FMath::Sqrt(Numerator / Denominator);
        const double CXp = S * Rx * Y1 / Ry, CYp = S * -Ry * X1 / Rx;
        const FVector2D Center(Co * CXp - Si * CYp + (Start.X + End.X) * 0.5,
                               Si * CXp + Co * CYp + (Start.Y + End.Y) * 0.5);
        const FVector2D U((X1 - CXp) / Rx, (Y1 - CYp) / Ry);
        const FVector2D V((-X1 - CXp) / Rx, (-Y1 - CYp) / Ry);
        const double StartAngle = FMath::Atan2(U.Y, U.X);
        double SweepAngle = FMath::Atan2(U.X * V.Y - U.Y * V.X, U.X * V.X + U.Y * V.Y);
        if (!Sweep && SweepAngle > 0) { SweepAngle -= 2 * PI; }
        if (Sweep && SweepAngle < 0) { SweepAngle += 2 * PI; }
        const int32 Pieces = FMath::Max(1, FMath::CeilToInt(FMath::Abs(SweepAngle) / (PI / 2)));
        auto Place = [&](FVector2D Unit) -> FVector2D
        {
            return Center + FVector2D(Co * Rx * Unit.X - Si * Ry * Unit.Y,
                                       Si * Rx * Unit.X + Co * Ry * Unit.Y);
        };
        for (int32 I = 0; I < Pieces; ++I)
        {
            const double A = StartAngle + SweepAngle * I / Pieces;
            const double B = StartAngle + SweepAngle * (I + 1) / Pieces;
            const double K = (4.0 / 3.0) * FMath::Tan((B - A) * 0.25);
            const FVector2D AUnit(FMath::Cos(A), FMath::Sin(A));
            const FVector2D BUnit(FMath::Cos(B), FMath::Sin(B));
            const FVector2D C1 = Place(AUnit + FVector2D(-AUnit.Y, AUnit.X) * K);
            const FVector2D C2 = Place(BUnit - FVector2D(-BUnit.Y, BUnit.X) * K);
            Contour.Cubic(C1, C2, I + 1 == Pieces ? End : Place(BUnit));
        }
    }

    bool PathToCurves(const FString& Text, const FString& Id, FAffine Matrix,
                      TArray<FSplineImportedCurve>& Out)
    {
        FNumScanner S(Text);
        FContour Shape; Shape.Transform = Matrix; Shape.Name = Id;
        TCHAR Cmd = 0;
        int32 CurveIndex = 0;
        int32 Guard = 0;
        while (S.At < Text.Len() && Guard++ < 2000000)
        {
            S.Skip();
            if (S.At >= Text.Len()) { break; }
            const TCHAR First = Text[S.At];
            if (FChar::IsAlpha(First)) { Cmd = First; ++S.At; }
            if (!Cmd) { return false; }
            const TCHAR Upper = FChar::ToUpper(Cmd);
            const bool bRelative = FChar::IsLower(Cmd);
            if (Upper == 'Z')
            {
                Shape.Close(); Shape.Name = Id + FString::Printf(TEXT("_%d"), ++CurveIndex);
                Shape.Emit(Out); Cmd = 0; continue;
            }
            const int32 BeforeArgs = S.At;
            FVector2D P;
            auto ReadEnd = [&](FVector2D& Result) -> bool
            {
                if (!S.Pair(Result)) { return false; }
                if (bRelative) { Result += Shape.Current; }
                return true;
            };
            if (Upper == 'M')
            {
                if (!ReadEnd(P)) { return false; }
                if (!Shape.Points.IsEmpty())
                {
                    Shape.Name = Id + FString::Printf(TEXT("_%d"), ++CurveIndex);
                    Shape.Emit(Out);
                }
                Shape.BeginAt(P);
                Cmd = bRelative ? 'l' : 'L'; // Subsequent coordinate pairs are lineto.
            }
            else if (Upper == 'L')
            {
                if (!ReadEnd(P)) { return false; }
                Shape.Line(P);
            }
            else if (Upper == 'H' || Upper == 'V')
            {
                double N;
                if (!S.Number(N)) { return false; }
                P = Shape.Current;
                if (Upper == 'H') { P.X = bRelative ? P.X + N : N; }
                else { P.Y = bRelative ? P.Y + N : N; }
                Shape.Line(P);
            }
            else if (Upper == 'C')
            {
                FVector2D C1, C2;
                if (!S.Pair(C1) || !S.Pair(C2) || !S.Pair(P)) { return false; }
                if (bRelative) { C1 += Shape.Current; C2 += Shape.Current; P += Shape.Current; }
                Shape.Cubic(C1, C2, P);
            }
            else if (Upper == 'S')
            {
                FVector2D C2;
                if (!S.Pair(C2) || !S.Pair(P)) { return false; }
                if (bRelative) { C2 += Shape.Current; P += Shape.Current; }
                const FVector2D C1 = Shape.bCubic ? 2.0 * Shape.Current - Shape.CubicControl : Shape.Current;
                Shape.Cubic(C1, C2, P);
            }
            else if (Upper == 'Q')
            {
                FVector2D Q;
                if (!S.Pair(Q) || !S.Pair(P)) { return false; }
                if (bRelative) { Q += Shape.Current; P += Shape.Current; }
                Shape.Quadratic(Q, P);
            }
            else if (Upper == 'T')
            {
                if (!ReadEnd(P)) { return false; }
                const FVector2D Q = Shape.bQuad ? 2.0 * Shape.Current - Shape.QuadControl : Shape.Current;
                Shape.Quadratic(Q, P);
            }
            else if (Upper == 'A')
            {
                double RX, RY, Rotation, Large, Sweep;
                if (!S.Number(RX) || !S.Number(RY) || !S.Number(Rotation) ||
                    !S.Number(Large) || !S.Number(Sweep) || !S.Pair(P)) { return false; }
                if (bRelative) { P += Shape.Current; }
                Arc(Shape, RX, RY, Rotation, Large != 0.0, Sweep != 0.0, P);
            }
            else { UE_LOG(LogSplineSvg, Error, TEXT("Unsupported SVG path command %c in %s"), Cmd, *Id); return false; }
            if (S.At <= BeforeArgs || Shape.Points.Num() > 100000) { return false; }
        }
        if (Guard >= 2000000) { return false; }
        Shape.Name = Id + FString::Printf(TEXT("_%d"), ++CurveIndex);
        Shape.Emit(Out);
        return true;
    }

    void ReadElements(const FXmlNode* Node, FAffine Parent,
                      TArray<FSplineImportedCurve>& Out, int32& Ordinal)
    {
        if (!Node || Out.Num() >= 4096) { return; }
        const FAffine Transform = Parent * ReadTransform(Node->GetAttribute(TEXT("transform")));
        FString Tag = Node->GetTag().ToLower();
        int32 Colon;
        if (Tag.FindLastChar(':', Colon)) { Tag = Tag.Mid(Colon + 1); }
        FString Name = Node->GetAttribute(TEXT("id"));
        if (Name.IsEmpty()) { Name = FString::Printf(TEXT("Curve_%04d"), ++Ordinal); }

        if (Tag == TEXT("path"))
        {
            TArray<FSplineImportedCurve> Shapes;
            if (!PathToCurves(Node->GetAttribute(TEXT("d")), Name, Transform, Shapes))
            {
                UE_LOG(LogSplineSvg, Warning, TEXT("Invalid path %s skipped."), *Name);
            }
            else { Out.Append(MoveTemp(Shapes)); }
        }
        else if (Tag == TEXT("line") || Tag == TEXT("polyline") || Tag == TEXT("polygon"))
        {
            TArray<FVector2D> Vectors;
            if (Tag == TEXT("line"))
            {
                auto Attribute = [&](const TCHAR* Key) { return FCString::Atod(*Node->GetAttribute(Key)); };
                Vectors.Add(FVector2D(Attribute(TEXT("x1")), Attribute(TEXT("y1"))));
                Vectors.Add(FVector2D(Attribute(TEXT("x2")), Attribute(TEXT("y2"))));
            }
            else
            {
                FNumScanner Scanner(Node->GetAttribute(TEXT("points")));
                FVector2D P;
                while (Scanner.HasNumber() && Vectors.Num() < 100000 && Scanner.Pair(P)) { Vectors.Add(P); }
            }
            if (Vectors.Num() >= 2)
            {
                FSplineImportedCurve& Curve = Out.AddDefaulted_GetRef();
                Curve.Name = Name;
                Curve.bClosedLoop = Tag == TEXT("polygon");
                if (Curve.bClosedLoop && Vectors.Num() > 2 && Vectors[0].Equals(Vectors.Last(), 1e-8))
                { Vectors.Pop(); }
                for (const FVector2D& P : Vectors)
                {
                    const FVector2D A = Transform.Apply(P);
                    FSplinePointData& Point = Curve.Points.AddDefaulted_GetRef();
                    Point.LocalPosition = FVector(A.X, -A.Y, 0.0);
                    Point.PointType = ESplinePointType::Linear;
                }
            }
        }
        for (const FXmlNode* Child : Node->GetChildrenNodes())
        {
            ReadElements(Child, Transform, Out, Ordinal);
        }
    }
}

bool SplineAssetImport::LoadSvgCurves(const FString& Filename, TArray<FSplineImportedCurve>& Curves)
{
    if (IFileManager::Get().FileSize(*Filename) > 20 * 1024 * 1024)
    {
        UE_LOG(LogSplineSvg, Error, TEXT("SVG exceeds 20 MiB import limit: %s"), *Filename);
        return false;
    }
    FString XML;
    if (!FFileHelper::LoadFileToString(XML, *Filename)) { return false; }
    FXmlFile Document(XML, EConstructMethod::ConstructFromBuffer);
    if (!Document.IsValid() || !Document.GetRootNode())
    {
        UE_LOG(LogSplineSvg, Error, TEXT("Invalid SVG XML %s: %s"), *Filename, *Document.GetLastError());
        return false;
    }
    int32 Ordinal = 0;
    ReadElements(Document.GetRootNode(), FAffine(), Curves, Ordinal);
    for (int32 I = 0; I < Curves.Num(); ++I)
    {
        // Numeric order handles duplicate SVG IDs without collapsing source curves.
        Curves[I].SourceId = Curves[I].Name + FString::Printf(TEXT("#%d"), I);
    }
    return !Curves.IsEmpty();
}

UObject* USplineSvgFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
    EObjectFlags Flags, const FString& Filename, const TCHAR* Parms,
    FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
    bOutOperationCanceled = false;
    TArray<FSplineImportedCurve> Curves;
    if (!SplineAssetImport::LoadSvgCurves(Filename, Curves)) { return nullptr; }
    return SplineAssetImport::CommitCurves(InParent, InName, Flags, Curves, Filename);
}
