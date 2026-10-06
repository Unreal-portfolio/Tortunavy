#pragma once

#include "CoreMinimal.h"

class UUserWidget;

/**
 * Dónde se pone un widget de pantalla completa. En la partida local (#311) el widget de un jugador va a su trozo de la
 * pantalla partida; lo que es de todos (menú de pausa, carga, contador de FPS) va a toda la pantalla.
 */
namespace TNScreen
{
	/**
	 * Pone un widget en la pantalla: en la partida local, si tiene jugador dueño, en su trozo de la pantalla partida
	 * (AddToPlayerScreen; con un solo jugador, la pantalla entera); si no, AddToViewport(ZOrder). Se quita con
	 * RemoveFromParent. Usarlo en lugar de AddToViewport en todo el juego.
	 */
	TORTUNABO_API void AddToScreen(UUserWidget* Widget, int32 ZOrder = 0);

	/** Como AddToScreen, pero siempre a toda la pantalla, por encima de las vistas de la pantalla partida. */
	TORTUNABO_API void AddToFullScreen(UUserWidget* Widget, int32 ZOrder = 0);

	/** ¿Está en la pantalla? Equivale a IsInViewport, que también cubre los widgets de la pantalla partida. */
	TORTUNABO_API bool IsOnScreen(const UUserWidget* Widget);
}
