// Límites y decorado del trazado del Rally (#303): barrera continua de neumáticos apilados a los dos lados de todo el trazado,
// pegada al borde de la calzada y apoyada en el suelo, con un carril de colisión continuo y poco rozamiento,
// decorado de playa de la Carrera fuera de la valla (TNBeachDecorKit), decorado lejano de piezas grandes
// (castillos, grupos de rocas, palmeras, pedruscos y conchas gigantes) a 15-60 m del borde, público en las curvas y en la meta y
// los pórticos de /Game/Art/IA/rally en las puertas. Cada máquina lo construye igual a partir del eje de ATN_RallyTrack y
// de una semilla (como la pista y el decorado de la ronda de la playa): no se replica nada.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/Function.h"
#include "World/Beach/TN_BeachTypes.h"
#include "TN_RallyTrackDressing.generated.h"

class ATN_RallyTrack;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPhysicalMaterial;
class UStaticMesh;
struct FTNRallyDressingBatches;

/**
 * Cómo se ve un tramo de límite (el carril de colisión es el mismo en todos). El borde por defecto es solo de neumáticos
 * apilados (#303, director, 03-10); los demás estilos quedan para los Blueprints que los usen.
 */
UENUM(BlueprintType)
enum class ETNRallyBarrierStyle : uint8
{
	PostRope UMETA(DisplayName = "Palos y cuerda"),
	Sandbags UMETA(DisplayName = "Sacos terreros"),
	Logs UMETA(DisplayName = "Troncos"),
	Tires UMETA(DisplayName = "Neumáticos apilados"),
	Castles UMETA(DisplayName = "Castillos y cubos de arena")
};

/** Hueco sin límite (atajo): de StartArcCm a EndArcCm del eje; en circuito puede dar la vuelta (End < Start). */
USTRUCT(BlueprintType)
struct FTNRallyDressingGap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally")
	float StartArcCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally")
	float EndArcCm = 0.f;

	/** -1 = izquierda, +1 = derecha, 0 = los dos lados. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally", meta = (ClampMin = "-1", ClampMax = "1"))
	int32 Side = 0;
};

/** Elemento de playa del decorado y su peso en el reparto. */
USTRUCT(BlueprintType)
struct FTNRallyDecorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally")
	ETNBeachElement Element = ETNBeachElement::PlantedUmbrella;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally", meta = (ClampMin = "0"))
	float Weight = 1.f;

	/** Tamaño respecto a la huella nominal (TNBeach::FootprintRadius), entre 0,5 y 1,6. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally", meta = (ClampMin = "0.5", ClampMax = "1.6"))
	float MinSize = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally", meta = (ClampMin = "0.5", ClampMax = "1.6"))
	float MaxSize = 0.9f;
};

/** De dónde sale la malla de una pieza del decorado lejano. */
UENUM(BlueprintType)
enum class ETNRallyFarDecorSource : uint8
{
	/** Receta del decorado de la playa de la Carrera (TNBeachDecorKit), con su colisión simple. */
	BeachElement UMETA(DisplayName = "Elemento de playa"),
	/** Palmera del mapa procedural (TNFloraMesh), sin colisión. */
	Palm UMETA(DisplayName = "Palmera"),
	/** Malla estática del proyecto (Mesh). */
	StaticMesh UMETA(DisplayName = "Malla estática")
};

/** Pieza grande del decorado lejano (#303): se lee a distancia, fuera del corredor. */
USTRUCT(BlueprintType)
struct FTNRallyFarDecorEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally")
	ETNRallyFarDecorSource Source = ETNRallyFarDecorSource::BeachElement;

	/** Con Source = BeachElement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally")
	ETNBeachElement Element = ETNBeachElement::SandCastleHuge;

	/** Con Source = StaticMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally", meta = (ClampMin = "0"))
	float Weight = 1.f;

	/** Radio de la huella en planta (cm): la pieza se escala para ocupar ese radio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally", meta = (ClampMin = "100"))
	float MinRadiusCm = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rally", meta = (ClampMin = "100"))
	float MaxRadiusCm = 1500.f;
};

/** Reglas puras de los límites y del reparto del decorado (Tortunabo.Rally.Dressing.*): sin mundo, para poder probarlas. */
namespace TNRallyDressing
{
	inline constexpr int32 LeftSide = 0;
	inline constexpr int32 RightSide = 1;

	/** -1 a la izquierda y +1 a la derecha del sentido de la carrera. */
	inline double SideSign(int32 Side) { return Side == LeftSide ? -1.0 : 1.0; }

	/** Muestra del eje: punto a la cota de la calzada, dirección en planta (unitaria) y arco (cm). */
	struct FAxisSample
	{
		FVector Location = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		double Arc = 0.0;
	};

	/** Lo que el decorado necesita del trazado (SampleTrack lo saca de ATN_RallyTrack; los tests lo montan a mano). */
	struct FTrackData
	{
		/** Muestras equiespaciadas del eje; en circuito, la última no repite la primera. */
		TArray<FAxisSample> Samples;
		double StepCm = 400.0;
		double LengthCm = 0.0;
		bool bClosed = false;
		/** Ancho de la calzada (cm); 0 = el de FBarrierParams::DefaultRoadWidthCm. */
		double RoadWidthCm = 0.0;
		/** Puertas a la cota de la calzada con X en el sentido de la carrera, y la de la meta (INDEX_NONE si no hay). */
		TArray<FTransform> Gates;
		int32 FinishGate = INDEX_NONE;
		double GateHalfWidthCm = 1200.0;
		bool bHasWater = false;
		double WaterZ = 0.0;
		/** Atajos: sin límite en esos arcos. */
		TArray<FTNRallyDressingGap> Gaps;
	};

	/** Ancho de una tortuga (cm, el diámetro de su cápsula): ningún hueco de la barrera puede pasar de esto (#303). */
	inline constexpr double TurtleWidthCm = 110.0;

	struct FBarrierParams
	{
		/**
		 * Barrera continua a los dos lados de todo el trazado (#303, director, 03-10), salvo los atajos (Gaps) y donde otro tramo
		 * se monta encima. Con false, solo en las curvas y en las caídas (como antes).
		 */
		bool bContinuous = true;
		/**
		 * Barrera pegada al borde de la calzada (#303, director, 03-10): a media calzada más RoadEdgeMarginCm a los dos lados, en
		 * recta y en curva, sin arcén ni escapatoria. Con false, las reglas de antes (arcén, escapatoria por fuera de las curvas).
		 */
		bool bHugRoad = true;
		/** Del borde de la calzada al eje de la barrera: el radio de un neumático apilado y un poco de holgura. */
		double RoadEdgeMarginCm = 70.0;
		double DefaultRoadWidthCm = 1400.0;
		/** Arcén entre el borde de la calzada y el límite en recta, y mínimo para no pisar los postes de las puertas. */
		double ShoulderCm = 400.0;
		double MinOffsetCm = 1500.0;
		/** Curva: radio de 250 m o menos; con 60 m o menos, toda la escapatoria por fuera. */
		double CurveMinCurvature = 1.0 / 25000.0;
		double CurveFullCurvature = 1.0 / 6000.0;
		double MaxRunoffCm = 800.0;
		/** Por dentro de la curva, el límite no pasa de esta fracción del radio ni baja del borde más este margen. */
		double InsideRadiusFraction = 0.6;
		double InsideMinMarginCm = 150.0;
		/** En una caída, el límite va al borde de la calzada más este margen. */
		double DropEdgeMarginCm = 250.0;
		/** Ventana para medir la curvatura y tramo que se alarga el límite antes y después de una curva o una caída. */
		double CurvatureWindowCm = 2000.0;
		double CurveLeadCm = 3000.0;
		double DropLeadCm = 1500.0;
		/** Huecos más cortos que esto entre dos tramos de límite se cierran (los atajos no). */
		double MinGapCm = 2500.0;
		/** Ventana de suavizado del desplazamiento lateral a lo largo del límite. */
		double SmoothWindowCm = 1600.0;
		/** Otro tramo del trazado (horquillas): se ignora a menos de este arco y cuenta si está a menos de MaxDz de altura. */
		double OtherSectionExcludeArcCm = 3000.0;
		double OtherSectionMaxDzCm = 800.0;
		double OtherSectionMarginCm = 400.0;
	};

	/** Desplazamiento lateral de cada muestra en un lado: 0 = sin límite. Runs: tramos seguidos, en orden de la carrera. */
	struct FBarrierSide
	{
		TArray<double> OffsetCm;
		TArray<TArray<int32>> Runs;

		bool IsLimited(int32 Index) const { return OffsetCm.IsValidIndex(Index) && OffsetCm[Index] > 0.0; }
	};

	struct FBarrierPlan
	{
		FBarrierSide Sides[2];
		/** Curvatura con signo (1/cm): positiva si la curva gira a la derecha. */
		TArray<double> Curvature;
		double RoadHalfCm = 0.0;
		double BaseOffsetCm = 0.0;

		/** Hasta dónde llega el corredor por ese lado: el límite si lo hay y, si no, el desplazamiento base. */
		double EdgeCm(int32 Side, int32 Index) const
		{
			return Sides[Side].IsLimited(Index) ? Sides[Side].OffsetCm[Index] : BaseOffsetCm;
		}
	};

	/** Semilla derivada estable (igual en todas las máquinas, no negativa) para el elemento (A, B). */
	TORTUNABO_API int32 SubSeed(int32 Seed, int32 A, int32 B);

	/** Semiancho de la calzada y desplazamiento del límite en recta (con bHugRoad, en todas partes). */
	TORTUNABO_API double RoadHalfWidthCm(const FTrackData& Track, const FBarrierParams& Params);
	TORTUNABO_API double BaseOffsetCm(double RoadHalfCm, const FBarrierParams& Params);

	/** Curvatura con signo de cada muestra (cambio de rumbo en la ventana / arco de la ventana). */
	TORTUNABO_API TArray<double> SignedCurvature(const TArray<FAxisSample>& Samples, bool bClosed, double WindowCm);

	/** Desplazamiento del límite en una curva: por fuera, base más escapatoria; por dentro, acotado por el radio. */
	TORTUNABO_API double BarrierOffsetCm(double Curvature, int32 Side, double RoadHalfCm, const FBarrierParams& Params);

	/** Punto a OffsetCm del eje por el lado Side, a la cota de la muestra. */
	TORTUNABO_API FVector LateralPoint(const FAxisSample& Sample, int32 Side, double OffsetCm);

	/**
	 * Límites de los dos lados: curvas (alargadas CurveLeadCm) y caídas (DropMask: bit 0 izquierda, bit 1 derecha; alargadas
	 * DropLeadCm), huecos cortos cerrados, atajos abiertos, desplazamiento suavizado y recortado donde pisaría otro tramo.
	 */
	TORTUNABO_API FBarrierPlan PlanBarriers(const FTrackData& Track, const TArray<uint8>& DropMask, const FBarrierParams& Params);

	/** Punto de una polilínea con su rumbo y la separación real con el siguiente. */
	struct FPolySpot
	{
		FVector Location = FVector::ZeroVector;
		double YawDeg = 0.0;
		double SeparationCm = 0.0;
	};

	/** Puntos repartidos a lo largo de la polilínea, centrados en tramos iguales de como mucho SpacingCm. */
	TORTUNABO_API TArray<FPolySpot> ResamplePolyline(const TArray<FVector>& Points, double SpacingCm);

	/**
	 * Huecos de la barrera de un lado (cm, en planta), en el orden de la carrera: entre el final de un tramo de límite y el
	 * principio del siguiente y, en punto a punto, desde la salida hasta el primero y desde el último hasta la meta. Un tramo de
	 * una sola muestra no tiene carril y no cuenta. Vacío si el lado está cerrado de punta a punta. La comprobación de #303.
	 */
	TORTUNABO_API TArray<double> BarrierGapsCm(const FTrackData& Track, const FBarrierPlan& Plan, int32 Side);

	/**
	 * Parte la polilínea en trozos de como mucho MaxLengthCm (en planta) que comparten el punto de corte: el estilo del límite
	 * cambia de un trozo a otro sin dejar hueco.
	 */
	TORTUNABO_API TArray<TArray<FVector>> ChunkPolyline(const TArray<FVector>& Points, double MaxLengthCm);

	/** True si Point (en planta) está a ClearCm o más de todas las muestras del eje. */
	TORTUNABO_API bool IsClearOfTrack(const FTrackData& Track, const FVector& Point, double ClearCm);

	enum class ESpotKind : uint8
	{
		Beach,
		Crab,
		Spectator,
		Far
	};

	/** Sitio de una pieza del decorado (a la cota del eje: el actor busca el suelo). Entry: índice de la entrada o variante. */
	struct FSpot
	{
		ESpotKind Kind = ESpotKind::Beach;
		int32 Entry = 0;
		FVector Location = FVector::ZeroVector;
		double YawDeg = 0.0;
		double RadiusCm = 0.0;
		float Size = 1.f;
		int32 Seed = 0;
	};

	struct FDecorParams
	{
		/** Elementos de playa y cangrejos de atrezo por kilómetro y lado. */
		double BeachPerKm = 30.0;
		double CrabsPerKm = 12.0;
		/** Hueco entre el borde del corredor y el decorado, y franja (más allá) en la que se reparte. */
		double ClearanceCm = 800.0;
		double BandCm = 4000.0;
		double CrabRadiusCm = 150.0;
		/** Público: grupos en las curvas cerradas (por fuera) y en la meta (a los dos lados). */
		int32 SpectatorsPerGroup = 8;
		double SpectatorGroupSpacingCm = 6000.0;
		double SpectatorMinCurvature = 1.0 / 12000.0;
		double SpectatorSetbackCm = 500.0;
		double SpectatorSpacingCm = 200.0;
		int32 SpectatorsAtFinish = 16;
		int32 SpectatorVariants = 4;
	};

	/** Decorado de playa y cangrejos fuera del corredor, sin solaparse entre sí ni con Reserved; determinista con Seed. */
	TORTUNABO_API TArray<FSpot> PlanDecor(const FTrackData& Track, const FBarrierPlan& Plan, const TArray<FTNRallyDecorEntry>& Entries,
		const FDecorParams& Params, int32 Seed, const TArray<FSpot>& Reserved = TArray<FSpot>());

	/** Decorado lejano: piezas grandes en una franja separada del borde del corredor, con presupuesto. */
	struct FFarDecorParams
	{
		/** Piezas por kilómetro y lado (antes de descartar las que no caben). */
		double PerKm = 18.0;
		/** Hueco entre el borde del corredor (límite o desplazamiento base) y la huella, y franja (más allá) en la que se reparte. */
		double MinFromEdgeCm = 1500.0;
		double BandCm = 4500.0;
		/** Presupuesto: piezas como mucho (cada una son una o dos instancias). */
		int32 MaxPieces = 160;
		/** Intentos por hueco del reparto antes de dejarlo vacío. */
		int32 Attempts = 4;
		/** Sondas de suelo en el contorno de la huella (a esta fracción del radio) y desnivel máximo entre ellas. */
		double ProbeRadiusFraction = 0.7;
		double MaxGroundStepCm = 600.0;
		/** El agua queda por debajo de WaterZ más este margen. */
		double WaterMarginCm = 10.0;
		/** Distancia de dibujado: MinCullCm más CullPerRadius por cm de radio, sin pasar de MaxCullCm. */
		double MinCullCm = 40000.0;
		double CullPerRadius = 30.0;
		double MaxCullCm = 120000.0;
	};

	/** Suelo firme bajo Probe (planta del punto, cota de referencia en Z): true y su cota; false si no hay suelo. */
	using FGroundQuery = TFunctionRef<bool(const FVector& Probe, double& OutGroundZ)>;

	/** Distancia mínima (cm) del eje a la huella de una pieza lejana: el borde base del corredor más el hueco. */
	TORTUNABO_API double FarMinAxisClearanceCm(const FBarrierPlan& Plan, const FFarDecorParams& Params);

	/** Distancia a la que deja de dibujarse una pieza lejana de radio RadiusCm (entre MinCullCm y MaxCullCm). */
	TORTUNABO_API double FarCullDistanceCm(double RadiusCm, const FFarDecorParams& Params);

	/**
	 * Piezas grandes lejos de la pista (Kind = Far, Location a la cota del suelo más bajo de su huella): fuera del corredor y
	 * de cualquier tramo del trazado, sobre suelo firme (Ground) y nunca en el agua, sin solaparse y con como mucho
	 * MaxPieces; determinista con Seed y el mismo suelo.
	 */
	TORTUNABO_API TArray<FSpot> PlanFarDecor(const FTrackData& Track, const FBarrierPlan& Plan, const TArray<FTNRallyFarDecorEntry>& Entries,
		const FFarDecorParams& Params, int32 Seed, FGroundQuery Ground);

	/** Público mirando a la calzada detrás del límite, en las curvas cerradas y en la meta; determinista con Seed. */
	TORTUNABO_API TArray<FSpot> PlanSpectators(const FTrackData& Track, const FBarrierPlan& Plan, const FDecorParams& Params, int32 Seed);

	/** Muestrea la pista ya construida cada StepCm (eje, puertas, meta y agua). */
	TORTUNABO_API FTrackData SampleTrack(const ATN_RallyTrack& Track, double StepCm);
}

/**
 * Decorado del trazado del Rally. Local en cada máquina (bReplicates = false): todo sale del eje de la pista, que cada máquina
 * construye con el mismo manifest, y de la semilla; el suelo se busca con trazas sobre el mismo terreno. Así la colisión es
 * idéntica en el servidor y en los clientes sin coste de red ni problemas de entrada tardía.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_RallyTrackDressing : public AActor
{
	GENERATED_BODY()

public:
	ATN_RallyTrackDressing();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Construye límites, decorado, público y pórticos (quita antes lo que hubiera). False sin trazado. */
	bool Build(const TNRallyDressing::FTrackData& Track, int32 Seed);

	/** Build con la pista ya construida; esconde los arcos provisionales de las puertas que reciben pórtico. */
	UFUNCTION(BlueprintCallable, Category = "Rally|Decorado")
	bool BuildFromTrack(ATN_RallyTrack* Track, int32 Seed);

	/** Usa el decorado del nivel (o crea uno) y lo construye para Track. Lo llama ATN_RallyGameState::PrepareTrack. */
	static ATN_RallyTrackDressing* BuildForTrack(ATN_RallyTrack* Track, int32 Seed);

	UFUNCTION(BlueprintCallable, Category = "Rally|Decorado")
	void ClearDressing();

	UFUNCTION(BlueprintPure, Category = "Rally|Decorado")
	int32 GetRailSegmentCount() const { return RailSegmentCount; }

	UFUNCTION(BlueprintPure, Category = "Rally|Decorado")
	int32 GetBarrierPieceCount() const { return BarrierPieceCount; }

	UFUNCTION(BlueprintPure, Category = "Rally|Decorado")
	int32 GetDecorCount() const { return DecorCount; }

	UFUNCTION(BlueprintPure, Category = "Rally|Decorado")
	int32 GetFarDecorCount() const { return FarDecorCount; }

	UFUNCTION(BlueprintPure, Category = "Rally|Decorado")
	int32 GetSpectatorCount() const { return SpectatorCount; }

	UFUNCTION(BlueprintPure, Category = "Rally|Decorado")
	int32 GetGateMeshCount() const { return GateMeshCount; }

	/**
	 * Base (centro en planta y cota de la cara de abajo del primer neumático) de cada pila de la barrera del lado Side
	 * (TNRallyDressing::LeftSide o RightSide), en el orden de la carrera. La usan los tests de #303 (apoyada y sin huecos).
	 */
	const TArray<FVector>& GetTireStackBases(int32 Side) const { return TireStackBases[Side == TNRallyDressing::LeftSide ? 0 : 1]; }

protected:
	// ── Límites ──

	/** Separación de las muestras del eje (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "100"))
	float SampleStepCm = 400.f;

	/** Ancho de la calzada (cm); 0 = el del trazado o 14 m. */
	UPROPERTY(EditAnywhere, Category = "Rally|Límites", meta = (ClampMin = "0"))
	float RoadWidthOverrideCm = 0.f;

	/** Atajos sin límite (el manifest no los trae). */
	UPROPERTY(EditAnywhere, Category = "Rally|Límites")
	TArray<FTNRallyDressingGap> ShortcutGaps;

	/** Estilos que se reparten por trozo de límite (con la semilla). Por defecto, solo neumáticos apilados (#303, director). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites")
	TArray<ETNRallyBarrierStyle> BarrierStyles;

	/** Largo (cm) de cada trozo de límite con un mismo estilo: el borde continuo los alterna sin dejar hueco entre ellos. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "1000"))
	float StyleSectionCm = 12000.f;

	/** Carril de colisión: alto, grueso y cuánto se hunde en el suelo (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "50"))
	float RailHeightCm = 300.f;

	/** Grueso: más que lo que avanza el buggy en un paso de física a toda velocidad, para que no lo atraviese (#303). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "10"))
	float RailThicknessCm = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "0"))
	float RailSinkCm = 40.f;

	/** Rozamiento y rebote del carril: bajos para que el buggy resbale a lo largo en vez de pararse en seco. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "0", ClampMax = "1"))
	float RailFriction = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "0", ClampMax = "1"))
	float RailRestitution = 0.1f;

	/** Material físico del carril; si no hay, se crea uno con RailFriction y RailRestitution (combinación Min). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites")
	TObjectPtr<UPhysicalMaterial> RailPhysicalMaterial;

	/** Dibuja el carril de colisión (depuración). */
	UPROPERTY(EditAnywhere, Category = "Rally|Límites")
	bool bShowRails = false;

	/** Caída: el suelo, a esta distancia más allá del límite base, queda más abajo que esto (o es agua). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "50"))
	float DropThresholdCm = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites")
	TSoftObjectPtr<UStaticMesh> TireMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "30"))
	float TireDiameterCm = 120.f;

	/** Pisos de cada pila; donde el suelo queda por debajo de la calzada se añaden los que falten para asomar lo mismo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Límites", meta = (ClampMin = "1", ClampMax = "6"))
	int32 TireStackCount = 3;

	// ── Decorado y público ──

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado")
	TArray<FTNRallyDecorEntry> DecorEntries;

	/** Densidad: elementos de playa y cangrejos por kilómetro y lado. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado", meta = (ClampMin = "0"))
	float DecorPerKm = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado", meta = (ClampMin = "0"))
	float CrabPropsPerKm = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado", meta = (ClampMin = "0"))
	float DecorBandCm = 4000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado")
	TSoftObjectPtr<UStaticMesh> CrabPropMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado", meta = (ClampMin = "50"))
	float CrabPropSizeCm = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado", meta = (ClampMin = "0", ClampMax = "24"))
	int32 SpectatorsPerGroup = 8;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado", meta = (ClampMin = "0", ClampMax = "64"))
	int32 SpectatorsAtFinish = 16;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado", meta = (ClampMin = "1000"))
	float SpectatorGroupSpacingCm = 6000.f;

	/** Material de color de vértice de las tortugas del público (el de los cosméticos y el decorado de playa). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado")
	TSoftObjectPtr<UMaterialInterface> SpectatorMaterial;

	// ── Decorado lejano (piezas grandes que se leen a distancia) ──

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado lejano")
	TArray<FTNRallyFarDecorEntry> FarDecorEntries;

	/** Piezas por kilómetro y lado (0 = sin decorado lejano). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado lejano", meta = (ClampMin = "0"))
	float FarDecorPerKm = 18.f;

	/** Hueco entre el borde del corredor y la huella de la pieza, y franja (más allá) en la que se reparte (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado lejano", meta = (ClampMin = "500"))
	float FarDecorMinFromEdgeCm = 1500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado lejano", meta = (ClampMin = "0"))
	float FarDecorBandCm = 4500.f;

	/** Presupuesto: piezas como mucho en todo el trazado. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado lejano", meta = (ClampMin = "0", ClampMax = "1000"))
	int32 FarDecorMaxPieces = 160;

	/** Distancia máxima de dibujado (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado lejano", meta = (ClampMin = "10000"))
	float FarDecorMaxCullCm = 120000.f;

	/** Colisión simple (sin cámara) en las piezas que la traen; las palmeras nunca. Queda lejos de la pista. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Decorado lejano")
	bool bFarDecorCollision = true;

	// ── Pórticos ──

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Pórticos")
	bool bPlaceGateMeshes = true;

	/** Esconde el arco provisional (BasicShapes) de ATN_RallyGate donde se pone un pórtico, para no duplicarlo. */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Pórticos")
	bool bReplaceTrackGateArches = true;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Pórticos")
	TSoftObjectPtr<UStaticMesh> StartGateMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Pórticos")
	TSoftObjectPtr<UStaticMesh> FinishGateMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Pórticos")
	TSoftObjectPtr<UStaticMesh> CheckpointMesh;

	/** Margen de los postes del pórtico fuera del volumen de la puerta, y alto si la malla es un poste suelto (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Rally|Pórticos", meta = (ClampMin = "0"))
	float GateMarginCm = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rally|Pórticos", meta = (ClampMin = "100"))
	float GatePostHeightCm = 900.f;

private:
	TNRallyDressing::FBarrierParams MakeBarrierParams() const;
	TNRallyDressing::FDecorParams MakeDecorParams() const;
	TNRallyDressing::FTrackData WithOverrides(const TNRallyDressing::FTrackData& Track) const;

	bool TraceGround(const FVector& Location, double UpCm, double DownCm, FVector& OutGround) const;
	/** Suelo cerca de la cota de referencia (ni agua ni a más de 6 m de desnivel). */
	bool FindGroundNear(const TNRallyDressing::FTrackData& Track, const FVector& Location, double ReferenceZ, FVector& OutGround) const;
	bool IsWater(const TNRallyDressing::FTrackData& Track, double Z) const;
	TArray<uint8> ProbeDrops(const TNRallyDressing::FTrackData& Track, double BaseOffsetCm) const;

	/**
	 * Puntos de un tramo de límite a la cota de la calzada (OutEdge) y del carril de colisión: el suelo si lo hay cerca y está
	 * más alto que la calzada y, si no (talud hacia abajo o caída), la cota de la calzada, para que el carril tape siempre.
	 */
	TArray<FVector> RunPoints(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierSide& Barrier, const TArray<int32>& Run,
		int32 Side, TArray<FVector>& OutEdge) const;
	/** Cada punto llevado al suelo cercano (si lo hay): base de los estilos de piezas sueltas. */
	TArray<FVector> ProjectToGround(const TNRallyDressing::FTrackData& Track, const TArray<FVector>& Points) const;
	/**
	 * Cota de apoyo de una pila de neumáticos de radio RadiusCm centrada en Center (a la cota de la calzada): el suelo más bajo
	 * bajo su centro y su contorno, para que no quede ningún lado en el aire; sin suelo cerca, el de más abajo o el agua.
	 */
	double TireStackGroundZ(const TNRallyDressing::FTrackData& Track, const FVector& Center, double YawDeg, double RadiusCm) const;
	void AddRails(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierPlan& Plan, int32 Seed, FTNRallyDressingBatches& Batches);
	void AddRailSegment(UStaticMesh* Cube, const FVector& A, const FVector& B, FTNRallyDressingBatches& Batches);
	void AddBarrierRun(const TNRallyDressing::FTrackData& Track, const TArray<FVector>& Points, ETNRallyBarrierStyle Style, int32 Side,
		int32 RunSeed, FTNRallyDressingBatches& Batches);
	void AddPostRopeRun(const TArray<TNRallyDressing::FPolySpot>& Spots, int32 RunSeed, FTNRallyDressingBatches& Batches);
	void AddPieceRun(const TArray<FVector>& Points, ETNBeachElement First, ETNBeachElement Second, float Size, int32 RunSeed,
		FTNRallyDressingBatches& Batches);
	/** Pilas de neumáticos a lo largo de Points (a la cota de la calzada), cada una apoyada en su suelo. */
	void AddTireRun(const TNRallyDressing::FTrackData& Track, const TArray<FVector>& Points, int32 Side, FTNRallyDressingBatches& Batches);
	/**
	 * Receta de playa: libre (giro, inclinación y hundimiento de su semilla) o alineada con ItemXf (límites). CullCm < 0 = la
	 * distancia del kit; con bAllowCameraBlock = false, la colisión nunca bloquea la cámara.
	 */
	bool AddBeachPiece(ETNBeachElement Element, int32 Seed, float Size, const FTransform& ItemXf, bool bFreePlacement, bool bCollision,
		FTNRallyDressingBatches& Batches, float CullCm = -1.f, bool bAllowCameraBlock = true);

	void AddDecor(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierPlan& Plan, int32 Seed,
		const TArray<TNRallyDressing::FSpot>& Reserved, FTNRallyDressingBatches& Batches);

	// Decorado lejano (TN_RallyTrackDressingFar.cpp).
	TNRallyDressing::FFarDecorParams MakeFarDecorParams() const;
	/** Planifica y coloca el decorado lejano; devuelve las piezas colocadas (el decorado cercano no las pisa). */
	TArray<TNRallyDressing::FSpot> AddFarDecor(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierPlan& Plan, int32 Seed,
		FTNRallyDressingBatches& Batches);
	bool AddFarPiece(const FTNRallyFarDecorEntry& Entry, const TNRallyDressing::FSpot& Spot, float CullCm, FTNRallyDressingBatches& Batches);
	UStaticMesh* GetFarPalmMesh(int32 Variant);
	void AddSpectators(const TNRallyDressing::FTrackData& Track, const TNRallyDressing::FBarrierPlan& Plan, int32 Seed,
		FTNRallyDressingBatches& Batches);
	UStaticMesh* GetSpectatorMesh(int32 Variant);
	void AddGateMeshes(const TNRallyDressing::FTrackData& Track, FTNRallyDressingBatches& Batches);
	bool PlaceGateMesh(UStaticMesh* Mesh, const TNRallyDressing::FTrackData& Track, const FTransform& Gate, FTNRallyDressingBatches& Batches);
	void HideTrackGateArches(const ATN_RallyTrack& Track) const;

	void CreateComponents(const FTNRallyDressingBatches& Batches);
	/** Collision: FTNRallyDressingBatches::ECollision (definido en el .cpp). */
	void ApplyCollision(UInstancedStaticMeshComponent* Comp, uint8 Collision);
	UPhysicalMaterial* GetRailMaterial();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> MeshComponents;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> RuntimeRailMaterial;

	/** Tortugas del público (mallas en ejecución, una por variante de color). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> SpectatorMeshes;

	/** Palmeras del decorado lejano (mallas en ejecución, una por variante). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> FarPalmMeshes;

	/** Puertas que han recibido pórtico (índice de puerta). */
	TArray<bool> DressedGates;

	/** Base de cada pila de neumáticos por lado (GetTireStackBases). */
	TArray<FVector> TireStackBases[2];

	bool bVisuals = true;
	int32 RailSegmentCount = 0;
	int32 BarrierPieceCount = 0;
	int32 DecorCount = 0;
	int32 FarDecorCount = 0;
	int32 SpectatorCount = 0;
	int32 GateMeshCount = 0;
};
