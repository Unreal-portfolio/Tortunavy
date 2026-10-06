#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Player/TN_DizzyBirdsComponent.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "TN_BeachEnemy.generated.h"

class ACharacter;
class ATortugaCharacter;
class UBoxComponent;
class USceneComponent;
class UTN_BeachEnemySynthComponent;

/** Mareo de los enemigos por lo que se les lanza (ATN_BeachEnemy::ApplyHitStun). */
namespace TNBeachHitStun
{
	/** Segundos de mareo que da un objeto lanzado (piedra, pulpo, bola...) a un enemigo vivo. */
	constexpr float ThrownSeconds = 3.f;

	/** Segundos de mareo que da una bola de caparazón lanzada (una tortuga en bola que va deprisa). */
	constexpr float ShellSeconds = 2.5f;

	/** Velocidad (cm/s) desde la que una bola de caparazón marea a un enemigo al darle. */
	constexpr float ShellMinSpeed = 900.f;
}

/**
 * Pajaritos y estrellas del mareo de un enemigo: los de las tortugas (UTN_DizzyBirdsComponent, con su sonido) a la escala
 * del enemigo. Como no es un personaje con hueso de cabeza, cada fotograma se coloca donde dice
 * ATN_BeachEnemy::GetHitStunAnchor. Lo crea el enemigo la primera vez que se marea, solo en máquinas con pantalla.
 */
UCLASS(ClassGroup = (Custom))
class TORTUNABO_API UTN_BeachEnemyDizzyComponent : public UTN_DizzyBirdsComponent
{
	GENERATED_BODY()

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

/**
 * Empujones pendientes al ragdoll del derribo (TNBeach::KnockDownTurtle). El derribo llega a cada máquina por el
 * multicast de la tortuga y el empujón por el del enemigo, sin orden entre ellos: el empujón se guarda hasta que el
 * ragdoll de esa tortuga simula en esta máquina (o se descarta al segundo) y entonces se da la velocidad a sus cuerpos.
 * Así el ragdoll sale lanzado igual en todas las máquinas (el derribo solo hereda la velocidad que llevaba la cápsula).
 */
struct TORTUNABO_API FTNBeachRagdollPushes
{
	/** Guarda el empujón (o lo da ya si el ragdoll simula). */
	void Add(ACharacter* Turtle, const FVector& Push, const FVector& Spin);

	/** Da los pendientes cuyo ragdoll ya simula y descarta los viejos. */
	void Tick(float DeltaSeconds);

	bool IsEmpty() const { return Items.Num() == 0; }

	/** Velocidad Push (cm/s) y giro Spin (grados/s) a todos los cuerpos del ragdoll; false si no simula. */
	static bool TryApply(ACharacter* Turtle, const FVector& Push, const FVector& Spin);

private:
	struct FItem
	{
		TWeakObjectPtr<ACharacter> Turtle;
		FVector Push = FVector::ZeroVector;
		FVector Spin = FVector::ZeroVector;
		float Left = 1.f;
	};

	TArray<FItem> Items;
};

/**
 * Movimiento replicado barato de un enemigo de la playa: dónde está (el suelo bajo el cuerpo), hacia dónde mira, su
 * estado, cuándo empezó (reloj del servidor) y un punto de interés del estado (dónde cae la pinza, la roca a la que
 * corre...). Se replica con la frecuencia de red del actor (~10 Hz) y solo lo que cambia; los clientes interpolan.
 */
USTRUCT()
struct FTNBeachMoverRep
{
	GENERATED_BODY()

	/** Suelo bajo el centro del cuerpo (cm, cuantizado a 1 cm). */
	UPROPERTY()
	FVector_NetQuantize Location = FVector_NetQuantize(0.0, 0.0, 0.0);

	/** Punto de interés del estado actual. */
	UPROPERTY()
	FVector_NetQuantize Aim = FVector_NetQuantize(0.0, 0.0, 0.0);

	/** Hacia dónde mira (grados comprimidos a 16 bits). */
	UPROPERTY()
	uint16 Yaw = 0;

	/** Estado propio de cada enemigo (su enum). */
	UPROPERTY()
	uint8 State = 0;

	/** Sube en cada cambio de estado (aunque se repita el mismo). */
	UPROPERTY()
	uint8 Serial = 0;

	/** Reloj del servidor (GetServerWorldTimeSeconds) al empezar el estado. */
	UPROPERTY()
	float StateTime = 0.f;
};

/**
 * Base de los enemigos de la playa (cangrejo, erizo, lagarto, paso de quads y zona de gaviotas). No cambia el contrato
 * de ATN_BeachElement: lo amplía con lo que comparten.
 *
 *  - Red con servidor escucha: el servidor decide objetivos, golpes y aturdimientos (ServerTick) y escribe Mover; los
 *    clientes interpolan la posición y el giro (con una pizca de extrapolación) y animan a partir del estado. Los
 *    momentos puntuales (golpes) van por multicast no fiable. Siempre relevantes: si no, un cliente destruiría y
 *    reconstruiría sus mallas al alejarse más de 150 m por la playa.
 *  - Visual (VisualTick) solo en máquinas con pantalla; la raíz animada (Rig) se coloca en todas (su colisión cuenta
 *    también en un servidor dedicado).
 *  - Utilidades: tortugas vivas, suelo bajo un punto, reloj del servidor, aturdir en bola
 *    (TNBeach::StunTurtle) o derribar con ragdoll y empujón (TNBeach::KnockDownTurtle), temblor de cámara y una voz
 *    sintetizada (UTN_BeachEnemySynthComponent).
 *  - Muchos a la vez: los que andan se apartan entre sí (GetBodyRadius) y rodean lo grande del reparto; los numerosos
 *    (bThrottleWhenFar) se actualizan más despacio y mandan menos por red lejos de las tortugas y de la cámara.
 *  - Mareo por lo que se les lanza (ApplyHitStun): piedras, pulpos, bolas de caparazón... que dan en su cuerpo
 *    (GetHitCapsule) lo dejan mareado un momento, con pajaritos y estrellas (UTN_BeachEnemyDizzyComponent), sin atacar.
 *    El final va replicado (HitStunEndTime); cada subclase decide qué hace mientras (IsHitStunned en su ServerTick).
 *  - Tortuga sujeta (BeginHoldTurtle, PlaceHeldTurtle, EndHoldTurtle): en la boca, el pico... igual en todas las máquinas.
 *
 * Consola: TN.Beach.Enemy.Debug 1 dibuja radios y estados en el servidor; TN.Beach.Enemy.Stats cuenta y mide;
 * TN.Beach.StunNearest [s] marea al más cercano.
 */
UCLASS(Abstract)
class TORTUNABO_API ATN_BeachEnemy : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachEnemy();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** TN.Beach.Enemy.Debug: dibujar radios y estados (servidor). */
	static bool IsDebugDraw();

	/** Reloj del servidor en esta máquina (s): el replicado del GameState o, sin él, el del mundo. */
	static double ServerNow(const UObject* WorldContext);

	/** Tortugas de jugadores vivas, en juego y sin haber llegado a la meta (en cualquier máquina). */
	static void GatherTurtles(const UObject* WorldContext, TArray<ATortugaCharacter*>& Out);

	/** Suelo (geometría estática) bajo Where: traza de Where + Up a Where - Down. */
	static bool TraceGround(const UObject* WorldContext, const FVector& Where, float& OutZ, FVector* OutNormal = nullptr,
		float Up = 3000.f, float Down = 8000.f);

	/**
	 * Donde cae lo que se suelta desde arriba (la cagada de una gaviota) en la vertical de Where: lo primero que se ve desde
	 * Up cm por encima hasta Down por debajo. Cuentan la arena, el decorado, los castillos, las fortalezas (colisión
	 * dinámica) y las plataformas; no los muros invisibles ni los volúmenes (canal de visibilidad), ni las tortugas y sus
	 * bolas, ni Ignore (quien suelta). false si no hay nada (o la traza empieza dentro de algo).
	 */
	static bool TraceDropSurface(const UObject* WorldContext, const FVector& Where, float& OutZ, FVector* OutNormal = nullptr,
		const AActor* Ignore = nullptr, float Up = 3000.f, float Down = 3000.f);

	/** Distancia (cm) de Where a la cámara local más cercana; enorme si no hay (servidor dedicado). */
	static float LocalViewDistance(const UObject* WorldContext, const FVector& Where);

	/** true si una gaviota o un pelícano lleva a esta tortuga en el pico (cualquier máquina). */
	static bool IsTurtleHeld(const ATortugaCharacter* Turtle);

	/** La marca (o desmarca) como llevada en el pico: nadie más le da mientras tanto. */
	static void SetTurtleHeld(ATortugaCharacter* Turtle, bool bHeld);

	/** El enemigo que sujeta a esta tortuga en esta máquina (en la boca, el pico...), o null. */
	static ATN_BeachEnemy* FindHolder(const ATortugaCharacter* Turtle);

	/**
	 * true si quien sujeta a esta tortuga en esta máquina se la lleva por el aire (CarriesHeldTurtleThroughAir): mientras
	 * tanto su cámara se aleja para ver adónde va (ATortugaCharacter::TickCameraInterp).
	 */
	static bool IsTurtleCarriedThroughAir(const ATortugaCharacter* Turtle);

	/** Se lleva por el aire a la tortuga que sujeta (la zona de gaviotas); no la que tiene en la boca o arrastra por el suelo. */
	virtual bool CarriesHeldTurtleThroughAir() const { return false; }

	/**
	 * Servidor: quien sujete a esta tortuga la suelta ya, como el seguro de tiempo (no la vuelve a coger en 2 s). La usa la
	 * recolocación de TNBeach::RelocateTurtle antes de moverla. true si alguien la sujetaba.
	 */
	static bool ServerReleaseHeldTurtle(ATortugaCharacter* Turtle, const TCHAR* Reason);

	/**
	 * Servidor: la tortuga se mete en su caparazón mientras la sujeta un enemigo (TNBeach::SlipFromHolder, desde
	 * UTN_ShellComponent): se escurre. Quien la sujeta la suelta ya, antes de que nazca la bola (OnHeldTurtleSlips: la
	 * gaviota, aturdida en bola como al acabar el vuelo; los demás, sin más, y su lógica ve la bola en su siguiente paso).
	 * Así nunca hay una bola que un enemigo sigue colocando en su pico o en su boca. true si alguien la sujetaba.
	 */
	static bool ServerSlipHeldTurtle(ATortugaCharacter* Turtle);

	/**
	 * Lógica pura de la sujeción: si un enemigo puede colocar ahora a la tortuga en su pico o en su boca en esta máquina.
	 * No si otra cosa la mueve aquí: su bola del caparazón (metida en el caparazón o con la caja enganchada), el ragdoll
	 * del derribo, otra tortuga que la lleva o la muerte. Dos que la mueven a la vez (el pico la clava y la caja la
	 * arrastra) es la bola que gira alrededor de la cápsula clavada y se hunde en la arena.
	 */
	static bool CanHoldTurtle(bool bInShell, bool bHasLocalBody, bool bRagdoll, bool bCarried, bool bDead)
	{
		return !bInShell && !bHasLocalBody && !bRagdoll && !bCarried && !bDead;
	}

	/** Se le puede dar: viva, sin aturdir, sin derribar y sin ir en el pico de nadie. */
	static bool CanBeHit(const ATortugaCharacter* Turtle);

	/**
	 * Servidor: derribo con ragdoll y mareo (TNBeach::KnockDownTurtle) y el empujón Push (cm/s) con giro Spin (grados/s)
	 * al ragdoll de esta máquina. Quien lo llama manda el empujón a las demás (MulticastRagdollPush o el suyo propio).
	 * false si no se ha podido derribar.
	 */
	static bool ServerKnockDown(ATortugaCharacter* Turtle, float Seconds, const FVector& Push, const FVector& Spin);

	/** Enemigos de este mundo y cuántos van despacio por estar lejos (TN.Beach.Enemy.Stats). */
	static void GatherStats(const UObject* WorldContext, int32& OutTotal, int32& OutThrottled, int32& OutMovers);

	/**
	 * Servidor: un objeto lanzado (piedra, pulpo, bola de caparazón…) le ha dado: queda mareado Seconds (pajaritos y
	 * estrellas encima; ni persigue ni ataca ni agarra, y suelta lo que lleve). Si ya lo estaba, alarga hasta el mayor de
	 * los dos finales. Lo llama ATN_ThrowableItemActor al chocar con un enemigo vivo. Las subclases miran IsHitStunned en
	 * su lógica; pueden sobrescribirlo para reaccionar (llamando a la base).
	 */
	virtual void ApplyHitStun(float Seconds, AActor* InstigatorActor);

	/** true mientras dura el mareo por un golpe (replicado: se ve igual en todas las máquinas). */
	bool IsHitStunned() const;

	/** Segundos que le quedan de mareo por un golpe (0 si no lo está). */
	float GetHitStunLeft() const;

	/** false en los que no se marean nunca (el paso de quads): ApplyHitStun no les hace nada. */
	virtual bool AcceptsHitStun() const { return true; }

	/**
	 * Cuerpo que recibe lo que se lanza: cápsula de OutA a OutB con radio OutRadius (mundo, cm; lo que se ve en esta
	 * máquina). false si ahora no se le puede dar (escondido, en lo alto del cielo...). Por defecto, los que andan con
	 * radio de cuerpo: una esfera de ese radio apoyada en el suelo.
	 */
	virtual bool GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const;

	/** Dónde dan vueltas los pajaritos del mareo (mundo). Por defecto, encima del cuerpo. */
	virtual FVector GetHitStunAnchor() const;

	/** Escala de los pajaritos del mareo (1 = los de una tortuga). Por defecto, por el radio del cuerpo. */
	virtual float GetHitStunScale() const;

	/**
	 * El enemigo vivo al que da algo que va de From a To con radio Radius (lo que se ve en esta máquina): el primero por
	 * el camino, con el punto de su eje más cercano en OutAxisPoint. Null si no da a ninguno. Lo usan los objetos lanzados
	 * (ATN_ThrowableItemActor, ATN_InkProjectile) y las bolas de caparazón.
	 */
	static ATN_BeachEnemy* FindProjectileHit(const UObject* WorldContext, const FVector& From, const FVector& To, float Radius, FVector* OutAxisPoint = nullptr);

	/** Servidor: si lo que va de From a To (radio Radius) da a un enemigo, lo marea Seconds. Devuelve el enemigo o null. */
	static ATN_BeachEnemy* ServerHitWithProjectile(AActor* Projectile, const FVector& From, const FVector& To, float Radius,
		float Seconds = TNBeachHitStun::ThrownSeconds);


	virtual void PostInitializeComponents() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Servidor: objetivos, estados, golpes. */
	virtual void ServerTick(float DeltaSeconds) {}

	/**
	 * Bloque sólido del cuerpo (cangrejo gigante, tanque): no choca con los cuerpos físicos (PhysicsBody) y, en su lugar, el
	 * servidor saca de él a las bolas de caparazón por un lado y nunca hacia abajo (TNShellLogic::PushBallOutOfBlock). Movido
	 * sin barrido con la raíz animada (y extrapolado en los clientes), se metía en la bola y la hundía en el terreno.
	 */
	void RegisterSolidBlock(UBoxComponent* Block);

	/** Servidor, cada tic tras colocar la raíz: el empuje propio del bloque sólido sobre las bolas de caparazón. */
	void ServerPushShellBalls() const;

	/** El bloque sólido del cuerpo, si tiene (RegisterSolidBlock). */
	TWeakObjectPtr<UBoxComponent> SolidBlock;

	/** Máquinas con pantalla: animación, efectos y sonido. */
	virtual void VisualTick(float DeltaSeconds) {}

	/** Todas las máquinas: ha cambiado el estado replicado (en el servidor, al cambiarlo). */
	virtual void OnMoverStateChanged(uint8 OldState) {}

	/**
	 * Servidor: otro sistema le quita la tortuga que sujeta (ServerReleaseHeldTurtle: red de seguridad, rescate, gusano),
	 * justo antes de EndHoldTurtle. La subclase olvida a su víctima y deja el ataque (sin lanzarla ni aturdirla): si no, al
	 * acabar la sujeción la lanzaría desde donde la han dejado o la volvería a coger.
	 */
	virtual void OnHoldAborted(ATortugaCharacter* Turtle) {}

	/**
	 * Servidor: se le escurre la tortuga que sujeta (se mete en su caparazón; ServerSlipHeldTurtle), justo antes de que nazca
	 * su bola. Por defecto la suelta sin más (EndHoldTurtle) y la subclase lo ve en su siguiente paso (la tortuga ya va en
	 * su caparazón). La gaviota la suelta como al acabar el vuelo: aturdida en bola hacia la salida.
	 */
	virtual void OnHeldTurtleSlips(ATortugaCharacter* Turtle);

	/** Radio (cm) de relevancia visual: más lejos de la cámara local no se anima. */
	virtual float GetVisualRange() const { return 30000.f; }

	// ── Movimiento replicado ──────────────────────────────────────────────

	UPROPERTY(ReplicatedUsing = OnRep_Mover)
	FTNBeachMoverRep Mover;

	UFUNCTION()
	void OnRep_Mover();

	/** Servidor: nueva posición (suelo bajo el cuerpo) y giro de la simulación. */
	void ServerMoveTo(const FVector& Location, float YawDeg);

	/** Servidor: cambia de estado, con su hora y su punto de interés. */
	void ServerSetState(uint8 NewState, const FVector& Aim);
	void ServerSetState(uint8 NewState) { ServerSetState(NewState, FVector(Mover.Aim)); }

	/** Servidor: cambia el punto de interés sin cambiar de estado. */
	void ServerSetAim(const FVector& Aim);

	uint8 GetMoverState() const { return Mover.State; }

	/** Segundos en el estado actual (en clientes, con el reloj del servidor replicado). */
	float GetStateAge() const;

	/** Crea la raíz animada (Rig), enganchada a la del actor y colocada en el mundo en cada fotograma. */
	USceneComponent* MakeRig();

	/** Voz sintetizada enganchada a Parent (la crea la primera vez; null sin audio). */
	UTN_BeachEnemySynthComponent* GetVoice(USceneComponent* Parent, float InnerRadius, float Falloff);

	/** Servidor: aturde en bola (TNBeach::StunTurtle) y apunta la hora para no repetir con la misma. */
	void StunTurtle(ATortugaCharacter* Turtle, float Seconds, const FVector& Launch);

	/** Servidor: derriba con ragdoll y mareo y lanza el ragdoll con Push y Spin en todas las máquinas. */
	void KnockDownTurtle(ATortugaCharacter* Turtle, float Seconds, const FVector& Push, const FVector& Spin = FVector::ZeroVector);

	/** Todas las máquinas: el empujón del ragdoll del derribo (se aplica en cuanto el ragdoll simula). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastRagdollPush(ACharacter* Turtle, FVector_NetQuantize10 Push, FVector_NetQuantize10 Spin);

	/** Servidor: ignorar a esta tortuga durante Seconds (tras golpearla). */
	void IgnoreTurtle(ATortugaCharacter* Turtle, float Seconds);
	bool IsIgnored(const ATortugaCharacter* Turtle) const;

	/**
	 * Servidor: la tortuga atacable más cercana a From a menos de MaxDist (plano) que además esté a menos de Leash de
	 * InHome (Leash <= 0: sin correa). Atacable: CanBeHit y no ignorada.
	 */
	ATortugaCharacter* FindTarget(const FVector& From, float MaxDist, const FVector& InHome, float Leash) const;

	/** Tortuga atacable (servidor): CanBeHit y no ignorada. */
	bool IsTargetable(const ATortugaCharacter* Turtle) const;

	// ── Muchos enemigos a la vez ─────────────────────────────────────────

	/** Radio del cuerpo en planta (cm) para apartarse de los demás enemigos que andan; 0 = no se aparta. */
	virtual float GetBodyRadius() const { return 0.f; }

	/** Con una tortuga a menos de esto (cm) el servidor lo mueve en cada fotograma (bThrottleWhenFar). */
	virtual float GetActiveRange() const { return 9000.f; }

	/**
	 * Servidor: Next corregido para no meterse en otro enemigo que anda (medio solape por fotograma: cada uno se aparta
	 * su mitad).
	 */
	FVector ResolveStep(const FVector& Next, float SelfRadius);

	/** Suelo bajo Where (traza contra la geometría estática). */
	bool GroundHeightAt(const FVector& Where, float& OutZ) const;

	/** Texto emergente de dibujos («¡PLOF!») en WorldAt; solo con pantalla y con la cámara a menos de 60 m. */
	void ShowPop(const FText& Text, const FColor& Color, const FVector& WorldAt, float Size = 140.f);

	// ── Mareo por lo que se le lanza ─────────────────────────────────────

	/** Reloj del servidor en que se le pasa el mareo por un golpe (0 = nunca se ha mareado). */
	UPROPERTY(Replicated)
	float HitStunEndTime = 0.f;

	/** Todas las máquinas: el golpe que lo marea (estrellas, «¡TOING!», sonido y temblor) en Where. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHitStunFX(FVector_NetQuantize Where);

	// ── Tortuga sujeta (en la boca, en el pico...) ───────────────────────

	/**
	 * Todas las máquinas: sujeta a la tortuga: movimiento apagado, sin suavizado de red en los demás clientes, sin
	 * correcciones al dueño en el servidor, pataleta en el aire y marcada como llevada (nadie más le da). El enemigo la
	 * coloca con PlaceHeldTurtle en cada fotograma, después de su movimiento. Una a la vez. Seguro: si una sujeción dura
	 * más de 6 s (algo ha fallado), se suelta sola y esa tortuga no se puede volver a sujetar en 2 s. No sujeta a una
	 * tortuga que otra cosa mueve en esta máquina (CanHoldTurtle: su bola, su ragdoll, otra que la lleva), y
	 * PlaceHeldTurtle la suelta en cuanto otra cosa empiece a moverla (en los clientes, la bola puede llegar antes que la
	 * suelta del enemigo).
	 */
	void BeginHoldTurtle(ATortugaCharacter* Turtle);

	/** Segundos que puede durar una sujeción antes de que el seguro de tiempo la suelte (6). */
	virtual double GetMaxHoldSeconds() const;

	/**
	 * Todas las máquinas: la suelta. A prueba de todo: el servidor le devuelve las correcciones al dueño, se quita la
	 * pataleta y, si nada más la mueve (ni la bola del caparazón, ni el ragdoll del derribo, ni otra que la lleve, ni otro
	 * enemigo o un gusano que la sujete), vuelve a caer por su cuenta (MOVE_Falling con el movimiento encendido). Y durante
	 * 3 s lo vuelve a comprobar en cada fotograma, por si otro sistema la deja a medias (la bola que no llega a esta máquina,
	 * una patada de la tormenta, un mareo...); al final devuelve el suavizado de red a los demás clientes.
	 */
	void EndHoldTurtle();

	/** La tortuga que sujeta en esta máquina (o null). */
	ATortugaCharacter* GetHeldTurtle() const { return HeldTurtle.Get(); }

	/** Coloca a la sujeta con la espalda de su caparazón (hueso Spine2; sin malla, la cápsula) en Grip, mirando a Yaw. */
	void PlaceHeldTurtle(const FVector& Grip, float Yaw);

	/** Se actualiza más despacio lejos de las tortugas y de la cámara (los numerosos: cangrejo, erizo, lagarto). */
	bool bThrottleWhenFar = false;

	/** Frecuencia de red de cerca (la de lejos es 3 Hz como mucho). */
	float NetFrequencyNear = 10.f;

	/** Posición y giro simulados (servidor). */
	FVector SimLoc = FVector::ZeroVector;
	float SimYaw = 0.f;

	/** Posición y giro que se ven en esta máquina (en el servidor, los simulados). */
	FVector ShownLoc = FVector::ZeroVector;
	float ShownYaw = 0.f;

	/** Sitio del enemigo (donde lo colocó el generador). */
	FVector Home = FVector::ZeroVector;

	/** Raíz animada: sigue a ShownLoc y ShownYaw. */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Rig;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BeachEnemySynthComponent> Voice;

	/** Azar del servidor (sembrado con Spec.Seed). */
	FRandomStream ServerRng;

	/** Esta máquina dibuja (no es un servidor dedicado). */
	bool bHasScreen = false;

	/** Distancia de ShownLoc a la cámara local (se refresca cada fotograma con pantalla). */
	float ViewDistance = 1.0e9f;

	/** El enemigo usa Mover (los que andan); si no, la base no toca Rig ni ShownLoc. */
	bool bUsesMover = true;

private:
	TMap<TWeakObjectPtr<ATortugaCharacter>, double> IgnoreUntil;

	FVector LastRepLoc = FVector::ZeroVector;
	FVector RepVelocity = FVector::ZeroVector;
	double LastRepTime = -1.0;
	uint8 LastSerial = 0;
	uint8 LastState = 0;
	bool bHasRep = false;
	bool bVoiceTried = false;

	/** Nivel de detalle: cada cuánto se revisa y si ahora va despacio (TN_BeachEnemyLod.h). */
	float LodTimer = 0.f;
	bool bThrottled = false;

	/** Para el tope de ritmo completo: si lo pediría por sí solo y su prioridad (cm, menor = más cerca). */
	bool bLodWantsFull = false;
	float LodPriority = 1.0e9f;

	/** Cuántos enemigos numerosos de este mundo piden ritmo completo y están más cerca que este. */
	int32 CountCloserFullRate() const;

	FTNBeachRagdollPushes RagdollPushes;

	/** Textos emergentes (se reutilizan en rueda). */
	FTNTrapPopText Pops[3];
	int32 NextPop = 0;
	bool bPopsLive = false;

	/** Pajaritos del mareo (se crean la primera vez que hace falta, solo con pantalla). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_BeachEnemyDizzyComponent> Dizzy;
	bool bDizzyShown = false;

	/** Último objeto que lo ha mareado y cuándo (reloj del servidor): el mismo no lo vuelve a marear en cada fotograma. */
	TWeakObjectPtr<AActor> LastHitInstigator;
	double LastHitTime = -10.0;

	/** Tortuga sujeta en esta máquina, el suavizado de red que tenía y desde cuándo (reloj del mundo). */
	TWeakObjectPtr<ATortugaCharacter> HeldTurtle;
	uint8 HeldSavedSmoothing = 0;
	bool bHeldSmoothingSaved = false;
	double HoldStartTime = 0.0;

	/** Tortuga que no se puede volver a sujetar hasta HoldBlockedUntil (tras soltarla por el seguro de tiempo). */
	TWeakObjectPtr<ATortugaCharacter> HoldBlocked;
	double HoldBlockedUntil = 0.0;

	/** Una tortuga recién soltada que se vigila unos segundos (EndHoldTurtle). */
	struct FReleaseWatch
	{
		TWeakObjectPtr<ATortugaCharacter> Turtle;
		float Left = 0.f;
		uint8 Smoothing = 0;
		bool bSmoothingSaved = false;
	};
	TArray<FReleaseWatch> ReleaseWatches;

	/** Devuelve a la soltada lo que la sujeción le quitó, si nada más la está moviendo (bFinal: la última vez). */
	void RestoreReleasedTurtle(FReleaseWatch& Watch, bool bFinal);
	void TickReleaseWatches(float DeltaSeconds);

	void UpdateShown(float DeltaSeconds);
	void UpdateLod();
	void UpdateHitStunVisual();
};
