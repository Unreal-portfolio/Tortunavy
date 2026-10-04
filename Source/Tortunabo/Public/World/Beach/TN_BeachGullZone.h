#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachGullTuning.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"
#include "TN_BeachGullZone.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** Ataque en curso de una zona de gaviotas (replicado; cada máquina anima el pájaro con el reloj del servidor). */
USTRUCT()
struct FTNBeachGullAttack
{
	GENERATED_BODY()

	/** Tortuga a la que va (en el picado, la que se lleva si la coge). */
	UPROPERTY()
	TObjectPtr<ATortugaCharacter> Victim = nullptr;

	/**
	 * Dónde cae la cagada o dónde da el picado (en el suelo). Sigue a la tortuga (TN_BeachGullTuning.h: algo más rápido de
	 * lo que corre y, en el último tramo, lanzado por la línea que ella llevaba) hasta el golpe; en los clientes se suaviza
	 * (ShownAim). Mareo: dónde le han dado.
	 */
	UPROPERTY()
	FVector_NetQuantize Aim = FVector_NetQuantize(0.0, 0.0, 0.0);

	/**
	 * Picado con agarre: centro de la tortuga al cogerla (de ahí sale su camino colgada del pico, el mismo en todas las
	 * máquinas). Picado fallido: dónde pica (la arena o lo que la cubría). Mareo: la arena donde se queda sentada.
	 */
	UPROPERTY()
	FVector_NetQuantize Hold = FVector_NetQuantize(0.0, 0.0, 0.0);

	/** Reloj del servidor al empezar. */
	UPROPERTY()
	float StartTime = 0.f;

	/** 0 nada, 1 cagada, 2 picado, 3 mareada (le han dado con algo: cae a la arena y se queda sentada con pajaritos). */
	UPROPERTY()
	uint8 Kind = 0;

	/** Qué pájaro de la zona. */
	UPROPERTY()
	uint8 Bird = 0;

	/** 0 en curso, 1 acierto (mancha o la coge), 2 fallo (esquivado, a cubierto o nadie debajo). */
	UPROPERTY()
	uint8 Result = 0;

	/** 1 cuando Aim ya no se mueve (el golpe, la cagada en el suelo, el mareo). */
	UPROPERTY()
	uint8 bLocked = 0;

	/**
	 * Reloj del servidor al soltar a la tortuga del pico (0 = aún no). Los clientes dejan de sujetarla en cuanto llega,
	 * aunque su reloj vaya un poco por detrás del del servidor.
	 */
	UPROPERTY()
	float ReleaseTime = 0.f;

	/** Sube con cada ataque nuevo. */
	UPROPERTY()
	uint8 Serial = 0;
};

/** Una mancha en el caparazón de una tortuga (replicada, para quien entra o reconecta con ella fresca). */
USTRUCT()
struct FTNBeachGullStain
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ATortugaCharacter> Turtle = nullptr;

	/** Hora del servidor (ATN_BeachEnemy::ServerNow) en que cayó: la edad con la que se pinta y cuándo se seca. */
	UPROPERTY()
	float ServerTime = 0.f;

	/** El ataque (FTNBeachGullAttack::Serial) y cuál de las manchadas es: dan la forma, la misma en todas las máquinas. */
	UPROPERTY()
	uint8 Serial = 0;

	UPROPERTY()
	uint8 Variant = 0;
};

/**
 * Zona de gaviotas y pelícanos (ETNBeachElement::GullZone): las aves de la fauna a escala (gaviotas de 25 m de
 * envergadura y, a veces, un pelícano de 40 m) dando vueltas sobre la zona, cada una en su círculo (centro desplazado,
 * radio, forma y sentido propios) y a su altura (capas separadas 8 m), con sus sombras en la arena.
 *
 * Cuando hay tortugas bien dentro de la zona (80 % de su huella), cada 4-7 s la más cercana baja a por una de ellas (las
 * cifras, en TN_BeachGullTuning.h):
 *  - Cagada: vuela sobre ella y la suelta desde 30 m; cae un pegote blanco bien visible con su estela y una sombra dura y
 *    negra que nace pequeña y crece hasta la mancha según cae (2,1 s para apartarse). Sobre la tortuga a la que va, un
 *    signo de exclamación que parpadea cada vez más rápido según cae (WarnMark). El blanco la sigue a 2,5 m/s (más que
 *    andando y menos que corriendo, #636) y, los últimos 1,5 s (el «!» se queda fijo), cae por la línea que llevaba: andando
 *    te pilla; esprintando en línea recta, girando corriendo (60° o más) en ese momento, o tirándote en
 *    plancha a tiempo, te libras. Quien
 *    esté dentro al caer (y no a cubierto ni en plancha) cae derribada con ragdoll y mareo (TNBeach::KnockDownTurtle), con
 *    la cagada PINTADA en el caparazón (un decal sujeto a su hueso de la espalda con el material M_PoopSplatDecal;
 *    Scripts/create_poop_decal.py): 8 s entera y luego se seca y se desvanece hasta desaparecer a los 12 s. «¡PLOF!».
 *  - Picado: sube casi encima de ella y baja en picado 2,3 s siguiéndola por el aire a 4,2 m/s y, los últimos 1,5 s
 *    (pliega las alas del todo), lanzado por la línea que llevaba la tortuga; en la arena, una sombra dura y negra que
 *    nace diminuta al empezar a bajar y crece con él marca dónde va a dar. Abre el pico en el último momento y, si la
 *    tortuga sigue debajo (se esquiva girando corriendo al lanzarse, con el panzazo o en bola), la coge por el
 *    caparazón: colgando del pico pataleando, sube, vuela un
 *    poco hacia la salida y la suelta abriendo el pico: cae en bola aturdida (TNBeach::StunTurtle). Si falla, baja igual
 *    hasta clavar el pico en la arena (o en la sombrilla que la cubría), pica dos veces (arena que salta y sonido) y
 *    vuelve a subir.
 *  - Sombras de los que vuelan: la de verdad, bajo el cuerpo de cada pájaro; cuanto más baja, más pequeña y oscura.
 *  - Lo que se le lanza al que baja en picado (o al que lleva una tortuga) lo marea (ApplyHitStun): suelta a la tortuga,
 *    cae a la arena y se queda sentado con pajaritos; mientras, la zona no ataca. Luego vuelve a su círculo.
 *
 * Red: el servidor decide a quién, cuándo y si acierta; replica un único ataque (FTNBeachGullAttack) y los efectos
 * puntuales van por multicast. Mientras la lleva, cada máquina coloca a la tortuga en el pico con el mismo camino (del
 * reloj del servidor y de Attack.Hold) con la sujeción de ATN_BeachEnemy (BeginHoldTurtle: movimiento apagado y sin
 * correcciones al dueño; EndHoldTurtle y su seguro la sueltan del todo pase lo que pase).
 */
UCLASS()
class TORTUNABO_API ATN_BeachGullZone : public ATN_BeachEnemy
{
	GENERATED_BODY()

public:
	ATN_BeachGullZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Servidor (pruebas): ataca ya a la tortuga más cercana (1 cagada, 2 picado, 0 al azar). */
	void DebugAttackNow(int32 InKind);

	/**
	 * Servidor (pruebas, TN.Beach.Gull.Grab): coge a Turtle con el pico Times veces seguidas. Cada vez, en cuanto está libre
	 * (de pie, sin bola ni derribo), un picado que ya va por el último medio segundo y la coge si no se mueve; mientras
	 * quedan agarres, la zona no ataca a nadie más. Para reproducir «segunda gaviota + caparazón».
	 */
	void DebugGrab(ATortugaCharacter* Turtle, int32 Times);

	// ── Mareo por lo que se le lanza (solo el pájaro que baja en picado, el que lleva a una tortuga o el ya mareado) ──
	virtual void ApplyHitStun(float Seconds, AActor* InstigatorActor) override;
	virtual bool GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const override;
	virtual FVector GetHitStunAnchor() const override;
	virtual float GetHitStunScale() const override;

protected:
	virtual void ApplySpec() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void ServerTick(float DeltaSeconds) override;
	virtual void VisualTick(float DeltaSeconds) override;
	virtual void OnHoldAborted(ATortugaCharacter* Turtle) override;
	/** Se mete en el caparazón colgando del pico: la suelta como al acabar el vuelo (aturdida en bola hacia la salida). */
	virtual void OnHeldTurtleSlips(ATortugaCharacter* Turtle) override;
	virtual float GetVisualRange() const override { return 40000.f; }

	/**
	 * Todas las máquinas: la cagada ha caído en Where y ha derribado a Hit (sonido, gotas, mancha en la arena). La mancha del
	 * caparazón la pinta aquí solo el anfitrión; en los clientes, OnRep_ShellStains (que también llega a quien entra luego).
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSplat(FVector_NetQuantize Where, const TArray<ATortugaCharacter*>& Hit);

private:
	UPROPERTY(ReplicatedUsing = OnRep_Attack)
	FTNBeachGullAttack Attack;

	UFUNCTION()
	void OnRep_Attack();

	/**
	 * Manchas en los caparazones como estado: el servidor las apunta al caer la cagada y las quita al secarse (StainLife).
	 * Quien entra o reconecta con una fresca también la ve (F_gaps_steam N-C); la multicast solo trae el golpe.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ShellStains)
	TArray<FTNBeachGullStain> ShellStains;

	UFUNCTION()
	void OnRep_ShellStains();

	/** Servidor: quita de ShellStains las que ya se han secado. */
	void PruneShellStains(double Now);

	/** Manchas ya pintadas en esta máquina (Serial << 8 | Variant), para no repetirlas. */
	TSet<uint16> ShownStainKeys;

	/** Un pájaro de la zona (vuelo en todas las máquinas; sus piezas, solo con pantalla, en BirdParts desde FirstPart). */
	struct FBird
	{
		bool bPelican = false;
		/** Su círculo: centro desplazado del de la zona, radio, achatamiento, giro del óvalo, altura y velocidad angular. */
		FVector2D CenterOffset = FVector2D::ZeroVector;
		float Radius = 3500.f;
		float Ratio = 1.f;
		float OvalYaw = 0.f;
		float Height = 6000.f;
		float Phase = 0.f;
		float AngSpeed = 0.2f;
		float DriftPhase = 0.f;
		float Scale = 28.f;
		float Span = 2500.f;
		int32 FirstPart = 0;
		int32 NumParts = 0;
		int32 WingL = INDEX_NONE;
		int32 WingR = INDEX_NONE;
		int32 LegL = INDEX_NONE;
		int32 LegR = INDEX_NONE;
		int32 Head = INDEX_NONE;
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		float Yaw = 0.f;
		float Pitch = 0.f;
		float Bank = 0.f;
		float ShadowZ = 0.f;
		float ShadowTimer = 0.f;
		float SquawkTimer = 3.f;
		/** Pico: abierto (grados) y tiempo que le queda abierto por un graznido. */
		float Jaw = 0.f;
		float JawOpenLeft = 0.f;
		/** Malla de su sombra según lo nítida que va (0 blanda, 1 media, 2 nítida; -1 = aún la de serie). */
		int32 ShadowEdge = -1;
	};

	TArray<FBird> Birds;

	/** Raíz de cada pájaro (absoluta: se coloca en el mundo). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> BirdRoots;

	/** Piezas de todos los pájaros (cuerpo, cabeza, alas, patas). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BirdParts;

	/** Mandíbula de abajo de cada pájaro (enganchada a su cabeza). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Jaws;

	/** Pivote de reposo de cada pieza (espacio del cuerpo o de la raíz, medidas de la fauna). */
	TArray<FVector> PartPivots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Shadows;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Dropping;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DropShadow;

	/** Aviso del picado: sombra dura y negra donde va a dar (nace diminuta y crece según baja el pájaro). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DiveMarker;

	/** Manchas en la arena (y, sin el material del decal, pegotes en los caparazones), con su hora de nacer y su vida. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Splats;

	TArray<float> SplatBorn;
	TArray<float> SplatLife;
	TArray<float> SplatScale;

	/** Cagadas pintadas en las tortugas: decal sujeto al hueso de la espalda, su material propio (Seed y Fade) y su hora de nacer. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> StainDecals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> StainMids;

	TArray<float> StainBorn;

	/** Aviso de la cagada: signo de exclamación sobre la tortuga a la que va (parpadea más deprisa según cae). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WarnMark;

	/** Fase del parpadeo del aviso (vueltas) y contador de manchas para variar la forma de cada una. */
	float WarnPhase = 0.f;
	int32 StainCounter = 0;

	float SizeK = 1.f;
	float AttackRadius = 3800.f;
	float CircleRadius = 3800.f;

	/** Hacia la salida de la carrera (al revés del mar del generador; sin él, -X): hacia donde se la lleva. */
	FVector CourseBack = FVector(-1.0, 0.0, 0.0);

	// Servidor.
	double NextAttackTime = 0.0;
	bool bReleased = false;
	bool bRoofChecked = false;

	/** Lo que el blanco recuerda al lanzarse en el último tramo del ataque (se pone a cero con cada ataque nuevo). */
	TNBeachGullTuning::FChaseState AimChase;

	/** Pruebas (DebugGrab): a quién coger y cuántas veces más. */
	TWeakObjectPtr<ATortugaCharacter> DebugGrabVictim;
	int32 DebugGrabsLeft = 0;

	/** Blanco que se ve en esta máquina: en el servidor, Attack.Aim; en los clientes, el replicado suavizado. */
	FVector ShownAim = FVector::ZeroVector;
	uint8 ShownAimSerial = 0;
	bool bShownAimValid = false;

	// Visual.
	float Clock = 0.f;
	float DropTrailTimer = 0.f;
	uint8 SeenSerial = 0;
	uint8 SeenResult = 0;
	bool bSwoopPlayed = false;
	bool bWhistlePlayed = false;
	bool bReleasePlayed = false;
	bool bPeckPlayed = false;
	/** Inclinación de la arena bajo el aviso del picado y bajo la sombra de la cagada (se miran a menudo). */
	FVector MarkerNormal = FVector::UpVector;
	float MarkerGroundTimer = 0.f;
	FVector DropNormal = FVector::UpVector;
	float DropGroundTimer = 0.f;
	/**
	 * Dónde cae de verdad la cagada: cuánto queda por encima de la arena del blanco lo primero firme desde arriba (un castillo,
	 * una fortaleza, una sombrilla; 0 en la arena). Con ella se colocan la sombra y la cagada que cae; se mira a menudo.
	 */
	float DropSurfaceLift = 0.f;
	TNAmbientFX::FEmitter Droplets;
	TNAmbientFX::FEmitter Feathers;
	TNAmbientFX::FEmitter Trail;
	TNAmbientFX::FEmitter SandPuff;
	TNAmbientFX::FEmitter PeckSand;

	void BuildBirds();
	/** Todas las máquinas: posición de la raíz del pájaro en su vuelta (sin ataque) en el instante Now. */
	FVector CirclePos(const FBird& Bird, double Now) const;
	/** Todas las máquinas: posición de la raíz del pájaro que ataca fuera del agarre (subida, picado, fallo y vuelta). */
	FVector AttackPos(const FBird& Bird, double Now, float Tau) const;
	/** Arena bajo Where (la del generador, sin trazas; sin él, traza; si no hay nada, la altura del actor). */
	float GroundAt(const FVector& Where) const;

	// ── Agarre (mismas cuentas en todas las máquinas) ──

	/** Punto del pico que sujeta, desde la raíz del pájaro (sin escalar), con la cabeza girada HeadPitch. */
	FVector GripOffset(const FBird& Bird, float HeadPitch) const;
	/** Cuánto hay del centro de la tortuga al punto del caparazón por el que la sujeta (cm). */
	float GripDropFor(const ATortugaCharacter* Turtle) const;
	/** Punto del caparazón que va en el pico durante el agarre (U: segundos desde que la coge). */
	FVector GripPath(float U) const;
	/** Giro del pájaro que la lleva (U: segundos desde que la coge) y cabeceo de su cabeza. */
	FRotator CarryRotation(float U) const;
	float CarryHeadPitch(float U) const;
	/** Raíz del pájaro al final del picado: con el pico en el punto del caparazón de la tortuga. */
	FVector StrikeRoot(const FBird& Bird, double Now) const;
	/** Rumbo del picado (de donde empezó el ataque hacia el blanco). */
	float DiveYaw(const FBird& Bird, const FVector& Target) const;
	/** Raíz del pájaro con el pico clavado en Attack.Hold (el picado fallido). */
	FVector PeckRoot(const FBird& Bird) const;
	/** Todas las máquinas: dónde está y cómo va girado el pájaro Index en Now, sin suavizar (para darle con lo lanzado). */
	void BirdPose(int32 Index, double Now, FVector& OutRoot, FRotator& OutRot) const;
	/** Servidor: suelta a la tortuga que lleva en el pico (cae en bola aturdida). */
	void ReleaseCarried();
	/** Raíz de un pájaro con el pico (girado Rot, cabeza HeadPitch) en Grip. */
	FVector RootForGrip(const FBird& Bird, const FVector& Grip, const FRotator& Rot, float HeadPitch) const;
	/** Hacia dónde mira la tortuga colgada (la misma dirección que el pájaro: hacia la salida). */
	float HeldYaw() const;

	/** Servidor: empieza un ataque (Kind 1 cagada, 2 picado) contra Victim con el pájaro BirdIndex. */
	void StartAttack(ATortugaCharacter* Victim, uint8 InKind, int32 BirdIndex);
	void ServerPoop(float Tau, float DeltaSeconds);
	void ServerDive(float Tau, float DeltaSeconds);
	/**
	 * Servidor: el blanco (Attack.Aim) sigue a la tortuga por la arena según Plan a los Tau s del ataque
	 * (TNBeachGullTuning::StepAim: a su velocidad de persecución y, en el último tramo, lanzado por la línea que ella
	 * llevaba).
	 */
	void ServerTrackAim(float DeltaSeconds, float Tau, const TNBeachGullTuning::FChasePlan& Plan);
	/** Blanco que se ve en esta máquina (ShownAim). */
	FVector CurrentAim() const;
	/** Todas las máquinas: ShownAim hacia el Attack.Aim replicado, sin saltos. */
	void UpdateShownAim(float DeltaSeconds);
	/** Normal de la arena en Where (con dos muestras más del suelo del generador). */
	FVector GroundNormalAt(const FVector& Where) const;
	void EndAttack(double Now);
	/** Servidor: el pájaro más cercano a Where (el pelícano no caga). */
	int32 PickBird(const FVector& Where, bool bForPoop) const;
	/**
	 * Servidor: At está junto al frente de la tormenta en marcha (detrás o a menos de CarryBack + 10 m por delante): ahí no
	 * se la lleva en el pico (el vuelo la metería dentro); caga en vez de picar y, si pica, falla.
	 */
	bool IsNearStormFront(const FVector& At) const;

	/** Todas las máquinas: coloca a la tortuga en el pico o la suelta, según el ataque replicado. */
	void TickHold();

	/** Todas las máquinas: ha cambiado el ataque replicado (graznidos, plumas, arena). */
	void OnAttackChanged();
	void PoseBird(int32 Index, float DeltaSeconds, bool bAttacking, float Tau);
	/** Mancha en la arena (InTurtle nulo) o pegote en el caparazón de InTurtle (el plan B de SpawnStain). */
	void SpawnSplat(const FVector& Where, ATortugaCharacter* InTurtle, float InScale, float Life);
	/**
	 * Todas las máquinas: cagada pintada (decal) en el caparazón de InTurtle; sin el material, el pegote pequeño y pegado.
	 * Serial y Variant dan la forma; Age, los segundos que ya lleva (quien entra la ve con la edad que tiene).
	 */
	void SpawnStain(ATortugaCharacter* InTurtle, uint8 Serial, int32 Variant, float Age = 0.f);
	/** Visual: las cagadas de las tortugas se secan (Fade del material) y se quitan a los 12 s. */
	void TickStains();
	/** Visual: el signo de exclamación sobre la tortuga objetivo de la cagada (posición, cámara y parpadeo). */
	void TickWarnMark(float DeltaSeconds, double Now, const FVector& View, bool bNear);
};
