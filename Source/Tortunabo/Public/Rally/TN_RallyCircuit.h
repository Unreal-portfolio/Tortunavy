// Datos de los circuitos por vueltas generados (#622, Docs/Rally_Circuitos_Vueltas.md): peralte por punto del eje (bank_deg) y
// elementos del trazado (elements: saltos, rasantes, horquillas, curvas peraltadas...) del manifest de la variante. Lógica pura
// sin mundo: la usan ATN_RallyTrack (orientación de las puertas), las notas del copiloto (saltos y rasantes del manifest) y el
// piloto IA (frenada antes de saltos y horquillas). Tests Tortunabo.Rally.Circuit.*.
#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace TNRallyCircuit
{
	/** Tipo de un elemento del trazado (campo type del manifest). */
	enum class EElementKind : uint8
	{
		Other,
		Straight,
		BankedCorner,
		Hairpin,
		Chicane,
		Jump,
		Crest
	};

	/** Un elemento de elements, con los arcos en metros desde la línea de salida medidos por road_uu. */
	struct FElement
	{
		EElementKind Kind = EElementKind::Other;
		FString Id;
		double StartM = 0.0;
		double EndM = 0.0;
		/** Punto clave: el labio en los saltos (lip_s_m) y la cima en los rasantes (crest_s_m); negativo si no hay. */
		double KeyM = -1.0;
		/** Final de la zona de aterrizaje de un salto (landing_s_m[1]); negativo si no hay. */
		double LandingEndM = -1.0;
		/** Velocidad de diseño en el labio de un salto (v_design_kmh); 0 si no viene. */
		double DesignKmh = 0.0;
		/** Radio de las curvas (radius_m); 0 si no viene. */
		double RadiusM = 0.0;
	};

	/** El mismo elemento con los arcos en cm de la spline de la pista (ya envueltos a [0, longitud)). */
	struct FFeatureArc
	{
		EElementKind Kind = EElementKind::Other;
		double StartCm = 0.0;
		double EndCm = 0.0;
		/** Labio o cima (cm de spline); igual que StartCm si el elemento no tiene punto clave. */
		double KeyCm = 0.0;
		double DesignKmh = 0.0;
		double RadiusM = 0.0;
	};

	/** Frenada del piloto IA ante los elementos del manifest (#622). */
	struct FBrakeTuning
	{
		/** Velocidad en el labio: la de diseño por este factor (más lento cae antes en la mesa, nunca después de la rodilla). */
		float JumpLipSpeedFactor = 0.9f;
		/** Lateral con la que se toma una horquilla: v = sqrt(g · R · esto). */
		float HairpinLateralG = 0.6f;
		/** Nunca se pide menos que esto (km/h). */
		float MinKmh = 25.f;
	};

	/** Tipo de elemento por su campo type («recta», «curva_peraltada», «horquilla», «chicane», «salto», «rasante»). */
	TORTUNABO_API EElementKind KindFromName(const FString& Type);

	/**
	 * Lee bank_deg y elements de la raíz del manifest. Sin los campos, las listas quedan vacías y devuelve true; false (con
	 * OutError) si vienen mal formados. bank_deg con otro número de valores que road_uu (RoadPoints) se descarta.
	 */
	TORTUNABO_API bool ReadCircuitFields(const FJsonObject& Root, int32 RoadPoints, TArray<double>& OutBankDeg,
		TArray<FElement>& OutElements, FString& OutError);

	/** Rotación de una puerta con el rumbo YawDeg y el peralte BankDeg (positivo: el lado derecho de la marcha más bajo). */
	TORTUNABO_API FRotator GateRotation(double YawDeg, double BankDeg);

	/**
	 * Arco de la spline (cm) de cada punto de road_uu: la longitud acumulada por la polilínea escalada a SplineLengthCm (la
	 * spline se construye desde road_uu y empieza en su primer punto). En circuito cuenta el tramo que cierra el lazo.
	 * OutRoadLengthCm recibe la longitud de la polilínea.
	 */
	TORTUNABO_API TArray<double> RoadPointArcs(const TArray<FVector>& Road, bool bClosed, double SplineLengthCm, double& OutRoadLengthCm);

	/** Arco de la spline (cm) de Meters metros por road_uu desde su primer punto, envuelto en circuito. */
	TORTUNABO_API double RoadMetersToArc(double Meters, double RoadLengthCm, double SplineLengthCm, bool bClosed);

	/** Elementos con los arcos en la spline (RoadMetersToArc). */
	TORTUNABO_API TArray<FFeatureArc> ToFeatureArcs(TConstArrayView<FElement> Elements, double RoadLengthCm, double SplineLengthCm,
		bool bClosed);

	/**
	 * Peralte (grados) en el arco Arc interpolando las muestras (SampleArcs crecientes, una por punto de road_uu). En circuito
	 * interpola también entre la última y la primera. 0 sin muestras.
	 */
	TORTUNABO_API double BankAtArc(TConstArrayView<double> SampleArcs, TConstArrayView<double> BankDeg, double Arc, double LengthCm,
		bool bClosed);

	/**
	 * Velocidad máxima (km/h) que permiten ahora los saltos y horquillas de los siguientes ProbeCm frenando con DecelCms2: en
	 * cada labio, la de diseño por JumpLipSpeedFactor; en cada horquilla (y dentro de ella), la de su radio. MaxKmh si no hay
	 * ninguno. Un salto cuyo labio ya ha quedado atrás no frena.
	 */
	TORTUNABO_API float FeatureSpeedLimitKmh(TConstArrayView<FFeatureArc> Features, double ArcCm, double LengthCm, bool bClosed,
		double ProbeCm, double DecelCms2, float MaxKmh, const FBrakeTuning& Tuning = FBrakeTuning());
}
