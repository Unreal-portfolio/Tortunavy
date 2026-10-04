#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "TN_BeachCatapult.generated.h"

class ACharacter;
class ATN_ShellBody;
class ATortugaCharacter;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTN_BeachTrapSynthComponent;
class UTN_PlaygroundSynthComponent;

/**
 * Catapulta de playa hecha con cosas de niños: una cuchara gigante (de plástico, de madera o dos palos de polo atados con
 * un vasito de yogur por cazo) apoyada como un balancín sobre un tapón de garrafa encima de una piedra (o sobre una
 * piedra sola), con un cubito de arena mojada por contrapeso en el extremo del mango y un palo de polo de pie que lo
 * sujeta en alto. El cazo descansa en la arena (-X) y lanza hacia su X local si ya mira al mar (±45°: el reparto le
 * deja libre el arco de salto por ahí); si no, se gira sola hacia el mar (TNBeachRideKit::LaunchYawInActor).
 *
 * Con SizeScale = 1: brazo de ~11 m, fulcro a 2,5 m, cazo de ~3 x 2,2 m. Se entra en el cazo andando (borde bajo) o se
 * sube por el mango hasta el cubito.
 *
 * Reglas (servidor): una tortuga libre en el cazo, o metida en su caparazón y quieta en él (su bola física, ATN_ShellBody:
 * la forma divertida de usarla), la arma: mientras dura el aviso la bola se mantiene quieta (velocidad a cero en cada tic
 * del servidor) y al disparar el servidor le da la velocidad de lanzamiento a su caja, igual que a una de pie, con la reserva
 * Launch del árbitro mientras vuela. Aviso de WarnSeconds (el palo que sujeta tiembla y cruje cada
 * vez más, el banderín parpadea; el temblor del brazo y del cazo es solo de sus mallas visibles: el eje y las colisiones no
 * se mueven hasta el disparo, así que la tortuga, o la bola de caparazón, que espera en el cazo no sale despedida); si el
 * cazo se queda vacío, se desarma. Si una tortuga sube al cubito (de un salto desde el mango o
 * cayendo encima) con alguien en el cazo o en el mango, dispara al momento (sin nadie a quien lanzar no: la gastaba vacía). Al disparar, el palo sale volando, el cubito cae, la cuchara da la vuelta (golpe y rebote contra la
 * arena) y lanza como bolas de caparazón (UTN_ShellComponent: vuelan, rebotan y ruedan; salen solas al pararse) a las
 * que estén en el cazo (LaunchSpeed a LaunchPitch grados hacia el mar, ±DeviationDeg de desvío al azar) y, más flojo, a
 * las que estén en el mango.
 *
 * Un solo uso (bSingleUse, lo normal): la primera tortuga que la dispara la gasta. Tras el rebote el brazo se parte por
 * el cuello del cazo: el cazo queda colgando de unas astillas, el mango tumbado con el cubito en la arena, el palo en el
 * suelo y el banderín arrancado; no se recarga en toda la ronda (quien llega tarde se fastidia). Sin bSingleUse recarga
 * como antes (ReloadSeconds: vuelve despacio con una carraca y el banderín pasa de rojo a verde).
 *
 * Potenciada (Spec.Flags & TNBeach::FlagBoosted, la de la cima de las fortalezas): cuchara dorada, cubito azul marino
 * con la estrella de Tortunavy, guirnalda de banderines y bandera; lanza a BoostedLaunchSpeed (unas 2,2 veces más lejos en
 * llano), con menos desvío, un «¡ZAAAS!» dorado, más temblor y la fanfarria.
 *
 * Cartel (TNBeachSignKit): tabla de madera clavada en la arena por el lado por el que se llega (-X del marco), a un lado
 * del brazo y girada hacia el centro, con una palanca y una flecha en arco pintadas y el rótulo «¡CATAPULTA!» (dorada y
 * con estrella en la potenciada). Rebota y brilla al acercarse la tortuga local. Partida, se tuerce, lleva una cinta roja
 * en aspa y dice «¡ROTA!».
 *
 * Red: el servidor decide (ArmedAt y FiredAt, horas del servidor replicadas) y lanza las bolas (la caja física se
 * replica sola: sin predicción ni correcciones). Cada máquina anima la cuchara, la rotura, el palo, el banderín, el polvo
 * y los sonidos desde esas horas con el reloj del servidor suavizado (quien llega tarde la ve ya rota, sin oírla). La
 * colisión del brazo se apaga durante el golpe para que no toque las bolas que salen; la del cazo, para siempre al
 * gastarse.
 */
UCLASS()
class TORTUNABO_API ATN_BeachCatapult : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachCatapult();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Lista para disparar (en cualquier máquina). */
	bool IsLoaded(double ServerTime) const;

	/** Gastada: de un solo uso y ya disparada (en cualquier máquina). */
	bool IsSpent() const { return bSingleUse && FiredAt >= 0.f; }

	/** Potenciada (Spec.Flags & TNBeach::FlagBoosted). */
	bool IsBoosted() const { return bBoosted; }

	/**
	 * Servidor: dónde acaba el arco de conchitas que dibuja su vuelo (UTN_BeachLootSubsystem, al repartir las conchas de la
	 * ronda), a la altura del centro de la bola. Las que no están potenciadas lanzan hacia ahí, con su elevación y la rapidez
	 * justa para caer en él aunque la bola frene en el aire (TNBeachCatapultAim), sin el desvío al azar (#257): antes salían
	 * desde el cazo con la rapidez fija y ±12° de desvío, y se quedaban cortas y fuera del arco. Sin punto, o si queda muy
	 * de lado respecto al morro de la cuchara, como antes.
	 */
	void SetLaunchTarget(const FVector& WorldTarget);

	/** Aviso desde que alguien se sube al cazo hasta que dispara. */
	UPROPERTY(EditAnywhere, Category = "Catapulta", meta = (ClampMin = "0.0"))
	float WarnSeconds = 1.0f;

	/** Un solo disparo por ronda: tras el primero el brazo queda partido. Apagado, recarga en ReloadSeconds. */
	UPROPERTY(EditAnywhere, Category = "Catapulta")
	bool bSingleUse = true;

	/** Recarga desde que dispara hasta que vuelve a estar lista (solo sin bSingleUse). */
	UPROPERTY(EditAnywhere, Category = "Catapulta", meta = (ClampMin = "1.0"))
	float ReloadSeconds = 4.6f;

	/** Velocidad de la bola lanzada desde el cazo (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Catapulta", meta = (ClampMin = "0.0"))
	float LaunchSpeed = 2300.f;

	/** Elevación del lanzamiento (grados). */
	UPROPERTY(EditAnywhere, Category = "Catapulta", meta = (ClampMin = "10.0", ClampMax = "80.0"))
	float LaunchPitch = 44.f;

	/** Desvío máximo a cada lado (grados, al azar en cada tiro). */
	UPROPERTY(EditAnywhere, Category = "Catapulta", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float DeviationDeg = 12.f;

	/** Fracción de la velocidad para las que estén en el mango (no en el cazo). */
	UPROPERTY(EditAnywhere, Category = "Catapulta", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HandleLaunchFraction = 0.55f;

	/** Potenciada: velocidad de la bola (cm/s; ~77 m en llano frente a los ~35 de la normal, con el rozamiento de la bola). */
	UPROPERTY(EditAnywhere, Category = "Catapulta|Potenciada", meta = (ClampMin = "0.0"))
	float BoostedLaunchSpeed = 3800.f;

	/** Potenciada: elevación (grados). */
	UPROPERTY(EditAnywhere, Category = "Catapulta|Potenciada", meta = (ClampMin = "10.0", ClampMax = "80.0"))
	float BoostedLaunchPitch = 40.f;

	/** Potenciada: desvío máximo a cada lado (grados): apunta a la zona despejada de delante. */
	UPROPERTY(EditAnywhere, Category = "Catapulta|Potenciada", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float BoostedDeviationDeg = 4.f;

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	UFUNCTION()
	void OnRep_Shot();

	/** Hora del servidor en que se armó (aviso en marcha; < 0 = no). */
	UPROPERTY(ReplicatedUsing = OnRep_Shot)
	float ArmedAt = -1.f;

	/** Hora del servidor del último disparo (< 0 = ninguno; con bSingleUse, el único). */
	UPROPERTY(ReplicatedUsing = OnRep_Shot)
	float FiredAt = -1.f;

	/** Marco orientado hacia el mar. */
	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<USceneComponent> Frame;

	/** Piedra, tapón y banderines. */
	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> BaseMesh;

	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<UProceduralMeshComponent> BaseCollision;

	/** Eje del balancín, sobre el tapón (gira en cabeceo). */
	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<USceneComponent> ArmPivot;

	/** Mango con el cubito, del cuello del cazo al extremo (espacio del eje: X hacia el cubito). */
	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> ArmMesh;

	/** Mango y cubito: base móvil con nombre estable por red. */
	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<UProceduralMeshComponent> ArmCollision;

	/** Bisagra del cuello del cazo (hija del eje, en CrackX): por aquí se parte el brazo y el cazo queda colgando. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<USceneComponent> BowlHinge;

	/** Cazo con el trozo de brazo hasta el cuello (espacio de la bisagra). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> BowlMesh;

	/** Suelo y bordes del cazo: base móvil con nombre estable por red (se apaga para siempre al gastarse). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<UProceduralMeshComponent> BowlCollision;

	/** Astillas del corte (espacio del eje; solo con el brazo partido). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> SplinterMesh;

	/** Palo de polo que sujeta el mango en alto (cae al disparar). */
	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<USceneComponent> PropPivot;

	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> PropMesh;

	/** Banderín verde (lista) y rojo (armada o recargando); ninguno con el brazo partido. */
	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> FlagGreen;

	UPROPERTY(VisibleAnywhere, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> FlagRed;

	/** Pie del cartel (en el marco, por el lado por el que se llega): rebota, y se tuerce al partirse la catapulta. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<USceneComponent> SignPivot;

	/** Postes, tabla e icono pintado. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> SignMesh;

	/** Cinta roja en aspa (solo partida). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<UStaticMeshComponent> SignCross;

	/** Rótulo: «¡CATAPULTA!» o «¡ROTA!». */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Catapulta")
	TObjectPtr<UTextRenderComponent> SignText;

	UPROPERTY(Transient)
	TObjectPtr<UTN_BeachTrapSynthComponent> Voice;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PlaygroundSynthComponent> Toy;

private:
	struct FRider
	{
		bool bOnBucket = false;
	};

	void ServerTick(double Now);

	/** Servidor: dispara y lanza a quien esté en el cazo y en el mango. */
	void Fire(double Now);

	/** Cabeceo del brazo (grados; + = cubito arriba) a la hora Now. */
	double ArmPitchAt(double Now) const;

	/** Cabeceo del cazo sobre su bisagra (grados; 0 = entero, DangleDeg = colgando) a la hora Now. */
	double BowlPitchAt(double Now) const;

	/** Dónde está una tortuga sobre el brazo: 0 = fuera, 1 = cazo, 2 = mango, 3 = encima del cubito. */
	int32 WhereOnArm(const ACharacter* Character) const;

	/** Velocidad de lanzamiento desde From (donde sale la bola): al final del arco si tiene punto (SetLaunchTarget). */
	FVector LaunchVelocity(float Fraction, const FVector& From) const;

	/**
	 * Servidor: el cuerpo físico del caparazón (ATN_ShellBody) de esta tortuga si está metida en él, quieta dentro del cazo
	 * y puede ir de pasajera (viva, sin aturdir, sin que la lleve ni la sujete nadie, sin reserva de la tormenta o de la red
	 * de seguridad); nullptr si no. Cuenta como una tortuga de pie en el cazo: arma la catapulta y sale lanzada.
	 */
	ATN_ShellBody* BowlBallOf(const ACharacter* Character) const;

	/**
	 * Servidor: lanza la bola del cazo dándole la velocidad al cuerpo físico que ya tiene (sin recrearlo: solo la API pública
	 * de la caja). Queda con la salida bloqueada y sale sola del caparazón al pararse, como las demás lanzadas.
	 */
	bool LaunchBowlBall(ATortugaCharacter* Turtle, ATN_ShellBody* Body, const FVector& Velocity);

	/** Servidor: reserva a la lanzada con el árbitro (TNBeach::ClaimTurtle, Launch) mientras vuela en su bola. */
	void BeginFlight(ACharacter* Turtle);

	/** Servidor: suelta la reserva de las que ya han aterrizado (salen del caparazón) o se han pasado de tiempo. */
	void TickFlights();

	void TickVisuals(double Now, float DeltaSeconds);
	void SetBowlCollision(bool bOn);

	/**
	 * Temblor del aviso, solo visual: mueve la malla visible del brazo y la del cazo (sin colisión), nunca el eje ni las
	 * colisiones, que se quedan quietas con el cabeceo de reposo (la tortuga o la bola que espera en el cazo no se mueve).
	 * Warn de 0 a 1 es la intensidad; menos de 0 lo apaga y deja las mallas en su sitio. Solo en máquinas con pantalla.
	 */
	void ApplyVisualShake(double Now, double Warn);

	/** Cartel: pose (botecito o torcido) y rótulo según el estado (en máquinas con pantalla). */
	void TickSign(double Now, float DeltaSeconds, bool bBroken);

	// Medidas (cm; espacio del eje del brazo, X hacia el cubito).
	double LongArm = 750.0;
	double ShortArm = 350.0;
	double PivotZ = 250.0;
	double RestDeg = 16.0;
	double FiredDeg = -40.0;
	double BowlLength = 300.0;
	double BowlHalfWidth = 110.0;
	double BowlDepth = 38.0;
	double ArmThick = 30.0;
	double BucketRadius = 88.0;
	double BucketHeight = 70.0;
	double FrameYawDeg = 0.0;
	/** Cuello del cazo (X del eje) por donde se parte, y cuánto gira el cazo al quedar colgando. */
	double CrackX = -200.0;
	double DangleDeg = 110.0;

	bool bBoosted = false;
	/** Final del arco de conchitas (SetLaunchTarget); solo en el servidor. */
	FVector LaunchTarget = FVector::ZeroVector;
	bool bHasLaunchTarget = false;
	/** Cartel: lado (+1/-1 en Y del marco), giro de su tabla y estado de su animación. */
	double SignSide = 1.0;
	double SignYawDeg = 0.0;
	bool bSignShowsBroken = false;
	float SignAge = 10.f;
	bool bSignNear = false;
	float SignGlow = 0.f;
	float SignGlowApplied = -1.f;
	/** El cartel se está moviendo (solo entonces se toca su transformada). */
	bool bSignMoving = false;
	double LastVisualNow = -1.0;
	double LastRatchetAt = -1.0;
	float CreakTimer = 0.f;
	bool bArmCollisionOn = true;
	bool bBowlCollisionOn = true;
	/** Las mallas visibles del brazo y del cazo están desplazadas por el temblor (solo entonces se restauran). */
	bool bMeshShaken = false;
	double EmptySince = -1.0;

	TMap<TWeakObjectPtr<ACharacter>, FRider> Riders;
	/** Servidor: lanzadas en vuelo con la reserva Launch del árbitro y la hora del mundo en que caduca. */
	TMap<TWeakObjectPtr<ACharacter>, double> Flights;
	FTNTrapClock Clock;
	FTNTrapBurst Dust;
	FTNTrapBurst Chips;
	FTNTrapPopText Pop;
	FTNTrapPopText BreakPop;
};
