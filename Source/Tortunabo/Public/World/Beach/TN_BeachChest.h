#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachLoot.h"
#include "TN_BeachChest.generated.h"

class APawn;
class ATN_BeachChestSpot;
class UBoxComponent;
class UPointLightComponent;
class UStaticMeshComponent;
struct FTN_InventoryItem;

/**
 * Cofre de la playa del modo carrera (ETNBeachElement::TreasureChest, huella de 350 cm; Docs/Modo_Carrera.md, «Cofres»).
 * Es el elemento que crean el reparto y las fortalezas con ATN_BeachElement::SpawnElement y el spec TreasureChest. Solo
 * existe en el servidor (no se replica): al empezar crea en su sitio, con su giro, el cofre de verdad
 * (ATN_BeachChestSpot), que es lo que se ve, se abre y se replica. Al quitarlo (ronda nueva, ClearElementsAround,
 * TN.Beach.Place clear) se va el cofre con lo que haya soltado y nadie haya cogido. El frente del cofre mira a su +X; si
 * se mueve el elemento después de crearlo, el cofre no le sigue. En la ronda de prueba del editor (Preview Round) crea
 * también el cofre, sin guardarlo.
 *
 * Consola: TN.Beach.Chest pone uno delante de tu tortuga (en el anfitrión; TN.Beach.Place clear lo quita).
 */
UCLASS()
class TORTUNABO_API ATN_BeachChest : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachChest();

	virtual void Destroyed() override;

	/** El cofre que ha creado (servidor, o la ronda de prueba del editor). */
	ATN_BeachChestSpot* GetChestSpot() const;

protected:
	virtual void ApplySpec() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ATN_BeachChestSpot> ChestSpot;
};

/**
 * El cofre que se ve y se abre: una variante playera del cofre del lobby (ATN_TreasureChest), más grande (2,2 veces:
 * 3,4 m de ancho y 2,6 m de alto con la tapa), de madera blanqueada con herrajes oxidados, percebes, algas, una estrella
 * de mar pegada y arena amontonada al pie; el tesoro de dentro lleva perlas y una vieira. Se abre como un rebuscable
 * (hereda de ATN_BeachSearchSpot: mantener E, aro del HUD, tiempo contado en el servidor, «¡puf!» y saltito del objeto),
 * con estas diferencias:
 *  - Tarda más: 5,5 s manteniendo E (SearchSeconds; no hace caso de tn.Search.Seconds), con la tapa que cruje y se
 *    entreabre a tirones y monedas y chispas que saltan hacia la tortuga.
 *  - Una vez por ronda para todas, y mientras una tortuga lo abre las demás no pueden (lo de siempre de los rebuscables).
 *  - Siempre da premio (no hace caso de tn.Search.Luck), de lo mejor para avanzar: los pesos de la carrera sesgados a lo
 *    mejor (ChestWeight). Sale un objeto hacia quien lo abre (el saltito de siempre) y, a la vez, un segundo objeto y
 *    seis conchas de puntos (de 25 a 100) que saltan de dentro a su alrededor. Todo lleva su brillo (el de lo que se coge).
 *  - Queda abierto y vacío (sin el tesoro de dentro), con un brillo dorado apagado. Mientras está por abrir, una columna
 *    de luz dorada se ve de lejos y la luz de dentro late por la rendija de la tapa.
 *
 * Red: el estado de la búsqueda es el de ATN_ProcSearchSpot (replicado y dormido casi siempre); además se replican los
 * premios que saltan y dónde caen (Prizes y PrizeLandings, junto con el resultado), y cada máquina anima sus saltos con
 * el reloj del servidor. Relevante a 400 m (la playa mide 0,8 km). Lo crea ATN_BeachChest en el servidor.
 */
UCLASS()
class TORTUNABO_API ATN_BeachChestSpot : public ATN_BeachSearchSpot
{
	GENERATED_BODY()

public:
	ATN_BeachChestSpot();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── ATN_InteractableBase ─────────────────────────────────────────────────
	virtual float GetHoldDuration() const override;

	/**
	 * Peso de cada objeto del catálogo en el cofre: los de la carrera (TNBeachLoot::RaceWeight) sesgados a lo mejor para
	 * avanzar (la energía sin fin y la barra llena, mucho más; los que derriban o ciegan a las de delante, algo más; la
	 * concha trampa, menos; la cabezota y el tótem, nunca).
	 */
	static float ChestWeight(FName RowName, const FTN_InventoryItem& Row);

	/** Servidor: es el cofre de la cima de una fortaleza (lo pone ATN_BeachChest por TNBeach::FlagSummitPrize). */
	void SetSummitPrize(bool bInSummitPrize) { bSummitPrize = bInSummitPrize; }
	bool IsSummitPrize() const { return bSummitPrize; }

protected:
	// ── ATN_ProcSearchSpot ───────────────────────────────────────────────────
	virtual float GetLuck() const override;
	virtual float GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const override;
	/**
	 * El cofre da lo mejor: los objetos de carrera con los pesos del cofre, según el puesto de quien lo abre. El de la cima de
	 * una fortaleza (SetSummitPrize) usa la tabla de las últimas para cualquier puesto.
	 */
	virtual ETNRaceLootSource GetRaceLootSource() const override { return bSummitPrize ? ETNRaceLootSource::Summit : ETNRaceLootSource::Chest; }
	virtual FVector GetLootOrigin(const APawn* Pawn) const override;
	virtual FVector GetRummageOrigin(const APawn* Searcher) const override;
	virtual FVector FindLanding(const APawn* Pawn, const FVector& From) const override;
	virtual void OnSearchStateChanged(const FTNSearchSpotState& OldState) override;
	virtual bool WantsFrameTick() const override;

	/** Caja del cofre: madera, herrajes y adornos de la playa. Sin colisión (la pone ChestBlock). */
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> ChestBody;

	/** El tesoro de dentro (monedas, gemas, perlas...): se va con los premios y el cofre queda vacío. */
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> ChestTreasure;

	/** Tapa de medio cañón, con el origen en la bisagra (borde de atrás de la caja): se abre girando hacia atrás. */
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> ChestLid;

	/** Colisión del cofre cerrado: se choca con él. */
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UBoxComponent> ChestBlock;

	/** Brillo dorado de dentro: late por la rendija mientras está por abrir, sube con la tapa y queda apagado al vaciarse. */
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UPointLightComponent> GlowLight;

	/** Columna de luz dorada que se ve de lejos mientras está por abrir (la de lo que se coge, más grande). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> BeaconBeam;

private:
	/** Cofre de la cima de una fortaleza: sortea con la tabla de los mejores objetos. Solo lo mira el servidor al abrirlo. */
	bool bSummitPrize = false;

	/** Premios que saltan de dentro al abrirse (el segundo objeto y las conchas de puntos). */
	UPROPERTY(ReplicatedUsing = OnRep_Prizes)
	TArray<TObjectPtr<AActor>> Prizes;

	/** Dónde cae cada premio (el mismo sitio en todas las máquinas, aunque el actor llegue a medio salto). */
	UPROPERTY(Replicated)
	TArray<FVector_NetQuantize10> PrizeLandings;

	UFUNCTION()
	void OnRep_Prizes();

	/** Mallas de la caja, del tesoro y de la tapa (editor y ejecución; no en servidor dedicado). */
	void BuildChestMeshes();

	/** Columna de luz (se crea la primera vez; solo con pantalla). */
	void EnsureBeacon();

	/** Servidor, al abrirse: el segundo objeto y las conchas, alrededor del cofre (Opener: quien lo ha abierto). */
	void SpawnPrizes(const APawn* Opener);

	/** Tapa, tesoro, luz, columna, destellos y crujidos según el estado replicado (máquinas con pantalla). */
	void TickChest(float DeltaSeconds);

	/** Saltos de los premios desde la boca del cofre hasta su sitio (cada máquina, con el reloj del servidor). */
	void TickPrizeHops();

	/** Boca del cofre (centro de la caja a ras de su borde), en el mundo. */
	FVector GetMouthPoint() const;

	/** Distancia (cm) del centro al borde de la caja en la dirección horizontal Dir (mundo). */
	double BodyReach(const FVector& Dir) const;

	/**
	 * Suelo bajo Target, cerca de la cota del cofre (lo de lo alto de una fortaleza no cae al pie); si no lo hay, lo prueba
	 * más cerca del cofre y, si tampoco, a la cota del cofre.
	 */
	FVector GroundNear(const FVector& Target, const APawn* Ignore) const;

	/** Ángulo de la tapa (grados; 0 = cerrada) y su velocidad (grados/s). */
	float LidAngle = 0.f;
	float LidSpeed = 0.f;
	/** Adónde iba la tapa en el último tick (grados). */
	float LidTarget = 0.f;
	float ChestClock = 0.f;
	float CreakClock = 0.f;
	float GlintClock = 0.f;
	float ThumpCooldown = 0.f;
	/** La cámara local está cerca (la luz late a cada fotograma). */
	bool bChestNearView = false;
	/** Premios en el aire en esta máquina, y ya puestos en su sitio (para quien llega tarde). */
	bool bPrizeHopsActive = false;
	bool bPrizesSettled = false;
	/** Premios que ya han caído en esta máquina (bit por premio). */
	uint32 PrizeLandedMask = 0;
};
