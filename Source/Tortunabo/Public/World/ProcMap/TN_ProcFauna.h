#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_ProcFauna.generated.h"

class ATN_ProcMapGenerator;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class USceneComponent;

/**
 * ATN_ProcFauna
 *
 * Fauna ambiental del mapa procedural: animales low-poly de caras planas (cangrejos, tortuguitas, monos,
 * flamencos, cabras montesas, peces que saltan...) repartidos junto al camino según el bioma de cada
 * módulo (TN_ProcMapFaunaMeshes.h). Solo visual: sin colisión, sin red (cada máquina simula los suyos) y
 * nada en un servidor dedicado. No es un enemigo: no ataca ni estorba.
 *
 *  - Reparto (Init): por cada módulo del camino principal, grupos pequeños de las especies de su bioma
 *    entre el borde del camino y ~40 m fuera (algunos en el propio camino); las de agua, solo donde hay
 *    agua. El reparto inicial sale de la semilla: es el mismo en todas las máquinas.
 *  - Simulación (Tick): solo los animales a menos de WakeRadius de alguna cámara local; el resto, a escala
 *    0 y sin coste. Máquina de estados por animal: reposo con acciones propias (mirar, picotear, rascarse,
 *    a una pata, flexiones, vigilar de pie...), paseo, alerta, huida cuando un pawn de jugador entra en su
 *    rango (se entierra, trepa paredes que la tortuga no sube, vuela, se sumerge o corre, de 2 a 3 veces
 *    más rápido que la carrera de la tortuga), escondido y reaparición en otro sitio junto al camino por
 *    delante de la cámara (reciclaje: la fauna no se acaba nunca).
 *  - Dibujo: una ISM por pieza de cada especie (cuerpo, cabeza, patas, alas, cola, pinzas...) con las
 *    transformadas calculadas en la CPU cada fotograma (giros por pivote) y subidas en bloque, sin
 *    asignar memoria por fotograma.
 *
 * Lo crea el generador tras construir el mapa con SpawnMapActor (así Clear() lo destruye al regenerar).
 * Consola: TN.Fauna.Enable 0/1 (esconde o muestra la fauna), TN.Fauna.Stats 1 (métricas en el log).
 */
UCLASS()
class TORTUNABO_API ATN_ProcFauna : public AActor
{
	GENERATED_BODY()

public:
	ATN_ProcFauna();

	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Coloca la fauna del mapa de InGenerator con la semilla InSeed (la del layout). Si el mapa aún no está
	 * listo, espera en el Tick; si el generador regenera sin destruir este actor, se reconstruye solo. En un
	 * servidor dedicado no hace nada.
	 */
	void Init(const ATN_ProcMapGenerator* InGenerator, uint32 InSeed);

	/** Un sitio junto al que viven animales en un mapa que no es el generado (Todos contra Todos, #829). */
	struct FCustomAnchor
	{
		FVector2D P = FVector2D::ZeroVector;
		/** Orden de reaparición (como el progreso por el camino): cualquier valor repartido de 0 a 14000. */
		float S = 0.f;
		/** Grupo de animales: los de la tabla del bioma se reparten por módulos. */
		int32 Module = 0;
		ETNProcBiome Biome = ETNProcBiome::Beach;
		double Width = 800.0;
	};

	/** El terreno de un mapa que no es el generado: suelo, sitios prohibidos, agua y sitios con animales. */
	struct FCustomTerrain
	{
		/** Cota del suelo bajo un punto del mundo (muy baja si no hay). */
		TFunction<float(const FVector&)> HeightAt;
		/** true si en ese punto (mundo, XY) no hay fauna (salidas, puntos de objetos). */
		TFunction<bool(const FVector2D&)> Blocked;
		/** Altura del agua ahora (mundo); si falta, WaterZ fijo. */
		TFunction<float()> WaterZNow;
		float WaterZ = 0.f;
		TArray<FCustomAnchor> Anchors;
	};

	/**
	 * Como Init, para un mapa de terreno fijo sin generador (#829): los animales salen junto a Terrain.Anchors, el suelo lo da
	 * Terrain.HeightAt y los de tierra se esconden cuando les llega el agua (Terrain.WaterZNow). En un servidor dedicado no hace nada.
	 */
	void InitCustom(const FCustomTerrain& Terrain, uint32 InSeed);

	/** Borra animales, piezas, mallas y efectos. */
	void ClearFauna();

	/** Animales en todo el mapa (métricas). */
	int32 GetNumAnimals() const { return Animals.Num(); }

	/** Animales simulados en el último fotograma (métricas). */
	int32 GetNumAwake() const { return NumAwake; }

	/** Radio (cm) alrededor de las cámaras locales en el que los animales se simulan y se ven. */
	UPROPERTY(EditAnywhere, Category = "Fauna", meta = (ClampMin = "2000.0"))
	float WakeRadius = 8000.f;

	/** Tope de animales en todo el mapa. */
	UPROPERTY(EditAnywhere, Category = "Fauna", meta = (ClampMin = "0"))
	int32 MaxAnimals = 900;

	/** Multiplicador de los animales por módulo de cada bioma. */
	UPROPERTY(EditAnywhere, Category = "Fauna", meta = (ClampMin = "0.0"))
	float Density = 2.6f;

private:
	/** Sitio válido para una especie junto al camino (mundo) con su progreso por el camino principal. */
	struct FTNFaunaSpot
	{
		FVector Pos = FVector::ZeroVector;
		float S = 0.f;
		int32 Module = INDEX_NONE;
	};

	/** Estado de un animal (solo local). Los uint8 guardan los enums de TN_ProcMapFaunaMeshes.h. */
	struct FTNFaunaAnimal
	{
		/** Raíz: suelo bajo el cuerpo; en vuelo, en el agua o revoloteando, su posición 3D. */
		FVector Pos = FVector::ZeroVector;
		/** Centro de su zona. */
		FVector Home = FVector::ZeroVector;
		/** Destino del paseo o del agua. */
		FVector Goal = FVector::ZeroVector;
		/** Velocidad en vuelo o en salto. */
		FVector Vel = FVector::ZeroVector;
		/** Peligro del que huye. */
		FVector Threat = FVector::ZeroVector;
		FVector2D FleeDir = FVector2D(1.0, 0.0);
		/** Progreso de su sitio por el camino (reciclaje hacia delante). */
		float HomeS = 0.f;
		float Yaw = 0.f;
		float Pitch = 0.f;
		float Roll = 0.f;
		/** cm/s por el suelo (o nadando). */
		float Speed = 0.f;
		/** Altura de los saltitos sobre el suelo. */
		float Air = 0.f;
		/** 0..1: enterrado o sumergido. */
		float Sink = 0.f;
		/** 0..1: escala de aparición. */
		float Presence = 1.f;
		/** Escala propia del ejemplar. */
		float Size = 1.f;
		/** Tiempo en el estado y duración prevista. */
		float StateT = 0.f;
		float Dur = 0.f;
		/** Contador del estado (reaparición, cambio de rumbo, turno de pánico, próximo salto). */
		float Timer = 0.f;
		/** Tiempo en la acción de reposo y su duración. */
		float ActT = 0.f;
		float ActDur = 0.f;
		/** Fases (ciclos) de la marcha, de los saltitos y del aleteo. */
		float Gait = 0.f;
		float HopPh = 0.f;
		float FlapPh = 0.f;
		/** 0..1: alas abiertas. */
		float Open = 0.f;
		/** Giro de la cabeza (grados) y hacia dónde va. */
		float Look = 0.f;
		float LookGoal = 0.f;
		/** Reloj propio (oscilaciones desfasadas entre ejemplares). */
		float Clock = 0.f;
		/** Cota al empezar a trepar. */
		float StartZ = 0.f;
		int32 Kind = 0;
		/** Instancia en las ISM de su especie. */
		int32 Slot = 0;
		uint8 Species = 0;
		uint8 State = 0;
		uint8 Phase = 0;
		/** Modo de huida en uso (con el plan B aplicado). */
		uint8 FleeKind = 0;
		uint8 Act = 0;
		/** Cangrejos: +1 anda hacia su derecha, -1 hacia su izquierda. */
		int8 SideSign = 1;
		bool bAwake = false;
		/** Lo último escrito en sus instancias es visible. */
		bool bShown = false;
		/** Se desvanece a lo lejos al terminar la huida. */
		bool bFading = false;
		/** En el aire (vuelo o salto): patas recogidas. */
		bool bAirborne = false;
		/** En alerta, esperando su turno para huir con el grupo. */
		bool bPanic = false;
	};

	/** Especie presente en el mapa: sus piezas instanciadas, sus animales y sus sitios. */
	struct FTNFaunaKind
	{
		uint8 Species = 0;
		int32 FirstPart = 0;
		int32 NumParts = 0;
		float BodyZ = 0.f;
		float HalfLen = 0.f;
		float Height = 0.f;
		float Draft = 0.f;
		TArray<int32> Members;
		/** Sitios válidos (ordenados por progreso) para reaparecer. */
		TArray<FTNFaunaSpot> Spots;
		/** Instancias visibles y rango de instancias cambiadas en este fotograma. */
		int32 Shown = 0;
		int32 DirtyMin = MAX_int32;
		int32 DirtyMax = -1;
		bool bVisible = false;
	};

	/** Debe coincidir con TNFauna::ETNFaunaBone::Count. */
	static constexpr int32 FaunaBoneCount = 11;

	UPROPERTY(VisibleAnywhere, Category = "Fauna")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Una ISM por pieza de cada especie presente. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PartISMs;

	/** Mallas de las piezas, construidas en ejecución. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> PartMeshes;

	TWeakObjectPtr<const ATN_ProcMapGenerator> GeneratorRef;
	/** Mapa propio sin generador (InitCustom). */
	bool bCustom = false;
	FCustomTerrain Custom;
	uint32 FaunaSeed = 0;
	int32 BuiltForGeneration = INDEX_NONE;
	bool bPendingBuild = false;
	bool bBuilt = false;

	TArray<FTNFaunaAnimal> Animals;
	TArray<FTNFaunaKind> Kinds;
	/** Por pieza (mismo índice que PartISMs): canal de animación, pivote y transformadas de sus instancias. */
	TArray<uint8> PartBone;
	TArray<FVector> PartPivot;
	TArray<TArray<FTransform>> PartXf;

	/** Cámaras locales y pawns de jugador de este fotograma (reutilizan su memoria). */
	TArray<FVector> ViewLocs;
	TArray<FVector> ViewDirs;
	TArray<FVector> ThreatLocs;
	/** Progreso por el camino de la primera cámara local (se refresca cada medio segundo). */
	float ViewProgress = 0.f;
	float ProgressTimer = 0.f;
	/** Cota (mundo) de la superficie del agua. */
	float WaterZ = 0.f;
	float StatsTimer = 0.f;
	double StatsSeconds = 0.0;
	int32 StatsFrames = 0;
	int32 NumAwake = 0;
	int32 SweepCursor = 0;
	uint32 SimRng = 0x2545F491u;
	/** Emisores de TNAmbientFX: gotas, ondas en el agua y polvo al enterrarse. */
	int32 SplashFX = INDEX_NONE;
	int32 RingFX = INDEX_NONE;
	int32 DustFX = INDEX_NONE;

	void BuildFauna();
	UInstancedStaticMeshComponent* MakePartISM(UStaticMesh* Mesh, const TArray<FTransform>& Initial, bool bCastShadow);
	void GatherViewsAndThreats();
	void SimulateAnimal(FTNFaunaAnimal& A, FTNFaunaKind& K, float Dt);
	void SimCalm(FTNFaunaAnimal& A, FTNFaunaKind& K, float Dt, bool bThreat, const FVector& ThreatLoc, float ThreatSq);
	void SimFlee(FTNFaunaAnimal& A, float Dt, bool bThreat, const FVector& ThreatLoc);
	void SimHidden(FTNFaunaAnimal& A, FTNFaunaKind& K, float Dt);
	void SimEmerge(FTNFaunaAnimal& A, float Dt);
	void SimArrive(FTNFaunaAnimal& A, float Dt);
	void SimFish(FTNFaunaAnimal& A, float Dt, bool bThreat, float ThreatSq);
	void SimHover(FTNFaunaAnimal& A, float Dt);
	void StartFlee(FTNFaunaAnimal& A, FTNFaunaKind& K, const FVector& From, bool bSpread);
	void StartIdle(FTNFaunaAnimal& A);
	void StartWalk(FTNFaunaAnimal& A);
	void HideAnimal(FTNFaunaAnimal& A);
	bool Respawn(FTNFaunaAnimal& A, FTNFaunaKind& K, bool bFarOnly);
	void RecycleBehind();
	bool StepGround(FTNFaunaAnimal& A, const FVector2D& Dir, float MoveSpeed, float Dt, bool bClimb, bool bAnyGround);
	FVector2D ChooseFleeDir(const FTNFaunaAnimal& A, const FVector2D& Away, bool bClimb, float& OutGain) const;
	bool FindWater(const FTNFaunaAnimal& A, const FVector2D& Away, FVector& OutGoal) const;
	bool HabitatOk(uint8 InSpecies, float GroundH) const;
	/** Mapa propio: un animal de tierra al que le ha llegado el agua. */
	bool IsFloodedLand(const FTNFaunaAnimal& A) const;
	float GroundAt(const FVector& P) const;
	float MinViewDistSq(const FVector& P) const;
	bool NearestThreat(const FVector& P, FVector& OutLoc, float& OutDistSq) const;
	void WriteAnimal(FTNFaunaAnimal& A, FTNFaunaKind& K);
	void WriteHidden(FTNFaunaAnimal& A, FTNFaunaKind& K);
	static void PoseBones(const FTNFaunaKind& K, const FTNFaunaAnimal& A, FTransform* OutBones);
	void BurstFX(int32 Emitter, const FVector& Where, int32 Count);
	float RandUnit();
	float RandIn(float Lo, float Hi);
};
