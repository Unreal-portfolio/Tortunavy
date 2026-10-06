#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VR/TN_VRMode.h"
#include "VR/TN_VRHandMath.h"
#include "TN_VRRig.generated.h"

class APlayerController;
class ATortugaCharacter;
class UInputAction;
class UInputTrigger;
class SWidget;
class UCameraComponent;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UMotionControllerComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTN_VRScreenWidget;
class UTN_VRSeatComponent;
class UUserWidget;
class UWidgetComponent;
class UWidgetInteractionComponent;
struct FCollisionQueryParams;

/**
 * El jugador local en VR (Docs/Modo_VR.md): lo crea UTN_VRSubsystem en cada mundo de juego mientras hay VR. Solo existe en
 * la máquina del jugador (no se replica).
 *
 * - Sigue a la vista: con gafas, su raíz es el origen del seguimiento de la cámara que se esté viendo (el de la tortuga,
 *   ATortugaCharacter::GetVROrigin); simulado, la propia cámara. Sin peón (menú principal), la vista es su cámara.
 * - Aletas: dos mandos con seguimiento (MotionSource LeftGrip/RightGrip) con una aleta de tortuga en cada uno; simulado,
 *   quietas delante de la cámara.
 * - Pantalla: UTN_VRScreenWidget en un panel curvo que rodea los ojos (un trozo de cilindro con el eje en la cabeza, pintado
 *   con el mismo material del UWidgetComponent, que queda plano e invisible para el puntero). Jugando, el HUD flota
 *   delante y sigue a la cabeza con retraso; con un menú (el juego enseña el cursor), el panel se queda quieto delante,
 *   más ancho, y la aleta derecha apunta con un láser (gatillo = clic). Simulado, apunta el ratón. El panel se acerca si
 *   hay una pared en medio.
 * - Carga: mientras sale el huevo, una playa en 360 (cielo, horizonte, mar y arena) rodea la cabeza.
 * - Mandos jugando: los añade como mapeo propio sobre las acciones de siempre (IA_Move, IA_Jump...), gira por pasos con el
 *   stick derecho y recentra con su clic. Los gatillos van también por su eje (OpenXR no da el «clic» de los Touch).
 * - Agarres: cogen el objeto con física más cercano a la aleta (UTN_VRGrabComponent) y lo sueltan con la velocidad de la
 *   mano; sin nada que coger, el derecho suelta el objeto de la mano y el izquierdo corre, como antes. Con un menú, una
 *   rueda o la tortuga sin poder usar las manos (derribada, en el caparazón, llevada), los agarres se anulan sin lanzar ni
 *   soltar lo que lleva en la aleta, y no vuelven a contar hasta abrir la mano.
 * - Manos y escenario (TN_VRRigHands.cpp): las manos no atraviesan paredes ni el suelo (se quedan en su superficie, y con
 *   ellas lo que se coge y los brazos del cuerpo); vibración de los mandos al coger, soltar, lanzar, tocar la pared, perder
 *   lo que se lleva, ser derribada y en los menús; viñeta de confort al moverse (TN.VR.ComfortVignette).
 * - Vehículos (un peón con UTN_VRSeatComponent: la conductora o la artillera del buggy): el rig va en el asiento (con gafas,
 *   su origen del seguimiento), le da las manos cada fotograma y no pone los mandos de la tortuga (el vehículo tiene los
 *   suyos); los agarres y el giro los decide el vehículo.
 */
UCLASS(NotBlueprintable, Transient)
class TORTUNABO_API ATN_VRRig : public AActor
{
	GENERATED_BODY()

public:
	ATN_VRRig();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ── Pantalla ─────────────────────────────────────────────────────────────

	bool HostWidget(UUserWidget* Widget, int32 ZOrder, bool bPlayerScreen = false);
	bool IsHosting(const UUserWidget* Widget) const;
	bool HostSlate(const TSharedRef<SWidget>& Widget, int32 ZOrder);
	bool UnhostSlate(const TSharedRef<SWidget>& Widget);

	/** Al apagar el modo VR: lo que había en el panel vuelve al viewport. */
	void ReleaseScreenToViewport();

	/** ¿Un menú delante (panel quieto y puntero)? */
	bool IsMenuMode() const { return bMenuMode; }

	/** Vuelve a poner el panel delante de la cabeza. */
	void RecenterPanel();

	// ── Puntero (lo llama el procesador de entrada VR) ───────────────────────

	void PointerPress();
	void PointerRelease();
	void PointerScroll(float Delta);

	/** Aleta derecha o izquierda (para enganchar el objeto que se lleva en la mano). */
	USceneComponent* GetHand(bool bRight) const;

	/** Para TN.VR.Status: qué hace cada mano (libre o parada por el escenario, qué agarra), la viñeta y la vibración. */
	FString DescribeHands() const;


	/**
	 * Dónde se queda una mano que va de From (los ojos) a To si el escenario está en medio: en su superficie, con Radius de
	 * holgura. Solo el escenario (el canal de la cámara: paredes, suelo, rocas), no lo que se coge ni las tortugas. true si
	 * algo la para (OutLocation, ese punto); false si llega (OutLocation = To).
	 */
	static bool BlockHandLocation(const UWorld* World, const FVector& From, const FVector& To, float Radius,
		const FCollisionQueryParams& Params, FVector& OutLocation);

	/** El modo cambió entre gafas y simulado. */
	void OnModeChanged(ETNVRMode NewMode);

	/**
	 * Disparador de las asignaciones de los gatillos por su eje: la acción solo se activa con el gatillo a partir del 55 %
	 * (TNVRMath::AnalogPressThreshold). Lo prueba Tortunabo.VR.TriggerThreshold.
	 */
	static UInputTrigger* MakeAnalogPressTrigger(UObject* Outer);

protected:
	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<USceneComponent> RigRoot;

	/** Vista propia cuando no hay peón (menú principal). */
	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UCameraComponent> RigCamera;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UMotionControllerComponent> LeftGrip;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UMotionControllerComponent> RightGrip;

	/** Pose de apuntar del mando derecho (el láser). */
	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UMotionControllerComponent> RightAim;

	/** Pose de apuntar del mando izquierdo (la torreta de los vehículos con la mano izquierda). */
	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UMotionControllerComponent> LeftAim;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<USceneComponent> LeftHand;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<USceneComponent> RightHand;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UStaticMeshComponent> LeftFlipper;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UStaticMeshComponent> RightFlipper;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UStaticMeshComponent> LaserBeam;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UStaticMeshComponent> LaserDot;

	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UWidgetInteractionComponent> Pointer;

	/** Panel de la interfaz: plano, no se pinta (solo lo usa el puntero y dibuja la textura); se ve CurvedPanel. */
	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UWidgetComponent> ScreenPanel;

	/** La interfaz curvada alrededor de los ojos, con el material del panel. */
	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UProceduralMeshComponent> CurvedPanel;

	/** Playa en 360 alrededor de la cabeza mientras sale la pantalla de carga. */
	UPROPERTY(VisibleAnywhere, Category = "VR")
	TObjectPtr<UProceduralMeshComponent> LoadingDome;

	UPROPERTY(Transient)
	TObjectPtr<UTN_VRScreenWidget> Screen;

	/** Mandos VR sobre las acciones del juego (solo con gafas). */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> VRMapping;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LaserMaterial;

	/** Correr con el agarre izquierdo cuando no hay nada que coger (se inyecta mientras se mantiene). */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SprintAction;

private:
	APlayerController* GetLocalPC() const;
	void EnsureScreen();
	void BuildHands();
	void BuildLaser();

	/** Raíz del rig en el origen de la vista que se esté viendo (ver arriba): la tortuga, el asiento del vehículo u otra cámara. */
	void UpdateViewAttachment(APlayerController* PC, ATortugaCharacter* Turtle, UTN_VRSeatComponent* Seat);
	/** Aletas de los mandos; con la tortuga vista desde sus ojos (bTurtleView), sus brazos van a ellas (IK) y las aletas
	 *  sueltas se ocultan. */
	void UpdateHands(ATortugaCharacter* Turtle, bool bTurtleView, float DeltaSeconds);
	/** En un vehículo: las manos al asiento (dónde están, hacia dónde apuntan y el agarre); los brazos de la tortuga sentada las siguen. */
	void UpdateSeatHands(APlayerController* PC, UTN_VRSeatComponent* Seat);
	void UpdatePanel(APlayerController* PC, float DeltaSeconds);
	void UpdatePointer(APlayerController* PC);
	void UpdateInput(APlayerController* PC, ATortugaCharacter* Turtle, UTN_VRSeatComponent* Seat, float DeltaSeconds);
	void EnsureVRMapping(APlayerController* PC);
	void RemoveVRMapping();

	/** Cámara (de este fotograma o del anterior) desde la que se ve. */
	bool GetViewPoint(APlayerController* PC, FVector& OutLocation, FRotator& OutRotation) const;
	/** Distancia a la que cabe el panel delante de la vista sin meterse en una pared. */
	float FitDistance(const FVector& From, const FVector& Dir, float Desired) const;
	/** Igual para el HUD anclado: con el centro, los lados y el borde de abajo (que no lo tape el suelo al mirar abajo). */
	float FitHudDistance(const FVector& From, const FRotator& ViewRotation, float Desired, float ArcDeg) const;
	/** Lo que no aparta el panel: la tortuga, la vista y lo que se lleva en las manos. */
	void AddViewIgnores(FCollisionQueryParams& Params) const;
	/** Panel curvo a Distance en la dirección Direction, DropFraction de la distancia por debajo de los ojos, con el eje del
	 *  cilindro en los ojos y HorizontalFov grados de arco. */
	void PlacePanel(const FVector& ViewLocation, const FRotator& Direction, float Distance, float HorizontalFov, float DropFraction = 0.1f);

	/** Rayo del puntero: el mando derecho (gafas) o el ratón (simulado). */
	bool GetPointerRay(APlayerController* PC, FVector& OutOrigin, FVector& OutDir) const;

	/** Malla del panel curvo para ese arco (en el espacio del panel) y su material, el del UWidgetComponent. */
	void UpdateCurvedPanel(float ArcDeg);
	void BuildLoadingDome();
	void UpdateLoadingDome(APlayerController* PC);

	/** Agarres: coger objetos con física o, sin nada cerca, soltar el objeto (derecho) y correr (izquierdo). */
	void UpdateGrips(APlayerController* PC, ATortugaCharacter* Turtle, float DeltaSeconds);
	void ReleaseGrips(ATortugaCharacter* Turtle);
	/** Agarre recién apretado (Point: punto de agarre de esa mano). */
	void PressGrip(int32 Hand, ATortugaCharacter* Turtle, const FTransform& Point);
	/** Agarre recién soltado: lanza o deja lo que lleva esa mano. */
	void ReleaseGrip(int32 Hand, ATortugaCharacter* Turtle);
	/** Agarre mantenido: mueve lo cogido o corre. */
	void HoldGrip(int32 Hand, APlayerController* PC, ATortugaCharacter* Turtle, const FTransform& Point);
	/** Sin poder usar las manos (menú, rueda, derribo, caparazón): suelta lo cogido con física y acaba la interacción de
	 *  mantener, sin lanzar ni dejar caer lo que lleva en la aleta ni al compañero. */
	void CancelGrip(int32 Hand, ATortugaCharacter* Turtle);
	/** Botones e interruptores pulsados con la punta de la aleta (sin apretar el agarre). */
	void UpdatePoke(int32 Hand, ATortugaCharacter* Turtle, const FVector& Tip, float DeltaSeconds);
	/** Velocidad de la mano respecto del origen de la vista (ventana de TNVRHands::FHandVelocityWindow). */
	void UpdateHandVelocity(int32 Hand, const FTransform& Origin, const FVector& Point, float DeltaSeconds);
	/** Manos fuera del escenario (BlockHandLocation desde los ojos de la tortuga); vibran al empezar a tocarlo. */
	void BlockHandsByWorld(const ATortugaCharacter* Turtle, bool bTurtleView);
	/** Un toque de vibración en esa mano (0 izquierda, 1 derecha), solo con gafas. */
	void PulseHaptic(int32 Hand, const TNVRHands::FHapticPulse& Pulse);
	/** Vibración de los mandos cada fotograma (OpenXR la mantiene un fotograma): toques, tirón de lo cogido y derribo. */
	void UpdateHaptics(APlayerController* PC, const ATortugaCharacter* Turtle);
	void StopHaptics(APlayerController* PC);
	/** Viñeta de confort sobre la cámara de la tortuga al moverse o girar suave (con gafas). */
	void UpdateComfortVignette(APlayerController* PC, ATortugaCharacter* Turtle, float DeltaSeconds);
	/** Pone la viñeta de confort Intensity en Camera encima de la que ya tenga (0: la quita y la deja como estaba). */
	void ApplyComfortVignette(UCameraComponent* Camera, float Intensity);
	/** Punto de agarre de la aleta (cerca de la punta) en el mundo. */
	FTransform GetGrabPoint(bool bRight) const;
	/** La velocidad de las manos vuelve a medirse desde cero (tras un giro de golpe, al recentrar o al cambiar de tortuga). */
	void ResetHandVelocity();

	/** Cámara desde la que se ve ahora (la de las gafas de la tortuga, la del rig o la del que se sigue). */
	UCameraComponent* GetViewCamera(APlayerController* PC) const;
	/** HUD anclado a la cámara (siempre fijo en la vista) o suelto en el mundo (menús). */
	void AttachPanelToCamera(UCameraComponent* Camera);
	void DetachPanelFromCamera();

	/** Qué hace cada agarre mientras se mantiene: nada, algo de la tortuga (objeto, compañero), un objeto con física o
	 *  correr (el izquierdo sin nada que coger). */
	enum class EGripUse : uint8 { None, Turtle, Grab, Sprint };
	EGripUse GripUse[2] = { EGripUse::None, EGripUse::None };
	/** Lo que la tortuga tiene cogido con cada agarre (ATortugaCharacter::EVRGrip). */
	uint8 GripTurtle[2] = { 0, 0 };
	bool bGripHeld[2] = { false, false };
	/** Punto de agarre y origen de la vista del fotograma anterior, y la velocidad de cada mano respecto de ese origen (en
	 *  los ejes del mundo): andar, saltar o girar con el stick no la cambian (TNVRMath::RelativeHandVelocity). */
	FVector PrevGrabPoint[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FTransform PrevGrabOrigin[2];
	FVector HandVelocity[2] = { FVector::ZeroVector, FVector::ZeroVector };
	TNVRHands::FHandVelocityWindow HandVelocityWindow[2];
	TNVRHands::FPokeState PokeState[2];

	bool bPrevGrabPointValid[2] = { false, false };
	/** Mano parada por el escenario (para vibrar al empezar a tocarlo). */
	bool bHandBlocked[2] = { false, false };
	/** Vibración: hasta cuándo (tiempo real) y con qué fuerza va el toque de cada mano, y si el mando está vibrando. */
	double HapticUntil[2] = { 0.0, 0.0 };
	float HapticAmplitude[2] = { 0.f, 0.f };
	bool bHapticOn[2] = { false, false };
	bool bWasKnockedDown = false;
	bool bPointerOverButton = false;
	/** Giro suave de este fotograma (grados/s) y viñeta de confort que se está aplicando. */
	float SmoothTurnRate = 0.f;
	float ComfortVignetteNow = 0.f;
	/** La viñeta de confort sobre la del caparazón, y la cámara en la que está puesta. */
	TNVRHands::FVignetteLayer ComfortVignetteLayer;
	TWeakObjectPtr<UCameraComponent> ComfortVignetteCamera;

	/** Giros de golpe de la tortuga ya vistos (ATortugaCharacter::GetVRTurnSerial). */
	uint32 LastTurnSerial = 0;

	/** Arco (grados) con el que está hecha la malla del panel curvo y el que tiene ahora. */
	float CurvedArcBuilt = -1.f;
	float PanelArc = 60.f;

	ETNVRMode Mode = ETNVRMode::Off;
	bool bMenuMode = false;
	bool bPanelPlaced = false;
	bool bHudFollowing = false;
	bool bSnapLatched = false;
	bool bRecenterHeld = false;
	bool bPointerDown = false;
	bool bOwnsViewTarget = false;
	float HudYaw = 0.f;
	float PanelDistanceSmoothed = 0.f;
	FVector MenuPlacedFrom = FVector::ZeroVector;

	TWeakObjectPtr<USceneComponent> AttachedBase;
	FName AttachedSocket = NAME_None;
	TWeakObjectPtr<ATortugaCharacter> ViewTurtle;
	/** Asiento del vehículo propio con la vista sentada encendida. */
	TWeakObjectPtr<UTN_VRSeatComponent> ViewSeat;
	TWeakObjectPtr<APlayerController> MappedPC;
};
