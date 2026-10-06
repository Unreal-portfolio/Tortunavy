#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"
#include "TN_RaceItems.generated.h"

class AActor;
class APawn;
class ATortugaCharacter;
class UDataTable;
class UStaticMesh;
class UTexture2D;
class UWorld;

/**
 * Objetos propios de la carrera de la playa (Docs/Modo_Carrera.md, «Objetos de carrera»). Se definen solo desde código, sin
 * filas nuevas en DT_Items: cada uno es un FTN_InventoryItem con UseType == RaceItem e ItemId «Race_<nombre>» (ver
 * TNRaceItems::MakeItem). Su malla y su icono se construyen en ejecución y cada máquina los pone por el ItemId
 * (TNRaceItems::ResolveVisuals), así que no hay que replicar ni guardar ningún asset.
 *
 * Adaptación de los objetos de Mario Kart a la playa de las tortugas:
 *  - Coconut: coco turbo (champiñón), TripleCoconut3/2/1: tres cocos (uno por uso), GoldenCoconut: coco dorado.
 *  - PelicanTaxi: el pelícano que te coge y te lleva volando por delante de todas (bala).
 *  - Sunscreen: protector solar, invulnerable y algo más rápida, derriba lo que toca (estrella).
 *  - HomingCrab: cangrejo teledirigido (concha roja).
 *  - GullStrike: gaviota justiciera que va a por la líder (concha azul).
 *  - SandMine: mina de arena lanzable (bob-omb).
 *  - StormCloud: nube de tormenta que marea a todas las demás (rayo).
 *  - Frisbee: disco volador que va y vuelve (bumerán).
 *  - Whistle: silbato del sargento, aturde a los enemigos de alrededor.
 *  - Box: la caja de objetos del suelo (el «?» de las carreras de karts); nunca se lleva encima: al cogerla sale un objeto
 *    según la posición de quien la coge.
 */
UENUM(BlueprintType)
enum class ETNRaceItem : uint8
{
	None            UMETA(DisplayName = "Ninguno"),
	Box             UMETA(DisplayName = "Caja de objetos"),
	Coconut         UMETA(DisplayName = "Coco turbo"),
	TripleCoconut3  UMETA(DisplayName = "Triple coco (3)"),
	TripleCoconut2  UMETA(DisplayName = "Triple coco (2)"),
	TripleCoconut1  UMETA(DisplayName = "Triple coco (1)"),
	GoldenCoconut   UMETA(DisplayName = "Coco dorado"),
	PelicanTaxi     UMETA(DisplayName = "Pelícano taxi"),
	Sunscreen       UMETA(DisplayName = "Protector solar"),
	HomingCrab      UMETA(DisplayName = "Cangrejo teledirigido"),
	GullStrike      UMETA(DisplayName = "Gaviota justiciera"),
	SandMine        UMETA(DisplayName = "Mina de arena"),
	StormCloud      UMETA(DisplayName = "Nube de tormenta"),
	Frisbee         UMETA(DisplayName = "Disco volador"),
	Whistle         UMETA(DisplayName = "Silbato del sargento"),
	Count           UMETA(Hidden)
};

/** De dónde sale un objeto: cambia los pesos (el cofre da lo mejor). */
UENUM()
enum class ETNRaceLootSource : uint8
{
	/** Un decorado que se rebusca. */
	Search,
	/** La caja de objetos del suelo. */
	Box,
	/** Un cofre de la playa. */
	Chest,
	/**
	 * El cofre de la cima de una fortaleza: los pesos del cofre con la tabla de las últimas para cualquier puesto, así que
	 * hasta quien va la primera puede sacar la bala, el protector o el coco dorado (#741).
	 */
	Summit
};

/** Puesto de una tortuga en la carrera ahora mismo (0 = va la primera). */
struct FTNRaceRank
{
	int32 Place = 0;
	/** Tortugas en carrera (contando esta). */
	int32 Count = 1;
	/** 0 = va la primera, 1 = va la última; 0,5 si va sola. Es lo que decide los pesos. */
	float Norm = 0.5f;
};

namespace TNRaceItems
{
	// ── Valores comunes (los usan el uso de los objetos, el componente y los actores) ─────────────────────────────────

	/** Coco turbo: la velocidad se multiplica (por la de correr) durante estos segundos. */
	constexpr float TurboMultiplier = 2.f;
	constexpr float TurboSeconds = 3.f;
	/** Coco dorado: turbo sin parar durante estos segundos (y energía sin fin). */
	constexpr float GoldenSeconds = 7.f;
	/** Protector solar: segundos, multiplicador de velocidad y radio de contacto (cm); los derribos, en UTN_CombatTuning. */
	constexpr float StarSeconds = 8.f;
	constexpr float StarMultiplier = 1.25f;
	constexpr float StarContactRadius = 320.f;
	/** Tope de la velocidad al sumar efectos (por ejemplo, turbo con el protector puesto). */
	constexpr float MaxSpeedMultiplier = 2.4f;

	// ── Turbo en la predicción del movimiento (FTNSavedMove_Turtle, issue #22) ─────────────────────────────────────────

	/**
	 * Margen (s) en que el servidor sigue aceptando movimientos del dueño marcados con turbo cuando aquí el efecto ya se ha
	 * acabado: el dueño lo ve acabar más tarde (su reloj del servidor va medio ping por detrás) y sus movimientos tardan otro
	 * medio en llegar. Un ping de ida y vuelta más un cuarto de segundo, entre 0,3 y 1 s.
	 */
	inline float BoostGraceSeconds(float RoundTripSeconds)
	{
		return FMath::Clamp(FMath::Max(0.f, RoundTripSeconds) + 0.25f, 0.3f, 1.f);
	}

	/**
	 * Servidor: multiplicador con el que simula un movimiento del dueño marcado con turbo. Current, el de ahora; Recent, el
	 * último mayor que 1 que tuvo, hace SecondsSinceRecent (negativo si nunca). Sin turbo que lo justifique (nunca lo tuvo o
	 * pasó el margen), 1: se simula sin turbo y el dueño recibe la corrección.
	 */
	inline float ResolveClaimedBoost(float Current, float Recent, double SecondsSinceRecent, float GraceSeconds)
	{
		if (Current > 1.f)
		{
			return Current;
		}
		if (Recent > 1.f && SecondsSinceRecent >= 0.0 && SecondsSinceRecent <= static_cast<double>(GraceSeconds))
		{
			return Recent;
		}
		return 1.f;
	}
	/** Silbato del sargento: radio (cm) y segundos de mareo a los enemigos de alrededor. */
	constexpr float WhistleRadius = 5500.f;
	constexpr float WhistleStunSeconds = 5.f;
	/** Nube de tormenta: segundos de aviso sobre cada víctima y de aturdimiento en bola. */
	constexpr float StormTelegraphSeconds = 1.1f;
	constexpr float StormStunSeconds = 2.2f;

	// ── Catálogo ──────────────────────────────────────────────────────────────────────────────────────────────────

	/** ItemId del objeto («Race_Coconut»...). */
	TORTUNABO_API FName IdOf(ETNRaceItem Item);

	/** El objeto de carrera de un ItemId (None si no lo es). */
	TORTUNABO_API ETNRaceItem KindOfId(FName ItemId);

	/** El objeto de carrera de una fila del inventario (None si no lo es). */
	TORTUNABO_API ETNRaceItem KindOf(const FTN_InventoryItem& Item);

	/** Nombre que se ve (español). */
	TORTUNABO_API FText DisplayName(ETNRaceItem Item);

	/** Nombre para el registro y la consola, sin acentos («Coconut», «PelicanTaxi»...). */
	TORTUNABO_API FString CodeName(ETNRaceItem Item);

	/** Lo que hay que buscar en la consola: nombre en inglés, en español o número. false si no es ninguno. */
	TORTUNABO_API bool ParseKind(const FString& Text, ETNRaceItem& OutKind);

	/**
	 * La fila del inventario del objeto: ItemId, UseType == RaceItem, PickupActorClass (el pickup de siempre) y peso 0.
	 * Sin malla ni icono: cada máquina los pone con ResolveVisuals.
	 */
	TORTUNABO_API FTN_InventoryItem MakeItem(ETNRaceItem Item);

	/**
	 * Si Item es de carrera, le pone la malla (EquippedMesh, escala y giro) y el icono de esta máquina (construidos en
	 * ejecución, una vez, sin assets). No hace nada en servidor dedicado ni con otros objetos. Lo llaman el inventario al
	 * recibir un objeto (servidor) o al replicarse (clientes) y los pickups. Los de Todos contra Todos los pasa a
	 * TNTctItems::ResolveVisuals y los de DT_Items a TNCatalogItemVisuals::ResolveVisuals (World/TN_CatalogItemVisuals.h).
	 */
	TORTUNABO_API void ResolveVisuals(FTN_InventoryItem& Item);

	// ── Posición en la carrera y sorteo ─────────────────────────────────────────────────────────────────────────────

	/** Puesto de Pawn entre las tortugas en carrera (por lo que ha avanzado por la playa). Sin playa, va sola. */
	TORTUNABO_API FTNRaceRank GetRank(const APawn* Pawn);

	/**
	 * Peso de un objeto de carrera para quien va en Norm (0 primera, 1 última), según de dónde sale y cuántas corren. Los
	 * de atrás reciben lo que las hace remontar (la bala, el protector, el turbo dorado); los de delante, lo que se lanza y
	 * lo defensivo (Docs/Modo_Carrera.md, «Pesos por posición»).
	 */
	TORTUNABO_API float PositionWeight(ETNRaceItem Item, float Norm, int32 Racers, ETNRaceLootSource Source);

	/** Igual, para los objetos de siempre de DT_Items (por su uso). */
	TORTUNABO_API float PositionWeightForUse(ETN_ItemUseType Use, float Norm, ETNRaceLootSource Source);

	/**
	 * Sortea un objeto para Picker (servidor): los objetos de DT_Items (Catalog) y los de carrera, cada uno con su peso según
	 * el puesto de Picker y la fuente. Catalog nulo = solo los de carrera. false si no sale nada.
	 */
	TORTUNABO_API bool RollLoot(const APawn* Picker, ETNRaceLootSource Source, const UDataTable* Catalog, FTN_InventoryItem& OutItem);

	/** Ruta de DT_Items (el catálogo de siempre). */
	TORTUNABO_API const TCHAR* CatalogPath();

	/** DT_Items cargado (null si no está). */
	TORTUNABO_API const UDataTable* LoadCatalog();

	// ── Uso ─────────────────────────────────────────────────────────────────────────────────────────────────────────

	/**
	 * Servidor: la tortuga usa el objeto de carrera Item (el equipado). Valida, hace el efecto y lo gasta (el triple coco
	 * pasa a tener un uso menos). Si no se puede usar ahora (aturdida, sin nadie a quien apuntar, sin sitio...), suena un
	 * «nop» y el objeto se queda. Lo llama ATortugaCharacter::ServerUseEquippedItem.
	 */
	TORTUNABO_API void ServerUse(ATortugaCharacter* Turtle, const FTN_InventoryItem& Item);

	/** Servidor: da Kind a la tortuga (en la mano o, si está llena, en el caparazón; si no, sustituye lo de la mano). */
	TORTUNABO_API bool GiveItem(ATortugaCharacter* Turtle, ETNRaceItem Kind);

	/** Dirección de lanzamiento de Turtle: hacia donde mira su cámara en el plano, elevada PitchDeg grados. */
	TORTUNABO_API FVector ThrowDirection(const ATortugaCharacter* Turtle, float PitchDeg);

	// ── Estado de la tortuga (lo que dicen el protector y el pelícano) ──────────────────────────────────────────────

	/**
	 * true si nada la puede aturdir ni derribar ahora: protector solar puesto, volando en el pelícano o protegida por el pez
	 * globo (objeto del coop, UTN_CoopItemComponent). Cualquier máquina.
	 */
	TORTUNABO_API bool IsInvulnerable(const AActor* Turtle);

	/** true mientras la lleva el pelícano taxi. Cualquier máquina. */
	TORTUNABO_API bool IsRiding(const AActor* Turtle);

	/** Reloj del servidor en esta máquina (s): el replicado del GameState o, sin él, el del mundo. */
	TORTUNABO_API double ServerNow(const UWorld* World);

	/** true si la tortuga puede usar un objeto ahora (viva, de pie, fuera del caparazón, sin ir en el pico ni en el taxi). */
	TORTUNABO_API bool CanUseNow(const ATortugaCharacter* Turtle);

	/** Las tortugas en carrera (vivas, sin haber llegado). Lo mismo que ATN_BeachEnemy::GatherTurtles. */
	TORTUNABO_API void GatherRacers(const UObject* WorldContext, TArray<ATortugaCharacter*>& Out);

	/** Progreso de una tortuga por la playa (0 = salida, 1 = filo del acantilado); sin playa, a lo largo del eje X. */
	TORTUNABO_API float CourseProgress(const UObject* WorldContext, const FVector& Where);

	/**
	 * Quién es el objetivo de un ataque de Attacker: Ahead (true) = la tortuga más cercana que va por delante; false = la
	 * que va la primera de todas (otra que Attacker). null si no hay.
	 */
	TORTUNABO_API ATortugaCharacter* FindTurtleTarget(const ATortugaCharacter* Attacker, bool bNearestAhead);

	/** Servidor: aturde/derriba comprobando antes que no sea invulnerable. */
	TORTUNABO_API bool CanBeHurt(const ATortugaCharacter* Turtle);
}
