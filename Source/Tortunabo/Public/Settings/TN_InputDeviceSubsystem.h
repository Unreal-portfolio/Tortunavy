#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/TN_InputGlyphs.h"
#include "TN_InputDeviceSubsystem.generated.h"

class APlayerController;
class IInputProcessor;
class UInputAction;

/**
 * @brief El último aparato con el que se ha jugado (teclado y ratón o mando) y la familia del mando (Xbox, PlayStation,
 * Steam Deck o Switch), para que los avisos de botones del HUD, del tutorial y de las ruedas enseñen lo que el jugador tiene en las
 * manos (#347) y cambien al momento al pasar de uno a otro. Con el modo VR (TNVR::IsEnabled) el aparato es siempre el
 * tercero, «VR»: los avisos nombran los botones de los mandos Touch (TNVRControls, #644).
 *
 * Lo decide un preprocesador de entrada de Slate: una tecla o un botón cambian de aparato; los sticks y los gatillos, solo
 * pasado un umbral (la deriva no cuenta); el ratón, solo si se mueve de verdad (TNInputGlyphs). No se usa
 * UInputDeviceSubsystem del motor: el teclado y el primer mando comparten el aparato 0 y, una vez tocado el mando, el teclado
 * no lo devolvía a «teclado». La familia del mando sale de Steam Input con Steam en marcha y, sin él, del nombre del aparato (el de XInput o el
 * HardwareId del perfil del lector DirectInput, #743).
 *
 * Para probar sin mando: TN.Input.Device 0|1|2|3 (solo, teclado, mando o VR) y TN.Input.PadFamily 0|1|2|3|4 (solo, Xbox,
 * PlayStation, Steam Deck o Switch).
 */
UCLASS()
class TORTUNABO_API UTN_InputDeviceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UTN_InputDeviceSubsystem* Get(const UObject* WorldContext);

	/**
	 * El aparato de los avisos de un jugador (con TN.Input.Device, el forzado). Va por usuario de la plataforma: en una
	 * partida a pantalla partida cada uno tiene el suyo; sin PC, el del primer jugador.
	 */
	ETNInputDevice GetDevice(const APlayerController* PC = nullptr) const;

	bool IsUsingGamepad(const APlayerController* PC = nullptr) const { return GetDevice(PC) == ETNInputDevice::Gamepad; }

	/** ¿Se juega con los mandos Touch (modo VR)? Entonces no cuenta como mando: los avisos nombran los botones Touch. */
	bool IsUsingVR(const APlayerController* PC = nullptr) const { return GetDevice(PC) == ETNInputDevice::VR; }

	/** La familia del mando (con TN.Input.PadFamily, la forzada). Se vuelve a mirar cada pocos segundos. */
	ETNPadFamily GetPadFamily() const;

	/**
	 * La tecla o el botón que se enseña de una acción de Enhanced Input con el aparato de ahora: la del contexto de entrada
	 * del jugador (con lo reasignado en Ajustes) y, si no hay de ese aparato, la del otro. Vacía si la acción no tiene.
	 */
	FKey KeyForAction(const APlayerController* PC, const UInputAction* Action) const;

	/** Lo llama el preprocesador de entrada: el usuario UserIndex (de Slate) acaba de usar este aparato. */
	void NoteDevice(int32 UserIndex, ETNInputDevice Device, FName Cause = NAME_None);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	TSharedPtr<IInputProcessor> Tracker;
	/** Último aparato de cada usuario (los que no están, el de partida). */
	TMap<int32, ETNInputDevice> LastDevices;
	ETNInputDevice StartDevice = ETNInputDevice::KeyboardMouse;
	mutable ETNPadFamily CachedFamily = ETNPadFamily::Xbox;
	mutable double FamilyCheckedAt = -1.0;

	ETNPadFamily DetectPadFamily() const;
};
