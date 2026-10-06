#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/**
 * Los botones de los mandos Meta Quest Touch tal como los lee el jugador (Docs/Modo_VR.md, «Controles»): cómo se llama cada
 * uno en pantalla y qué botón lleva cada acción del juego. Con gafas (TNVR::IsEnabled) los avisos del HUD, el tutorial, el
 * fantasma, los karts y el Rally dicen estos nombres en lugar de la tecla o el botón del mando (#644), y Ajustes > Controles
 * los enseña en solo lectura (#647). Funciones sin mundo: las prueba Tortunabo.VR.Controls.
 *
 * La asignación de verdad está en ATN_VRRig::EnsureVRMapping (IMC_VR) y en UTN_BuggyInputSet / UTN_KartInputSet: esta tabla
 * es lo que se enseña, y Tortunabo.VR.Controls comprueba que cada botón de la tabla es uno de los de FTNVRKeys.
 */
namespace TNVRControls
{
	/** Nombre en pantalla de un botón Touch («Gatillo derecho», «A», «Stick izquierdo»...). Vacío si no es de los Touch. */
	TORTUNABO_API FText KeyName(const FKey& Key);

	/**
	 * El botón Touch que lleva una acción jugando a pie. ActionId es el de las filas de controles
	 * (UTN_GameSettingsSubsystem::GetKeyBindings: «IA_Interact», «IA_Jump»..., «Talk», «Pause») más «IA_Move» e «IA_Look».
	 * FKey() si esa acción no tiene botón en los mandos.
	 */
	TORTUNABO_API FKey KeyForAction(const FString& ActionId);

	/** Una línea de la guía de controles: qué se hace y con qué botones. */
	struct FGuideLine
	{
		FText Label;
		FText Buttons;
	};

	/** La guía de los mandos Touch para Ajustes > Controles (solo lectura), en el orden en que se enseña. */
	TORTUNABO_API TArray<FGuideLine> GetGuide();
}
