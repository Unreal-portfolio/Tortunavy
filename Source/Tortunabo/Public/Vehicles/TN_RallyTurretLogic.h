// Lógica pura de la torreta del buggy (sin mundo ni actores): munición, calentamiento del coco, cargas especiales,
// apuntado, retroceso, apuntado automático y escudo. Valores de 1.ª pasada de Docs/Rally_MVP.md. Tests en
// Tortunabo.Rally.Turret.*.
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyVehicle.h"

namespace TNRallyTurret
{
	// ── Tabla de munición (Docs/Rally_MVP.md, «Torreta y munición») ─────────────

	/** Coco: impulso lateral al buggy alcanzado (cm/s) y bamboleo de la dirección (s). */
	constexpr float CocoLateralCms = 350.f;
	constexpr float CocoWobbleSeconds = 0.4f;
	/**
	 * Alga: charco de 6 m durante 5 s; agarre ×0,35 y velocidad máxima ×0,5 a cualquier buggy dentro (#770: con ×0,5 y ×0,6
	 * se notaba como barro, no como un resbalón). Al entrar, además, un derrape corto (PuddleEntrySpinDegPerSecond).
	 */
	constexpr float AlgaPuddleRadiusCm = 600.f;
	constexpr float AlgaPuddleSeconds = 5.f;
	constexpr float AlgaGripMultiplier = 0.35f;
	constexpr float AlgaSpeedMultiplier = 0.5f;
	/** Vuelo del alga (#770): sale más despacio y cae con el doble de gravedad, para que el charco quede cerca. */
	constexpr float AlgaSpeedCms = 3000.f;
	constexpr float AlgaGravityScale = 2.f;
	/** Burbuja: flota 6 s; el primer buggy que la toca gana un escudo de 4 s que anula un impacto o un charco. */
	constexpr float BubbleFloatSeconds = 6.f;
	constexpr float ShieldSeconds = 4.f;
	/** Mortero: explosión de 5 m con impulso vertical de 450 cm/s, también al propio buggy. */
	constexpr float MortarRadiusCm = 500.f;
	constexpr float MortarUpCms = 450.f;
	/** Tinta: mancha la pantalla de las dos ocupantes 3 s. */
	constexpr float InkSeconds = 3.f;
	/** Ancla: se engancha al buggy alcanzado y lo frena 2 s (TNRallyCombat::AnchorDragAccel). */
	constexpr float AnchorSeconds = 2.f;

	/** Conchas (#629): velocidad por el suelo (cm/s). La recta dura 6 s y la teledirigida 12 s (SpecFor). */
	constexpr float ShellSpeedCms = 4600.f;

	/** Velocidad punta del buggy sin charco (cm/s, ~110 km/h con el ajuste de UTN_BuggyData). */
	constexpr float BuggyTopSpeedCms = 3050.f;

	/** Vuelo y retroceso de cada munición. */
	struct FAmmoSpec
	{
		/** Velocidad de salida (cm/s). */
		float SpeedCms = 0.f;
		/** Escala de la gravedad del proyectil (0 = vuela recto o flota). */
		float GravityScale = 0.f;
		/** Vida máxima del proyectil (s). */
		float LifeSeconds = 0.f;
		/** Retroceso del buggy propio (cm/s), en sentido contrario al disparo. */
		float RecoilCms = 0.f;
		/** Segundos mínimos entre dos disparos de esta munición. */
		float FireInterval = 0.f;
		/** Cargas que da una caja. 0 = infinita (Coco). */
		int32 BoxCharges = 0;
	};

	TORTUNABO_API FAmmoSpec SpecFor(ETNRallyAmmo Ammo);

	/** Si la munición es especial (la dan las cajas): todas menos None y Coco. */
	TORTUNABO_API bool IsSpecial(ETNRallyAmmo Ammo);

	/** Concha o concha teledirigida: no es un proyectil que vuela, corre pegada al suelo (ATN_KartShell). */
	TORTUNABO_API bool IsGroundShell(ETNRallyAmmo Ammo);

	// ── Calentamiento del coco ──────────────────────────────────────────────────

	/** 6 disparos seguidos sobrecalientan 2,5 s. */
	constexpr int32 ShotsToOverheat = 6;
	constexpr float OverheatSeconds = 2.5f;
	/** Enfriado por segundo (fracción del calor total): se vacía en 2,5 s. */
	constexpr float CoolPerSecond = 0.4f;
	/** Segundos sin disparar antes de empezar a enfriar: los disparos seguidos (cadencia 0,25 s) no enfrían. */
	constexpr float CoolDelaySeconds = 0.6f;

	struct FHeat
	{
		/** Calor en [0, 1]. */
		float Heat = 0.f;
		/** Segundos de sobrecalentamiento que quedan (0 = se puede disparar). */
		float OverheatLeft = 0.f;
		/** Segundos desde el último disparo. */
		float SinceShot = 0.f;
	};

	TORTUNABO_API bool IsOverheated(const FHeat& State);
	TORTUNABO_API bool CanFireCoco(const FHeat& State);

	/** Estado tras un disparo de coco: suma 1/ShotsToOverheat y, al llegar a 1, sobrecalienta OverheatSeconds. */
	TORTUNABO_API FHeat AfterCocoShot(const FHeat& State);

	/**
	 * Avanza Dt: el sobrecalentamiento baja y, al acabar, deja el calor a 0; si no, el calor se enfría tras
	 * CoolDelaySeconds sin disparar.
	 */
	TORTUNABO_API FHeat Cool(const FHeat& State, float Dt);

	// ── Cargas especiales ───────────────────────────────────────────────────────

	struct FSpecial
	{
		ETNRallyAmmo Ammo = ETNRallyAmmo::None;
		int32 Charges = 0;
	};

	/** Carga de una caja: sustituye a la anterior. Sin cargas o con munición no especial, queda vacía. */
	TORTUNABO_API FSpecial Give(ETNRallyAmmo Ammo, int32 Charges);
	TORTUNABO_API bool CanFireSpecial(const FSpecial& State);
	/** Gasta una carga; con la última, la munición vuelve a None. */
	TORTUNABO_API FSpecial AfterSpecialShot(const FSpecial& State);

	// ── Apuntado, cadencia y retroceso ──────────────────────────────────────────

	/** Cabeceo permitido de la torreta (grados, relativo al buggy). */
	constexpr float MinPitchDeg = -10.f;
	constexpr float MaxPitchDeg = 45.f;

	/** Apuntado relativo al buggy válido: cabeceo en [MinPitchDeg, MaxPitchDeg], guiñada en (-180, 180], sin alabeo. */
	TORTUNABO_API FRotator ClampAim(const FRotator& RelativeAim);

	/** Rechaza valores no finitos (NaN o infinito) que mande un cliente. */
	TORTUNABO_API bool IsAimFinite(float Yaw, float Pitch);

	/** Dirección en mundo del apuntado relativo, sobre la rotación del buggy (solo su guiñada, cabeceo y alabeo). */
	TORTUNABO_API FVector AimWorldDirection(const FRotator& BuggyRotation, const FRotator& RelativeAim);

	/**
	 * Separación máxima (grados) entre la dirección en mundo que manda la artillera y la que calcula el servidor con su
	 * orientación del buggy (#333). Con 150 ms de ping y el buggy girando a tope, la diferencia llega a unos 9°.
	 */
	constexpr float MaxClientAimErrorDeg = 12.f;

	/**
	 * Dirección del disparo de la artillera (#333): la que vio el cliente (ClientDir, en mundo) si es finita, no nula y se
	 * separa como mucho MaxErrorDeg de ServerDir; si no, ServerDir. Devuelve siempre un vector unitario.
	 */
	TORTUNABO_API FVector ResolveClientFireDirection(const FVector& ServerDir, const FVector& ClientDir,
		float MaxErrorDeg = MaxClientAimErrorDeg);

	/** Apuntado relativo que corresponde a una dirección en mundo (para la IA y la conductora sola). Ya limitado. */
	TORTUNABO_API FRotator RelativeAimFromWorld(const FRotator& BuggyRotation, const FVector& WorldDir);

	/**
	 * Boca visible de la torreta en mundo: ForwardCm por delante del pivote en la dirección del apuntado y SideCm a su
	 * derecha (horizontal en el marco del buggy: el cabeceo no la mueve de lado). El proyectil y el fogonazo salen de ahí.
	 */
	TORTUNABO_API FVector MuzzleWorldLocation(const FVector& PivotWorld, const FRotator& BuggyRotation, const FRotator& RelativeAim,
		float ForwardCm, float SideCm);

	/** Cadencia: true si han pasado al menos Interval × Tolerance segundos (margen para la latencia). */
	TORTUNABO_API bool CadenceOk(double Now, double LastShot, float Interval, float Tolerance = 0.85f);

	/** Cambio de velocidad del retroceso: opuesto a la dirección del disparo, en el plano horizontal, de RecoilCms. */
	TORTUNABO_API FVector RecoilVelocity(const FVector& AimWorldDir, float RecoilCms);

	/** Fracción del retroceso que levanta el extremo del buggy hacia el que se dispara. */
	constexpr float RecoilLiftRatio = 0.8f;

	/**
	 * Tope del levantamiento (cm/s), el de la concha (250 · 0,8): con el del mortero sin tope (700 · 0,8 = 560 cm/s en el
	 * morro) el buggy daba la vuelta a 50 km/h, y el bot del Rally volcaba cada vez que gastaba uno (#695). El frenazo
	 * horizontal del mortero (RecoilVelocity) no cambia.
	 */
	constexpr float MaxRecoilLiftCms = 200.f;

	/** Componente vertical del retroceso, en espacio local del buggy. */
	struct FRecoilLift
	{
		/** Cambio de velocidad hacia arriba (cm/s) que se aplica en LocalPoint. */
		float LiftCms = 0.f;
		/** Punto de aplicación (cm, local): el morro al disparar hacia delante, la trasera al disparar hacia atrás. */
		FVector LocalPoint = FVector::ZeroVector;
	};

	/**
	 * Levantamiento del retroceso: proporcional a cuánto apunta el disparo hacia delante o hacia atrás (LocalAimDir en
	 * espacio del buggy). Disparar hacia delante frena (RecoilVelocity) y levanta el morro; hacia atrás, acelera y levanta
	 * la trasera. Un disparo lateral no levanta. Nunca pasa de MaxRecoilLiftCms.
	 */
	TORTUNABO_API FRecoilLift RecoilLift(const FVector& LocalAimDir, float RecoilCms, float HalfLengthCm);

	// ── Munición seleccionada ───────────────────────────────────────────────────

	/** Munición que puede elegir la artillera, en orden de ciclo: el coco siempre y la especial si le quedan cargas. */
	TORTUNABO_API TArray<ETNRallyAmmo> AvailableAmmo(const FSpecial& Special);

	/**
	 * Selección válida: el coco se queda; una especial se queda si sigue cargada; si la caja la ha cambiado por otra, pasa
	 * a la nueva; sin cargas, vuelve al coco.
	 */
	TORTUNABO_API ETNRallyAmmo ResolveSelection(ETNRallyAmmo Selected, const FSpecial& Special);

	/** Siguiente munición disponible en el sentido de Direction (positivo, adelante; negativo, atrás; 0, la misma). */
	TORTUNABO_API ETNRallyAmmo CycleAmmo(ETNRallyAmmo Selected, const FSpecial& Special, int32 Direction);

	// ── Apuntado automático de la conductora sola ───────────────────────────────

	/** Alcance horizontal (cm) de un tiro con cabeceo PitchDeg lanzado HeightCm por encima del punto de caída. */
	TORTUNABO_API float LobRangeCm(float PitchDeg, float HeightCm, float SpeedCms, float GravityCms2);

	/**
	 * Cabeceo (grados, a pasos de 0,5) del tiro parabólico más bajo que llega a RangeCm lanzado HeightCm por encima del
	 * blanco, con velocidad SpeedCms y gravedad GravityCms2 (positiva). Fuera de alcance, 45. En [MinPitchDeg, MaxPitchDeg].
	 */
	TORTUNABO_API float LobPitchDeg(float RangeCm, float HeightCm, float SpeedCms, float GravityCms2);

	constexpr float AutoAimRangeCm = 6000.f;
	constexpr float AutoAimHalfAngleDeg = 30.f;

	/**
	 * Índice del candidato más cercano dentro del cono (Origin, Dir, RangeCm, HalfAngleDeg) en el plano horizontal;
	 * INDEX_NONE si no hay ninguno.
	 */
	TORTUNABO_API int32 PickAutoAimTarget(const FVector& Origin, const FVector& Dir, TConstArrayView<FVector> Candidates,
		float RangeCm = AutoAimRangeCm, float HalfAngleDeg = AutoAimHalfAngleDeg);

	// ── Escudo ──────────────────────────────────────────────────────────────────

	struct FImpactOutcome
	{
		/** El efecto llega al buggy. */
		bool bApplies = true;
		/** El escudo se gasta. */
		bool bShieldConsumed = false;
	};

	/** Un impacto (o un charco) sobre un buggy: con escudo, lo anula y se gasta; sin escudo, se aplica. */
	TORTUNABO_API FImpactOutcome ResolveImpact(bool bShielded);

	/** Multiplicador de agarre y velocidad máxima de un buggy según si pisa un charco de alga. */
	TORTUNABO_API float PuddleGripMultiplier(bool bInPuddle);
	TORTUNABO_API float PuddleSpeedCapCms(bool bInPuddle);

	// ── Alga: charco apoyado en el suelo y derrape al entrar (#770) ─────────────

	/** Giro de guiñada (grados/s) que el servidor da al buggy que entra en un charco, a partir de AlgaSpinFullSpeedCms. */
	constexpr float AlgaSpinYawDegPerSecond = 90.f;
	/** Por debajo de esta velocidad (cm/s) no derrapa; hasta AlgaSpinFullSpeedCms el giro crece en proporción. */
	constexpr float AlgaSpinMinSpeedCms = 300.f;
	constexpr float AlgaSpinFullSpeedCms = 1500.f;
	/** Segundos que quien suelta el charco en Karts no lo pisa (cae detrás, a menos de su radio). */
	constexpr float AlgaDropperGraceSeconds = 2.f;
	/** Una normal con menos Z que esto no es suelo (pared, lateral de la barrera): el charco se busca más atrás. */
	constexpr float PuddleMinGroundNormalZ = 0.5f;
	/** Bajada máxima (cm) desde el impacto o desde donde acaba el vuelo para buscar el suelo del charco. */
	constexpr float PuddleGroundProbeCm = 5000.f;
	/** Muestras del borde del disco (a PuddleRimSampleFraction del radio) con las que se ajusta su plano al suelo. */
	constexpr int32 PuddleRimSamples = 6;
	constexpr float PuddleRimSampleFraction = 0.7f;

	/**
	 * Giro del derrape al entrar en un charco (grados/s, positivo en el sentido de las agujas visto desde arriba si
	 * bClockwise): 0 por debajo de AlgaSpinMinSpeedCms y AlgaSpinYawDegPerSecond desde AlgaSpinFullSpeedCms.
	 */
	TORTUNABO_API float PuddleEntrySpinDegPerSecond(float SpeedCms, bool bClockwise);

	/**
	 * Si el charco afecta a un buggy: a quien lo soltó (Karts), no durante sus primeros AlgaDropperGraceSeconds; a uno en el
	 * aire (el bote de la medusa, #771), nunca.
	 */
	TORTUNABO_API bool PuddleAffects(bool bIsDropper, float PuddleAgeSeconds, bool bAirborne = false);

	/** Si una normal de impacto es suelo donde puede quedar un charco (no una pared). */
	TORTUNABO_API bool IsPuddleGround(const FVector& Normal);

	/**
	 * Plano del suelo bajo el charco a partir de puntos del suelo (el centro y el borde): centro medio y normal media de los
	 * triángulos centro-borde, hacia arriba. Con menos de 3 puntos, el primero y la vertical. False si no hay puntos.
	 */
	TORTUNABO_API bool FitGroundPlane(TConstArrayView<FVector> Points, FVector& OutCenter, FVector& OutNormal);

	/** Giro del disco del charco: su eje Z sobre la normal del suelo y su X lo más cerca posible de Forward. */
	TORTUNABO_API FQuat PuddleRotation(const FVector& GroundNormal, const FVector& Forward);

	// ── Ráfaga de erizos (#715) ─────────────────────────────────────────────────

	/** Una carga: 12 púas en 1,5 s mientras se mantiene el gatillo. */
	constexpr int32 ErizosSpikes = 12;
	constexpr float ErizosBurstSeconds = 1.5f;
	constexpr float ErizosSpikeInterval = ErizosBurstSeconds / ErizosSpikes;
	/** Cada púa: rápida y con poca caída. */
	constexpr float ErizosSpeedCms = 9000.f;
	constexpr float ErizosGravityScale = 0.3f;
	constexpr float ErizosLifeSeconds = 1.5f;
	/** Cada púa que acierta: empujón lateral (cm/s) y bamboleo de la dirección (s). */
	constexpr float ErizosLateralCms = 120.f;
	constexpr float ErizosWobbleSeconds = 0.15f;
	/** Retroceso de cada púa en el buggy propio (cm/s). */
	constexpr float ErizosRecoilCms = 40.f;
	/**
	 * Lo que dura cada petición del gatillo de una persona (s): el cliente repite la petición cada ErizosSpikeInterval
	 * mientras lo mantiene y, si deja de llegar, la ráfaga se para (y sigue donde iba al volver a apretar). Un bot no aprieta
	 * nada: su ráfaga entera sale de una vez (BurstHoldSeconds).
	 */
	constexpr float ErizosHoldSeconds = 0.35f;

	/** Munición que dispara en ráfaga mientras se mantiene el gatillo (los erizos). */
	TORTUNABO_API bool IsBurstAmmo(ETNRallyAmmo Ammo);

	/** Ráfaga en marcha: púas que quedan de la carga, hora de la siguiente y hasta cuándo sigue apretado el gatillo. */
	struct FBurst
	{
		int32 SpikesLeft = 0;
		double NextSpikeAt = 0.0;
		double HoldUntil = -1.0;
	};

	/** Si queda alguna púa de la carga empezada. */
	TORTUNABO_API bool IsBurstActive(const FBurst& Burst);

	/**
	 * Gatillo apretado en Now: sin ráfaga empezada, empieza una de Spikes púas con la primera ya; con ella, solo alarga el
	 * gatillo hasta Now + HoldSeconds (las peticiones seguidas nunca adelantan las púas: la cadencia la lleva el servidor).
	 */
	TORTUNABO_API FBurst HoldBurst(const FBurst& Burst, double Now, float HoldSeconds, int32 Spikes = ErizosSpikes);

	/** Si toca disparar una púa en Now: queda alguna, ha llegado su hora y el gatillo sigue apretado. */
	TORTUNABO_API bool BurstSpikeDue(const FBurst& Burst, double Now);

	/** Tras una púa en Now: una menos y la siguiente a Interval de la anterior (o de Now, si la ráfaga estaba parada). */
	TORTUNABO_API FBurst AfterBurstSpike(const FBurst& Burst, double Now, float Interval = ErizosSpikeInterval);

	/** Lo que alarga el gatillo cada petición: el de una persona, ErizosHoldSeconds; el de un bot, la ráfaga entera. */
	TORTUNABO_API float BurstHoldSeconds(bool bHumanTrigger);

	/**
	 * Dirección del empujón de una púa sobre un buggy que mira a Forward: la parte horizontal de PushDir perpendicular al
	 * morro (de lado); si PushDir va a lo largo del morro, hacia el lado al que se incline o, recto, a la derecha.
	 */
	TORTUNABO_API FVector SpikePushDir(const FVector& Forward, const FVector& PushDir);

	// ── Medusa saltarina (#771) ─────────────────────────────────────────────────

	/** Altura del bote (cm) en llano y cambio de velocidad hacia arriba que la da con la gravedad normal (sqrt(2 g h)). */
	constexpr float JellyfishHopCm = 300.f;
	constexpr float JellyfishUpCms = 770.f;
	/** Una concha teledirigida que persigue al buggy a menos de esto (cm) es motivo para botar (bots). */
	constexpr float HopShellThreatCm = 2500.f;
	/** Un charco por delante a menos de esto (cm, del centro del buggy al borde del charco) también. */
	constexpr float HopPuddleLookAheadCm = 2500.f;

	/** Munición que no lanza nada: actúa sobre el propio buggy (la medusa). */
	TORTUNABO_API bool IsSelfAmmo(ETNRallyAmmo Ammo);

	/** Cambio de velocidad vertical (cm/s) que sube HeightCm con la gravedad GravityCms2 (positiva). */
	TORTUNABO_API float HopUpCms(float HeightCm, float GravityCms2);

	/** Altura (cm) que sube un cambio de velocidad vertical UpCms con la gravedad GravityCms2 (positiva). */
	TORTUNABO_API float HopApexCm(float UpCms, float GravityCms2);

	/** Si se puede usar la medusa: con el buggy en el aire, no. */
	TORTUNABO_API bool CanHop(bool bAirborne);

	/** Bots: una concha teledirigida que le persigue (bTargetsMe) a menos de HopShellThreatCm. */
	TORTUNABO_API bool IsShellThreat(const FVector& Buggy, const FVector& Shell, bool bTargetsMe);

	/** Bots: un charco de radio RadiusCm por delante (en la dirección Forward), a menos de HopPuddleLookAheadCm de su borde. */
	TORTUNABO_API bool IsPuddleAhead(const FVector& Buggy, const FVector& Forward, const FVector& Puddle, float RadiusCm = AlgaPuddleRadiusCm);
}
