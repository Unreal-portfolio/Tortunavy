#pragma once

#include "CoreMinimal.h"

/**
 * @brief Lector de los mandos que no son de Xbox (#743): DualShock 4, DualSense, Switch Pro y mandos HID genéricos, por
 * DirectInput, en el editor y en el juego. Los de Xbox los sigue leyendo XInput (el lector se los salta) y los que Steam Input
 * ya traduce a mando de Xbox, también. Traduce cada mando a las teclas Gamepad_* de serie con su perfil de
 * UTN_GamepadSettings (Config/DefaultInput.ini), así valen los IMC_* sin tocarlos. Docs/Mandos.md.
 *
 * Comandos: TN.Input.Pads (mandos conocidos y ajustes), TN.Input.Pads.Rescan, TN.Input.PadDebug 1 (botones y ejes en crudo)
 * y TN.Input.DirectInput 0|1|2 (apagado, automático, siempre).
 */
namespace TNGamepadDevice
{
	/** Da de alta el lector como dispositivo de entrada del motor (IInputDeviceModule). Lo llama el módulo del juego al arrancar. */
	TORTUNABO_API void Register();

	/** Lo da de baja (al cerrar el módulo). */
	TORTUNABO_API void Unregister();

	/** true si el lector está dado de alta en el motor. */
	TORTUNABO_API bool IsRegistered();

	/** true si el motor ya ha creado el lector (en el primer fotograma con Slate). */
	TORTUNABO_API bool IsCreated();

	/** Los ajustes y los mandos conocidos, línea a línea (lo que escribe TN.Input.Pads). */
	TORTUNABO_API TArray<FString> Describe();
}
