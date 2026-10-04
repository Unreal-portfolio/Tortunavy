#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/Beach/TN_BeachLayout.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_BeachRaceGenerator.generated.h"

class ACharacter;
class ATN_BeachDecorField;
class ATN_BeachElement;
class ATN_BeachRaceGenerator;
class ATN_ProcWaterVolume;
/** Teselas del terreno calculadas en otro hilo y pendientes de subir (TN_BeachRaceGenerator_Build.cpp). */
struct FTNBeachTileBatch;
/** Ronda que se está montando por partes en esta máquina (TN_BeachRaceGenerator_Round.cpp). */
struct FTNBeachRoundBuild;
class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBeachTurtleReachedWater, ACharacter*, Turtle);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBeachTurtleReachedWaterNative, ACharacter* /*Turtle*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBeachRoundLayoutReady, ATN_BeachRaceGenerator* /*Generator*/);

/** Lo único que se replica de la ronda: con la semilla, cada cliente rehace los asientos (los elementos llegan solos). */
USTRUCT()
struct FTNBeachRoundNet
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Seed = 0;

	/** Se incrementa en cada GenerateRound (y en ClearRound); 0 = sin ronda. */
	UPROPERTY()
	int32 Round = 0;

	/** La ronda se ha quitado (ClearRound): playa vacía y sin asientos. */
	UPROPERTY()
	bool bCleared = false;

	/** Los huevos de la salida se han roto (al dar la salida); cada ronda nueva los cierra. */
	UPROPERTY()
	bool bStartOpen = false;

	/** Hora del servidor (GetServerWorldTimeSeconds) a la que se rompieron. */
	UPROPERTY()
	float StartOpenTime = 0.f;

	/** Los huevos están en la línea del sprint final (SetStartEggsAtSprint); cada ronda nueva los devuelve a la salida. */
	UPROPERTY()
	bool bSprintEggs = false;

	/** Dificultad con la que se repartió la ronda: con la semilla, cada cliente rehace el mismo reparto y el mismo decorado. */
	UPROPERTY()
	ETNProcDifficulty Difficulty = ETNProcDifficulty::Normal;

	/**
	 * La ronda se repartió para el sprint final (TNBeachLayout::GenerateRound con bSprint): el nido de los huevos vacío y el
	 * castillo principal lejos de él. Va con la semilla: cada cliente rehace el mismo reparto.
	 */
	UPROPERTY()
	bool bSprintLayout = false;
};

/**
 * Círculos donde se ha quitado lo de la ronda (ClearElementsAround: el nido del sprint). Los actores se destruyen en el
 * servidor y la destrucción llega sola; el decorado es local, así que cada máquina quita el suyo con esta lista.
 */
USTRUCT()
struct FTNBeachDecorCuts
{
	GENERATED_BODY()

	/** Ronda (RoundNet.Round) a la que pertenecen. */
	UPROPERTY()
	int32 Round = 0;

	/** Cada círculo: X e Y en el espacio del generador y Z = radio (cm). */
	UPROPERTY()
	TArray<FVector> Circles;
};

/** Tiempos de la última ronda en esta máquina (registro «[Playa] ronda N: reparto X ms, decorado Y ms...» y TN.Beach.Perf). */
struct FTNBeachRoundTimings
{
	int32 Round = 0;
	bool bServer = false;
	/** Reparto (en otro hilo, con las teselas del terreno calculadas ahí mismo). */
	double LayoutMs = 0.0;
	/** Espera en el hilo de juego a que acabe el reparto (fotogramas sin nada que hacer, no tiempo gastado). */
	double LayoutWaitMs = 0.0;
	/** Subir las teselas con los asientos (colisión incluida), decorado local, actores (servidor) y botín (servidor). */
	double TerrainMs = 0.0;
	double DecorMs = 0.0;
	double ElementsMs = 0.0;
	double LootMs = 0.0;
	/** De GenerateRound (o de llegar la semilla) a la ronda lista, y en cuántos fotogramas. */
	double WallMs = 0.0;
	int32 Frames = 0;
	/** Actores replicados creados (servidor) e instancias de decorado. */
	int32 Actors = 0;
	int32 DecorInstances = 0;
};

/**
 * La playa del modo carrera (LVL_BeachRace, Docs/Modo_Carrera.md): el terreno fijo y el reparto procedural de cada ronda.
 *
 * - Terreno fijo, igual en todas las máquinas y en el editor (TNBeachLayout): 1200 m de arena por 280 m jugables con
 *   desnivel hacia el mar y relieve irregular (dunas, corredores que se separan y se juntan, dunas con cresta y
 *   cornisa, pozas de agua nadable y dos líneas de trincheras con caballones, tablones y sacos terreros), en teselas de
 *   UProceduralMeshComponent con colisión y el material del terreno del mapa procedural (M_ProcTerrain con relieve y
 *   guijarros); a los lados y detrás, bancos que suben a la selva de palmeras y árboles de 200-300 m (vegetación
 *   instanciada del mapa procedural, con lianas, enredaderas, helechos y hojas enormes en los huecos entre copas); al
 *   final, la repisa y el acantilado de roca de 15,5 m (TNBeach::CliffHeight) sobre el agua de meta, con el mar animado,
 *   las banderas que flotan y el arco de neumático de la meta del mapa procedural (a escala). Salida en el linde de la
 *   selva: una fila de cuatro huevos en su nido de arena, entre las raíces de un árbol colosal y bajo hojas enormes, con
 *   el cartel «¡A LA META!»; al dar la salida (OpenStartEggs) las tapas saltan y las tortugas salen lanzadas hacia el
 *   mar. Muros invisibles a los lados, detrás y mar adentro.
 * - Ronda (GenerateRound, servidor): destruye los elementos de la anterior, reparte con la semilla y la dificultad
 *   (TNBeachLayout::GenerateRound, en otro hilo), deja en la arena el asiento de cada elemento (el suelo liso bajo su
 *   huella; rehace solo las teselas tocadas, unas pocas por fotograma), monta el decorado local e instanciado
 *   (ATN_BeachDecorField) y crea cada elemento que no es decorado con ATN_BeachElement::SpawnElement. Se replican la
 *   semilla y la dificultad: cada cliente rehace los mismos asientos y el mismo decorado. El terreno no se cava: los
 *   hoyos (la plataforma) los traen los elementos.
 * - Meta: en el servidor, la primera vez por ronda que los pies de una tortuga tocan el agua de meta avisa con
 *   OnTurtleReachedWater; en cada máquina, un chapuzón al entrar en ella.
 *
 * Espacio local: X hacia el mar (la salida en X = 0, el filo en X ≈ 1200 m), Y a lo ancho y el agua en Z = 0 (ver
 * TN_BeachLayout.h). Consola: TN.Beach.ShowFootprints 1 enseña en juego las huellas del reparto.
 */
UCLASS()
class TORTUNABO_API ATN_BeachRaceGenerator : public AActor
{
	GENERATED_BODY()

public:
	ATN_BeachRaceGenerator();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** El generador de la playa de este mundo (el primero; en LVL_BeachRace solo hay uno). */
	static ATN_BeachRaceGenerator* Find(const UObject* WorldContext);

	// ── Ronda ──────────────────────────────────────────────────────────────

	/**
	 * Servidor: reparte una ronda nueva con esta semilla y la dificultad Difficulty (destruye los elementos de la
	 * anterior) y la replica ya; lo demás se monta por partes (Docs/Modo_Carrera.md, «Rendimiento y red»): el reparto en
	 * otro hilo, los asientos en la arena y el decorado local en varios fotogramas, luego los actores replicados y el
	 * botín. IsRoundReady es false hasta que está todo. Vuelve a armar la meta (cada tortuga avisa otra vez al tocar el
	 * agua). Si la partida está en el sprint final (ATN_BeachRaceGameState::bSprintFinal), el reparto es el de sprint: deja
	 * vacío el nido de los huevos de la línea del sprint y aparta de él el castillo principal (el GameMode lo despeja al
	 * poner a las finalistas).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beach")
	void GenerateRound(int32 InSeed);

	/** Servidor: quita los elementos y sus asientos (playa vacía). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beach")
	void ClearRound();

	/**
	 * La ronda está montada entera en esta máquina: el terreno con sus asientos y el decorado local (en el servidor,
	 * además, sus elementos replicados y el botín). Mientras se monta por partes, false.
	 */
	UFUNCTION(BlueprintPure, Category = "Beach")
	bool IsRoundReady() const;

	UFUNCTION(BlueprintPure, Category = "Beach")
	int32 GetRoundNumber() const { return RoundNet.Round; }

	UFUNCTION(BlueprintPure, Category = "Beach")
	int32 GetRoundSeed() const { return RoundNet.Seed; }

	/** Dificultad con la que se repartió la ronda actual (replicada con ella). */
	UFUNCTION(BlueprintPure, Category = "Beach")
	ETNProcDifficulty GetRoundDifficulty() const { return RoundNet.Difficulty; }

	/**
	 * Dificultad de las rondas que se repartan desde ahora (servidor; la del general, SelectedProcDifficulty). Se replica
	 * con cada ronda (RoundNet), no por sí sola.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beach")
	ETNProcDifficulty Difficulty = ETNProcDifficulty::Normal;

	/** Reparto de la ronda actual en esta máquina (en los clientes, rehecho con la semilla). */
	const TNBeachLayout::FRoundLayout& GetRoundLayout() const { return Layout; }

	/**
	 * Elementos replicados creados en esta ronda (solo servidor): trampas, enemigos, fortalezas, cofres... El decorado no
	 * está: es local e instanciado (GetDecorField).
	 */
	const TArray<TObjectPtr<ATN_BeachElement>>& GetRoundElements() const { return RoundElements; }

	/** El actor creado para el elemento ItemIndex del reparto (solo servidor; null en el decorado y en lo que falte). */
	ATN_BeachElement* GetElementForItem(int32 ItemIndex) const;

	/**
	 * Decorado de la ronda en esta máquina (ATN_BeachDecorField: mallas instanciadas por tipo y variante, con su colisión;
	 * sin replicar, el mismo en todas). Para buscar decorado: GetRoundLayout().Items con TNBeach::CategoryOf == Decor (el
	 * índice del reparto es el del campo: HasItem, GetSearchShape, GetItemBounds). Null hasta la primera ronda.
	 */
	ATN_BeachDecorField* GetDecorField() const { return DecorField; }

	/** Tiempos de la última ronda montada en esta máquina (TN.Beach.Perf). */
	const FTNBeachRoundTimings& GetLastRoundTimings() const { return LastTimings; }

	/**
	 * En cada máquina, al quedar montada entera una ronda (IsRoundReady): en el servidor, con sus elementos y su botín;
	 * en cada cliente, con su decorado (los elementos replicados llegan solos, según la distancia). Para lo que se reparte
	 * después con el mismo diseño (el botín): GetRoundLayout().Interest trae los arcos de salto (libres), las cimas, los
	 * atajos, los rincones, las trincheras y los caminos alternativos, en el espacio local del generador.
	 */
	FOnBeachRoundLayoutReady OnRoundLayoutReady;

	// ── Salida: los huevos ──────────────────────────────────────────────────

	/**
	 * Servidor: rompe los huevos de la salida (las tapas saltan dando vueltas); las tortugas que estén en la salida se ven
	 * 1 s en su huevo (se ponen de pie, se sacuden la cáscara y miran al mar: TNEggHatch) y salen lanzadas hacia el mar a
	 * la vez (servidor y cliente dueño, con el reloj del servidor, como la salida de huevos del cooperativo). Cada ronda
	 * nueva (GenerateRound) los vuelve a cerrar. El GameMode lo llama al dar la salida, con las tortugas ya sueltas.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beach")
	void OpenStartEggs();

	/**
	 * Servidor (consola TN.Beach.Egg): cierra otra vez los huevos de la línea en la que estén, mete a cada tortuga en uno
	 * (por orden de jugador), quieta y mirando al mar, y al poco los rompe con OpenStartEggs. Para probar la salida sin
	 * empezar otra ronda.
	 */
	void ReplayStartEggs();

	UFUNCTION(BlueprintPure, Category = "Beach")
	bool AreStartEggsOpen() const { return RoundNet.bStartOpen; }

	/**
	 * Servidor: lleva el nido de huevos de la salida a la línea del sprint final (bAtSprint) o lo devuelve a la salida, con
	 * los huevos cerrados: bases y anillo de arena en los sitios del sprint (los de GetSprintStartTransform) y las tapas
	 * encima. OpenStartEggs los rompe y lanza hacia el mar a las tortugas de la línea en la que estén. GenerateRound y
	 * ClearRound los devuelven a la salida: el GameMode lo llama justo después de GenerateRound.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beach")
	void SetStartEggsAtSprint(bool bAtSprint);

	UFUNCTION(BlueprintPure, Category = "Beach")
	bool AreStartEggsAtSprint() const { return RoundNet.bSprintEggs; }

	// ── Salida, meta y consultas (cualquier máquina) ────────────────────────

	/**
	 * Dónde empieza el jugador PlayerIndex (4 en fila en la salida y, del quinto al octavo, en la fila de huevos de detrás),
	 * 110 cm sobre el suelo, mirando al mar.
	 */
	UFUNCTION(BlueprintPure, Category = "Beach")
	FTransform GetStartTransform(int32 PlayerIndex) const;

	/** Huevos del nido de la salida (y del sprint): dos filas de cuatro (TNBeachLayout::MaxStartEggs). */
	UFUNCTION(BlueprintPure, Category = "Beach")
	int32 GetNumStartSpots() const { return TNBeachLayout::MaxStartEggs; }

	/**
	 * Sitio Index del sprint de desempate: a mitad del recorrido (TNBeachLayout::SprintLineX, siempre la misma línea), en
	 * fila como la salida (4 a 10 m y más filas detrás), 110 cm sobre el suelo (con los asientos de la ronda), mirando al
	 * mar, sobre arena seca y casi llana, fuera de pozas, trincheras y cornisas.
	 */
	UFUNCTION(BlueprintPure, Category = "Beach")
	FTransform GetSprintStartTransform(int32 Index) const;

	/**
	 * Servidor: quita los elementos de la ronda actual cuya huella toque el círculo de Radius (cm) alrededor de WorldCenter
	 * y devuelve cuántos: destruye los replicados y quita el decorado local en todas las máquinas (DecorCuts, replicado),
	 * con sus rebuscables. Para despejar la salida del sprint; el resto de la ronda (y los asientos en la arena) se queda.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beach")
	int32 ClearElementsAround(const FVector& WorldCenter, float Radius);

	/** Si un punto (los pies de la tortuga) está en el agua de meta: más allá del filo y a ras del agua o por debajo. */
	UFUNCTION(BlueprintPure, Category = "Beach")
	bool IsFinishWater(const FVector& WorldLocation) const;

	/**
	 * Franja de la zambullida: los últimos 7,5 m de la repisa antes del filo y el vacío sobre el agua (hasta 40 m más
	 * allá). La animación pone a la tortuga de cabeza si salta desde aquí.
	 */
	UFUNCTION(BlueprintPure, Category = "Beach")
	bool IsCliffJumpZone(const FVector& WorldLocation) const;

	/** Distancia (cm) al filo del acantilado a lo largo del recorrido: negativa antes del filo, positiva sobre el agua. */
	UFUNCTION(BlueprintPure, Category = "Beach")
	float GetCliffEdgeDistance(const FVector& WorldLocation) const;

	/** Progreso 0..1 de la salida al filo (para el HUD o para ordenar a quien no llegó). */
	UFUNCTION(BlueprintPure, Category = "Beach")
	float GetCourseProgress(const FVector& WorldLocation) const;

	/** Cota del suelo (arena, roca o fondo del mar) bajo un punto, con los asientos de la ronda; sin trazas. */
	UFUNCTION(BlueprintPure, Category = "Beach")
	float GetGroundHeightAt(const FVector& WorldLocation) const;

	/**
	 * La superficie de verdad de la malla del terreno en la vertical de WorldLocation: una traza de arriba abajo solo contra
	 * las teselas con colisión (sin decorado, elementos ni tortugas). OutZ, la primera desde arriba; false si ahí no hay
	 * tesela con colisión. Para confirmar lo que dice GetGroundHeightAt (la red de seguridad y la patada de la tormenta).
	 */
	bool TraceTerrainAt(const FVector& WorldLocation, float& OutZ) const;

	/** Hacia dónde está el mar (el eje X del generador). */
	UFUNCTION(BlueprintPure, Category = "Beach")
	FVector GetSeaDirection() const { return GetActorForwardVector(); }

	/** Servidor: vuelve a armar la meta sin repartir de nuevo (GenerateRound ya lo hace). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beach")
	void ResetFinishWater();

	/** Servidor: una tortuga ha tocado el agua de meta (una vez por tortuga y ronda). */
	UPROPERTY(BlueprintAssignable, Category = "Beach")
	FOnBeachTurtleReachedWater OnTurtleReachedWater;

	FOnBeachTurtleReachedWaterNative OnTurtleReachedWaterNative;

	// ── Editor ─────────────────────────────────────────────────────────────

	/** Reparte una ronda en el editor con EditorSeed (o una al azar) para ver el reparto; no se guarda con el nivel. */
	UFUNCTION(CallInEditor, Category = "Beach|Editor")
	void PreviewRound();

	/** Quita la ronda de prueba del editor. */
	UFUNCTION(CallInEditor, Category = "Beach|Editor")
	void ClearPreview();

	UPROPERTY(EditAnywhere, Category = "Beach|Editor")
	int32 EditorSeed = 1;

	UPROPERTY(EditAnywhere, Category = "Beach|Editor")
	bool bEditorRandomSeed = false;

	/** Dibuja en el suelo las huellas del reparto (en el editor; en juego, con TN.Beach.ShowFootprints 1). */
	UPROPERTY(EditAnywhere, Category = "Beach|Editor")
	bool bShowFootprints = true;

	/** Multiplica la selva de los bordes. */
	UPROPERTY(EditAnywhere, Category = "Beach", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float JungleDensity = 1.f;

	/**
	 * Si nadie reparte en unos segundos (nivel abierto sin el GameMode de la carrera), el servidor reparte una ronda al azar
	 * para que se pueda jugar.
	 */
	UPROPERTY(EditAnywhere, Category = "Beach")
	bool bAutoGenerateIfIdle = true;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Beach")
	TObjectPtr<USceneComponent> BeachRoot;

	/**
	 * Mallas generadas en código (RF_Transient; sus punteros, Transient: si se guardaran, al cargar el nivel llegarían a
	 * nulo). Repisa, acantilado y rocas del pie (con colisión).
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> CliffMesh;

	/** Fondo del mar (con colisión). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> SeabedMesh;

	/** Superficie del mar (sin colisión: el agua es el volumen nadable). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> SeaMesh;

	/** Salida: tronco, raíces, tallos y postes del cartel (con colisión). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> GroveSolidMesh;

	/** Salida: copa, hojas enormes y el cartel (sin colisión). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> GroveDecoMesh;

	/** Meta: neumático en arco y mástiles (con colisión). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> FinishSolidMesh;

	/** Meta: rótulos, banderines y banderolas (sin colisión). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> FinishDecoMesh;

	/** Boyas con banderas a cuadros que flotan (y se mecen) delante del acantilado. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> FloatMesh;

	/** Huellas del reparto dibujadas en el suelo (editor; en juego, con TN.Beach.ShowFootprints 1). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> FootprintMesh;

	/** Relieve fijo con colisión: caballones, sacos y puentes de las trincheras, cornisas de las crestas y rocas de las pozas. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> FeatureMesh;

	/** Relieve fijo sin colisión: tarimas del fondo de las trincheras y postes de los tablones. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> FeatureDecoMesh;

	/** Superficie del agua de las pozas (el agua nadable es el volumen). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Beach")
	TObjectPtr<UProceduralMeshComponent> PoolMesh;

private:
	UPROPERTY(ReplicatedUsing = OnRep_RoundNet)
	FTNBeachRoundNet RoundNet;

	UFUNCTION()
	void OnRep_RoundNet();

	/** Teselas del terreno (creadas al construir; transitorias, no se duplican). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> TerrainTiles;

	/** Selva instanciada y sus mallas construidas en ejecución (RF_Transient | RF_DuplicateTransient). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> FloraComps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> FloraMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> Walls;

	/** Material del mar con la hondura y la espuma a la escala de la playa. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SeaMaterial;

	/** El mismo mar para las pozas, con la hondura y la espuma de un charco. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PoolMaterial;

	/** Tapas de los huevos de la salida (creadas en ejecución, una por huevo) y sus mallas. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> StartEggLids;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> StartEggLidMeshes;

	/** Nido del sprint final: las bases de los huevos y su anillo de arena en la línea del sprint (vacío fuera de él). */
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> SprintNestMesh;

	/** Elementos de la ronda (servidor, o la ronda de prueba en el editor). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ATN_BeachElement>> RoundElements;

	/** El actor de cada elemento del reparto (mismo índice que Layout.Items; null en el decorado y en lo que falte). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ATN_BeachElement>> ElementByItem;

	// ── Rendimiento y red (TN_BeachRaceGenerator_Round.cpp) ──

	/** Decorado local e instanciado de la ronda (sin replicar; lo crea cada máquina). */
	UPROPERTY(Transient)
	TObjectPtr<ATN_BeachDecorField> DecorField;

	/** Decorado quitado en esta ronda (el nido del sprint): cada máquina lo quita del suyo. */
	UPROPERTY(ReplicatedUsing = OnRep_DecorCuts)
	FTNBeachDecorCuts DecorCuts;

	UFUNCTION()
	void OnRep_DecorCuts();

	/** Ronda que se monta por partes (null si no hay ninguna a medias) y su número (RoundNet.Round). */
	TSharedPtr<FTNBeachRoundBuild> PendingBuild;
	int32 PendingRound = 0;
	/** Teselas del terreno pendientes de subir con los asientos de la ronda nueva. */
	TSharedPtr<FTNBeachTileBatch> PendingTiles;
	/** Hay teselas con asientos a medio subir: la ronda siguiente las rehace todas. */
	bool bTerrainDirtyAll = false;
	/** Círculos de DecorCuts ya quitados del decorado de esta máquina. */
	int32 AppliedDecorCuts = 0;
	FTNBeachRoundTimings LastTimings;

	TWeakObjectPtr<ATN_ProcWaterVolume> WaterVolume;

	/** Reparto aplicado en esta máquina (sus asientos están en el terreno). */
	TNBeachLayout::FRoundLayout Layout;
	int32 AppliedRound = 0;

	/** Rejilla del terreno (coordenadas de las filas y columnas de vértices) y teselas. */
	TArray<double> GridXs;
	TArray<double> GridYs;
	int32 TilesX = 0;
	int32 TilesY = 0;

	/** Tortugas que ya han tocado el agua de meta en esta ronda (servidor). */
	TSet<TWeakObjectPtr<ACharacter>> Finishers;
	/** Si cada tortuga estaba en el agua de meta el fotograma anterior (chapuzón al entrar, en cada máquina). */
	TMap<TWeakObjectPtr<ACharacter>, bool> WetTurtles;

	int32 SplashDropsFX = INDEX_NONE;
	int32 SplashFoamFX = INDEX_NONE;
	int32 SplashRingFX = INDEX_NONE;
	float FloatClock = 0.f;
	float IdleTime = 0.f;
	uint32 BuiltKey = 0;
	bool bBuilt = false;
	bool bRoundReady = false;
	bool bLiving = false;

	/** Huevos de la salida en esta máquina: rotos o no, desde cuándo (s del mundo), cuáles ya y si lanzan a las tortugas. */
	bool bEggsOpenLocal = false;
	bool bEggsLaunch = false;
	bool bEggsAnimating = false;
	double EggsOpenedAt = 0.0;
	int32 EggsHatchedMask = 0;
	/** Línea de los huevos en esta máquina (la del sprint o la salida) y ronda aplicada con la que se hizo el nido del sprint. */
	bool bEggsAtSprintLocal = false;
	int32 SprintNestRound = -1;
	/** TN.Beach.Egg: rotura pendiente de los huevos que ha vuelto a cerrar (servidor). */
	FTimerHandle EggReplayHandle;

	// ── Construcción (TN_BeachRaceGenerator_Build.cpp) ──
	void BuildAll();
	void ClearGenerated();
	void BuildTerrain();
	/** Rehace la tesela Index del terreno con los asientos Stamps (crea su componente si falta). */
	void BuildTerrainTile(int32 Index, const TArray<TNBeachLayout::FStamp>& Stamps);
	/** Rehace varias teselas: las alturas en paralelo y la subida (con su colisión) en el hilo de juego. */
	void BuildTerrainTiles(const TArray<int32>& Indices, const TArray<TNBeachLayout::FStamp>& Stamps);
	void BuildCliff();
	void BuildSeabedAndSea();
	void BuildWalls();
	void SpawnWaterVolume();
	/** Cota del terreno fijo en (X, Y) local tal y como lo dibujan las teselas (sin asientos): interpola su triángulo. */
	double MeshGroundZ(double X, double Y) const;

	// ── Relieve fijo (TN_BeachRaceGenerator_Features.cpp) ──
	/** Trincheras (caballones, tablones, sacos, tarimas y puentes), cornisas de las crestas y rocas de las pozas. */
	void BuildFeatures();
	/** Superficie del agua de las pozas. */
	void BuildPoolWater();

	// ── Salida con huevos (TN_BeachRaceGenerator_Start.cpp) ──
	/** Tapas de los huevos (las bases y el nido van con la salida). */
	void BuildStartEggs();
	/**
	 * Pone los huevos según RoundNet.bStartOpen: cerrados, o rompiéndose (bLive, o un cliente al que le llegan antes del
	 * lanzamiento: con la pausa en el huevo y el salto de las tortugas) o ya rotos.
	 */
	void ApplyStartEggs(bool bLive);
	/** Pose de las tapas mientras vuelan; false cuando ya no queda ninguna. */
	bool UpdateStartEggs();
	/**
	 * La pausa de 1 s en el huevo (TNEggHatch) de cada tortuga que esté en la línea de los huevos, en esta máquina; la
	 * sujetan y la lanzan hacia el mar al acabar quienes la mueven (servidor: todas; cliente: la suya).
	 */
	void LaunchTurtlesFromEggs();
	/** Base del huevo Index (local) en la línea de esta máquina: la salida o la del sprint (con los asientos de la ronda). */
	FVector StartEggCup(int32 Index) const;
	/** Los huevos de esta máquina no están en la línea que dice la ronda (o el nido del sprint es de otro reparto). */
	bool IsStartEggLineStale() const;
	/** Lleva los huevos a la línea que dice la ronda: hace el nido del sprint o lo vacía. */
	void ApplyStartEggLine();
	void BuildSprintNest();

	// ── Escenografía (TN_BeachRaceGenerator_Scenery.cpp) ──
	void BuildStartGrove();
	void BuildFinishDecor();
	void BuildJungle();
	void BuildFootprints();
	UInstancedStaticMeshComponent* MakeFlora(UStaticMesh* Mesh, bool bShadow, int32 WpoDistance);
	void StartLiving();
	void StopLiving();
	void Splash(const FVector& WorldLocation);

	// ── Terreno por partes (TN_BeachRaceGenerator_Build.cpp) ──
	/** Teselas que toca alguno de los dos juegos de asientos (con dos filas de margen), o todas si bAll. */
	static void FindTouchedTiles(const TArray<double>& Xs, const TArray<double>& Ys, int32 NumTilesX, int32 NumTilesY,
		const TArray<TNBeachLayout::FStamp>& OldStamps, const TArray<TNBeachLayout::FStamp>& NewStamps, bool bAll, TArray<int32>& OutIndices);
	/** Calcula las teselas Indices con los asientos Stamps, listas para subir (lógica pura: vale en cualquier hilo). */
	static TSharedPtr<FTNBeachTileBatch> ComputeTileBatch(const TArray<double>& Xs, const TArray<double>& Ys, int32 NumTilesX,
		const TArray<int32>& Indices, const TArray<TNBeachLayout::FStamp>& Stamps);
	/** Sube teselas de PendingTiles (con su colisión) hasta gastar BudgetSeconds (al menos una); true cuando no queda ninguna. */
	bool UploadPendingTiles(double BudgetSeconds);

	// ── Ronda por partes y decorado local (TN_BeachRaceGenerator_Round.cpp) ──
	/** Reparto de una ronda (lógica pura: vale en cualquier hilo); bSprint, el del sprint final. */
	static void MakeRoundLayout(int32 Seed, ETNProcDifficulty InDifficulty, bool bSprint, TNBeachLayout::FRoundLayout& Out);
	/** Servidor: la ronda que se reparte ahora es el sprint final (lo dice el estado de la partida de la carrera). */
	bool IsSprintFinalRound() const;
	/** Empieza a montar en esta máquina la ronda de RoundNet: el reparto y las teselas en otro hilo (o aquí mismo si bNow). */
	void StartRoundBuild(bool bNow);
	/** Sigue con la ronda a medias hasta gastar el presupuesto del fotograma (o hasta acabarla si bNow). */
	void TickRoundBuild(bool bNow);
	/** Suelta lo que haya a medias (otra ronda, ClearRound o EndPlay). */
	void CancelRoundBuild();
	/** Ronda montada entera: botín (servidor), registro de tiempos y aviso. */
	void FinishRoundBuild();
	/** Crea el campo de decorado si falta (pegado al generador, en su espacio). */
	ATN_BeachDecorField* EnsureDecorField();
	/** Si el nido de los huevos se hizo con otro reparto (el nuevo aún no estaba), lo rehace con los asientos nuevos. */
	void SyncStartEggsWithLayout();
	/** Quita del decorado de esta máquina los círculos de DecorCuts que falten. */
	void ApplyDecorCuts();

	// ── Ronda (TN_BeachRaceGenerator.cpp) ──
	/** Aplica un reparto en esta máquina: deja sus asientos en la arena (y quita los de la anterior) y dibuja sus huellas. */
	void ApplyLayoutLocal(const TNBeachLayout::FRoundLayout& NewLayout);
	/** Crea los elementos del reparto (menos el decorado, que es local); devuelve las clases que faltan (vacío si están todas). */
	FString SpawnRoundElements();
	/** Crea el elemento Index del reparto (salvo el decorado) y cuenta las clases que faltan. */
	void SpawnRoundElement(int32 Index, TArray<int8>& HasClass, TMap<FString, int32>& MissingByClass);
	/** Servidor: el vigilante de los erizos checos de la ronda (#688): derriban al chocar deprisa (ATN_BeachTankTrap). */
	void SpawnTankTrapGuard();
	static FString DescribeMissing(const TMap<FString, int32>& MissingByClass);
	void DestroyRoundElements();
	void TickTurtles(float DeltaSeconds);
	void TickAutoGenerate(float DeltaSeconds);
	bool ShouldShowFootprints() const;
};
