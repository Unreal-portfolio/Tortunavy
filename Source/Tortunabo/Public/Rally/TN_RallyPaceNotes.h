// Notas de copiloto del Rally generadas a partir del eje del trazado: curvas con dirección y grado 1-6 (convención de
// rally: 6 = casi recta, 1 = horquilla), «cresta» en los cambios de rasante convexos, «salto» al final de las rampas y
// «agua» en los tramos bajo el nivel del agua. Lógica pura sin mundo (Tortunabo.Rally.PaceNotes.*); la tableta de la
// artillera (UTN_RallyCopilotTablet) la usa para el perfil y la próxima nota.
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyCircuit.h"

class ATN_RallyTrack;

namespace TNRallyPaceNotes
{
	enum class ENoteKind : uint8
	{
		Turn,
		Crest,
		Jump,
		Water
	};

	enum class ETurnDirection : uint8
	{
		None,
		Left,
		Right
	};

	inline constexpr int32 MinGrade = 1;
	inline constexpr int32 MaxGrade = 6;
	/** Distancia que enseña la tableta por delante del buggy (cm). */
	inline constexpr double DefaultLookAheadCm = 40000.0;
	/** Un giro de al menos esto con grado 1 se canta como «horquilla» (grados). */
	inline constexpr double HairpinMinAngleDeg = 150.0;

	struct FPaceNote
	{
		ENoteKind Kind = ENoteKind::Turn;
		/** Arco del eje donde empieza la nota (cm). En las de rasante, el punto de la rotura. */
		double ArcCm = 0.0;
		/** Longitud del tramo que cubre (cm): la curva o el vadeo; 0 en crestas y saltos. */
		double LengthCm = 0.0;
		ETurnDirection Direction = ETurnDirection::None;
		/** 1..6 en las curvas; 0 en el resto. */
		int32 Grade = 0;
		/** Giro total de la curva (grados, positivo). */
		double AngleDeg = 0.0;
		/** Radio mínimo de la curva (cm). */
		double RadiusCm = 0.0;
		/** La cuerda de la curva se aleja tanto del eje que cortarla saca de la pista. */
		bool bDontCut = false;
	};

	struct FParams
	{
		/** Separación de las muestras del eje con que se analiza (cm). */
		double StepCm = 500.0;
		/** Con radio mayor que esto el eje cuenta como recto (cm). */
		double StraightRadiusCm = 25000.0;
		/** Una curva que gira menos que esto no lleva nota (grados). */
		double MinTurnDeg = 15.0;
		/** Muestras a cada lado con que se suaviza el giro por vértice. */
		int32 CurvatureHalfWindow = 1;
		/** Base a cada lado del vértice con que se miden las pendientes (cm). */
		double SlopeBaseCm = 1500.0;
		/** Caída de pendiente (después menos antes) a partir de la que hay cresta. */
		double CrestBreak = 0.08;
		/** Pendiente de subida mínima de una rampa de salto. */
		double JumpRampSlope = 0.12;
		/** Caída de pendiente mínima de un salto. */
		double JumpBreak = 0.2;
		/** Flecha (separación entre la cuerda y el eje) desde la que se canta «no cortes» (cm). */
		double DontCutSagittaCm = 2500.0;
		/** El eje está «en el agua» si queda por debajo de WaterZ menos esto (cm). */
		double WaterMarginCm = 0.0;
		/** Dos notas de rasante a menos de esto se cantan como una (cm). */
		double MinVerticalSpacingCm = 3000.0;
	};

	/** Eje remuestreado (Points[i] en el arco i × StepCm) y sus notas ordenadas por arco. */
	struct FTrackNotes
	{
		TArray<FVector> Points;
		double StepCm = 0.0;
		double LengthCm = 0.0;
		bool bClosed = false;
		bool bHasWater = false;
		double WaterZ = 0.0;
		TArray<FPaceNote> Notes;

		bool IsValid() const { return Points.Num() >= 2 && LengthCm > 0.0 && StepCm > 0.0; }
	};

	struct FNoteAhead
	{
		FPaceNote Note;
		/** Distancia hacia delante desde el buggy hasta el inicio de la nota (cm). */
		double DistanceCm = 0.0;
	};

	/**
	 * Remuestrea la polilínea a paso uniforme (el más cercano a StepCm que reparte la longitud exacta). En circuito no se
	 * repite el primer punto al final. OutStepCm y OutLengthCm reciben el paso real y la longitud.
	 */
	TORTUNABO_API TArray<FVector> Resample(const TArray<FVector>& Polyline, double StepCm, bool bClosed, double& OutStepCm,
		double& OutLengthCm);

	/** Grado de rally de una curva por su radio mínimo y su giro total (1 = horquilla .. 6 = casi recta). */
	TORTUNABO_API int32 GradeFor(double RadiusCm, double AngleDeg);

	/** Notas de una polilínea del eje con alturas. Sin agua, bHasWater = false. */
	TORTUNABO_API FTrackNotes Build(const TArray<FVector>& Polyline, bool bClosed, bool bHasWater, double WaterZ,
		const FParams& Params = FParams());

	/**
	 * Si Features trae saltos o rasantes (elements de un circuito generado, #622), las notas de salto y cresta salen de ellos y no
	 * de la forma del eje: «salto» en cada labio y «cresta» en cada cima. Sin ninguno, no toca las notas.
	 */
	TORTUNABO_API void ApplyAuthoredVerticalNotes(FTrackNotes& Track, TConstArrayView<TNRallyCircuit::FFeatureArc> Features);

	/**
	 * Notas de una pista ya construida (solo su API pública: arco, longitud, circuito, nivel del agua y, si el manifest los trae,
	 * sus saltos y rasantes con ApplyAuthoredVerticalNotes).
	 */
	TORTUNABO_API FTrackNotes BuildForTrack(const ATN_RallyTrack& Track, const FParams& Params = FParams());

	/** Punto del eje remuestreado en el arco dado (envuelto en circuito, recortado en punto a punto). */
	TORTUNABO_API FVector LocationAtArc(const FTrackNotes& Track, double ArcCm);

	/** Notas que empiezan entre FromArcCm y FromArcCm + RangeCm, de la más cercana a la más lejana. */
	TORTUNABO_API TArray<FNoteAhead> NotesAhead(const FTrackNotes& Track, double FromArcCm, double RangeCm = DefaultLookAheadCm);

	/**
	 * Distancias hacia delante desde FromArcCm hasta los arcos ArcsCm que caen en [0, RangeCm] (en circuito, dando la vuelta),
	 * de la más cercana a la más lejana. La tableta lo usa con las filas de cajas de munición (#299).
	 */
	TORTUNABO_API TArray<double> ArcsAhead(TConstArrayView<double> ArcsCm, double LengthCm, bool bClosed, double FromArcCm,
		double RangeCm = DefaultLookAheadCm);

	/** Lo que canta la copiloto: «izquierda 3», «horquilla derecha», «cresta», «salto», «agua», con «, no cortes». */
	TORTUNABO_API FText NoteText(const FPaceNote& Note);

	/** Distancia redondeada a 10 m: «120 m». */
	TORTUNABO_API FText DistanceText(double DistanceCm);
}
