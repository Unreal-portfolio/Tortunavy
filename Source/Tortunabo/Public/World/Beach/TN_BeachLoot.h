#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/Beach/TN_BeachTypes.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "World/Beach/TN_RaceItems.h"
#include "TN_BeachLoot.generated.h"

class AActor;
class ATN_BeachElement;
class ATN_BeachRaceGenerator;
class ATN_BeachSearchRegistry;
class UDataTable;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
struct FTN_InventoryItem;

/**
 * Botín en la playa del modo carrera (Docs/Modo_Carrera.md, «Botín en la playa» y «Rendimiento y red»). En cada ronda,
 * en el servidor y con la semilla de la ronda:
 *  - casi todo el decorado se puede rebuscar: un registro de puntos rebuscables (uno por pieza elegida del decorado
 *    local) cuyo estado (libre o ya rebuscado) se replica en un bitset (ATN_BeachSearchRegistry); el actor interactivo
 *    (ATN_BeachSearchSpot: el rebuscable del mapa procedural con las reglas de la carrera) se crea solo cuando alguna
 *    tortuga se acerca y se quita cuando se alejan todas;
 *  - hay objetos y power-ups sueltos por todo el recorrido (sueltos y en filas de lado a lado de la playa);
 *  - hay conchas de puntos como en el cooperativo (ATN_ScorePickup): rachas de 1 por los caminos alternativos, arcos de 1
 *    sobre palas, trampolines y catapultas, normales de 25 junto a los peligros y especiales de 50 y 100 en lo difícil o
 *    escondido (lo alto de los castillos, la sala de arriba del castillo con salas, tras el alambre y las minas, sobre
 *    las plataformas móviles).
 * Los objetos, del catálogo de siempre (DT_Items) con los pesos de la carrera.
 */
namespace TNBeachLoot
{
	/**
	 * Servidor: reparte el botín de la ronda actual del generador (quita antes el de la anterior). Pensado para llamarlo
	 * desde ATN_BeachRaceGenerator::GenerateRound justo después de crear los elementos (SpawnRoundElements); si nadie lo
	 * llama, UTN_BeachLootSubsystem lo hace solo en cuanto ve la ronda nueva (el mismo fotograma o el siguiente).
	 */
	TORTUNABO_API void SpawnRoundLoot(ATN_BeachRaceGenerator& Generator);

	/**
	 * Servidor: los rebuscables cuyo borde toca el círculo (WorldCenter, Radius cm) dejan de estar (se marcan como usados y
	 * se quita su actor). Lo llama ATN_BeachRaceGenerator::ClearElementsAround al quitar el decorado del nido del sprint.
	 */
	TORTUNABO_API void ClearSearchAround(ATN_BeachRaceGenerator& Generator, const FVector& WorldCenter, float Radius);

	/** Un punto rebuscable del registro (servidor): la huella de su pieza de decorado. */
	struct FSearchPoint
	{
		/** Índice de la pieza en el reparto (GetRoundLayout().Items). */
		int32 Item = INDEX_NONE;
		/** Centro en el suelo y eje del lado largo (mundo); radio, semilargo y alto (cm). */
		FVector Center = FVector::ZeroVector;
		FVector Axis = FVector::ForwardVector;
		float Radius = 100.f;
		float HalfLength = 0.f;
		float Height = 150.f;
		double Progress = 0.0;
	};

	/**
	 * Dónde queda el actor interactivo de un punto rebuscable (servidor): en su montículo, no en el centro del decorado (#254).
	 * El aviso «Mantén para rebuscar», el alcance, las chispitas, el anillo y la tierra salen así donde se ve que se rebusca y
	 * no por cualquier lado de un decorado grande (un castillo da la vuelta de más de 20 m y el montículo está en un lado).
	 * Ground es el pie del montículo en la arena y Radius, su base con los terrones que asoman (cm).
	 */
	struct FSearchAnchor
	{
		FVector Ground = FVector::ZeroVector;
		float Radius = 0.f;
	};

	/** Alto (cm) que se le da a la huella de ese actor (el montículo del tutorial usa lo mismo): ventana vertical del aviso y chispitas. */
	constexpr float MoundSpotHeight = 110.f;

	/**
	 * Giros (grados) respecto al lado por el que se llega (hacia la salida, ±50°) con los que se prueba, por orden, dónde
	 * poner el montículo de un punto rebuscable: ese lado, los costados, la espalda y, al final, las diagonales.
	 */
	constexpr double MoundSideTurns[] = { 0.0, 90.0, -90.0, 180.0, 45.0, -45.0, 135.0, -135.0 };
	constexpr int32 NumMoundSides = UE_ARRAY_COUNT(MoundSideTurns);

	/**
	 * El primer lado libre (IsClear(i) para MoundSideTurns[i], por orden y sin preguntar de más) o INDEX_NONE si ninguno: un
	 * decorado sin sitio libre para su montículo no es rebuscable (#254). Antes se dejaba el montículo en el primer lado aunque
	 * estuviera ocupado: metido en otra pieza o en el agua de una poza, y el aviso salía sin montículo a la vista.
	 */
	inline int32 PickMoundSide(TFunctionRef<bool(int32)> IsClear)
	{
		for (int32 Side = 0; Side < NumMoundSides; ++Side)
		{
			if (IsClear(Side))
			{
				return Side;
			}
		}
		return INDEX_NONE;
	}

	/** Catálogo de objetos (el de los rebuscables y las zonas de objetos). */
	inline const TCHAR* CatalogPath() { return TEXT("/Game/Blueprints/Gameplay/Items/DT_Items.DT_Items"); }

	/** Probabilidad de que salga algo al rebuscar en la playa (en el cooperativo, 55 %). */
	constexpr float SearchLuck = 0.7f;

	/**
	 * Rebuscables por ronda como mucho, en cuántos tramos iguales del recorrido se reparten y cuántos por tramo (con la
	 * dificultad normal; SearchSpotScale los multiplica). Son puntos del registro: el actor solo existe cerca de alguien.
	 * 360 y 75 por tramo con 1200 m de recorrido; 240 y 50 con 800 m (TNBeach::CourseLengthScale: la misma densidad).
	 */
	constexpr int32 MaxSearchSpots = static_cast<int32>(360.0 * TNBeach::CourseLengthScale + 0.5);
	constexpr int32 Sections = 6;
	constexpr int32 MaxSearchSpotsPerSection = static_cast<int32>(75.0 * TNBeach::CourseLengthScale + 0.5);

	/**
	 * El actor de un punto rebuscable aparece cuando una tortuga está a menos de ProxySpawnDistance (cm) de su borde y se
	 * quita cuando todas están a más de ProxyReleaseDistance (si nadie lo rebusca y no queda lo que soltó sin coger). Las
	 * chispitas se ven desde 35 m de la cámara y el anillo desde 18 m: aparece antes de hacer falta.
	 */
	constexpr double ProxySpawnDistance = 5000.0;
	constexpr double ProxyReleaseDistance = 7000.0;
	/** Cada cuánto (s) mira el servidor qué rebuscables hacen falta. */
	constexpr float ProxyCheckSeconds = 0.2f;
	/** Distancia (cm) a la que un cliente tiene el actor de un rebuscable (le basta con verlo al acercarse). */
	constexpr float ProxyNetRelevance = 9000.f;

	/** Rebuscables según la dificultad (las ayudas del perfil: fácil ×1,6; difícil ×1,4, que está lleno de todo). */
	float SearchSpotScale(ETNProcDifficulty Difficulty);

	/** Separación entre rebuscables (cm): de centro a centro como poco, y de borde a borde (un rebuscable por corrillo). */
	constexpr double MinSearchSpacing = 900.0;
	constexpr double MinSearchRimGap = 300.0;

	/**
	 * Objetos sueltos por ronda: sueltos (uno por tramo igual del recorrido) y filas de lado a lado de la playa. Con 1200 m
	 * eran 30-38 y 4 filas; con 800 m, 20-25 y 3 (TNBeach::CourseLengthScale).
	 */
	constexpr int32 MinLooseItems = static_cast<int32>(30.0 * TNBeach::CourseLengthScale + 0.5);
	constexpr int32 MaxLooseItems = static_cast<int32>(38.0 * TNBeach::CourseLengthScale + 0.5);
	constexpr int32 ItemRows = static_cast<int32>(4.0 * TNBeach::CourseLengthScale + 0.5);
	/** Separación (cm) de los objetos de una fila, de lado a lado. */
	constexpr double ItemRowStep = 3200.0;
	/** Desde dónde hay objetos sueltos (cm desde la línea de salida: nada en la salida). */
	constexpr double LooseStartX = 9000.0;

	/**
	 * Probabilidad de que un decorado de este tipo sea rebuscable en una ronda: casi todo (lo grande, siempre o casi); 0
	 * = nunca (lo diminuto o fino, la medusa y lo que es camino).
	 */
	float SearchChance(ETNBeachElement Element);

	/**
	 * Peso de cada objeto del catálogo en la carrera (rebuscables y sueltos), por su uso: los que ayudan a correr o
	 * fastidian a las demás, más; la cabezota, poco (en la playa no protege de nada); el tótem, nada (no se muere).
	 */
	float RaceWeight(FName RowName, const FTN_InventoryItem& Row);

	/** Color del polvo de rebuscar en la arena. */
	FLinearColor SandDust();

	/**
	 * Si un disco de Radius cm en Local (espacio del generador) queda libre de lo que ocupa cada elemento del reparto de
	 * la ronda (TNBeachLayout::Clearance; las gaviotas, que van por encima, no cuentan), salvo el de SkipIndex, y fuera
	 * del agua de las pozas.
	 */
	bool IsClearOfLayout(const ATN_BeachRaceGenerator& Generator, const FVector2D& Local, double Radius, int32 SkipIndex = INDEX_NONE);
}

/**
 * Decorado de la playa que se puede rebuscar: el ATN_ProcSearchSpot de siempre (mantener E, aro, «¡puf!» u «¡pof!»,
 * saltito del objeto, una vez para todas) con las reglas de la carrera: más suerte (TNBeachLoot::SearchLuck), los pesos
 * de TNBeachLoot::RaceWeight, polvo de arena y las pistas visuales a la escala de la playa (chispitas desde más lejos y
 * el anillo fijo). Lo crea UTN_BeachLootSubsystem en el montículo de su punto y con la huella de su base
 * (TNBeachLoot::FSearchAnchor): el aviso solo sale junto a un montículo (#254).
 *
 * Montículo propio: cada punto rebuscable tiene su montículo de arena que vibra (ATN_BeachSearchRegistry, mismo índice).
 * El servidor le dice al actor qué índice es (SetMoundIndex, replicado una vez) y el anillo fijo se centra en ese
 * montículo, a ras de su arena y algo mayor que él (GetMarkerAnchor). Sin índice (el cofre de la playa, que hereda de
 * esta clase), el anillo abarca la huella como siempre.
 */
UCLASS()
class TORTUNABO_API ATN_BeachSearchSpot : public ATN_ProcSearchSpot
{
	GENERATED_BODY()

public:
	ATN_BeachSearchSpot();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor, justo después de crearlo: el punto del registro (y del montículo) que es este actor. */
	void SetMoundIndex(int32 InMoundIndex) { MoundIndex = InMoundIndex; }

	/** Alguien lo está rebuscando ahora. */
	bool IsBeingSearched() const { return GetSearchState().Searcher != nullptr; }

	/** Lo que salió de aquí sigue en el suelo sin coger (si se quitara el actor, se iría con él). */
	bool HasLootLying() const { return IsValid(GetSearchState().LootPickup.Get()); }

protected:
	virtual float GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const override;

	/** El anillo fijo, en el montículo de este punto (Pending mientras el registro no lo tenga montado en esta máquina). */
	virtual ETNSearchMarkerAnchor GetMarkerAnchor(FVector& OutGround, float& OutFootRadius) const override;

	/**
	 * Los pesos dependen del puesto de quien rebusca (TNRaceItems::RollLoot): a las de atrás les tocan los objetos que
	 * hacen remontar (el pelícano taxi, el protector solar...) y a las de delante, lo que se lanza y lo defensivo. Suma los
	 * objetos de carrera definidos en código a los de DT_Items.
	 */
	virtual bool PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const override;

	/** De dónde sale lo que se encuentra aquí (un rebuscable de la playa; el cofre lo cambia por el suyo). */
	virtual ETNRaceLootSource GetRaceLootSource() const { return ETNRaceLootSource::Search; }

private:
	/** Índice de su punto en el registro (y de su montículo); INDEX_NONE = sin montículo propio. Se replica una vez. */
	UPROPERTY(Replicated)
	int32 MoundIndex = INDEX_NONE;

	/** El registro de este mundo, buscado la primera vez que hace falta (solo para el anillo; en máquinas con pantalla). */
	mutable TWeakObjectPtr<ATN_BeachSearchRegistry> MoundRegistry;
};

/** Estado replicado de los puntos rebuscables de la ronda: cuántos hay y cuáles ya se han rebuscado (un bit por punto). */
/**
 * Montículo de arena removida de un punto rebuscable (lo que se ve en todas las máquinas), compacto: 8 bytes. Sitio en
 * el espacio del generador (X cada 2 cm; Y y Z cada cm), giro y aspecto.
 */
USTRUCT()
struct FTNBeachSearchMound
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 X = 0;

	UPROPERTY()
	int16 Y = 0;

	UPROPERTY()
	int16 Z = 0;

	/** Giro alrededor de Z (256 pasos por vuelta). */
	UPROPERTY()
	uint8 Yaw = 0;

	/** Variante (bits 0-1: liso, chapa, palito de helado o trozo de concha asomando) y tamaño (bits 2-7: 0,8-1,25). */
	UPROPERTY()
	uint8 Look = 0;
};

USTRUCT()
struct FTNBeachSearchNet
{
	GENERATED_BODY()

	/** Ronda del generador (RoundNet.Round) de estos puntos; 0 = ninguno. */
	UPROPERTY()
	int32 Round = 0;

	/** Tirada del botín de esa ronda (TN.Beach.Loot.Reroll la cambia). */
	UPROPERTY()
	int32 Salt = 0;

	UPROPERTY()
	int32 Count = 0;

	/** Bit i = punto i ya rebuscado (o quitado). */
	UPROPERTY()
	TArray<uint32> UsedBits;

	/** El montículo de cada punto (mismo índice). Se manda una vez por ronda. */
	UPROPERTY()
	TArray<FTNBeachSearchMound> Mounds;
};

namespace TNBeachSearchMoundTypes
{
	/** Un montículo en esta máquina: sus instancias (vivo y aplanado) y cómo está. */
	struct FMound
	{
		FTransform LiveXf;
		FTransform FlatXf;
		/** Giro alrededor de Z, sin la inclinación del suelo (la pose es esa inclinación x este giro; #744). */
		FQuat YawRot = FQuat::Identity;
		int32 Variant = 0;
		int32 LiveInstance = INDEX_NONE;
		int32 FlatInstance = INDEX_NONE;
		bool bFlatShown = false;
		/** Hay una tortuga cerca (tiembla más a menudo y más fuerte). */
		bool bTurtleNear = false;
		/**
		 * Ya apoyado en la malla del terreno, con las mismas trazas que su anillo (FitMoundToGround). Hasta entonces, y para los
		 * lejanos, la pose sale de la altura del generador (que no siempre coincide con la malla).
		 */
		bool bGroundFitted = false;
		/** Veces que se ha intentado apoyar y a partir de cuándo (s del mundo) se vuelve a intentar. */
		uint8 FitTries = 0;
		double NextFitTime = 0.0;
	};

	/** Montículo que tiembla ahora (un componente de la reserva, cerca de una cámara local). */
	struct FMoundAnim
	{
		int32 Mound = INDEX_NONE;
		float Time = 0.f;
		float NextBurst = 0.f;
		float BurstLeft = 0.f;
		float BurstLength = 0.f;
		float Strength = 0.f;
		/** Aplastándose al quedar rebuscado (s desde que empezó; < 0 = no). */
		float Flatten = -1.f;
		FVector ShakeAxis = FVector(1.0, 0.0, 0.0);
	};
}

/**
 * Registro replicado de los rebuscables de la playa (uno por mundo; lo crea el servidor). Solo lleva el estado de cada
 * punto, compacto (FTNBeachSearchNet: un bit por punto y su montículo), siempre relevante y dormido salvo al cambiar:
 * cientos de rebuscables cuestan unos pocos bytes. Los puntos (su huella) los tiene el servidor; el actor interactivo
 * de cada uno (ATN_BeachSearchSpot) solo existe cerca de alguna tortuga.
 *
 * Montículos (TN_BeachSearchMounds.cpp; Docs/Modo_Carrera.md y Docs/Botin_Decorados.md, «Montículos de arena»): junto a
 * cada punto, en la arena, un montículo pequeño de arena removida (a veces con una chapa, un palito o un trozo de concha
 * asomando) dice «aquí se puede rebuscar». En cada máquina con pantalla y a partir de lo replicado: instanciados (uno
 * por variante) y quietos lejos; cerca de una cámara local (MoundAnimRange, los MaxMoundAnims más cercanos) un
 * componente de una reserva los hace temblar a ratos, con granitos que saltan, más a menudo y más fuerte con una tortuga
 * cerca. Rebuscado, se aplasta y queda aplanado y quieto.
 */
UCLASS(NotPlaceable)
class TORTUNABO_API ATN_BeachSearchRegistry : public AActor
{
	GENERATED_BODY()

public:
	ATN_BeachSearchRegistry();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** El registro de este mundo (null si aún no hay). */
	static ATN_BeachSearchRegistry* Find(const UObject* WorldContext);

	/** Servidor: puntos nuevos (uno por montículo), todos libres. */
	void ServerReset(int32 Round, int32 Salt, const TArray<FTNBeachSearchMound>& InMounds);

	/** Servidor: el punto Index ya está rebuscado (o quitado). */
	void ServerMarkUsed(int32 Index);

	bool IsUsed(int32 Index) const;
	int32 NumPoints() const { return SearchNet.Count; }
	int32 NumUsed() const;
	int32 GetRound() const { return SearchNet.Round; }

	/** Montículos que tiemblan ahora en esta máquina (TN.Beach.Perf). */
	int32 NumMoundAnims() const;

	/**
	 * El montículo del punto Index tal como está montado en esta máquina: el sitio de su base en el suelo (mundo) y el radio
	 * de su base (cm, con tamaño y terrones). False si no hay pantalla o aún no está montado. Lo usa el anillo fijo de
	 * ATN_BeachSearchSpot.
	 */
	bool GetMoundFoot(int32 Index, FVector& OutGround, float& OutRadius) const;

	/** Distancia (cm) a una cámara local hasta la que tiembla un montículo, y cuántos a la vez como mucho. */
	static constexpr float MoundAnimRange = 4500.f;
	static constexpr int32 MaxMoundAnims = 16;
	/** Distancia (cm) de una tortuga a la que el montículo tiembla más a menudo y más fuerte. */
	static constexpr float MoundNearTurtle = 1200.f;
	/** Distancia (cm) a la cámara hasta la que se dibujan. */
	static constexpr float MoundCullDistance = 12000.f;
	/**
	 * Distancia (cm) a una cámara local a la que un montículo se apoya en la malla del terreno (con las trazas de su anillo),
	 * cuántos como mucho por revisión (cada 0,25 s) y cuántas veces se intenta si la colisión aún no está lista.
	 */
	static constexpr float MoundFitRange = 6500.f;
	static constexpr int32 MaxMoundFitsPerCheck = 6;
	static constexpr int32 MaxMoundFitTries = 8;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Search")
	TObjectPtr<USceneComponent> RegistryRoot;

private:
	UPROPERTY(ReplicatedUsing = OnRep_SearchNet)
	FTNBeachSearchNet SearchNet;

	UFUNCTION()
	void OnRep_SearchNet();

	FTimerHandle SleepTimer;

	/** Despierto para mandar un cambio y dormido otra vez al poco. */
	void WakeForChange();

	// ── Montículos (máquinas con pantalla; TN_BeachSearchMounds.cpp) ──

	/** Uno por variante con los montículos vivos y el último con los aplanados. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> MoundComps;

	/** Reserva de componentes que hacen temblar los montículos cercanos. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MoundAnimPool;

	TArray<TNBeachSearchMoundTypes::FMound> Mounds;
	TArray<TNBeachSearchMoundTypes::FMoundAnim> MoundAnims;
	/** Ronda, tirada y número de montículos con que se montaron (si cambia, se rehacen). */
	uint32 BuiltMoundKey = 0;
	float MoundCheckClock = 0.f;
	int32 FxGrains = INDEX_NONE;
	double LastGrainsTime = -100.0;

	bool HasVisuals() const;
	/** Pone los montículos como dice lo replicado (los rehace si cambian; aplana los rebuscados). */
	void RefreshMounds();
	void RebuildMounds();
	UInstancedStaticMeshComponent* EnsureMoundComp(int32 Index);
	void SetMoundFlat(int32 Index, bool bFlat);
	void UpdateMoundAnims();
	/**
	 * Apoya el montículo Index en la malla del terreno: su sitio y su inclinación salen de las mismas cuatro trazas que usa su
	 * anillo (TNSearchMarker::TraceRimGround), y así quedan en el mismo plano (#744). Sin colisión todavía, se reintenta.
	 */
	void FitMoundToGround(int32 Index);
	void StartMoundAnim(int32 Index);
	void StopMoundAnim(int32 Slot);
	void StopAllMoundAnims();
	/** Pose del montículo que tiembla en el hueco Slot; false si ha acabado de aplastarse. */
	bool PoseMoundAnim(int32 Slot, float DeltaSeconds);
	void EmitGrains(const FVector& Where, int32 Count, float SpeedScale);
};

/**
 * Reparte el botín de cada ronda de la playa, en el servidor, sin tocar el generador (ATN_BeachRaceGenerator): cuando
 * cambia la ronda quita el botín de la anterior (los rebuscables se llevan lo que nadie recogió; las conchas y los
 * objetos sueltos que quedan, también) y reparte el de la nueva (TNBeachLoot::SpawnRoundLoot lo adelanta):
 *  - Rebuscables: entre el decorado de la ronda, con la probabilidad de TNBeachLoot::SearchChance, uno por corrillo (9 m
 *    entre centros y 3 m entre bordes), hasta TNBeachLoot::MaxSearchSpots repartidos a lo largo. Cada uno con la huella
 *    real de su decorado (la caja de su malla, girada como ella: cápsula a lo largo del lado largo), en un registro de
 *    puntos (FSearchPoint) con su estado replicado en ATN_BeachSearchRegistry; el actor de cada punto se crea cerca de
 *    una tortuga y se quita al alejarse todas (TickSearchProxies), puesto en su montículo (FSearchAnchor) y no en el centro
 *    del decorado. Un decorado sin sitio libre para su montículo no es rebuscable.
 *  - Objetos sueltos: uno por tramo igual del recorrido desde los 90 m y filas de lado a lado en cuatro sitios, en la
 *    arena libre (a 3 m de cualquier huella del reparto, pasos de quads incluidos).
 *  - Conchas de puntos (TN_BeachLootShells.cpp).
 *
 * Consola: TN.Beach.Loot 0 (sin botín ni conchas desde la ronda siguiente) y TN.Beach.Loot.Reroll (lo vuelve a
 * repartir ya). Registro: «[Playa] botín de la ronda N: ...» y «[Playa] conchas de la ronda N: ...».
 */
UCLASS()
class TORTUNABO_API UTN_BeachLootSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Servidor: reparte el botín de la ronda actual de Gen (quita antes el que hubiera). */
	void SpawnForRound(ATN_BeachRaceGenerator& Gen);

	/** Servidor: quita el botín de la ronda actual y lo reparte otra vez. */
	void Reroll();

	/** Servidor: los puntos rebuscables que toca el círculo dejan de estar (TNBeachLoot::ClearSearchAround). */
	void ClearSearchAround(const FVector& WorldCenter, float Radius);

	/** Puntos rebuscables de la ronda y actores que hay ahora (servidor). */
	int32 NumSearchPoints() const { return SearchPoints.Num(); }
	int32 NumSearchProxies() const;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/**
	 * Elige los puntos rebuscables de la ronda (el registro) con el montículo de cada uno; devuelve cuántos. OutNoMoundSite
	 * cuenta los que habrían salido pero no tenían sitio libre para su montículo (no se crean).
	 */
	int32 BuildSearchRegistry(ATN_BeachRaceGenerator& Gen, FRandomStream& Rng, int32& OutCandidates, int32& OutNoMoundSite);
	/** Crea el actor de los puntos a los que se acerca alguna tortuga y quita el de los que ya nadie tiene cerca. */
	void TickSearchProxies();
	ATN_BeachSearchRegistry* EnsureRegistry();
	void MarkSearchUsed(int32 Index);
	int32 SpawnLooseItems(ATN_BeachRaceGenerator& Gen, FRandomStream& Rng, const UDataTable& Catalog);
	/** Conchas de puntos de la ronda (TN_BeachLootShells.cpp); devuelve el resumen para el registro. */
	FString SpawnRoundShells(ATN_BeachRaceGenerator& Gen, int32 Seed);
	void ClearLoot();

	TWeakObjectPtr<ATN_BeachRaceGenerator> CachedGenerator;

	/** Lo repartido en esta ronda (servidor): objetos sueltos y conchas. */
	TArray<TWeakObjectPtr<AActor>> SpawnedItems;
	TArray<TWeakObjectPtr<AActor>> SpawnedShells;

	/** Registro de puntos rebuscables de la ronda (servidor), su actor si lo tiene ahora y si ya se han usado. */
	TArray<TNBeachLoot::FSearchPoint> SearchPoints;
	/** Dónde se pone el actor de cada punto: su montículo (mismo índice que SearchPoints y que los montículos del registro). */
	TArray<TNBeachLoot::FSearchAnchor> SearchAnchors;
	TArray<TWeakObjectPtr<ATN_BeachSearchSpot>> SearchProxies;
	TArray<bool> SearchUsed;
	TWeakObjectPtr<ATN_BeachSearchRegistry> Registry;
	float ProxyClock = 0.f;

	/** Centro (XYZ) y radio del borde (W) de cada rebuscable de la ronda, para no poner objetos sueltos pegados a ellos. */
	TArray<FVector4> SpotDiscs;

	/** Ronda cuyo botín está repartido (0 = ninguna). */
	int32 LootRound = 0;
	/** Tiradas de TN.Beach.Loot.Reroll (cambian la semilla del botín de la misma ronda). */
	int32 RerollSalt = 0;
	float FindClock = 0.f;
};
