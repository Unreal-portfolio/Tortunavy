#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class UUserWidget;
class SWidget;
class UGameViewportClient;
class UTexture;
class USceneComponent;

/**
 * Modo de realidad virtual de Tortunavy (Docs/Modo_VR.md). Lo decide UTN_VRSubsystem cada fotograma con el ajuste «Modo
 * VR» del menú, la variable de consola TN.VR y la línea de comandos (-vr, -vrsim, -novr).
 */
enum class ETNVRMode : uint8
{
	/** Pantalla de siempre: tercera persona, HUD y menús en la pantalla. */
	Off,
	/** Con gafas (OpenXR: Meta Quest por Link o en la propia Quest): primera persona, cabeza y mandos con seguimiento. */
	Headset,
	/**
	 * Sin gafas, en la ventana: la misma primera persona, las mismas aletas y la misma interfaz en el mundo, con el ratón
	 * como cabeza y como puntero. Sirve para probar todo el modo VR en el PC.
	 */
	Simulated
};

/** Botones de los mandos Meta Quest Touch con OpenXR (nombres de FKey del motor; se crean por nombre). */
struct TORTUNABO_API FTNVRKeys
{
	static const FKey LeftStickX;
	static const FKey LeftStickY;
	static const FKey RightStickX;
	static const FKey RightStickY;
	static const FKey LeftStickUp;
	static const FKey LeftStickDown;
	static const FKey LeftStickLeft;
	static const FKey LeftStickRight;
	static const FKey RightStickUp;
	static const FKey RightStickDown;
	static const FKey RightStickLeft;
	static const FKey RightStickRight;
	static const FKey LeftStickClick;
	static const FKey RightStickClick;
	static const FKey LeftTrigger;
	static const FKey RightTrigger;
	static const FKey LeftTriggerAxis;
	static const FKey RightTriggerAxis;
	static const FKey LeftGrip;
	static const FKey RightGrip;
	static const FKey LeftGripAxis;
	static const FKey RightGripAxis;
	static const FKey A;
	static const FKey B;
	static const FKey X;
	static const FKey Y;
	static const FKey Menu;

	/** Botón de un mando VR (cualquiera de los de arriba). */
	static bool IsVRKey(const FKey& Key);
};

namespace TNVR
{
	/** Modo actual (lo pone UTN_VRSubsystem; Off en servidores dedicados y hasta que arranca). */
	TORTUNABO_API ETNVRMode GetMode();
	TORTUNABO_API bool IsEnabled();
	TORTUNABO_API bool IsHeadset();
	TORTUNABO_API bool IsSimulated();

	/** Solo UTN_VRSubsystem. */
	TORTUNABO_API void SetMode(ETNVRMode NewMode);

	// ── Interfaz ─────────────────────────────────────────────────────────────

	/**
	 * Pone un widget de pantalla completa en la pantalla: sin VR, AddToViewport(ZOrder) de siempre; con VR, en el panel
	 * del mundo (ATN_VRRig: el HUD flota delante y los menús se quedan quietos delante y se apuntan con la aleta). Se quita
	 * con RemoveFromParent como siempre. Usarlo en lugar de AddToViewport en todo el juego.
	 * En la partida local (#311), el widget de un jugador va a su trozo de la pantalla partida (AddToPlayerScreen); lo que es
	 * de todos (menú de pausa, carga, contador de FPS) va con AddToFullScreen.
	 */
	TORTUNABO_API void AddToScreen(UUserWidget* Widget, int32 ZOrder = 0);

	/** Como AddToScreen, pero siempre a toda la pantalla, por encima de las vistas de la pantalla partida. */
	TORTUNABO_API void AddToFullScreen(UUserWidget* Widget, int32 ZOrder = 0);

	/** ¿Está en la pantalla (el viewport o el panel VR)? Usarlo en lugar de IsInViewport. */
	TORTUNABO_API bool IsOnScreen(const UUserWidget* Widget);

	/**
	 * Lo mismo para un widget Slate suelto (AddViewportWidgetContent / RemoveViewportWidgetContent).
	 * @return true si ha ido al panel VR (false: al viewport).
	 */
	TORTUNABO_API bool AddSlateToScreen(UGameViewportClient* Viewport, const TSharedRef<SWidget>& Widget, int32 ZOrder);
	TORTUNABO_API void RemoveSlateFromScreen(UGameViewportClient* Viewport, const TSharedRef<SWidget>& Widget);

	/** ¿Hay ya panel VR en el mundo de WorldContext? (Sin él, lo que se pone en pantalla va al viewport.) */
	TORTUNABO_API bool HasPanel(const UObject* WorldContext);

	// ── Cámara ───────────────────────────────────────────────────────────────

	/**
	 * Con VR la vista no se va a las cámaras de escena (la almeja y el gusano de la playa): se sigue en primera persona. En
	 * las gafas, una cámara que se mueve sola marea. (El probador sí cambia de vista, sin fundido: allí uno se mira.)
	 */
	TORTUNABO_API bool KeepFirstPersonView();

	/** Tiempo de fundido entre vistas: el de siempre sin VR; 0 con VR (sin deslizar la cámara). */
	TORTUNABO_API float ViewBlendTime(float FlatSeconds);

	// ── Manos ────────────────────────────────────────────────────────────────

	/** La aleta derecha o izquierda del jugador local en VR (para enganchar lo que lleva en la mano); o nullptr. */
	TORTUNABO_API USceneComponent* GetHand(const UObject* WorldContext, bool bRight);
}
