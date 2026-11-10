#include "SplineDataAssetFactory.h"
#include "SplineImportHelpers.h"
#include "StrandProjectSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "XmlFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSplineSvg, Log, All);

namespace
{
    // FXmlFile's attribute tokenization can stop a quoted attribute at a physical
    // line break. Illustrator and other DCC exporters frequently wrap long SVG
    // path data (d="...") over several lines. That previously discarded every
    // path command after the first newline, including closing Z/z commands.
    //
    // XML 1.0 attribute normalization treats literal CR/LF/TAB as spaces, so
    // perform that normalization before handing the document to FXmlFile.
    // Keep everything outside quoted XML attributes unchanged, including
    // comments, CDATA, and text node contents.
    void NormalizeMultilineXmlAttributes(FString& Xml)
    {
        bool bInsideTag = false;
        TCHAR Quote = 0;
        const int32 Count = Xml.Len();
        for (int32 I = 0; I < Count; ++I)
        {
            if (!bInsideTag)
            {
                // Skip comments and CDATA without altering their text. Check
                // characters directly instead of allocating a substring for
                // every character in large imported SVG documents.
                if (Xml[I] == '<')
                {
                    if (I + 3 < Count && Xml[I + 1] == '!' &&
                        Xml[I + 2] == '-' && Xml[I + 3] == '-')
                    {
                        const int32 End = Xml.Find(TEXT("-->"), ESearchCase::CaseSensitive,
                            ESearchDir::FromStart, I + 4);
                        if (End == INDEX_NONE) { return; } // Malformed XML is reported below.
                        I = End + 2;
                        continue;
                    }
                    if (I + 8 < Count && Xml[I + 1] == '!' && Xml[I + 2] == '[' &&
                        Xml[I + 3] == 'C' && Xml[I + 4] == 'D' &&
                        Xml[I + 5] == 'A' && Xml[I + 6] == 'T' &&
                        Xml[I + 7] == 'A' && Xml[I + 8] == '[')
                    {
                        const int32 End = Xml.Find(TEXT("]]>"), ESearchCase::CaseSensitive,
                            ESearchDir::FromStart, I + 9);
                        if (End == INDEX_NONE) { return; }
                        I = End + 2;
                        continue;
                    }
                    bInsideTag = true;
                }
                continue;
            }
            if (Quote != 0)
            {
                if (Xml[I] == Quote)
                {
                    Quote = 0;
                }
                else if (Xml[I] == '\r' || Xml[I] == '\n' || Xml[I] == '\t')
                {
                    Xml[I] = ' ';
                }
            }
            else if (Xml[I] == '\"' || Xml[I] == '\'')
            {
                Quote = Xml[I];
            }
            else if (Xml[I] == '>')
            {
                bInsideTag = false;
            }
        }
    }

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
        // Arc flags are *single characters*. SVG permits compact notation,
        // for example "A 5 5 0 0110 10" (flags 0,1 then X=10).
        bool Flag(bool& OutFlag)
        {
            Skip();
            if (At >= Source.Len() || (Source[At] != '0' && Source[At] != '1'))
            {
                return false;
            }
            OutFlag = Source[At++] == '1';
            return true;
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

    // Each SVG segment is represented by its exact cubic Hermite equivalent.
    // We intentionally use CurveCustomTangent for both curved and straight
    // segments. A straight segment has both tangents equal to its chord; it
    // remains geometrically linear without overwriting a shared endpoint's
    // independent incoming or outgoing handle.
    struct FContour
    {
        FAffine Transform;
        bool bMirrorSvgX = true;
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
            // SVG uses downward-positive Y. Keep it on native Unreal's XY plane.
            // The standard level Top-view projection reverses X relative to SVG
            // artwork. Mirror X by default so text and asymmetric geometry read
            // correctly when viewed from above. The legacy mapping remains a
            // per-asset reimport option, for projects relying on old coordinates.
            return FVector(bMirrorSvgX ? -Q.X : Q.X, -Q.Y, 0.0);
        }
        void BeginAt(FVector2D P)
        {
            Begin = Current = P;
            Points.Reset();
            bClosed = bCubic = bQuad = false;
            FSplinePointData& Pt = Points.AddDefaulted_GetRef();
            Pt.LocalPosition = Position(P);
            Pt.PointType = ESplinePointType::CurveCustomTangent;
        }
        void Line(FVector2D End)
        {
            if (Points.IsEmpty()) { BeginAt(End); return; }
            const FVector EndPosition = Position(End);
            const FVector Chord = EndPosition - Points.Last().LocalPosition;
            Points.Last().LeaveTangent = Chord;
            FSplinePointData& Pt = Points.AddDefaulted_GetRef();
            Pt.LocalPosition = EndPosition;
            Pt.ArriveTangent = Chord;
            Pt.PointType = ESplinePointType::CurveCustomTangent;
            Current = End;
            bCubic = bQuad = false;
        }
        void Cubic(FVector2D C1, FVector2D C2, FVector2D End)
        {
            if (Points.IsEmpty()) { BeginAt(End); return; }
            const FVector StartPosition = Points.Last().LocalPosition;
            const FVector EndPosition = Position(End);
            // Hermite keys are spaced by one. Cubic Bezier derivatives at the
            // endpoints are 3 * (C1 - P0) and 3 * (P3 - C2), respectively.
            Points.Last().LeaveTangent = 3.0 * (Position(C1) - StartPosition);
            FSplinePointData& Pt = Points.AddDefaulted_GetRef();
            Pt.LocalPosition = EndPosition;
            Pt.ArriveTangent = 3.0 * (EndPosition - Position(C2));
            Pt.PointType = ESplinePointType::CurveCustomTangent;
            CubicControl = C2;
            Current = End;
            bCubic = true;
            bQuad = false;
        }
        void Quadratic(FVector2D Q, FVector2D End)
        {
            const FVector2D C1 = Current + (Q - Current) * (2.0 / 3.0);
            const FVector2D C2 = End + (Q - End) * (2.0 / 3.0);
            Cubic(C1, C2, End);
            QuadControl = Q;
            bQuad = true;
            bCubic = false;
        }
        void Close()
        {
            if (Points.Num() < 2) { return; }
            // Endpoints may match after a transform even if their source-space
            // coordinates differ slightly. Test the transformed points so the
            // choice of whether to remove a redundant point is consistent with
            // the actual geometry stored in the asset.
            const FVector FirstPosition = Points[0].LocalPosition;
            const FVector LastPosition = Points.Last().LocalPosition;
            if (LastPosition.Equals(FirstPosition, 0.00001))
            {
                // If the SVG already draws a final segment back to M, the
                // duplicate endpoint is not a new point of the native loop.
                // Preserve its incoming handle as the first point's arrival;
                // the first point's original departure must remain untouched.
                if (Points.Num() > 2)
                {
                    const FSplinePointData Repeated = Points.Pop(EAllowShrinking::No);
                    Points[0].ArriveTangent = Repeated.ArriveTangent;
                }
            }
            else
            {
                // SVG Z/z creates a STRAIGHT closing edge when the path has not
                // already reached M. Write both endpoint derivatives for that
                // edge, preserving any separate tangents for adjacent edges.
                const FVector ClosingChord = FirstPosition - LastPosition;
                Points.Last().LeaveTangent = ClosingChord;
                Points[0].ArriveTangent = ClosingChord;
            }
            bClosed = Points.Num() >= 2;
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
            Points.Reset();
            bClosed = false;
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
                      TArray<FSplineImportedCurve>& Out, bool bMirrorSvgX)
    {
        FNumScanner S(Text);
        FContour Shape; Shape.Transform = Matrix; Shape.Name = Id;
        Shape.bMirrorSvgX = bMirrorSvgX;
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
                double RX, RY, Rotation;
                bool bLarge = false, bSweep = false;
                if (!S.Number(RX) || !S.Number(RY) || !S.Number(Rotation) ||
                    !S.Flag(bLarge) || !S.Flag(bSweep) || !S.Pair(P)) { return false; }
                if (bRelative) { P += Shape.Current; }
                Arc(Shape, RX, RY, Rotation, bLarge, bSweep, P);
                // A/a does not count as a cubic for S/s control reflection.
                Shape.bCubic = Shape.bQuad = false;
            }
            else { UE_LOG(LogSplineSvg, Error, TEXT("Unsupported SVG path command %c in %s"), Cmd, *Id); return false; }
            if (S.At <= BeforeArgs || Shape.Points.Num() > 100000) { return false; }
        }
        if (Guard >= 2000000) { return false; }
        Shape.Name = Id + FString::Printf(TEXT("_%d"), ++CurveIndex);
        Shape.Emit(Out);
        return true;
    }

    FString SvgTag(const FXmlNode* Node)
    {
        FString Tag = Node->GetTag().ToLower();
        int32 Colon;
        if (Tag.FindLastChar(':', Colon)) { Tag = Tag.Mid(Colon + 1); }
        return Tag;
    }

    double SvgNumber(const FXmlNode* Node, const TCHAR* Attribute, double Fallback = 0.0)
    {
        const FString Value = Node->GetAttribute(Attribute);
        return Value.IsEmpty() ? Fallback : FCString::Atod(*Value);
    }

    void IndexIds(const FXmlNode* Node, TMap<FString, const FXmlNode*>& Ids)
    {
        if (!Node) { return; }
        const FString Id = Node->GetAttribute(TEXT("id"));
        if (!Id.IsEmpty() && !Ids.Contains(Id)) { Ids.Add(Id, Node); }
        for (const FXmlNode* Child : Node->GetChildrenNodes()) { IndexIds(Child, Ids); }
    }

    // bReferenced controls whether render-suppressed containers (<defs>, <symbol>)
    // can be visited through <use>. Never render their definitions by default.
    void ReadElements(const FXmlNode* Node, const FAffine& Parent,
                      const TMap<FString, const FXmlNode*>& Ids,
                      TSet<const FXmlNode*>& ActiveReferences,
                      TArray<FSplineImportedCurve>& Out, int32& Ordinal,
                      bool bMirrorSvgX, bool bReferenced = false, int32 Depth = 0)
    {
        if (!Node || Out.Num() >= 4096 || Depth >= 64) { return; }
        const FString Tag = SvgTag(Node);
        if ((Tag == TEXT("defs") || Tag == TEXT("symbol") ||
             Tag == TEXT("clippath") || Tag == TEXT("mask") ||
             Tag == TEXT("marker") || Tag == TEXT("pattern") ||
             Tag == TEXT("filter")) && !bReferenced)
        {
            return;
        }
        const FAffine Transform = Parent * ReadTransform(Node->GetAttribute(TEXT("transform")));
        FString Name = Node->GetAttribute(TEXT("id"));
        if (Name.IsEmpty()) { Name = FString::Printf(TEXT("Curve_%04d"), ++Ordinal); }

        if (Tag == TEXT("use"))
        {
            FString Href = Node->GetAttribute(TEXT("href"));
            if (Href.IsEmpty()) { Href = Node->GetAttribute(TEXT("xlink:href")); }
            if (!Href.StartsWith(TEXT("#")))
            {
                UE_LOG(LogSplineSvg, Warning, TEXT("Skipping unsupported external SVG <use> href: %s"), *Href);
                return;
            }
            const FXmlNode* const* Referenced = Ids.Find(Href.Mid(1));
            if (!Referenced || ActiveReferences.Contains(*Referenced))
            {
                UE_LOG(LogSplineSvg, Warning, TEXT("Unresolved or recursive SVG <use>: %s"), *Href);
                return;
            }
            FAffine Offset;
            Offset.E = SvgNumber(Node, TEXT("x"));
            Offset.F = SvgNumber(Node, TEXT("y"));
            ActiveReferences.Add(*Referenced);
            ReadElements(*Referenced, Transform * Offset, Ids, ActiveReferences,
                         Out, Ordinal, bMirrorSvgX, true, Depth + 1);
            ActiveReferences.Remove(*Referenced);
            return;
        }

        if (Tag == TEXT("path"))
        {
            TArray<FSplineImportedCurve> Shapes;
            if (!PathToCurves(Node->GetAttribute(TEXT("d")), Name, Transform, Shapes, bMirrorSvgX))
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
                Vectors.Add(FVector2D(SvgNumber(Node, TEXT("x1")), SvgNumber(Node, TEXT("y1"))));
                Vectors.Add(FVector2D(SvgNumber(Node, TEXT("x2")), SvgNumber(Node, TEXT("y2"))));
            }
            else
            {
                FNumScanner Scanner(Node->GetAttribute(TEXT("points")));
                FVector2D P;
                while (Scanner.HasNumber() && Vectors.Num() < 100000 && Scanner.Pair(P))
                {
                    Vectors.Add(P);
                }
            }
            if (Vectors.Num() >= 2)
            {
                FContour Contour;
                Contour.Name = Name;
                Contour.Transform = Transform;
                Contour.bMirrorSvgX = bMirrorSvgX;
                Contour.BeginAt(Vectors[0]);
                for (int32 Index = 1; Index < Vectors.Num(); ++Index) { Contour.Line(Vectors[Index]); }
                if (Tag == TEXT("polygon")) { Contour.Close(); }
                Contour.Emit(Out);
            }
        }
        else if (Tag == TEXT("rect"))
        {
            const double X = SvgNumber(Node, TEXT("x"));
            const double Y = SvgNumber(Node, TEXT("y"));
            const double W = SvgNumber(Node, TEXT("width"));
            const double H = SvgNumber(Node, TEXT("height"));
            if (W > 0.0 && H > 0.0)
            {
                const FString RXText = Node->GetAttribute(TEXT("rx"));
                const FString RYText = Node->GetAttribute(TEXT("ry"));
                double RX = RXText.IsEmpty() ? SvgNumber(Node, TEXT("ry")) : SvgNumber(Node, TEXT("rx"));
                double RY = RYText.IsEmpty() ? RX : SvgNumber(Node, TEXT("ry"));
                RX = FMath::Clamp(RX, 0.0, W * 0.5);
                RY = FMath::Clamp(RY, 0.0, H * 0.5);
                FContour Contour;
                Contour.Name = Name;
                Contour.Transform = Transform;
                Contour.bMirrorSvgX = bMirrorSvgX;
                if (RX > 0.0 && RY > 0.0)
                {
                    Contour.BeginAt(FVector2D(X + RX, Y));
                    Contour.Line(FVector2D(X + W - RX, Y));
                    Arc(Contour, RX, RY, 0.0, false, true, FVector2D(X + W, Y + RY));
                    Contour.Line(FVector2D(X + W, Y + H - RY));
                    Arc(Contour, RX, RY, 0.0, false, true, FVector2D(X + W - RX, Y + H));
                    Contour.Line(FVector2D(X + RX, Y + H));
                    Arc(Contour, RX, RY, 0.0, false, true, FVector2D(X, Y + H - RY));
                    Contour.Line(FVector2D(X, Y + RY));
                    Arc(Contour, RX, RY, 0.0, false, true, FVector2D(X + RX, Y));
                }
                else
                {
                    Contour.BeginAt(FVector2D(X, Y));
                    Contour.Line(FVector2D(X + W, Y));
                    Contour.Line(FVector2D(X + W, Y + H));
                    Contour.Line(FVector2D(X, Y + H));
                }
                Contour.Close();
                Contour.Emit(Out);
            }
        }
        else if (Tag == TEXT("circle") || Tag == TEXT("ellipse"))
        {
            const double CX = SvgNumber(Node, TEXT("cx"));
            const double CY = SvgNumber(Node, TEXT("cy"));
            const double RX = Tag == TEXT("circle") ? SvgNumber(Node, TEXT("r")) : SvgNumber(Node, TEXT("rx"));
            const double RY = Tag == TEXT("circle") ? RX : SvgNumber(Node, TEXT("ry"));
            if (RX > 0.0 && RY > 0.0)
            {
                FContour Contour;
                Contour.Name = Name;
                Contour.Transform = Transform;
                Contour.bMirrorSvgX = bMirrorSvgX;
                Contour.BeginAt(FVector2D(CX + RX, CY));
                Arc(Contour, RX, RY, 0.0, false, true, FVector2D(CX, CY + RY));
                Arc(Contour, RX, RY, 0.0, false, true, FVector2D(CX - RX, CY));
                Arc(Contour, RX, RY, 0.0, false, true, FVector2D(CX, CY - RY));
                Arc(Contour, RX, RY, 0.0, false, true, FVector2D(CX + RX, CY));
                Contour.Close();
                Contour.Emit(Out);
            }
        }
        for (const FXmlNode* Child : Node->GetChildrenNodes())
        {
            ReadElements(Child, Transform, Ids, ActiveReferences,
                         Out, Ordinal, bMirrorSvgX, bReferenced, Depth + 1);
        }
    }

}

bool SplineAssetImport::LoadSvgCurves(const FString& Filename, TArray<FSplineImportedCurve>& Curves,
    bool bMirrorX)
{
    if (IFileManager::Get().FileSize(*Filename) > 20 * 1024 * 1024)
    {
        UE_LOG(LogSplineSvg, Error, TEXT("SVG exceeds 20 MiB import limit: %s"), *Filename);
        return false;
    }
    FString XML;
    if (!FFileHelper::LoadFileToString(XML, *Filename)) { return false; }
    // Normalize wrapped attribute values before FXmlFile tokenizes them. This
    // is essential for SVG <path d="..."> data split over physical lines.
    NormalizeMultilineXmlAttributes(XML);
    FXmlFile Document(XML, EConstructMethod::ConstructFromBuffer);
    if (!Document.IsValid() || !Document.GetRootNode())
    {
        UE_LOG(LogSplineSvg, Error, TEXT("Invalid SVG XML %s: %s"), *Filename, *Document.GetLastError());
        return false;
    }
    int32 Ordinal = 0;
    TMap<FString, const FXmlNode*> Ids;
    IndexIds(Document.GetRootNode(), Ids);
    TSet<const FXmlNode*> ActiveReferences;
    ReadElements(Document.GetRootNode(), FAffine(), Ids, ActiveReferences, Curves, Ordinal, bMirrorX);
    // All subpaths from the same SVG share the SOURCE canvas origin. Never
    // normalize each contour separately or their original layout is lost.
    // viewBox values are SVG document coordinates BEFORE Unreal axis conversion.
    const FXmlNode* Root = Document.GetRootNode();
    FString BoundsString = Root->GetAttribute(TEXT("viewBox"));
    BoundsString.ReplaceInline(TEXT(","), TEXT(" "));
    TArray<FString> Values;
    BoundsString.ParseIntoArrayWS(Values);
    double MinX = 0.0, MinY = 0.0, Width = 0.0, Height = 0.0;
    if (Values.Num() == 4)
    {
        MinX = FCString::Atod(*Values[0]);
        MinY = FCString::Atod(*Values[1]);
        Width = FCString::Atod(*Values[2]);
        Height = FCString::Atod(*Values[3]);
    }
    else
    {
        // The width/height attributes can include units; Atod reads their
        // numeric prefix. This fallback is appropriate for untransformed SVGs.
        Width = FCString::Atod(*Root->GetAttribute(TEXT("width")));
        Height = FCString::Atod(*Root->GetAttribute(TEXT("height")));
    }
    if (Width > 0.0 && Height > 0.0)
    {
        const EStrandSvgCanvasOrigin Origin = GetDefault<UStrandProjectSettings>()->SvgCanvasOrigin;
        const double FX = (Origin == EStrandSvgCanvasOrigin::TopRight || Origin == EStrandSvgCanvasOrigin::BottomRight)
            ? 1.0 : (Origin == EStrandSvgCanvasOrigin::Center ? 0.5 : 0.0);
        const double FY = (Origin == EStrandSvgCanvasOrigin::BottomLeft || Origin == EStrandSvgCanvasOrigin::BottomRight)
            ? 1.0 : (Origin == EStrandSvgCanvasOrigin::Center ? 0.5 : 0.0);
        const FVector OriginInUnreal(bMirrorX ? -(MinX + Width * FX) : MinX + Width * FX,
                                    -(MinY + Height * FY), 0.0);
        for (FSplineImportedCurve& Curve : Curves)
        {
            for (FSplinePointData& Point : Curve.Points)
            {
                Point.LocalPosition -= OriginInUnreal;
            }
        }
    }
    else
    {
        UE_LOG(LogSplineSvg, Warning, TEXT("SVG has no usable canvas bounds; Strand canvas origin remains the SVG coordinate origin: %s"), *Filename);
    }
    int32 ClosedCount = 0;
    for (const FSplineImportedCurve& Curve : Curves)
    {
        ClosedCount += Curve.bClosedLoop ? 1 : 0;
    }
    UE_LOG(LogSplineSvg, Display,
        TEXT("Strand SVG '%s': %d independent path(s): %d closed, %d open. Open SVG strokes are not implicitly connected."),
        *Filename, Curves.Num(), ClosedCount, Curves.Num() - ClosedCount);
    for (int32 I = 0; I < Curves.Num(); ++I)
    {
        // Numeric order handles duplicate SVG IDs without collapsing source curves.
        Curves[I].SourceId = Curves[I].Name + FString::Printf(TEXT("#%d"), I);
        // Report topology and segment counts so import/reimport and applied
        // component mismatches can be diagnosed without reading a .uasset.
        if (I < 32)
        {
            const int32 Segments = Curves[I].bClosedLoop
                ? Curves[I].Points.Num() : Curves[I].Points.Num() - 1;
            UE_LOG(LogSplineSvg, Display, TEXT("  Contour %d '%s': %d points, %d segments, %s"),
                I, *Curves[I].Name, Curves[I].Points.Num(), Segments,
                Curves[I].bClosedLoop ? TEXT("closed") : TEXT("open"));
        }
    }
    if (Curves.Num() > 32)
    {
        UE_LOG(LogSplineSvg, Display, TEXT("  ... %d additional contours omitted from summary"),
            Curves.Num() - 32);
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
