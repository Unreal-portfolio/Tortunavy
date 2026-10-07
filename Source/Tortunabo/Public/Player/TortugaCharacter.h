#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Player/TN_DiveDecisions.h"
#include "TortugaCharacter.generated.h"

class APlayerController;
class UCameraComponent;
class USpringArmComponent;
class UInputMappingContext;
class UInputAction;
class UTN_InventoryComponent;
class UTN_ShellComponent;
class UTN_CarryComponent;
class UTN_FlipperSlapComponent;
class UTN_DizzyBirdsComponent;
class UTN_TurtleFaceComponent;
class UTN_SlopeTiltComponent;
class UTN_StaminaComponent;
class UTN_WadingComponent;
class UTN_TurtleMovementComponent;
class ATN_InteractableBase;
class USceneComponent;
class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;
class UTN_EmoteWheelDataAsset;
struct FTN_EmoteWheelEntry;
struct FTN_InventoryItem;

/**
 * @brief Personaje principal del jugador. Tortuga antropomórfica con cámara tercera persona, dive aéreo, knockdown, emotes y cosméticos.
 *
 * Concentra la mayoría de la lógica de gameplay del jugador:
 *  - Movimiento: Walk, sprint vía UTN_StaminaComponent, dive aéreo con momentum preservation.
 *  - Combate / hazards: knockdown con tilt 180° + ragdoll opcional, mareo, Big Head, sombrilla anti-tormenta.
 *  - Items: UTN_InventoryComponent con rotación equipado/guardado y consumo de Use Type.
 *  - Cosméticos: helmet (mesh attacheado a "Sombrero") y skin (materiales por slot del SkM).
 *  - Animación procedural: piernas vía pendulum inline + UTN_ProcAnimInstance para overrides component-space.
 *  - Emotes: catálogo replicado (incluido KNOCKDOWN_EMOTE_ID=100 que dispara el visual de tumbado).
 *  - VOIP: UProximityVoiceComponent attached como child component.
 *  - Cámara: SpringArm con lag, zoom dinámico de sprint, FOV interpolado.
 *  - Input: Enhanced Input con soft refs a IMC_Player + IA_*. Se carga en BeginPlay.
 */
UCLASS()
class TORTUNABO_API ATortugaCharacter : public ACharacter
{
	GENERATED_BODY()

	/** El monkey test (TN.Monkey) pulsa los mismos manejadores de entrada que el jugador. */
	friend class UTN_MonkeyComponent;
	/** Escenario de estrés «caos» (Testing/TN_StressChaos.h): juega con la misma entrada que el jugador. */
	friend class UTN_StressChaosSubsystem;

public:
	/** Con UTN_TurtleMovementComponent como movimiento (el arrastre del panzazo va dentro de la simulación, predicho). */
	ATortugaCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Índice de emote reservado para el knockdown visual.
	 *  Cuando el servidor aplica knockdown, establece ReplicatedEmoteIndex = KNOCKDOWN_EMOTE_ID.
	 *  El sistema de emotes maneja toda la replicación visual automáticamente.
	 */
	static constexpr int32 KNOCKDOWN_EMOTE_ID = 100;

	/**
	 * Alcance de interacción por defecto (cm) para coger e interactuar: MaxInteractionDistance lo toma de aquí y el alcance
	 * de los rebuscables (TNSearchSpotDetail::Reach en TN_ProcSearchSpot.cpp) también, para que no se separen. Antes 350.
	 */
	static constexpr float DefaultInteractionDistance = 250.f;

	/**
	 * Re-aplica el Enhanced Input Mapping Context localmente.
	 * Llamar desde el servidor para el listen-server tras un revival, un tick después
	 * de Possess + ClientRestart, para garantizar que el input queda activo.
	 */
	void ReapplyInputMapping();

	/**
	 * Servidor: marea a la tortuga Duration segundos (tope de velocidad MareoSpeedCap). Lo llaman el dardo de medusa y
	 * las trampas de la playa (#2). El estado va replicado (bMareo) y el tope, predicho en el movimiento
	 * (TNMovementLimits::PredictedCapMareoBit): empieza y acaba en el mismo movimiento en el dueño y en el servidor (#574).
	 * Otra vez mareada mientras dura: la cuenta vuelve a empezar si así acaba más tarde (un mareo corto, como el guantazo de
	 * la aleta, no acorta uno más largo, #832).
	 */
	void ApplyMareoEffect(float Duration);

	/** Está mareada (replicado a todos). */
	bool IsMareoActive() const { return bMareo; }

	/** Devuelve true si la protección de sombrilla está activa (#29). */
	bool HasUmbrellaProtection() const { return bHasUmbrellaProtection; }

	/** Activa/desactiva la protección de sombrilla. Solo llamar desde el servidor. */
	void SetUmbrellaProtection(bool bActive) { bHasUmbrellaProtection = bActive; }

	/** Devuelve el componente de inventario (acceso de solo lectura para sistemas externos). */
	UTN_InventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

	/**
	 * Punto donde nacen lo que se lanza y lo que se suelta: 120 cm delante y 40 cm por encima del centro de la
	 * cápsula, recortado con un barrido para que nunca quede al otro lado de un muro, puerta o valla (#571).
	 */
	FVector GetItemSpawnLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Jump() override;
	virtual void OnJumped_Implementation() override;

	/**
	 * @brief Llamado cuando la referencia al PlayerState se replica a este cliente.
	 *        Reaplica cosméticos (helmet/skin) en caso de que llegaran ANTES de que el pawn
	 *        fuera poseído y el timer-for-next-tick de BeginPlay ya hubiese disparado sin ellos.
	 */
	virtual void OnRep_PlayerState() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> RotateInventoryAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> DropItemAction;

	/** Entrar y salir del caparazon. Mapeada a Ctrl izquierdo en IMC_Player. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TSoftObjectPtr<UInputAction> ShellAction;

	/**
	 * Input actions for emotes 0–9.  The array must have exactly 10 elements.
	 * Assign IA_Emote0…IA_Emote9 here or override in your Blueprint Class Defaults.
	 * Keys 0–9 (numrow) should be mapped to each action in IMC_Player.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Input|Emotes")
	TArray<TSoftObjectPtr<UInputAction>> EmoteActions;

	// ── Dive Config ───────────────────────────────────────────────────────────

	/** Horizontal impulse speed applied on dive (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="200.0", ClampMax="3000.0"))
	float DiveForwardSpeed = 420.f;

	/**
	 * Eje de rotación del tilt del dive EN ESPACIO LOCAL DEL MESH (component frame).
	 * Default (1,0,0) = rota sobre X local — que con DiveMeshDefaultRot Yaw=90
	 * equivale a pitch lateral world (tortuga se inclina adelante).
	 * Probar (0,1,0) si rota sobre el eje frontal en vez del lateral (o viceversa).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive")
	FVector DiveTiltAxis = FVector(1.f, 0.f, 0.f);

	/** Downward component of the dive impulse (cm/s, ≥0 pushes toward ground). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="0.0"))
	float DiveDownwardSpeed = 200.f;

	/** Speed threshold below which the dive recovery ends (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="1.0"))
	float DiveStopSpeedThreshold = 80.f;

	/** Minimum lock duration after a dive, even if the character stops early (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="0.0"))
	float DiveMinLockDuration = 0.65f;

	/** How fast (1/s) the body tilts into/out of the dive pose (lerp speed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="1.0"))
	float DiveTiltSpeed = 12.f;

	/**
	 * Al levantarse del suelo tras el arrastre, el cuerpo vuelve a ponerse de pie algo más despacio (1/s) para que se vea
	 * el empujón de brazos (UTN_TurtleAnimInstance).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="1.0"))
	float DiveGetUpTiltSpeed = 7.f;

	/**
	 * Tope de seguridad de todo el panzazo (s): si algo lo deja colgado (sin sitio para levantarse mucho rato, por
	 * ejemplo), el servidor lo acaba igualmente.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="2.0"))
	float DiveMaxSeconds = 12.f;

	/**
	 * Estampado contra la pared (#355): segundos con los pajaritos del mareo dando vueltas tras el golpe (en cada máquina;
	 * si está derribada o aturdida, siguen lo que duren esos estados).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive|Splat", meta=(ClampMin="0.0"))
	float DiveSplatDizzySeconds = 2.5f;

	/** Velocidad de rotación del actor Yaw hacia DiveDir al iniciar el dash (deg/seg).
	 *  720 → completa 180° en 250 ms. Subir = más responsivo (más cerca de snap).
	 *  Bajar = más fluido (puede no completar la rotación durante el dash). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="60.0", ClampMax="3600.0"))
	float DiveYawInterpSpeed = 720.f;

	// ── Dive momentum preservation ─────────────────────────────────────────────
	// Captura velocity horizontal AL SALTAR. En el dash, compara cámara ACTUAL
	// contra dirección del salto. Bonus = JumpStartSpeed * Factor(alignment).
	// El bonus puede ser NEGATIVO (resta de la base) si miras opuesto al salto:
	//   alignment = +1 → cámara coincide con salto → Forward factor (positivo, suma)
	//   alignment =  0 → cámara lateral al salto    → Lateral factor (default 0, no efecto)
	//   alignment = -1 → cámara opuesta al salto    → Backward factor (negativo, resta)
	// Si Backward es lo bastante negativo, TotalSpeed sale negativo → DiveDir
	// invierte → el char "vuelve" hacia donde había saltado.

	/** Multiplicador cuando la cámara coincide con la dirección del salto. >0 suma. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive|Momentum", meta=(ClampMin="0.0", ClampMax="2.0"))
	float DiveMomentumForwardFactor = 1.0f;

	/** Multiplicador cuando la cámara está PERPENDICULAR al salto (90°). Default 0 = sin efecto. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive|Momentum", meta=(ClampMin="-1.0", ClampMax="2.0"))
	float DiveMomentumLateralFactor = 0.0f;

	/** Multiplicador cuando la cámara está OPUESTA al salto. Negativo = resta de la base. -0.5 hace que dashear atrás vaya un poco hacia el salto original. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive|Momentum", meta=(ClampMin="-2.0", ClampMax="0.0"))
	float DiveMomentumBackwardFactor = -0.5f;

	/** Cap absoluto del speed combinado |TotalSpeed| ≤ Max. Permite valores negativos (dash retroceso). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive|Momentum", meta=(ClampMin="200.0", ClampMax="5000.0"))
	float DiveMaxTotalSpeed = 1500.f;

	/** Velocity horizontal capturada en OnJumped (justo cuando el char salta).
	 *  Multi-safe: OnJumped se llama en todas las máquinas autoritativas. No
	 *  necesita replicar — cada máquina captura localmente al saltar y la usa
	 *  para el dash posterior (el server-side cálculo del dash lee la del server). */
	FVector JumpStartHorizontalVelocity = FVector::ZeroVector;

	/** Yaw target hacia el que el actor interpola al iniciar el dash. Replicado
	 *  para que clientes remotos (no owner) también interpolen suavemente al
	 *  recibir la dirección. */
	UPROPERTY(Replicated)
	float DiveTargetYaw = 0.f;

	/** Activo mientras la rotación del dash se está interpolando. Se desactiva
	 *  al alcanzar el target (ε ≤ 1°) o al terminar el dash. */
	UPROPERTY(Replicated)
	bool bDiveYawInterpActive = false;

	/**
	 * CapsuleHalfHeight while diving — shrinks the hitbox to match the horizontal pose.
	 * The capsule radius stays unchanged; halving the height makes it a flat disc shape.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="15.0"))
	float DiveCapsuleHalfHeight = 35.f;

	/**
	 * Altura (cm) sobre el suelo del punto de giro de la malla (sus pies) con el panzazo completo. La malla se sube
	 * para que la tripa quede apoyada: sin esto, al encoger la cápsula el cuerpo tumbado se hunde entero en el suelo.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="0.0"))
	float DiveBellyPivotHeight = 11.f;

	/**
	 * Cuánto se desplaza la malla hacia atrás (cm) con el panzazo completo: tumbada, el centro del cuerpo (la tripa y el
	 * caparazón) queda sobre la cápsula en vez de los pies, así que gira sobre la tripa y la cápsula cubre la parte más
	 * gruesa. La cabeza y las patas, que sobresalen, las frena UTN_TurtleMovementComponent (Belly Slide|Body), cuyas
	 * medidas suponen este centrado.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive", meta=(ClampMin="0.0", ClampMax="150.0"))
	float DiveBodyCenterShift = 70.f;

	/** Aplastado de la malla en el panzazo: tripa-espalda (se aplasta contra el suelo), ancho y largo (se estira). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dive")
	FVector DiveSquash = FVector(1.08, 0.8, 1.05);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UTN_InventoryComponent> InventoryComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stamina")
	TObjectPtr<UTN_StaminaComponent> StaminaComponent;

	/** Vadeo simple (no nado) en agua poco profunda. Ver TN_WadingComponent. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wading")
	TObjectPtr<UTN_WadingComponent> WadingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell")
	TObjectPtr<UTN_ShellComponent> ShellComponent;

	/** Coger y lanzar a otras tortugas (issue #6, fase 2). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Carry")
	TObjectPtr<UTN_CarryComponent> CarryComponent;

	/** Guantazo con la aleta: el botón de ataque sin objeto ni arma (#832). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UTN_FlipperSlapComponent> FlipperSlap;

	/** Pajaritos y estrellitas del mareo sobre la cabeza mientras está noqueada (local y cosmético). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knockdown")
	TObjectPtr<UTN_DizzyBirdsComponent> DizzyBirds;

	/**
	 * Cara (local y cosmética, con estado replicado): lengua con física (al viento al esprintar, colgando al jadear),
	 * caras de cansancio, sudor y boca que habla con el chat rápido o la voz.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Face")
	TObjectPtr<UTN_TurtleFaceComponent> TurtleFace;

	/** Inclinación visual de la malla con la pendiente del suelo (local y cosmética, #586). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slope Tilt")
	TObjectPtr<UTN_SlopeTiltComponent> SlopeTilt;

	// ── Nado ─────────────────────────────────────────────────────────────────

	/** Velocidad nadando (cm/s): entre andar (450) y esprintar (800). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Swim", meta = (ClampMin = "0.0"))
	float SwimSpeed = 625.f;

	/** Flotabilidad: algo por encima de 1 para que la tortuga suba a la superficie. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Swim", meta = (ClampMin = "0.0"))
	float SwimBuoyancy = 1.08f;

	/** Impulso vertical del salto desde el agua (el CMC no salta nadando): subir a orillas e isletas. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Swim", meta = (ClampMin = "0.0"))
	float SwimHopVelocity = 640.f;

	/** Impulso hacia delante del salto desde el agua. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Swim", meta = (ClampMin = "0.0"))
	float SwimHopForward = 250.f;

public:
	/** El estado de la tortuga permite el brinco desde el agua (ni derribada, ni muerta, ni en el caparazón). */
	bool CanSwimHopNow() const;

	/** Velocidad del brinco desde el agua con la orientación de ahora (TNSwimHop::HopVelocity). */
	FVector GetSwimHopVelocity() const;

protected:

	// ── Caídas ───────────────────────────────────────────────────────────────

	/** Caída libre a partir de la cual la tortuga se mete sola en el caparazón (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fall", meta = (ClampMin = "0.0"))
	float AutoShellFallHeight = 500.f;

	/** Caída a partir de la cual la tortuga se rompe al aterrizar (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fall", meta = (ClampMin = "0.0"))
	float FatalFallHeight = 3500.f;

	// ── Caparazón (visual procedural) ────────────────────────────────────────

	/** Segundos para encoger cabeza, patas y cola al meterse. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell|Visual", meta = (ClampMin = "0.01"))
	float ShellRetractSeconds = 0.12f;

	/** Segundos para estirarse al salir (el "desplegarse" tras un rebote). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shell|Visual", meta = (ClampMin = "0.01"))
	float ShellExtendSeconds = 0.35f;

	/**
	 * Mesh del casco equipado. Se adjunta al SceneComponent "Sombrero" en BeginPlay.
	 * Añade un SceneComponent hijo en BP_TortugaCharacter con nombre exacto "Sombrero"
	 * y colócalo sobre la cabeza de la tortuga.
	 * Si no existe "Sombrero", el casco se adjunta al root del personaje.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmetics")
	TObjectPtr<UStaticMeshComponent> HelmetMeshComp;

	// ── Arm Animation (blockout) ─────────────────────────────────────────────
	// Requires SceneComponents named "Brazo1" and "Brazo2" in the Blueprint.
	// Pivot must be at the SHOULDER joint.

	/** Resting offset from T-pose (degrees). Brazo1 applies -value, Brazo2 applies +value.
	 *  70° makes both arms hang down naturally from T-pose. */
	UPROPERTY(EditDefaultsOnly, Category = "Arm Animation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ArmRestAngleDeg = 70.f;

	/** Swing amplitude while walking (degrees). Matches style of leg animation. */
	UPROPERTY(EditDefaultsOnly, Category = "Arm Animation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ArmWalkAmplitudeDeg = 35.f;

	/** Swing amplitude while sprinting (degrees). */
	UPROPERTY(EditDefaultsOnly, Category = "Arm Animation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float ArmSprintAmplitudeDeg = 65.f;

	// ── Leg Animation (blockout) ──────────────────────────────────────────────
	// Add child SceneComponents named "Pata1" and "Pata2" in your Blueprint.
	// Set their origin at the HIP PIVOT (see setup guide below).

	/** Swing amplitude in degrees while walking. */
	UPROPERTY(EditDefaultsOnly, Category = "Leg Animation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float LegWalkAmplitudeDeg = 60.f;

	/** Oscillation frequency (cycles/s) while walking. */
	UPROPERTY(EditDefaultsOnly, Category = "Leg Animation", meta = (ClampMin = "0.1"))
	float LegWalkFrequency = 2.0f;

	/** Swing amplitude in degrees while sprinting. */
	UPROPERTY(EditDefaultsOnly, Category = "Leg Animation", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float LegSprintAmplitudeDeg = 90.f;

	/** Oscillation frequency (cycles/s) while sprinting. */
	UPROPERTY(EditDefaultsOnly, Category = "Leg Animation", meta = (ClampMin = "0.1"))
	float LegSprintFrequency = 3.5f;

	/**
	 * Rotation axis in the component's LOCAL space for the pendulum swing.
	 * (0,1,0) = local Y → forward/back swing (default, works for side-mounted legs).
	 * (1,0,0) = local X → lateral swing.
	 * Change if your component's local axes differ.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Leg Animation")
	FVector LegSwingAxis = FVector(1.f, 0.f, 0.f);

	/** Speed (cm/s) below which legs smoothly return to rest. */
	UPROPERTY(EditDefaultsOnly, Category = "Leg Animation", meta = (ClampMin = "0.0"))
	float LegMinSpeed = 20.f;

	// ── Air Dash (double-jump) ────────────────────────────────────────────────
	/** Horizontal velocity applied on air dash (cm/s). Overrides current XY velocity. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|AirDash", meta = (ClampMin = "0.0"))
	float AirDashHorizontalForce = 1400.f;

	/** Vertical velocity applied on air dash (cm/s). Overrides current Z velocity. */
	UPROPERTY(EditDefaultsOnly, Category = "Movement|AirDash", meta = (ClampMin = "0.0"))
	float AirDashVerticalBoost = 300.f;

	// ── Emote Config ─────────────────────────────────────────────────────────
	/** Seconds to smoothly interpolate all limbs back to rest after an emote ends. */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EmoteBlendOutDuration = 0.25f;

	/** Catálogo editable de emotes para la rueda radial y validación de cooldown/IDs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Emotes|Data")
	TObjectPtr<UTN_EmoteWheelDataAsset> EmoteWheelDataAsset;

	/**
	 * Sound to play for each emote (index 0–9). Assign in Blueprint Class Defaults.
	 * Each element maps 1:1 to the emote at that index. Leave null for silent emotes.
	 * Sound loops while the emote is active; stops on cancel/blend-out.
	 * Uses proximity attenuation matching the voice chat range.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes|Audio")
	TArray<TObjectPtr<USoundBase>> EmoteSounds;

	/** Segundos de fundido de entrada al empezar el sonido de un emote (0 = sin fundido). Solo la primera vez, no en cada vuelta del bucle. */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes|Audio", meta = (ClampMin = "0.0"))
	float EmoteAudioFadeInTime = 0.3f;

	/** Inner radius (cm) for emote audio — full volume inside this range. Matches voice chat default. */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes|Audio", meta = (ClampMin = "0.0"))
	float EmoteAudioInnerRadius = 300.f;

	/** Outer radius (cm) for emote audio — silent beyond this range. Matches voice chat default. */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes|Audio", meta = (ClampMin = "0.0"))
	float EmoteAudioOuterRadius = 2500.f;

	/**
	 * Eje de reposo/swing de los brazos en component space del SKM unificado.
	 * Corregido R_z(-90°) respecto al blockout: (0,-1,0) = -Y → sube/baja visto de frente.
	 * Ajustar en BP si el binding pose del nuevo SKM requiere otro valor.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes")
	FVector ArmSwingAxis = FVector(0.f, -1.f, 0.f);

	/** Up/down wag axis for Cola in component space. R_z(-90°) corrected: (1,0,0) = X. */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes")
	FVector TailUpDownAxis = FVector(1.f, 0.f, 0.f);

	/** Side-to-side wag axis for Cola in component space. Z is invariant: (0,0,1). */
	UPROPERTY(EditDefaultsOnly, Category = "Emotes")
	FVector TailSideAxis = FVector(0.f, 0.f, 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionScanInterval = 0.1f;

	/**
	 * Alcance (cm) para coger e interactuar: radio del escaneo de interactuables (UpdateFocusedInteractable) y base de la
	 * validación del servidor (ServerTryInteract / ServerBeginHoldInteract: max(esto, el del interactuable) + 100 + holgura
	 * por ping). Bajado de 350 a 250 (#214). El BP_TortugaCharacter no lo pisa.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float MaxInteractionDistance = DefaultInteractionDistance;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction|Networking", meta = (ClampMin = "0.0"))
	float MaxLagCompensationDistance = 120.f;

	// ── Puntería de los lanzamientos (objetos, tinta y el compañero cogido, con la E o con el panzazo) ──
	// Salen hacia donde mira la cámara en horizontal, con un ángulo bajo y recto: ThrowBasePitchDeg con la cámara a nivel
	// y solo una parte de lo que se mire arriba o abajo (ThrowAimPitchFactor), entre los topes.

	/** Ángulo sobre la horizontal (grados) con la cámara a nivel. */
	UPROPERTY(EditDefaultsOnly, Category = "Throwable", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float ThrowBasePitchDeg = 25.f;

	/** Parte del cabeceo de la cámara que se suma al ángulo (0 = siempre el mismo; 1 = todo lo que se mire arriba o abajo). */
	UPROPERTY(EditDefaultsOnly, Category = "Throwable", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ThrowAimPitchFactor = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Throwable", meta = (ClampMin = "-30.0", ClampMax = "60.0"))
	float ThrowMinPitchDeg = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "Throwable", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float ThrowMaxPitchDeg = 45.f;

	// ── Camera Cinematic Settings (AAA) ───────────────────────────────────────

	/** Longitud del brazo en reposo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "50", ClampMax = "1200"))
	float CameraArmLengthDefault = 170.f;

	/** Longitud del brazo cuando el jugador está esprintando. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "50", ClampMax = "1200"))
	float CameraArmLengthSprint = 240.f;

	/** Velocidad de interpolación de la longitud del brazo (mayor = más rápido). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float CameraArmLengthInterpSpeed = 6.f;

	/** Campo de visión (FOV) de la cámara en reposo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "40.0", ClampMax = "120.0"))
	float CameraFOVDefault = 72.f;

	/** Campo de visión (FOV) de la cámara al esprintar (ligeramente mayor para sensación de velocidad). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "40.0", ClampMax = "120.0"))
	float CameraFOVSprint = 82.f;

	/** Velocidad de interpolación del FOV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float CameraFOVInterpSpeed = 5.f;

	/** Brazo de más (cm) mientras un ave te lleva por el aire (Player/TN_CarriedCamera.h): se ve adónde te lleva. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Carried", meta = (ClampMin = "0", ClampMax = "1500"))
	float CameraCarriedExtraArm = 260.f;

	/** Altura de más (cm) del pivote del brazo mientras un ave te lleva por el aire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Carried", meta = (ClampMin = "0", ClampMax = "600"))
	float CameraCarriedLift = 90.f;

	/** Segundos en alejarse del todo al cogerte el ave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Carried", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float CameraCarriedRiseSeconds = 0.9f;

	/** Segundos en volver a la distancia de siempre al soltarte. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Carried", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float CameraCarriedReturnSeconds = 1.4f;

	/** Cuánto se ha alejado la cámara por ir llevada por el aire (0-1, TNCarriedCamera::StepPull). */
	float CameraCarriedPull = 0.f;

	/**
	 * Lag de posición del spring arm (qué tan fluido sigue a la cápsula).
	 * 6-10 = cinematic suave. 20+ = casi sin lag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float CameraPositionLagSpeed = 14.f;

	/** Lag de rotación del spring arm. 12-16 = respuesta rápida pero suavizada. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float CameraRotationLagSpeed = 20.f;

	/**
	 * Offset del socket de la cámara respecto al pivot del spring arm.
	 * X = adelante/atrás, Y = derecha (over-the-shoulder), Z = arriba.
	 * (0, 80, 70) → over-the-shoulder derecho ajustado, estilo God of War.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic")
	FVector CameraSocketOffset = FVector(0.f, 80.f, 70.f);

	/**
	 * Offset relativo del pivot del spring arm en espacio del personaje (eleva el pivot).
	 * (0, 0, 55) → pivot en zona del tronco/hombros de la tortuga.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic")
	FVector CameraBoomRelativeOffset = FVector(0.f, 0.f, 55.f);

	/**
	 * CAM-01 floor clamp: estado interno del lift actual aplicado al boom relative
	 * Z para evitar clipping contra el suelo. Interpola suavemente hacia el valor
	 * deseado calculado cada tick en TickCameraInterp.
	 */
	float CameraFloorLiftCurrent = 0.f;

	/**
	 * Inclina la cámara hacia abajo respecto al spring arm (grados, valor negativo = abajo).
	 * -14°: cámara más picada sobre el personaje, estilo God of War.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Cinematic", meta = (ClampMin = "-30.0", ClampMax = "0.0"))
	float CameraAimPitchOffset = -14.f;

	/** Si true, el eje Y del ratón (arriba/abajo) se invierte. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Input")
	bool bInvertCameraY = false;

	/**
	 * Multiplicador de sensibilidad para el eje X (izquierda/derecha — Yaw).
	 * 1.0 = por defecto.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Input", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float LookSensitivityX = 1.0f;

	/**
	 * Multiplicador de sensibilidad para el eje Y (arriba/abajo — Pitch).
	 * 0.5 = la mitad que el X para evitar mareo. Ajusta al gusto.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Input", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float LookSensitivityY = 0.5f;

private:
	void CacheInputAssets();
	void ApplyInputMappingIfLocal();

	/** BeginPlay: resuelve huesos/sockets de emote (Pata1/2, Brazo1/2, Cola, Cabeza). */
	void ResolveAnimationBones();
	/** BeginPlay: resuelve KnockdownVisualComp (KnockdownComponentName → SkeletalMesh → StaticMesh hijo → fallback). */
	void ResolveKnockdownVisualComponent();
	/** BeginPlay: aplica a CameraBoom/FollowCamera los valores UPROPERTY que el BP hijo pudo sobrescribir. */
	void ApplyCameraDefaultsFromProperties();
	/** BeginPlay: cachea los materiales por defecto del SKM unificado en DefaultSkelMeshMaterials. */
	void CacheDefaultSkelMeshMaterials();
	/** BeginPlay: arranca el timer que reintenta aplicar cosméticos hasta que el PlayerState esté disponible. */
	void StartCosmeticRetryTimer();

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> LoadedMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedMoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedLookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedJumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedInteractAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedRotateInventoryAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedSprintAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedDropItemAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedShellAction;

	TWeakObjectPtr<ATN_InteractableBase> FocusedInteractable;
	/** Interactuable de mantener (rebuscar) cuya tecla sigue pulsada en esta máquina; solo el jugador local. */
	TWeakObjectPtr<ATN_InteractableBase> HoldInteractable;
	FTimerHandle InteractionScanTimerHandle;
	bool bInputAssetsLoaded = false;
	FVector2D LastMovementInput = FVector2D::ZeroVector;
	bool bSprintHeld = false;

	/** Socket name on the Skeletal Mesh where the helmet attaches. */
	UPROPERTY(EditDefaultsOnly, Category = "Cosmetics")
	FName HelmetSocketName = TEXT("Sombrero");

	/** Timer handle for repeating cosmetic application retry in BeginPlay. */
	FTimerHandle CosmeticRetryTimerHandle;
	int32 CosmeticRetryCount = 0;

	// ── Emote State ─────────────────────────────────────────────────────────
	int32 ActiveEmoteIndex   = -1;   ///< -1 = no emote active (local animation driver)
	float EmoteTime          =  0.f; ///< seconds since emote started
	bool  bEmoteBlendingOut  = false;
	float EmoteBlendOutTimer =  0.f;

	/** Snapshots of component rotations captured when a blend-out begins. */
	FRotator SnapshotBrazo1;
	FRotator SnapshotBrazo2;
	FRotator SnapshotPata1;
	FRotator SnapshotPata2;
	FRotator SnapshotCola;
	FRotator SnapshotCabeza;
	/** Snapshot of KnockdownVisualComp rotation — captured when knockdown emote blend-out begins. */
	FRotator SnapshotKnockdownComp;
	bool     bKnockdownCompSnapshotValid = false;

	/** Snapshots of component locations captured when a blend-out begins. */
	FVector SnapshotBrazo1Loc;
	FVector SnapshotBrazo2Loc;
	FVector SnapshotPata1Loc;
	FVector SnapshotPata2Loc;
	FVector SnapshotColaLoc;
	FVector SnapshotCabezaLoc;

	/** Bone names resolved from sockets in BeginPlay. Used by SetAnimBoneRot. */
	FName Brazo1Bone;
	FName Brazo2Bone;
	FName ColaBone;
	FName CabezaBone;
	FRotator Brazo1RestRot = FRotator::ZeroRotator;
	FRotator Brazo2RestRot = FRotator::ZeroRotator;
	FRotator ColaRestRot   = FRotator::ZeroRotator;
	FRotator CabezaRestRot = FRotator::ZeroRotator;
	FVector  CabezaRestScale = FVector::OneVector;

	/** Rest locations in component space (for SetLoc emotes). Re-derived in BeginPlay. */
	FVector Brazo1RestLoc = FVector::ZeroVector;
	FVector Brazo2RestLoc = FVector::ZeroVector;
	FVector Pata1RestLoc  = FVector::ZeroVector;
	FVector Pata2RestLoc  = FVector::ZeroVector;
	FVector ColaRestLoc   = FVector::ZeroVector;
	FVector CabezaRestLoc = FVector::ZeroVector;

	/** Loaded emote Input Actions (one per slot 0-9, Transient). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> LoadedEmoteActions;

	/**
	 * Materiales originales del SkeletalMesh unificado, cacheados en BeginPlay.
	 * Usados para restaurar el aspecto por defecto cuando se desequipa un skin (NAME_None).
	 * Transient: se recalcula cada vez que spawnea el pawn.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> DefaultSkelMeshMaterials;

	/** Conjunto de cosméticos que lleva puesto (lo rellenan UpdateHelmetMesh, UpdateSkinVisual y ApplyShellSlot). */
	FTN_TurtleLook CosmeticLook;

	// ── Leg animation state (cosmetic, local-only, never replicated) ──────────
	float LegPhaseAccumulator    = 0.f;   // cycles [0,1)
	float LegAmplitudeMultiplier = 0.f;   // [0,1] fade envelope

	FName Pata1Bone;
	FName Pata2Bone;
	FRotator Pata1RestRot = FRotator::ZeroRotator;
	FRotator Pata2RestRot = FRotator::ZeroRotator;

	// ── Air Dash internals ────────────────────────────────────────────────────
	/** True after landing; false after the first air dash of a jump. Not replicated — tracked per-machine. */
	bool bCanAirDash = true;

	virtual void Landed(const FHitResult& Hit) override;
	void PerformAirDashLocally();

	// ── Caídas, caparazón visual y temblor al llevar a alguien ───────────────
	void TickFallRules(float DeltaTime);
	void TickShellVisual(float DeltaTime);

	/** Ojos (cosmético, local): parpadeo de dibujo cada pocos segundos y ojos en espiral noqueada o muerta. */
	void TickEyes(float DeltaTime);
	float EyeBlinkTimer = 2.f;
	float EyeBlinkClock = 0.f;
	bool bEyeBlinking = false;
	/** Últimos valores escritos en el material (-1 = hay que volver a escribirlos, p. ej. tras cambiar el aspecto). */
	float EyeBlinkApplied = -1.f;
	float EyeDizzyApplied = -1.f;

	/** Cota más alta desde que empezó la caída actual (ápice). */
	float FallApexZ = 0.f;
	bool bTrackingFall = false;
	/** Géiser, tobogán: la próxima caída no cuenta hasta aterrizar. */
	bool bFallImmune = false;
	bool bAutoShelledThisFall = false;
	/** 0 = fuera del caparazón, 1 = metida del todo. Local y cosmético. */
	float ShellVisualAlpha = 0.f;
	bool bShellVisualApplied = false;
	FRotator CarryShake = FRotator::ZeroRotator;

	UFUNCTION(Server, Reliable)
	void ServerPerformAirDash();

	void Move(const FInputActionValue& Value);
	void OnMoveReleased();
	void Look(const FInputActionValue& Value);
	void TryInteract();
	/** Al soltar la tecla de interactuar: corta la interacción de mantener (rebuscar) si había una. */
	void ReleaseInteract();
	void RotateInventory();
	void StartSprint();
	void StopSprint();

	/** @brief Input: pide al UTN_ShellComponent entrar o salir del caparazon. */
	void ToggleShell();
	void DropEquippedItem();
	void TryUseEquippedItem();
	void RefreshSprintRequest();
	void UpdateFocusedInteractable();
	FVector GetItemForwardDirection() const;

	void TickLegAnimation(float DeltaTime);
	void TickCameraInterp(float DeltaTime);
	void ApplyLegAngle(FName BoneName, const FRotator& RestRot, float AngleDeg) const;
	void ApplyArmAngle(FName BoneName, const FRotator& RestRot, float RestOffsetDeg, float SwingDeg) const;
	USceneComponent* FindChildByName(FName Name) const;
	/** Trace descendente para encontrar el suelo bajo WorldLocation. Usado al soltar y aterrizar ítems. */
	FVector FindGroundBelow(const FVector& WorldLocation) const;

	// ── Emote system ─────────────────────────────────────────────────────────
	/** Start emote at index (0-9). Called from input — starts locally + replicates. */
	void TriggerEmote(int32 Index);
	void CancelEmoteLocalOnly();
	/** Internal: start emote animation on this machine (no ownership check). */
	void StartEmoteLocally(int32 Index);
	/** Cancel the active emote and begin a smooth blend-out back to rest. */
	void CancelEmote();
	/** Per-frame emote tick: advances animation and drives component rotations. */
	void TickEmote(float DeltaTime);
	const FTN_EmoteWheelEntry* ResolveWheelEmoteEntry(int32 EmoteID) const;
	void PlayWheelEmoteMontage(int32 EmoteID);
	void StopWheelEmoteMontage(int32 EmoteID, float BlendOutTime);

	// ── Emote Audio ──────────────────────────────────────────────────────────
	/** Lazily-created audio component for emote sounds. Attached to root, proximity-attenuated. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> EmoteAudioComponent;

	/** Start playing the sound for the given emote index (looped, proximity-attenuated). */
	void PlayEmoteSound(int32 Index);
	/** Stop any currently playing emote sound. */
	void StopEmoteSound();
	/** Callback: restarts emote audio when the clip ends, while the emote is still active. */
	UFUNCTION()
	void OnEmoteAudioFinished();
	/** Apply a single-axis rotation additively on top of a bone's rest rotation. */
	void ApplyEmoteAngle(FName BoneName, const FRotator& Rest, float AngleDeg, const FVector& Axis) const;
	/** Apply two-axis compound rotation on a bone. */
	void ApplyEmoteAngles2(FName BoneName, const FRotator& Rest,
	                       float A1, const FVector& Ax1,
	                       float A2, const FVector& Ax2) const;
	void ApplyEmoteAngles3(FName BoneName, const FRotator& Rest,
	                       float A1, const FVector& Ax1,
	                       float A2, const FVector& Ax2,
	                       float A3, const FVector& Ax3) const;

	/** Set a bone's rotation in component space. No-op if bone is NAME_None or mesh is null. */
	void SetAnimBoneRot(FName BoneName, const FRotator& Rot) const;
	/** Get a bone's current rotation in component space. */
	FRotator GetAnimBoneRot(FName BoneName) const;
	/** Set a bone's location in component space. */
	void SetAnimBoneLoc(FName BoneName, const FVector& Loc) const;
	/** Get a bone's current location in component space. */
	FVector GetAnimBoneLoc(FName BoneName) const;
	/** Set a bone's scale in component space. Pass FVector::OneVector to clear override. */
	void SetAnimBoneScale(FName BoneName, const FVector& Scale) const;
	// Per-emote input handlers (one-liners, bound in SetupPlayerInputComponent)
	void OnEmote0(); void OnEmote1(); void OnEmote2(); void OnEmote3(); void OnEmote4();
	void OnEmote5(); void OnEmote6(); void OnEmote7(); void OnEmote8(); void OnEmote9();

	/** Server RPC: set emote index on the replicated property so all clients see it. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetEmote(int32 Index);

	/** OnRep: fired on remote clients when ReplicatedEmoteIndex changes. */
	UFUNCTION()
	void OnRep_ReplicatedEmoteIndex();

	UFUNCTION(Client, Reliable)
	void ClientRejectEmote(int32 Index);

	UFUNCTION(Server, Reliable)
	void ServerTryInteract(ATN_InteractableBase* Interactable);

	/** Interacción de mantener (rebuscar un decorado): empieza. El servidor valida la distancia y cuenta el tiempo. */
	UFUNCTION(Server, Reliable)
	void ServerBeginHoldInteract(ATN_InteractableBase* Interactable);

	/** Interacción de mantener: la tecla se ha soltado (si no se había completado, se cancela). */
	UFUNCTION(Server, Reliable)
	void ServerEndHoldInteract(ATN_InteractableBase* Interactable);

	/**
	 * Usa el ítem de la mano. AimPoint es el punto de mira del dueño con su cámara real (#894; bHasAimPoint = lo tiene):
	 * los lanzamientos van a él si el servidor lo valida (ValidateClientAimPoint).
	 */
	UFUNCTION(Server, Reliable)
	void ServerUseEquippedItem(FVector_NetQuantize AimPoint, bool bHasAimPoint);

	/** Servidor: punto de mira del dueño validado durante ServerUseEquippedItem (GetCrosshairPoint lo devuelve). */
	TOptional<FVector> ServerUseAimPoint;

	// ── ServerUseEquippedItem: una rama por ETN_ItemUseType (validar → consumir → efecto) ──
	void HandleUseSelfStaminaBoost(const FTN_InventoryItem& EquippedItem);
	void HandleUseSelfStaminaFull(const FTN_InventoryItem& EquippedItem);
	void HandleUseTotem(const FTN_InventoryItem& EquippedItem);

	UFUNCTION(Server, Reliable)
	void ServerDropEquippedItem();

	/**
	 * Tilt o ragdoll del derribo (idempotente). Un solo camino de estado (#78): el servidor lo aplica al cambiar
	 * bIsKnockedDown y los clientes en OnRep_IsKnockedDown, también quien entra tarde. El golpe suena aparte, una vez
	 * por máquina (MulticastPlaySfx, no fiable).
	 */
	void ApplyKnockdownVisual(bool bKnocked);

	UFUNCTION()
	void OnRep_IsKnockedDown();

	UFUNCTION()
	void OnRep_IsDead();

	UFUNCTION()
	void OnRep_RagdollFrozen();


	/**
	 * Patrón canónico Epic para activar ragdoll sin que el cuerpo "salga lanzado":
	 *  1. StopAllMontages + bPauseAnims=true (CRÍTICO: evita que AnimBP pelee con el solver)
	 *  2. Capsule NoCollision
	 *  3. CMC StopMovementImmediately + DisableMovement + tick off
	 *  4. Line trace al suelo (evita penetración inicial → impulso de resolución)
	 *  5. SetCollisionProfileName("Ragdoll") en SkM
	 *  6. bBlendPhysics=true (CRÍTICO: la pose anim deja de dominar los bones)
	 *  7. SetSimulatePhysics(true) + SetAllBodiesSimulatePhysics(true)
	 *  8. SetAllPhysicsLinearVelocity(0) defensivo (post-simulate, no antes)
	 *  9. WakeAllRigidBodies
	 * Llamado en server (SetDeadVisual) y cliente (OnRep_IsDead).
	 */
	void EnterRagdollState();

	/** Server-only (timer RagdollFreezeDelay tras la muerte): captura la posición
	 *  autoritativa del root bone, replica bRagdollFrozen+RagdollFrozenLoc y
	 *  congela el ragdoll local del host. */
	void ServerFreezeRagdoll();

	/** Todas las máquinas: detiene la simulación del ragdoll (la pose queda
	 *  cacheada en component-space) y snapea el actor por delta a
	 *  RagdollFrozenLoc. Si el ragdoll acaba de arrancar (JIP), difiere el
	 *  freeze ~0.5s para que Chaos pose el cuerpo antes de petrificarlo. */
	void ApplyRagdollFreeze();

	/** Reverso de EnterRagdollState: detiene simulación y restaura state vivo. */
	void ExitRagdollState();

	/** Oculta extremidades, cabeza, cola y casco (solo visual). */
	void HideLimbs();

	/** Restaura la visibilidad de extremidades, cabeza, cola y casco. */
	void ShowLimbs();

	// ── Revive channeling (server-driven) ────────────────────────────────────
	/** Try to start reviving a nearby DBNO player. Called from ServerSetEmote when emote starts. */
	void TryStartReviveChannel();
	/** Cancel any active revive channel. Called when emote ends, player moves, or conditions fail. */
	void CancelReviveChannel();
	/** Tick the revive channel (server timer, 0.1s). Checks proximity + conditions. */
	void TickReviveChannel();

	/** OnRep: fired on remote clients when bIsReviving changes — replica el audio del canal. */
	UFUNCTION()
	void OnRep_IsReviving();

	/** Multicast: reproduce el sonido de éxito de revive en todas las máquinas. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayReviveSuccessSound();

	/** Multicast: el «¡clonc!» del derribo en cada máquina (KnockdownSound o, sin recurso, el sintetizado). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayKnockdownSound();

	TWeakObjectPtr<APlayerController> ReviveTargetPC;
	float ReviveChannelElapsed = 0.f;
	FTimerHandle ReviveChannelTimerHandle;

	// ── DBNO/Revive Audio (private) ─────────────────────────────────────────

	/** Audio component for revive channel sound (spatialized, on the reviver). Lazy-init. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ReviveAudioComponent;

	/** Audio component for DBNO heartbeat (non-spatialized, local player only). Lazy-init. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> DBNOAudioComponent;

	/** Create ReviveAudioComponent if it doesn't exist (spatialized, proximity-attenuated). */
	UAudioComponent* EnsureReviveAudioComponent();
	/** Create DBNOAudioComponent if it doesn't exist (non-spatialized, local-only). */
	UAudioComponent* EnsureDBNOAudioComponent();

	/**
	 * Crea y configura (sin registrar) un UAudioComponent espacializado con atenuación
	 * por proximidad, compartido por el setup de EmoteAudioComponent y ReviveAudioComponent.
	 * El caller debe bindear OnAudioFinished y llamar RegisterComponent() después.
	 */
	UAudioComponent* CreateProximityAudioComponent(FName Name, float InnerRadius, float OuterRadius);

	void PlayReviveChannelSound();
	void StopReviveChannelSound();
	void PlayReviveSuccessSound();
	void PlayDBNOHeartbeatSound();
	void StopDBNOHeartbeatSound();

	/** Callback for ReviveAudioComponent: re-loops revive channel sound while channeling. */
	UFUNCTION()
	void OnReviveAudioFinished();

	/** Callback for DBNOAudioComponent: re-loops heartbeat while DBNO. */
	UFUNCTION()
	void OnDBNOAudioFinished();

	/** Cuerpo de RecoverFromKnockdown y RecoverFromKnockdownSilently. */
	void RecoverFromKnockdownImpl(bool bPlayReviveSound);

protected:
	// ── Emote replication ────────────────────────────────────────────────────
	/** Emote index replicado a todos los clientes. -1 = sin emote. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedEmoteIndex)
	int32 ReplicatedEmoteIndex = -1;

	// ── Knockdown state ──────────────────────────────────────────────────────
	/**
	 * Estado replicado de knockdown. true → mesh tiltado 180°, movimiento bloqueado.
	 * BlueprintReadOnly en protected para que BPs hijos puedan leerlo (ej. para UI).
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsKnockedDown, BlueprintReadOnly, Category = "Knockdown")
	bool bIsKnockedDown = false;

	/**
	 * Nombre del componente visual a rotar durante knockdown (Ruta B: tilt manual).
	 * NAME_None → usa GetMesh() directamente (correcto con mesh único sin SCs adicionales).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown")
	FName KnockdownComponentName = NAME_None;

	/**
	 * Multiplicador aplicado a la velocidad horizontal del jugador en el momento del knockdown.
	 * 1.0 = conserva el momentum actual, 0.0 = caída vertical, >1.0 = amplifica el impulso.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float KnockdownHorizontalMultiplier = 1.0f;

	/**
	 * Fuerza hacia abajo (cm/s, valor positivo → se aplica en -Z) añadida al momentum de knockdown.
	 * Asegura que el personaje caiga aunque tenga velocidad vertical neutra o positiva.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown", meta = (ClampMin = "0.0", ClampMax = "1500.0"))
	float KnockdownDownwardForce = 400.f;

	/**
	 * Umbral de velocidad horizontal (cm/s) bajo el cual el CMC se bloquea al
	 * aterrizar durante knockdown. Sobre este umbral, el slide continúa y la
	 * fricción del CMC lo va frenando de forma natural (resbalón del plátano).
	 * Bajarlo a 0 = desliza hasta parar completamente. Subirlo = stop más brusco.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown", meta = (ClampMin = "0.0"))
	float KnockdownGroundLockSpeed = 50.f;

	/** Velocidad máxima (cm/s) con la que arranca el ragdoll del derribo (los golpes muy fuertes se recortan). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown", meta = (ClampMin = "100.0"))
	float KnockdownRagdollMaxEntrySpeed = 2200.f;

	/** Margen (cm) sobre la superficie al devolver encima un ragdoll que la ha atravesado. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown", meta = (ClampMin = "0.0"))
	float KnockdownRagdollTunnelMargin = 25.f;

	/** Tiempo mínimo tumbada en el suelo (s): aunque el golpe pida menos, se queda quieta un momento. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown", meta = (ClampMin = "0.0"))
	float MinKnockdownSeconds = 2.2f;

	/** Duración de la animación de levantarse desde la pose del ragdoll (s); sin moverse mientras tanto. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown", meta = (ClampMin = "0.1"))
	float GetUpSeconds = 0.75f;

	/**
	 * Si true y el SkelMesh (GetMesh) tiene PhysicsAsset asignado, el knockdown
	 * activa ragdoll físico completo (`SetSimulatePhysics(true)`) en lugar del
	 * tilt de -180° manual. El ragdoll se desactiva en RecoverFromKnockdown y
	 * el mesh se re-attachea al capsule + restaura pose por defecto.
	 *
	 * También controla el ragdoll de muerte en SetDeadVisual.
	 *
	 * Si el BP no tiene PhysicsAsset, se cae silenciosamente al tilt tradicional
	 * — ningún cambio visible respecto al comportamiento previo.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown")
	bool bUsePhysicsRagdoll = true;

	/**
	 * Perfil de colisión aplicado al SkelMesh durante ragdoll. "Ragdoll" es el
	 * preset estándar de UE que permite físicas pero ignora al pawn. Cambiar
	 * sólo si el proyecto define perfiles custom.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown")
	FName RagdollCollisionProfile = TEXT("Ragdoll");

	/**
	 * Hueso de referencia para el tracking de posición durante el ragdoll de muerte.
	 * El servidor mueve el actor a este hueso cada tick → bReplicateMovement envía
	 * la posición real del cuerpo mientras cae, no el punto de muerte estancado.
	 * "pelvis" es el valor correcto para el esqueleto Mannequin/UE5 estándar.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Knockdown")
	FName RagdollTrackingBone = TEXT("pelvis");

	/**
	 * Small upward lift applied before death ragdoll starts. Prevents low leg
	 * bodies from spawning already penetrated into the floor.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Death", meta = (ClampMin = "0.0", ClampMax = "160.0"))
	float DeathRagdollSpawnLift = 60.f;

	/** Extra clearance between the skeletal mesh lower bound and the floor at death ragdoll startup. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Death", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float DeathRagdollFloorClearance = 25.f;

	/**
	 * Estado replicado de "muerte visual". true → extremidades/cabeza/cola/casco ocultos.
	 * El pawn NO se destruye: queda como cadáver interactuable para revive.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsDead, BlueprintReadOnly, Category = "Death")
	bool bIsDead = false;

	/**
	 * Suelo donde arranca el ragdoll de muerte (el servidor sube el cuerpo y deja de replicar el movimiento en el mismo
	 * fotograma). Se escribe junto a bIsDead: llegan en la misma actualización y OnRep_IsDead ya la tiene.
	 */
	UPROPERTY(Replicated)
	FVector_NetQuantize DeathGroundLocation = FVector_NetQuantize::ZeroVector;

	/**
	 * true cuando el servidor congeló el ragdoll de muerte (fin de la simulación
	 * local divergente). Un JIP lo recibe en el bunch inicial junto a bIsDead →
	 * aplica el cadáver ya congelado en la posición autoritativa.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_RagdollFrozen, BlueprintReadOnly, Category = "Death")
	bool bRagdollFrozen = false;

	/** Posición world autoritativa del root bone al congelar. El servidor la
	 *  escribe ANTES que bRagdollFrozen → ambas llegan en el mismo bunch. */
	UPROPERTY(Replicated)
	FVector_NetQuantize RagdollFrozenLoc = FVector_NetQuantize::ZeroVector;

	/** Segundos de simulación de ragdoll antes de que el servidor congele y
	 *  snapee a todos a su posición autoritativa. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Death", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float RagdollFreezeDelay = 2.f;

	/** Instante local (GetTimeSeconds) en que arrancó el ragdoll en ESTA máquina.
	 *  No replicado — sirve para diferir el freeze si la sim acaba de empezar (JIP). */
	float LocalRagdollStartTime = -1.f;

	FTimerHandle RagdollFreezeTimerHandle;

	FTimerHandle KnockdownTimerHandle;

	// ── Mareo (#2) ────────────────────────────────────────────────────────────

	/**
	 * Velocidad máxima durante el mareo (cm/s). Por defecto ~55% de la velocidad base.
	 * 0 = sin penalización de velocidad.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Mareo", meta = (ClampMin = "0.0"))
	float MareoSpeedCap = 250.f;

	/**
	 * Mareada: el servidor lo pone en ApplyMareoEffect y lo quita al acabar (EndMareo). Antes iba en una multicast no fiable:
	 * si se perdía, el dueño andaba a 450 mientras el servidor lo simulaba a 250 y lo corregía durante 3 s (#574).
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Mareo)
	bool bMareo = false;

	UFUNCTION()
	void OnRep_Mareo();

	/** El tope del mareo en esta máquina, una vez por cambio. */
	void ApplyMareoLocalState(bool bOn);
	bool bMareoApplied = false;

	/**
	 * Llamado en TODAS las máquinas (servidor + clientes) tras aplicar el visual
	 * de muerte o resurrección. Úsalo en BP_TortugaCharacter para activar el raptor,
	 * VFX de muerte, audio, etc.
	 * @param bDead  true = jugador acaba de morir, false = fue revivido.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Death")
	void OnDeathVisualSet(bool bDead);

	FTimerHandle MareoTimerHandle;

	/** Servidor: acaba el mareo (temporizador de ApplyMareoEffect, con CreateUObject: si el actor ya no está, no hace nada). */
	void EndMareo();

	/** Rotación relativa del mesh al spawnear (guardada en BeginPlay para restaurarla). */
	FRotator MeshDefaultRelativeRotation = FRotator::ZeroRotator;

	/** Componente visual para el tilt de knockdown (SkeletalMesh o StaticMesh blockout). */
	TWeakObjectPtr<USceneComponent> KnockdownVisualComp;

	// ── Ragdoll knockdown state ─────────────────────────────────────────────
	/** Transform relativo del SkelMesh antes de activar ragdoll — para restaurar pose. */
	FTransform SnapshotSkelMeshRelTransform;
	/** Perfil de colisión del SkelMesh antes del ragdoll — para restaurar al recover. */
	FName SnapshotSkelMeshCollisionProfile = NAME_None;
	/** true mientras el ragdoll físico está activo (evita doble-activación y no-ops al recover). */
	bool bKnockdownRagdollActive = false;
	/** Ya se ha comprobado (y avisado, si hacía falta) que los cuerpos del ragdoll chocan con el mundo. */
	bool bRagdollCollisionReported = false;
	/** Cuerpo raíz del ragdoll en el fotograma anterior: si de uno a otro cruza el suelo, se devuelve encima. */
	FVector RagdollProbeLast = FVector::ZeroVector;
	bool bRagdollProbeValid = false;
	/** Dónde estaba de pie la cápsula al caer: último recurso si al levantarse no hay suelo bajo el cuerpo. */
	FVector PreKnockdownStandLocation = FVector::ZeroVector;
	/**
	 * Cada fotograma con el ragdoll del derribo (en todas las máquinas): que el cuerpo no atraviese el suelo aunque vaya
	 * muy rápido y que la cápsula lo siga, para que la cámara (en el brazo de la cápsula) siga a la tortuga tumbada.
	 */
	void TickKnockdownRagdoll(float DeltaTime);
	/** Centro de la cápsula de pie sobre el suelo que hay bajo From (o encima, si el cuerpo quedó por debajo); false si no hay. */
	bool FindStandSpotNear(const FVector& From, FVector& OutStandLoc) const;
	/** Hasta cuándo (tiempo del mundo) dura la animación de levantarse: sin moverse ni saltar (local). */
	float GetUpLockUntil = -1.f;
	/** Pasa la pose del ragdoll (en locales, ya en el sitio nuevo de la cápsula) a la animación de levantarse. */
	void BeginGetUpFromWorldPose(const TArray<FTransform>& WorldPose);

	// ── Dive state ────────────────────────────────────────────────────────────

	/**
	 * true durante toda la fase de dive (vuelo + slide de recuperación).
	 * OnRep aplica el visual (tilt + capsule resize) en todos los clientes.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsDiving, BlueprintReadOnly, Category = "Dive")
	bool bIsDiving = false;

	/**
	 * Número del panzazo en curso (1-255; al dar la vuelta se salta el 0). Lo sube el servidor al empezar cada uno: el
	 * movimiento (UTN_TurtleMovementComponent) sabe así de qué panzazo se ha levantado ya y no vuelve a arrastrarse
	 * mientras llega el fin del panzazo.
	 */
	UPROPERTY(Replicated)
	uint8 DiveSerial = 0;

	// ── Sombrilla (#29) ───────────────────────────────────────────────────────

	/**
	 * true mientras la sombrilla está abierta y protege de la gaviota.
	 * La gaviota dinámica (TN_EnemySeagull) comprueba este flag antes de matar.
	 * Replicado para que todos los clientes puedan mostrar el estado visual.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Umbrella")
	bool bHasUmbrellaProtection = false;

	// ── La cabeza que sigue a la cámara (#623) ─────────────────────────────────
	/**
	 * Guiñada de la vista del dueño respecto del cuerpo, para los demás (TNHeadLook::EncodeYaw: un byte, de -180 a 180° en
	 * pasos de 1,4°). La escribe el servidor en PreReplication con el giro del mando, que de un cliente le llega con el
	 * movimiento (ServerMove): sin RPC propio. Solo cambia si se mueve 2 pasos o más. El cabeceo ya lo manda el motor
	 * (RemoteViewPitch16).
	 */
	UPROPERTY(Replicated)
	uint8 ReplicatedViewYaw = 0;

	/** Tiempo acumulado desde que comenzó el dive (para DiveMinLockDuration). */
	float DiveLockTimer = 0.f;

	/** Servidor: el panzazo que empezó en un movimiento lanza lo que llevaba en el siguiente TickDive (#24). */
	struct FTNPendingDiveThrow
	{
		bool bPending = false;
		FVector DiveDir = FVector::ZeroVector;
		FVector DiveVelocity = FVector::ZeroVector;
		FVector CarrierVelocity = FVector::ZeroVector;
	};
	FTNPendingDiveThrow PendingDiveThrow;

	/**
	 * Servidor: estampado contra la pared apuntado por el movimiento (#355; NoteDiveSplat). Lo hace ServerDiveSplat en el
	 * siguiente TickDive, si sigue en el mismo panzazo.
	 */
	struct FTNPendingDiveSplat
	{
		bool bPending = false;
		uint8 Serial = 0;
		FVector BallVelocity = FVector::ZeroVector;
		FVector Where = FVector::ZeroVector;
		FVector WallNormal = FVector::ZeroVector;
		float Strength = 0.f;
	};
	FTNPendingDiveSplat PendingDiveSplat;

	/** Todas las máquinas: polvo y golpe sintetizado contra la pared. Efecto de entrada, no fiable (#78); el mareo va por DiveSplatDizzyUntil. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_DiveSplatFX(FVector_NetQuantize Where, FVector_NetQuantizeNormal WallNormal, float Strength);

	/**
	 * Hora del servidor (AGameStateBase::GetServerWorldTimeSeconds) hasta la que dura el mareo del último estampado: estado
	 * replicado, así los pajaritos salen aunque se pierda el multicast y quien entra tarde los ve el tiempo que les quede.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_DiveSplatDizzyUntil)
	float DiveSplatDizzyUntil = 0.f;

	/** Enciende los pajaritos el tiempo que le quede a DiveSplatDizzyUntil (el servidor lo llama a mano). */
	UFUNCTION()
	void OnRep_DiveSplatDizzyUntil();

	/** Fin de los pajaritos del estampado en esta máquina (los deja si está derribada). */
	void EndDiveSplatDizzy();

	FTimerHandle DiveSplatDizzyTimerHandle;

	/** Alpha del tilt del cuerpo [0 = reposo, 1 = pose completa de dive]. Cosmético, local. */
	float DiveTiltAlpha = 0.f;

	/** HalfHeight original de la cápsula (guardada en BeginPlay). */
	float DiveCapsuleOrigHalfHeight = 88.f;

	/** Rotación relativa por defecto de GetMesh() (guardada en BeginPlay). */
	FRotator DiveMeshDefaultRot = FRotator::ZeroRotator;

	/** Posición relativa por defecto de GetMesh() (guardada en BeginPlay; el panzazo la sube). */
	FVector DiveMeshDefaultLoc = FVector::ZeroVector;

	/** Escala relativa por defecto de GetMesh() (guardada en BeginPlay; el panzazo la aplasta). */
	FVector DiveMeshDefaultScale = FVector::OneVector;

	// ── Jump Animation state (cosmetic, local-only) ───────────────────────────
	bool  bJumpAnimActive = false;
	float JumpAnimTime    = 0.f;

	/** El probador tiene dentro a esta tortuga (SetHeadLookSuppressed): la cabeza no sigue a la cámara. */
	bool bHeadLookSuppressed = false;

	void TryDive();

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_StartDive(FVector DiveDir);

	/** Inclinación y cápsula del panzazo (idempotente): el servidor al cambiar bIsDiving y los clientes en OnRep_IsDiving (#78). */
	void ApplyDiveVisual(bool bEnter);

	UFUNCTION()
	void OnRep_IsDiving();

	void EndDive();
	void TickDive(float DeltaTime);
	void TickJumpAnim(float DeltaTime);

	/**
	 * Fin del panzazo: la cápsula vuelve a estar de pie con los pies donde están. En el servidor y el dueño lo hace el
	 * movimiento (UTN_TurtleMovementComponent::RestoreStandingCapsule); en los demás, crece y sube lo mismo. Antes crecía en
	 * su sitio y la mitad de abajo se metía en la malla fina del terreno: al desincrustarse caía por debajo del mapa.
	 */
	void RestoreDiveCapsule();


public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Servidor: si la base de movimiento que se va a replicar no se puede encontrar por red (malla creada en ejecución: el
	 * terreno y el decorado local de la playa, piezas de los elementos), se replica como «sin base», con la posición del
	 * mundo de siempre. Si no, los demás clientes la ven «sin resolver» y el motor deja de simular y de suavizar a esta
	 * tortuga (se queda quieta, a tirones o con la malla en otro sitio). Ver UTN_TurtleMovementComponent::IsNetResolvableBase.
	 */
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;

	/** Campos de visión de la cámara en reposo y al correr (el ajuste de campo de visión del menú de pausa los cambia). */
	float GetCameraFOVDefault() const { return CameraFOVDefault; }
	float GetCameraFOVSprint() const { return CameraFOVSprint; }
	void SetCameraFOVs(float InDefault, float InSprint) { CameraFOVDefault = InDefault; CameraFOVSprint = InSprint; }

	/**
	 * Aplica knockdown a este personaje durante Duration segundos.
	 * Solo tiene efecto si se llama en el servidor (HasAuthority).
	 * Accesible desde TN_ThrowableItemActor y cualquier otro actor de gameplay.
	 */
	/**
	 * @param Duration      Segundos que dura el knockdown.
	 * @param ImpulseOverride Si != ZeroVector, usa este impulso en vez del momentum actual.
	 */
	UFUNCTION(BlueprintCallable, Category = "Knockdown")
	void ApplyKnockdown(float Duration, FVector ImpulseOverride = FVector::ZeroVector);

	/** Recover from knockdown immediately (server-only). Used by RunGameMode::RevivePlayer. */
	UFUNCTION(BlueprintCallable, Category = "Knockdown")
	void RecoverFromKnockdown();

	/**
	 * Como RecoverFromKnockdown, pero sin el sonido de reanimar (server-only). Para la muerte
	 * (ATN_RunGameMode::ApplyDeathVisuals), que también levanta el derribo y no debe sonar a «¡arriba!» (#348).
	 */
	void RecoverFromKnockdownSilently();

	/** Returns true if this character is currently in a knockdown/DBNO state. */
	UFUNCTION(BlueprintPure, Category = "Knockdown")
	bool IsKnockedDown() const { return bIsKnockedDown; }

	/** Returns true while the character is diving or in the locked recovery slide. */
	UFUNCTION(BlueprintPure, Category = "Dive")
	bool IsDiving() const { return bIsDiving; }

	/** Número del panzazo en curso (0 = aún ninguno). */
	uint8 GetDiveSerial() const { return DiveSerial; }

	// ── Panzazo predicho (#24) ───────────────────────────────────────────────

	/**
	 * Dentro del movimiento que pide el panzazo (UTN_TurtleMovementComponent::TickDiveStart), en el servidor y en el dueño,
	 * también al repetirlo: decide con TNDiveLogic::DecideDiveStart si empieza (DiveYaw comprimido; en el aire, sin otro
	 * lanzamiento en este paso) y, si empieza, cuenta el panzazo, encoge la cápsula y da la velocidad con que sale
	 * (OutLaunchVelocity, que el movimiento aplica). Fuera de una repetición, además: el giro hacia la dirección y lo visual; en
	 * el servidor, el emote cancelado, el lanzamiento de lo que lleve (en el siguiente TickDive: nada se crea dentro del
	 * movimiento del cliente) y el aviso a todos. VelocityBefore: la de antes de lanzarse (la del salto).
	 */
	bool StartDiveFromMove(uint16 DiveYaw, bool bInAir, bool bLaunchPending, bool bReplaying, const FVector& VelocityBefore, FVector& OutLaunchVelocity);

	/**
	 * Servidor, dentro del movimiento (UTN_TurtleMovementComponent::OnMovementUpdated, #355): en el vuelo del panzazo se ha
	 * estampado contra la pared. Solo lo apunta (el primero de este panzazo); lo hace ServerDiveSplat en el siguiente
	 * TickDive. BallVelocity: la reflejada; Where y WallNormal: el choque; Strength (0..1): la fuerza del golpe.
	 */
	void NoteDiveSplat(const FVector& BallVelocity, const FVector& Where, const FVector& WallNormal, float Strength);

	/** Servidor: hay un estampado apuntado que aún no se ha hecho. */
	bool HasPendingDiveSplat() const { return PendingDiveSplat.bPending; }

	/**
	 * Servidor, fuera del movimiento (lo llama TickDive con el estampado apuntado): si sigue en ese panzazo, lo acaba y la
	 * lanza como bola de caparazón con la velocidad reflejada (UTN_ShellComponent::StartBody; la caja se replica sola y sale
	 * del caparazón al pararse). Suelta antes a quien lleve. Avisa a todos del golpe (Multicast_DiveSplatFX).
	 */
	void ServerDiveSplat();

	/** Cliente dueño, en una corrección: el panzazo que tenía el servidor en ese movimiento (sin OnRep: la cápsula la pone el movimiento). */
	void ApplyServerDiveCorrection(bool bDiving, uint8 Serial);

	/** Derribada, muerta, en el caparazón o en brazos de otra: no puede empezar un panzazo. */
	bool IsDiveBlocked() const;

	/** Velocidad y ajustes de la inercia del panzazo (Dive y Dive|Momentum). */
	TNDiveLogic::FDiveMomentumParams GetDiveMomentumParams() const;

	/** Durante el panzazo, girando hacia su dirección: el giro (grados) y la velocidad del giro (grados/s). Lo aplica el movimiento. */
	bool GetDiveYawTurn(float& OutTargetYaw, float& OutDegreesPerSecond) const
	{
		OutTargetYaw = DiveTargetYaw;
		OutDegreesPerSecond = DiveYawInterpSpeed;
		return bIsDiving && bDiveYawInterpActive;
	}

	/** Velocidad horizontal al saltar (la inercia del panzazo); el movimiento la guarda y la repone al repetir. */
	const FVector& GetJumpStartHorizontalVelocity() const { return JumpStartHorizontalVelocity; }
	void SetJumpStartHorizontalVelocity(const FVector& InVelocity) { JumpStartHorizontalVelocity = InVelocity; }

	/** Movimiento de la tortuga (con el arrastre del panzazo); null si el Blueprint pusiera otra clase. */
	UTN_TurtleMovementComponent* GetTurtleMovement() const;

	/** Semialtura sin escalar de la cápsula de pie (la de la clase: sin el encogido del panzazo). */
	float GetStandingCapsuleHalfHeight() const;

	/**
	 * Pose de panzazo (en el aire o sobre la tripa). Se apaga en cuanto se levanta: el dueño y el servidor lo saben al
	 * momento por el movimiento; las demás máquinas, cuando llega el fin del panzazo.
	 */
	bool IsBellyPoseActive() const;

	/** Sobre la tripa en el suelo: arrastrándose tras el panzazo o reptando sin sitio para levantarse. */
	bool IsBellyOnGround() const;

	/** Returns true once the character has died and before any revive restores it. */
	UFUNCTION(BlueprintPure, Category = "Death")
	bool IsDead() const { return bIsDead; }

	/** Devuelve el componente de stamina (acceso de solo lectura para sistemas externos). */
	UTN_StaminaComponent* GetStaminaComponent() const { return StaminaComponent; }

	/** Devuelve el componente de caparazón (acceso de solo lectura para sistemas externos). */
	UTN_ShellComponent* GetShellComponent() const { return ShellComponent; }

	/** Componente de coger y lanzar. */
	UTN_CarryComponent* GetCarryComponent() const { return CarryComponent; }

	/** Componente del guantazo con la aleta. */
	UTN_FlipperSlapComponent* GetFlipperSlapComponent() const { return FlipperSlap; }

	/**
	 * Dirección de un lanzamiento (objeto, tinta o compañero) con el giro del mando AimRotation: el rumbo de la cámara y un
	 * ángulo bajo sobre la horizontal (ThrowBasePitchDeg con la cámara a nivel; ver Throwable). Vale en el servidor.
	 */
	FVector GetThrowDirection(const FRotator& AimRotation) const;

	/** Hacia dónde se lanza o se usa algo: el giro del mando, que es la cámara. Vale en el dueño y en el servidor. */
	FRotator GetTurtleAimRotation() const;

	/** Los lanzamientos van al punto del centro de la pantalla (si no, siguen al giro del mando con GetThrowDirection). */
	bool UsesCameraThrowAim() const;

	/**
	 * Velocidad inicial (dirección) para que un lanzamiento que sale de Origin a Speed (cm/s) caiga en el punto que se ve en
	 * el centro de la pantalla: el primer sitio que corta el rayo de la cámara (o, sin nada, un punto lejano), con el arco
	 * justo para llegar. Si no llega, el ángulo de máximo alcance. GravityCmS2 <= 0 usa la del mundo. LinearDamping (1/s) compensa
	 * el frenado en el aire de lo lanzado (la caja de la concha lo tiene). Vale en el servidor.
	 */
	FVector GetThrowDirectionToCrosshair(const FVector& Origin, const FRotator& AimRotation, float Speed, float GravityCmS2 = 0.f, float LinearDamping = 0.f) const;

	/** Como GetThrowDirectionToCrosshair, pero al punto Target ya conocido (el punto de mira que manda el dueño, #894). */
	FVector GetThrowDirectionToPoint(const FVector& Origin, const FVector& Target, const FRotator& AimRotation, float Speed, float GravityCmS2 = 0.f, float LinearDamping = 0.f) const;

	/**
	 * El punto del mundo que se ve en el centro de la pantalla (primer choque del rayo de la cámara, o uno lejano). En el
	 * servidor, mientras se atiende ServerUseEquippedItem, el punto que mandó el dueño si se validó.
	 */
	bool GetCrosshairPoint(FVector& OutPoint) const;

	/**
	 * Servidor: el punto de mira que manda el dueño (#894) si es creíble (TNThrowAim::IsClientAimPointValid) desde la
	 * cámara de esta tortuga y su rotación de control. Si no lo es (o no lo tiene), unset: se usa GetCrosshairPoint.
	 */
	TOptional<FVector> ValidateClientAimPoint(const FVector& AimPoint, bool bHasAimPoint) const;

	/** El golpe de brazo de lanzar un objeto, en todas las máquinas (cosmético; lo manda el servidor al lanzarlo). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastItemThrowAnim();

	/** Interactuable al alcance que se usaría ahora (solo en el jugador local; lo enseña el aviso del HUD). */
	ATN_InteractableBase* GetFocusedInteractable() const { return FocusedInteractable.Get(); }

	/** Interactuable de mantener cuya tecla sigue pulsada aquí (solo el jugador local; el aro del HUD); o nullptr. */
	ATN_InteractableBase* GetHoldInteractable() const { return HoldInteractable.Get(); }

	/** Acción de interactuar (Enhanced Input), para mostrar su tecla. */
	UInputAction* GetInteractAction() const { return LoadedInteractAction; }

	/** Emote que se está animando (-1 = ninguno; KNOCKDOWN_EMOTE_ID = tumbada) y su tiempo, para UTN_TurtleAnimInstance. */
	int32 GetActiveEmoteIndex() const { return ActiveEmoteIndex; }
	float GetEmoteTime() const { return EmoteTime; }

	/** La caída en curso (o la siguiente) no auto-encapsula ni mata hasta aterrizar o entrar al agua. */
	void SetFallImmuneUntilLanded() { bFallImmune = true; }

	UFUNCTION(BlueprintPure, Category = "Fall")
	bool IsFallImmune() const { return bFallImmune; }

	/** Temblor de cámara local (lo pone UTN_CarryComponent cuando la carga forcejea). */
	void SetCarryShake(const FRotator& Shake) { CarryShake = Shake; }

	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override;

	/** @brief true mientras el personaje está metido en su caparazón. */
	UFUNCTION(BlueprintPure, Category = "Shell")
	bool IsInShell() const;

	/**
	 * @brief Reacción del personaje a entrar o salir del caparazón.
	 * @note La llama UTN_ShellComponent::ApplyShellState en TODAS las máquinas.
	 *       El componente gobierna el estado y la velocidad; esto es lo que toca
	 *       al personaje (cancelar el emote en curso, visual del caparazón).
	 */
	void OnShellStateChanged(bool bInShell);

	/**
	 * Caparazón con física propia (UTN_ShellComponent / ATN_ShellBody): pone la cápsula de pie sobre la caja y la malla
	 * tumbada sobre la tripa con la transformación de la caja. Todas las máquinas, cada fotograma tras la física.
	 */
	void PlaceOnShellBody(const FTransform& BoxWorld);

	/** Devuelve la malla a su posición, giro y escala de serie dentro de la cápsula. */
	void ResetMeshTransform();

	/**
	 * Punto centralizado para matar a este personaje.
	 * Resuelve RunGameMode y PlayerController internamente.
	 * Los actores del mundo llaman esto en vez de acceder a GameMode directamente.
	 * Solo tiene efecto en el servidor (HasAuthority).
	 */
	UFUNCTION(BlueprintCallable, Category = "Death")
	void RequestKill(AActor* KillInstigator = nullptr);

	/**
	 * Activa/desactiva el visual de muerte: oculta extremidades, cola, cabeza, casco.
	 * El pawn permanece en el mundo como cadáver interactuable.
	 * Solo llamar desde el servidor — replica via OnRep + Multicast.
	 */
	UFUNCTION(BlueprintCallable, Category = "Death")
	void SetDeadVisual(bool bDead);

	// ── Tótem auto-revive ─────────────────────────────────────────────────────

	/**
	 * Sonido reproducido en todas las máquinas cuando el tótem del inventario
	 * te salva de morir. Arrastra tu SoundCue/Wave aquí — sin nodos BP.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Totem|Audio")
	TObjectPtr<USoundBase> TotemSelfReviveSound;

	/**
	 * VFX reproducido en todas las máquinas al auto-revivir con el tótem.
	 * Arrastra tu Particle System aquí — sin nodos BP.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Totem|FX")
	TObjectPtr<UParticleSystem> TotemSelfReviveVFX;

	/**
	 * Evento BP disparado en TODAS las máquinas cuando el tótem del inventario
	 * impide tu muerte. Usar para lógica BP extra (HUD, cámara shake, etc.).
	 * Audio/VFX básicos ya se reproducen solos.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory|Totem")
	void OnTotemAutoRevive();

	/** Notifica a todos los clientes del auto-revive (dispara OnTotemAutoRevive + Audio/VFX). */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_OnTotemAutoRevive();

	UFUNCTION(BlueprintCallable, Category = "Emotes")
	void RequestWheelEmote(uint8 EmoteID);

	/**
	 * Emote oculto (#839): el siguiente de los dos que no están en la rueda (TNSecretEmote::HiddenEmotes), por turnos. Lo
	 * llama AMP_GamePlayerController al escribir el código secreto con el teclado; se arranca, se pide al servidor y se
	 * replica como cualquier otro emote. Solo en la máquina que controla a la tortuga.
	 */
	void PlayHiddenEmote();

	UFUNCTION(BlueprintCallable, Category = "Stamina")
	void GrantInfiniteStamina(float DurationSeconds);

	/**
	 * Actualiza el mesh del casco en el socket "Sombrero" del personaje.
	 * Llamado desde TN_CoopPlayerState::OnRep_EquippedHelmetId (clientes)
	 * y desde MP_GamePlayerController::ServerSetEquippedHelmet (servidor/listen-server).
	 * HelmetId == NAME_None → oculta el casco.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	void UpdateHelmetMesh(FName HelmetId);

	/**
	 * Aplica el skin de personaje indicado.
	 * Busca FTN_SkinData en DT_Skins (vía MP_GameInstance::GetSkinDataTable) y mapea
	 * 5 materiales a los slots del SkM unificado:
	 *   0=Belly · 1=EyeShine · 2=EyesMouth · 3=Skin · 4=Shell.
	 * Slots con material null se mantienen con el default cacheado en BeginPlay.
	 * SkinId == NAME_None → restaura los materiales por defecto del BP.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	void UpdateSkinVisual(FName SkinId);

	/** Relee el caparazón del PlayerState (EquippedShellId) y vuelve a vestir a la tortuga. */
	void ApplyShellSlot();

	/** Viste a la tortuga con CosmeticLook (UTN_CosmeticLook::ApplyLook). */
	void RefreshCosmeticLook();

	/**
	 * @brief Re-aplica casco y skin leyendo el PlayerState actual (EquippedHelmetId/EquippedSkinId).
	 * @return true si se encontró un ATN_CoopPlayerState y se aplicaron los cosméticos; false si aún no hay PlayerState.
	 * @note Centraliza el patrón usado en el timer de reintento (BeginPlay), PawnClientRestart
	 *       y OnRep_PlayerState — los tres re-aplican cosméticos desde el PlayerState del pawn.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cosmetics")
	bool ApplyCosmeticsFromPlayerState();

	// ── Revive system (DBNO) ─────────────────────────────────────────────────

	/** Radius in cm within which a teammate can revive a DBNO player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DBNO", meta = (ClampMin = "50.0"))
	float ReviveRadiusCm = 300.f;

	/** Seconds required to channel a revive (must stay in range and keep emoting). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DBNO", meta = (ClampMin = "0.5"))
	float ReviveDurationSeconds = 3.f;

	/**
	 * True while this character is actively channeling a revive on a DBNO teammate.
	 * Replicated for HUD visualization on all clients. ReplicatedUsing dispara el audio
	 * de canal de revive en los clientes remotos (ver OnRep_IsReviving).
	 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IsReviving, Category = "DBNO")
	bool bIsReviving = false;

	/**
	 * Revive channel progress [0..1]. Owner-only replication for the reviver's HUD.
	 */
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "DBNO")
	float ReviveProgress = 0.f;

	// ── DBNO/Revive Audio ────────────────────────────────────────────────────

	/**
	 * Sound played in loop on the REVIVER while channeling a revive.
	 * Proximity-attenuated so nearby players hear it.
	 * Assign in Blueprint Class Defaults. Leave null for silence.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DBNO|Audio")
	TObjectPtr<USoundBase> ReviveChannelSound;

	/**
	 * One-shot sound played on the REVIVED player when revive completes.
	 * Proximity-attenuated — all nearby players hear the success cue.
	 * Assign in Blueprint Class Defaults. Leave null for silence.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DBNO|Audio")
	TObjectPtr<USoundBase> ReviveSuccessSound;

	/**
	 * Looped sound played locally on the DBNO player (heartbeat / tension).
	 * Only the incapacitated player hears this (non-spatialized, local only).
	 * Assign in Blueprint Class Defaults. Leave null for silence.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DBNO|Audio")
	TObjectPtr<USoundBase> DBNOHeartbeatSound;

	/** Inner radius (cm) for DBNO/revive audio attenuation (full volume). */
	UPROPERTY(EditDefaultsOnly, Category = "DBNO|Audio", meta = (ClampMin = "0.0"))
	float ReviveAudioInnerRadius = 300.f;

	/** Outer radius (cm) for DBNO/revive audio attenuation (silence). */
	UPROPERTY(EditDefaultsOnly, Category = "DBNO|Audio", meta = (ClampMin = "0.0"))
	float ReviveAudioOuterRadius = 2500.f;

	// ── SFX | Player Action Sounds ───────────────────────────────────────────
	// Sonidos del propio personaje (pasos, salto, knockdown, kill, pickup,
	// throw, consume). Se reproducen at-location en todas las máquinas vía
	// MulticastPlaySfx → cada cliente los oye en 3D según la atenuación que
	// traiga su SoundCue. Asignar en BP_TortugaCharacter Class Defaults.

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player")
	TObjectPtr<USoundBase> FootstepSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player", meta = (ClampMin = "0.05"))
	float FootstepWalkInterval = 0.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player", meta = (ClampMin = "0.05"))
	float FootstepSprintInterval = 0.28f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player", meta = (ClampMin = "0.0"))
	float FootstepMinGroundSpeed = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player")
	TObjectPtr<USoundBase> JumpSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player")
	TObjectPtr<USoundBase> KnockdownSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player")
	TObjectPtr<USoundBase> KillSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player")
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player")
	TObjectPtr<USoundBase> ThrowSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SFX|Player")
	TObjectPtr<USoundBase> ConsumeSound;

	/**
	 * Servidor: sacudida corta de cámara y vibración del mando (Strength 0..1) solo en la máquina del jugador que recibe
	 * el golpe, con sus ajustes (TNHitFeedback). Uno por fotograma: el primer aviso manda.
	 */
	void NotifyHitFeedback(float Strength);

	/**
	 * Cliente dueño: aplica la sacudida y la vibración de un golpe que ha decidido el servidor. Fiable: uno por golpe, y en
	 * una prueba con un cliente el no fiable se perdió en el primer derribo.
	 */
	UFUNCTION(Client, Reliable)
	void ClientPlayHitFeedback(float Strength);

private:
	/** Fotograma del último aviso de golpe (NotifyHitFeedback), para no repetirlo dentro del mismo. */
	uint64 LastHitFeedbackFrame = 0;

public:

	/**
	 * Multicast: spawnea Sound at-location en todas las máquinas. Llamar SOLO
	 * desde el servidor. Usado por TN_InventoryComponent (pickup/consume) y
	 * por el propio Character (jump/knockdown/kill/throw).
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySfx(USoundBase* Sound);

	// ── La cabeza que sigue a la cámara (#623, UTN_TurtleAnimInstance) ────────

	/**
	 * Hacia dónde mira el dueño respecto del cuerpo (grados; guiñada positiva a su derecha, cabeceo positivo arriba): el
	 * giro del mando en el dueño y en el servidor (el de un cliente llega con su movimiento); en los demás, la guiñada
	 * replicada y el cabeceo del motor (RemoteViewPitch16). Vale en todas las máquinas.
	 */
	void GetViewRelativeToBody(float& OutYaw, float& OutPitch) const;

	/** El probador (ATN_ChangingBooth) la tiene dentro: la cabeza mira al frente (su vista no es la de su cámara). Local en cada máquina. */
	void SetHeadLookSuppressed(bool bSuppressed) { bHeadLookSuppressed = bSuppressed; }
	bool IsHeadLookSuppressed() const { return bHeadLookSuppressed; }

private:
	bool IsValidWheelEmoteId(int32 EmoteID) const;
	/** Un emote que el servidor acepta: uno de la rueda o uno de los ocultos (#839). */
	bool IsPlayableEmoteId(int32 EmoteID) const;
	float GetWheelEmoteCooldown(int32 EmoteID) const;

	/** Veces que se ha escrito el código secreto en esta máquina: los dos emotes ocultos salen por turnos (#839). */
	int32 HiddenEmoteTurn = 0;

	/** Periodo del timer de TickReviveChannel; debe coincidir con el incremento de ReviveChannelElapsed. */
	static constexpr float ReviveChannelTickInterval = 0.1f;

	/** Spawn at-location del SFX en este actor (proxy local de MulticastPlaySfx). */
	void PlaySfxAtSelf(USoundBase* Sound) const;

	/** Tiempo restante hasta el siguiente paso (cosmetic, local-only, no replicado). */
	float FootstepCooldown = 0.f;
};
