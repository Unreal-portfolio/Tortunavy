#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Engine/NetSerialization.h"
#include "World/TN_InteractableBase.h"
#include "TN_ProcSearchSpot.generated.h"

class AActor;
class APawn;
class UDataTable;
class USphereComponent;
class UStaticMeshComponent;
class UWorld;
struct FTN_InventoryItem;

namespace TNSearchSynthDSP
{
	// Cola de disparos compartida con el generador de audio (Private/World/ProcMap/TN_ProcSearchSpot.cpp).
	struct FSfxShared;
}

/** Sonidos sintetizados de rebuscar (el orden es el del motor DSP). */
UENUM()
enum class ETNSearchSound : uint8
{
	/** Un puñado de arena y piedrecitas removidas (se repite mientras dura la búsqueda). */
	Rummage,
	/** ¡Puf! Golpe de aire con un tintineo de premio: ha salido algo. */
	Puff,
	/** ¡Pof! Golpe sordo, polvo y un «buuu» bajito: no había nada. */
	Pof,
	/** Crujido de madera a tirones: la tapa de un cofre que se entreabre (ATN_TreasureChest). */
	LidCreak,
	/** ¡Clonc!: la tapa de un cofre que cae sobre la caja, con el tintineo de los herrajes. */
	LidThump,
};

/** Resultado de rebuscar un decorado. */
UENUM()
enum class ETNSearchOutcome : uint8
{
	None,   ///< Sin buscar todavía.
	Found,  ///< Había algo: sale un objeto de un saltito.
	Empty,  ///< No había nada: nube de polvo del bioma.
};

/** Dónde va el anillo fijo de un rebuscable (ATN_ProcSearchSpot::GetMarkerAnchor). */
enum class ETNSearchMarkerAnchor : uint8
{
	Footprint,  ///< Centrado en el decorado y abarcando su huella (lo normal).
	Point,      ///< En un punto propio del rebuscable (el montículo de arena de la playa), algo mayor que él.
	Pending,    ///< Tiene punto propio, pero esta máquina aún no lo conoce: sin anillo hasta entonces.
};

/**
 * El suelo bajo el anillo fijo de un rebuscable y bajo su montículo de arena (#744): el mismo plano en los dos, para que el
 * montículo se incline con la cuesta igual que el anillo.
 */
namespace TNSearchMarker
{
	/** Con una normal más tumbada que esto (Z mínima) el suelo es demasiado empinado: ni anillo ni montículo se inclinan. */
	constexpr double MinTiltNormalZ = 0.6;

	/**
	 * Inclinación (desde +Z) del plano que forman cuatro puntos del suelo a 0°, 90°, 180° y 270° alrededor del centro, a la
	 * misma distancia: la normal sale de las diagonales (0°/180° y 90°/270°). Identidad si es muy empinado (MinTiltNormalZ).
	 */
	inline FQuat GroundTilt(const FVector& P0, const FVector& P90, const FVector& P180, const FVector& P270)
	{
		const FVector Normal = FVector::CrossProduct(P0 - P180, P90 - P270).GetSafeNormal();
		return Normal.Z > MinTiltNormalZ ? FQuat::FindBetweenNormals(FVector::UpVector, Normal) : FQuat::Identity;
	}

	/**
	 * Mira el suelo bajo un anillo de radio RingRadius centrado en Center, en cuatro puntos de su circunferencia (0°, 90°, 180°
	 * y 270°; el centro lo taparía el decorado o el montículo): una traza vertical contra WorldStatic, de 150 cm sobre Center a
	 * 400 bajo él. Un punto sin suelo a mano (la colisión del terreno aún se está cocinando) o muy distinto del centro (un
	 * escalón, otro nivel) cuenta como el suelo del centro. OutPoints son los cuatro puntos con su Z; devuelve cuántos dieron
	 * suelo de verdad (4 = todos). Lo usan el anillo fijo y el montículo de arena de la playa: el mismo suelo para los dos.
	 */
	int32 TraceRimGround(const UWorld* World, const FVector& Center, double RingRadius, const AActor* IgnoreActor, FVector (&OutPoints)[4]);

	/**
	 * Radio (cm) del anillo fijo que rodea algo de radio FootRadius (la base del montículo o la huella del decorado): lo
	 * justo para que los guiones queden un margen más allá de su borde. Es también la distancia a la que se mira el suelo.
	 */
	float RingRadiusForFoot(float FootRadius);
}

/** Huella del decorado (cápsula en planta a lo largo del +X del actor) y color del polvo de su bioma. Se replica una vez. */
USTRUCT()
struct FTNSearchSpotShape
{
	GENERATED_BODY()

	/** Radio de la huella (cm). */
	UPROPERTY()
	float Radius = 100.f;

	/** Semilargo del eje de la cápsula (cm; 0 = redonda). */
	UPROPERTY()
	float HalfLength = 0.f;

	/** Alto aproximado del decorado (cm). */
	UPROPERTY()
	float Height = 150.f;

	/** Color del polvo (valores lineales guardados tal cual, sin sRGB). */
	UPROPERTY()
	FColor Dust = FColor(200, 180, 140);
};

/** Estado de la búsqueda (lo cambia solo el servidor; todos lo leen para el aro del HUD, el sonido y los efectos). */
USTRUCT()
struct FTNSearchSpotState
{
	GENERATED_BODY()

	/** Quién está rebuscando ahora (nullptr = nadie). */
	UPROPERTY()
	TObjectPtr<APawn> Searcher = nullptr;

	/** Hora del servidor (GetServerWorldTimeSeconds) a la que empezó. */
	UPROPERTY()
	float SearchStart = 0.f;

	UPROPERTY()
	ETNSearchOutcome Outcome = ETNSearchOutcome::None;

	/** Hora del servidor del resultado: los efectos solo se ven si llega reciente (no al entrar más tarde en alcance). */
	UPROPERTY()
	float OutcomeTime = 0.f;

	/** De dónde sale el objeto o la nube (a ras del borde del decorado, hacia el que buscaba). */
	UPROPERTY()
	FVector_NetQuantize10 LootFrom = FVector_NetQuantize10(FVector::ZeroVector);

	/** Dónde cae el objeto (en el suelo, a un metro largo). */
	UPROPERTY()
	FVector_NetQuantize10 LootTo = FVector_NetQuantize10(FVector::ZeroVector);

	/** El objeto recogible que ha salido (para su saltito en cada máquina). */
	UPROPERTY()
	TObjectPtr<AActor> LootPickup = nullptr;

	/**
	 * Búsquedas completadas (da la vuelta al pasar de 255). Cada resultado nuevo la cambia: así se distingue también el
	 * segundo, el tercero... de los sitios que se rebuscan más de una vez (el cofre del lobby).
	 */
	UPROPERTY()
	uint8 SearchCount = 0;
};

/**
 * Efectos de sonido de rebuscar, sintetizados en tiempo real (sin archivos de audio): puñados de arena y piedrecitas
 * (granos de fricción por un paso banda, siseo, retumbo de roca y a veces el clic de una chinita), el «¡puf!» de premio
 * (aire que se cierra, «pop» grave y dos notas de campanita) y el «¡pof!» de vacío (golpe sordo, polvo y un «buuu»
 * que baja). Para el cofre del lobby, además, el crujido de la tapa al entreabrirse (roce a tirones por dos resonancias
 * de madera) y su «¡clonc!» al cerrarse (golpe grave, caja que resuena y tintineo de herrajes).
 *
 * Mismo patrón que UTN_PlaygroundSynthComponent: un ISoundGenerator en el hilo de render de audio sin UObjects,
 * asignaciones ni bloqueos, una cola de disparos sin bloqueos desde el hilo de juego, mono y espacializado con la
 * atenuación en código. Solo suena cuando hace falta y se para tras unos segundos de silencio.
 */
UCLASS(ClassGroup = (Audio))
class TORTUNABO_API UTN_SearchSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UTN_SearchSynthComponent(const FObjectInitializer& ObjectInitializer);

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Dispara un efecto (Pitch multiplica las frecuencias; Volume, 0..2). Nada si el oyente está fuera de alcance. */
	void TriggerSound(ETNSearchSound Sound, float Pitch = 1.f, float Volume = 1.f);

	/** Crea y registra el componente en InOwner, en InWorldLocation. Null en servidor dedicado, sin audio o fuera de juego. */
	static UTN_SearchSynthComponent* AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius = 350.f,
		float InFalloff = 2200.f);

	/** Radio con volumen pleno (cm). Solo antes de registrar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Audio", meta = (ClampMin = "0.0"))
	float InnerRadius = 350.f;

	/** Distancia (cm) desde el radio interior en la que se apaga. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Audio", meta = (ClampMin = "100.0"))
	float FalloffDistance = 2200.f;

	/** Volumen general. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Loudness = 1.f;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	void ConfigureSpatial();
	bool IsListenerNear() const;

	TSharedPtr<TNSearchSynthDSP::FSfxShared, ESPMode::ThreadSafe> SfxQueue;

	/** Segundos de silencio que quedan antes de parar el sintetizador. */
	float SilenceLeft = 0.f;
};

/**
 * Decorado del mapa procedural que se puede rebuscar: estatuas, cabezas de piedra, rocas grandes, barcas, carros,
 * cajas, restos... (los elige ATN_ProcMapGenerator::SpawnSearchSpots). El decorado va fundido en las mallas grandes del
 * mapa; este actor ligero (replicado y dormido casi siempre) se pone en su sitio con la huella del decorado y da:
 *  - Aviso «Mantén para rebuscar» al acercarse a su borde por cualquier lado (GetInteractionPointFor = punto del borde
 *    más cercano) y unas chispitas doradas al pie mientras quede por buscar. El anillo dorado de los objetos del suelo (la
 *    marca común de «aquí hay algo que coger», TN_LootGlowKit.h) está fijo en el suelo, centrado en el decorado y
 *    abarcando su huella (MarkerRadius es solo el mínimo) o, si la subclase da un punto propio (GetMarkerAnchor: el
 *    montículo de los de la playa), centrado en él y algo mayor: gira y respira como el de los objetos (más deprisa mientras
 *    alguien rebusca), se ve hasta MarkerDrawDistance de la cámara en todas las máquinas con pantalla y se apaga al
 *    agotarse (IsSpent). No sigue a nadie: el punto del borde por el que se rebusca sigue siendo el de
 *    GetInteractionPointFor, y de él salen la tierra y el objeto.
 *  - Mantener la tecla ~1,3 s (el servidor cuenta el tiempo y vigila que la tortuga siga cerca y en condiciones;
 *    soltar antes cancela): aro de progreso en el HUD, tierra y piedrecitas que saltan y el sonido de rebuscar.
 *  - Al completarse, el servidor sortea (LootChance, 55 %) un objeto de DT_Items (los consumibles y lanzables de
 *    siempre, con su pickup de siempre): «¡puf!», nubecilla blanca con chispas y el objeto sale de un saltito corto y
 *    queda a un metro largo; si no hay suerte, «¡pof!» y una nube pequeña del color del suelo del bioma.
 *  - Cada decorado se rebusca una vez para todo el grupo: el botín queda en el suelo para quien lo coja. Ya buscado no
 *    tiene aviso ni chispas.
 *
 * Red: el estado (FTNSearchSpotState) es replicado; los efectos salen de sus cambios en cada máquina (sin RPC
 * multicast) y solo si el resultado es reciente. El saltito del objeto se anima en cada máquina con pantalla; el
 * pickup (sin movimiento replicado) queda en LootTo en todas.
 *
 * Subclases: con bRepeatable se puede rebuscar otra vez tras RepeatCooldown s de respiro (MaxLootLying limita lo que
 * queda sin recoger), y los ganchos protegidos (GetLuck, GetLootOrigin, GetRummageOrigin, FindLanding,
 * OnSearchStateChanged, WantsFrameTick) cambian la suerte, de dónde sale y dónde cae el objeto y los efectos propios. Lo
 * usa el cofre del tesoro de la torre del homenaje del lobby (ATN_TreasureChest).
 *
 * Pruebas: tn.Search.Luck (forzar la suerte), tn.Search.Seconds (duración), tn.Search.Show (balizas de los buscables) y
 * TN.Debug.Interaction (registro del servidor). Ver Docs/Botin_Decorados.md.
 */
UCLASS()
class TORTUNABO_API ATN_ProcSearchSpot : public ATN_InteractableBase
{
	GENERATED_BODY()

public:
	ATN_ProcSearchSpot();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── ATN_InteractableBase ─────────────────────────────────────────────────
	virtual bool CanInteract(APawn* Interactor) const override;
	virtual FVector GetInteractionPointFor(const APawn* Interactor) const override;
	virtual float GetHoldDuration() const override;
	virtual void BeginHoldInteract(APawn* Interactor) override;
	virtual void EndHoldInteract(APawn* Interactor) override;
	virtual float GetHoldProgress(const APawn* Interactor) const override;

	/**
	 * Servidor, justo después de crearlo: huella del decorado (cápsula en planta a lo largo del +X del actor, que va
	 * en el suelo en el centro del decorado) y color del polvo de su bioma.
	 */
	void SetupSpot(float InRadius, float InHalfLength, float InHeight, const FLinearColor& InDustColor);

	bool IsSearched() const { return SearchState.Outcome != ETNSearchOutcome::None; }

	/**
	 * Sin aviso ni búsqueda posible por ahora: ya buscado (los de una vez) o en el respiro tras el último resultado (los
	 * repetibles). Mientras alguien rebusca no cuenta como agotado.
	 */
	bool IsSpent() const;

	/**
	 * Sorteo de un objeto del catálogo (filas FTN_InventoryItem con PickupActorClass y un uso), con el peso que dé
	 * WeightOf a cada fila (0 o menos la quita). Lo usan los rebuscables y el botín suelto de la playa (TN_BeachLoot.h).
	 */
	static bool PickCatalogItem(const UDataTable* Table, TFunctionRef<float(FName, const FTN_InventoryItem&)> WeightOf,
		FTN_InventoryItem& OutItem);

protected:
	/** Esfera invisible que cubre la huella: la encuentra el escaneo de interactuables (WorldDynamic, solo consultas). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Search")
	TObjectPtr<USphereComponent> ScanSphere;

	/** Segundos que hay que mantener la tecla. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search", meta = (ClampMin = "0.2"))
	float SearchSeconds = 1.3f;

	/** Probabilidad de que salga un objeto. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LootChance = 0.55f;

	/** Catálogo de objetos (filas FTN_InventoryItem con PickupActorClass y un uso). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search")
	TSoftObjectPtr<UDataTable> LootTable;

	/** LootTable ya cargado en BeginPlay (servidor), retenido mientras viva el sitio. */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> PreloadedLootTable;

	/** El catálogo para sortear: el precargado; en partida no se carga nada del disco (M12). */
	const UDataTable* GetLootTable() const;

	/** Peso de cada objeto en el sorteo por nombre de fila o ItemId (1 si no sale aquí; 0 lo quita). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search")
	TMap<FName, float> LootWeights;

	/**
	 * Margen (cm) sobre el alcance de interacción mientras se rebusca antes de cancelar por alejarse. 85 = los 120 de
	 * cuando el alcance era de 350, en proporción al de 250 (#214).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search", meta = (ClampMin = "0.0"))
	float ReachSlack = 85.f;

	/** Se puede rebuscar más de una vez (el cofre del lobby); los decorados del mapa, una sola vez para todo el grupo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search")
	bool bRepeatable = false;

	/** Repetibles: segundos de respiro tras cada resultado, para que salga el objeto antes de volver a empezar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search", meta = (ClampMin = "0.0", EditCondition = "bRepeatable"))
	float RepeatCooldown = 2.5f;

	/** Objetos sin recoger que pueden quedar a la vez de este sitio (0 = sin límite); al pasarse, se quita el más viejo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search", meta = (ClampMin = "0"))
	int32 MaxLootLying = 0;

	/** Tono del sonido de rebuscar (1 = arena y piedrecitas; más alto suena a chismes y monedas). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search|Audio", meta = (ClampMin = "0.25", ClampMax = "3.0"))
	float RummagePitch = 1.f;

	/** Distancia (cm, en planta) de la cámara al borde a la que salen las chispitas de «aquí se puede rebuscar». */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search|FX", meta = (ClampMin = "0.0"))
	float HintDistance = 1800.f;

	/**
	 * Distancia (cm) de la cámara local al borde del anillo hasta la que se dibuja el anillo fijo del decorado (la
	 * RingDrawDistance de los objetos del suelo: 90 m; 0 = sin anillo). En la playa el actor solo existe cerca de alguna
	 * tortuga, así que allí manda antes TNBeachLoot::ProxySpawnDistance.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search|FX", meta = (ClampMin = "0.0"))
	float MarkerDrawDistance = 9000.f;

	/**
	 * Radio mínimo (cm) del anillo. El anillo abarca la huella entera del decorado (SpotShape: radio más semilargo, más un
	 * margen), así que esto solo manda en los diminutos.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Search|FX", meta = (ClampMin = "20.0"))
	float MarkerRadius = 85.f;

	// ── Ganchos para las subclases ───────────────────────────────────────────

	/** Probabilidad de que salga un objeto (por defecto LootChance, o la de tn.Search.Luck si se fuerza). */
	virtual float GetLuck() const;

	/** Agotado para Interactor: sin aviso ni poder rebuscar. Por defecto, IsSpent (igual para todas). */
	virtual bool IsSpentFor(const APawn* Interactor) const { return IsSpent(); }

	/** Agotado para quien juega en esta pantalla: sin chispitas ni anillo. Por defecto, IsSpent (igual para todas). */
	virtual bool IsSpentForLocalView() const { return IsSpent(); }

	/** Peso de una fila del catálogo en el sorteo (por defecto, el de LootWeights por nombre de fila o ItemId, o 1). */
	virtual float GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const;

	/**
	 * Servidor: sortea el objeto que sale al completarse la búsqueda de Searcher (por defecto, de la tabla del coop:
	 * TNCoopItems::RollLoot, con las filas del catálogo con GetLootWeight y los objetos del coop definidos en código).
	 * La playa lo sobrescribe: allí los pesos dependen del puesto de quien rebusca (TNRaceItems::RollLoot).
	 */
	virtual bool PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const;

	/** Servidor: de dónde sale el objeto o la nube al completarse (por defecto, el borde hacia Pawn, a 40 cm). */
	virtual FVector GetLootOrigin(const APawn* Pawn) const;

	/** De dónde saltan la tierra y el sonido mientras Searcher rebusca (por defecto, el borde hacia él, a 20 cm). */
	virtual FVector GetRummageOrigin(const APawn* Searcher) const;

	/** Servidor: dónde cae el objeto que sale de From (por defecto, en el suelo a un metro largo hacia Pawn). */
	virtual FVector FindLanding(const APawn* Pawn, const FVector& From) const;

	/** Cada máquina, después de reaccionar a un cambio del estado (el anfitrión también). */
	virtual void OnSearchStateChanged(const FTNSearchSpotState& OldState) {}

	/** Cierto mientras la subclase necesite el tick a cada fotograma (animaciones propias). */
	virtual bool WantsFrameTick() const { return false; }

	/**
	 * Dónde va el anillo fijo. Por defecto, Footprint: centrado en el decorado y abarcando su huella. Un rebuscable con un
	 * punto propio (el montículo de arena de los de la playa, ATN_BeachSearchSpot) da Point con OutGround (el sitio en el
	 * suelo) y OutFootRadius (radio de su base, cm; el anillo sale algo mayor), o Pending si aún no lo conoce esta
	 * máquina. Tiene que dar lo mismo en todas las máquinas (sale de lo replicado).
	 */
	virtual ETNSearchMarkerAnchor GetMarkerAnchor(FVector& OutGround, float& OutFootRadius) const { return ETNSearchMarkerAnchor::Footprint; }

	const FTNSearchSpotState& GetSearchState() const { return SearchState; }

	/** Hora del servidor (GetServerWorldTimeSeconds; la del mundo sin estado de juego). */
	double ServerNow() const;

	/** Sonido sintetizado en Where (nada en servidor dedicado). */
	void PlaySearchSound(ETNSearchSound Sound, float Pitch, float Volume, const FVector& Where);

	/** Chispitas doradas en Where (nada en servidor dedicado). */
	void EmitSparkles(const FVector& Where, int32 Count, const FVector& Direction, float SpeedScale = 1.f);

private:
	UPROPERTY(ReplicatedUsing = OnRep_SpotShape)
	FTNSearchSpotShape SpotShape;

	UPROPERTY(ReplicatedUsing = OnRep_SearchState)
	FTNSearchSpotState SearchState;

	UFUNCTION()
	void OnRep_SpotShape();

	UFUNCTION()
	void OnRep_SearchState(const FTNSearchSpotState& OldState);

	/** Tamaño y sitio de la esfera de escaneo según la huella. */
	void ApplySpotShape();

	/** Reacción de esta máquina a un cambio del estado (efectos, sonido, colisión, saltito). */
	void HandleStateChanged(const FTNSearchSpotState& OldState);

	/** Servidor: despierta la réplica, aplica el cambio aquí (el anfitrión no recibe OnRep) y lo manda ya. */
	void CommitState(const FTNSearchSpotState& OldState);

	// ── Servidor ──
	void ServerTickSearch();
	void FinishSearch();
	void CancelSearch(const TCHAR* Why);
	bool CanPawnSearch(const APawn* Pawn) const;
	bool IsPawnInReach(const APawn* Pawn, float Slack) const;
	AActor* SpawnLoot(const FTN_InventoryItem& Item, const FVector& Where);
	void ScheduleDormancy();

	// ── Utilidades ──
	/** Punto del borde de la huella más cercano a WorldPoint, a ZAbove cm sobre la base. */
	FVector RimPointToward(const FVector& WorldPoint, float ZAbove) const;
	/** Punto del borde al azar (chispitas). */
	FVector RandomRimPoint(float ZAbove) const;

	// ── Efectos locales (máquinas con pantalla) ──
	void TickLocalFX(float DeltaSeconds);
	void EnsureFX();
	/** Estallido de Count partículas del emisor en Where (Direction: hacia dónde salen; SpeedScale: más o menos fuerte). */
	void BurstFX(int32 Emitter, const FVector& Where, int32 Count, const FVector& Direction = FVector::ZeroVector, float SpeedScale = 1.f);
	void StartHop(AActor* Pickup, double Elapsed);
	void TickHop();
	void DrawDebugSpot(float DeltaSeconds);
	/**
	 * Anillo dorado fijo en el suelo, centrado en el decorado y abarcando su huella (mientras quede por buscar y la cámara
	 * local esté a menos de MarkerDrawDistance): el de los objetos del suelo, con su giro y su respiración.
	 */
	void TickMarker(float DeltaSeconds);

	/**
	 * Centro (en el suelo) y radio (cm) del anillo, según GetMarkerAnchor: la huella entera con un margen (los guiones
	 * quedan fuera del decorado) o el punto propio algo mayor, y al menos MarkerRadius. bOutPending: el punto propio aún no
	 * se conoce (sin anillo todavía).
	 */
	float GetMarkerRing(FVector& OutCenter, bool& bOutPending) const;

	/** Apoya el anillo en el suelo: mira el suelo en cuatro puntos de su circunferencia y se inclina con el plano que forman. */
	void FitMarkerToGround(const FVector& Center, float RingRadius);

	/** Sonido de este decorado (se crea al primer uso). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_SearchSynthComponent> Synth;

	/** Anillo fijo del decorado (se crea la primera vez que hace falta; solo en máquinas con pantalla). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> MarkerRing;

	/** Servidor: objetos que han salido de aquí y no se han recogido (se van con el mapa al regenerarse). */
	TArray<TWeakObjectPtr<AActor>> SpawnedLoot;

	FTimerHandle DormancyTimer;

	/** Emisores de TNAmbientFX (INDEX_NONE hasta que hacen falta). */
	int32 FxSparkle = INDEX_NONE;
	int32 FxDust = INDEX_NONE;
	int32 FxBits = INDEX_NONE;
	int32 FxPoof = INDEX_NONE;
	/** Hora local (s) del último estallido: mientras queden partículas, el actor sigue moviéndolas. */
	double LastFxTime = -100.0;
	float HintClock = 0.f;
	float RummageClock = 0.f;
	float DebugClock = 0.f;
	bool bNearView = false;

	/**
	 * Anillo fijo: cuánto se ve (0-1), su reloj (giro y respiración), su sitio e inclinación en el suelo, el radio con el que
	 * se buscó el suelo, cuándo y cuántas veces (si el suelo aún no tiene colisión se reintenta) y si se anima cada fotograma.
	 */
	float MarkerAppear = 0.f;
	float MarkerClock = 0.f;
	FVector MarkerGround = FVector::ZeroVector;
	FQuat MarkerTilt = FQuat::Identity;
	FVector MarkerFitCenter = FVector::ZeroVector;
	float MarkerFitRadius = -1.f;
	double MarkerTraceTime = -100.0;
	int32 MarkerTraces = 0;
	bool bMarkerGrounded = false;
	bool bMarkerAnimating = false;

	/** Saltito del objeto que ha salido (se anima en cada máquina con pantalla). */
	TWeakObjectPtr<AActor> HopActor;
	TWeakObjectPtr<AActor> HoppedActor;
	double HopStart = 0.0;
	FVector HopFrom = FVector::ZeroVector;
	FVector HopTo = FVector::ZeroVector;
	FRotator HopRotation = FRotator::ZeroRotator;
	bool bHopActive = false;
};
