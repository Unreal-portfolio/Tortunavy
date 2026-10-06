#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcMapFlora.h"

/**
 * Decorado vivo de las arenas de Todos contra Todos (#829): lógica pura, sin mundo ni actores. La forma de cada mapa es la de
 * siempre (la variante de Scripts/terrain_volumes); encima se reparten con el sistema del mapa generado de ProcMap y Coop la
 * vegetación (TNProcMap::PlaceFloraRows, las mismas especies por bioma), los animales (anclas para ATN_ProcFauna) y el
 * decorado con colisión (rocas, troncos, castillos de arena... de ATN_BeachDecorField).
 *
 * Todo sale de una semilla y del suelo medido (FHeightField), sin nada que dependa de la calidad gráfica, el orden de carga ni
 * el reloj de cada máquina: el anfitrión y los clientes montan exactamente lo mismo (#828). Lo que tiene colisión se reparte solo
 * donde sobra sitio (FHeightField::OpenAt) y lejos de las salidas y de los puntos de objetos (FKeepOut): nunca cierra un paso.
 */
namespace TNTctScenery
{
	/** «Sin suelo» en FHeightField. */
	inline constexpr float NoGround = -1.0e9f;

	/** Cota del suelo en una rejilla regular (uu), con huecos donde no hay suelo (agua, vacío entre losas). */
	struct FHeightField
	{
		FVector2D Origin = FVector2D::ZeroVector;
		double Cell = 100.0;
		int32 NX = 0;
		int32 NY = 0;
		TArray<float> Z;
		/** Hasta dónde llega el suelo llano desde cada casilla: distancia (uu) a la casilla mala más cercana. */
		TArray<float> Open;

		bool IsValid() const { return NX > 1 && NY > 1 && Z.Num() == NX * NY; }
		float Get(int32 X, int32 Y) const;

		/** Mide el suelo del área [Min, Max] cada CellSize con Sampler(X, Y, OutZ) (false = sin suelo); las cotas, a cm enteros. */
		void Build(const FVector2D& Min, const FVector2D& Max, double CellSize, TFunctionRef<bool(double, double, float&)> Sampler);

		/**
		 * Calcula Open: una casilla es mala si no tiene suelo, si el suelo se inclina más de MaxSlopeDeg o si toca una sin suelo
		 * (un borde); Open es la distancia a la mala más cercana, con tope Cap.
		 */
		void ComputeOpen(float MaxSlopeDeg, float Cap);

		/** Cota en P si las cuatro casillas de alrededor tienen suelo. */
		bool HeightAt(const FVector2D& P, float& OutZ) const;
		/** Normal del suelo en P (la vertical si no se puede medir). */
		FVector NormalAt(const FVector2D& P) const;
		/** Open en P (0 sin suelo). */
		float OpenAt(const FVector2D& P) const;
		FVector2D Max() const { return Origin + FVector2D((NX - 1) * Cell, (NY - 1) * Cell); }
	};

	/** Un sitio que no se llena: las salidas y los puntos de objetos (centro y radio en uu, en el mundo). */
	struct FKeepOut
	{
		FVector2D Center = FVector2D::ZeroVector;
		float Radius = 0.f;
	};

	/** Distancia de P al borde del círculo más cercano (negativa dentro); un número grande sin ninguno. */
	double KeepOutDistance(const TArray<FKeepOut>& KeepOuts, const FVector2D& P);

	/** Una pieza de decorado con colisión (ATN_BeachDecorField): lo que es, dónde (mundo) y cómo. */
	struct FDecorPick
	{
		ETNBeachElement Element = ETNBeachElement::Rock;
		FVector Location = FVector::ZeroVector;
		float YawDeg = 0.f;
		float Scale = 1.f;
	};

	/** Un sitio junto al que viven animales (ATN_ProcFauna::InitCustom). */
	struct FFaunaAnchor
	{
		FVector2D P = FVector2D::ZeroVector;
		float S = 0.f;
		int32 Module = 0;
		ETNProcBiome Biome = ETNProcBiome::Beach;
	};

	struct FPlanOptions
	{
		/** Multiplica la densidad de la vegetación (1 = la del mapa generado). */
		float FloraDensity = 1.f;
		/** Probabilidad de que cada casilla de decorado (de 26 m) lleve una pieza. */
		float DecorChance = 0.55f;
		int32 MaxDecor = 64;
		/** Lo que queda libre alrededor de cada pieza con colisión (uu). */
		float DecorClearance = 600.f;
		/** Anclas de fauna: lado de casilla (uu) y tope. */
		float FaunaCell = 800.f;
		int32 MaxFaunaAnchors = 700;
	};

	struct FPlan
	{
		/** Plantas, rocas pequeñas y objetos sueltos, sin colisión (mundo). Species indexa FloraSpeciesFor(Biome). */
		TArray<TNProcMap::FFloraInstance> Flora;
		/** Decorado con colisión: lo único que importa que sea idéntico en todas las máquinas. */
		TArray<FDecorPick> Decor;
		TArray<FFaunaAnchor> Fauna;
		/** Huella de lo que tiene colisión (para compararla entre máquinas en el log y en las pruebas). */
		uint32 Fingerprint = 0;
	};

	/** Semilla de una arena: la de su manifest, su nombre y la de la partida (la elige el servidor y se replica). */
	uint32 MakeSeed(uint32 ManifestSeed, FName Variant, uint32 MatchSeed);

	/** Bioma de cada arena (por su carácter: playa, selva, volcán, pueblo...). Playa si no la conoce. */
	ETNProcBiome PrimaryBiome(FName Variant);

	/** El bioma con el que se mezcla el principal en manchas. */
	ETNProcBiome SecondaryBiome(ETNProcBiome Primary);

	/** Elementos de decorado que encajan con un bioma (rocas, troncos, castillos de arena...). */
	TArray<ETNBeachElement> DecorElementsFor(ETNProcBiome Biome);

	/** Huella de unas piezas de decorado (su sitio a cm enteros, su giro y su tamaño). */
	uint32 FingerprintOf(const TArray<FDecorPick>& Decor);

	/**
	 * Reparte el decorado vivo de la arena medida en Field (mundo; WaterBaseZ es la cota del mar) con la semilla Seed y sin
	 * llenar los sitios de KeepOuts. Determinista: la misma entrada da la misma salida en cualquier máquina.
	 */
	FPlan MakePlan(const FHeightField& Field, float WaterBaseZ, const TArray<FKeepOut>& KeepOuts, ETNProcBiome Primary, uint32 Seed,
		const FPlanOptions& Options = FPlanOptions());
}
