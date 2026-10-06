#pragma once

#include "CoreMinimal.h"

/** Teclado en pantalla de Steam (ISteamUtils). */
enum class ETNSteamKeyboard : uint8
{
	/** ShowFloatingGamepadTextInput: flotante sobre el juego (Steam Deck); las letras llegan como teclas normales. */
	Floating,
	/** ShowGamepadTextInput: el de pantalla completa de Big Picture; el texto llega al cerrarlo. */
	Overlay,
};

/** Lo que pide quien abre el teclado de Steam. */
struct FTNSteamKeyboardRequest
{
	/** Título del teclado de pantalla completa. */
	FText Description;
	/** Texto que ya hay escrito (se puede seguir desde ahí). */
	FString ExistingText;
	/** Caracteres como mucho. */
	int32 MaxChars = 0;
	/** Dónde está el campo en la ventana (px): el flotante se coloca para no taparlo. */
	FIntRect FieldRect;
};

/**
 * Steam para jugar solo con mando (#347, #354): si Steam está en marcha, si el juego corre en una Steam Deck, de qué tipo es
 * el mando (Steam Input con la emulación de mando que usa el juego) y el teclado en pantalla para escribir. Sin Steam (con
 * -NoSteam, en el editor o en Meta Quest), todo responde «no» y quien lo llama sigue como sin Steam.
 */
namespace TNSteamGamepadInput
{
	/** true si el subsistema en línea en uso es el de Steam (con su API ya iniciada). */
	TORTUNABO_API bool IsSteamActive();

	/** true si el juego corre en una Steam Deck. */
	TORTUNABO_API bool IsOnSteamDeck();

	/** ESteamInputType del mando del jugador (como entero); 0 sin Steam o si Steam no lo sabe. */
	TORTUNABO_API int32 GetPadInputType();

	/**
	 * ESteamInputType (como enteros, sin repetir) de los mandos que Steam Input traduce ahora para el juego; vacío sin Steam.
	 * Con esto el lector de mandos DirectInput (#743) se calla los que ya llegan como mando de Xbox de Steam.
	 */
	TORTUNABO_API TArray<int32> GetConnectedPadInputTypes();

	/**
	 * Qué teclados de Steam probar y en qué orden (regla pura, con prueba). Nada sin Steam o si se usa el teclado; en la
	 * Steam Deck, primero el flotante (deja ver el campo); en el PC, primero el de Big Picture, que es el que existe ahí.
	 */
	TORTUNABO_API TArray<ETNSteamKeyboard> KeyboardOrder(bool bSteamActive, bool bOnDeck, bool bUsingGamepad);

	/**
	 * Abre el teclado de Steam. Devuelve el teclado que se ha abierto o nada si no se ha podido (sin Steam, fuera de Big
	 * Picture...). Con el de pantalla completa, OnText llega en el hilo del juego al cerrarlo (con bSubmitted = false si se
	 * canceló); con el flotante, las letras llegan como teclas y OnText no se llama.
	 */
	TORTUNABO_API TOptional<ETNSteamKeyboard> OpenKeyboard(const FTNSteamKeyboardRequest& Request, bool bUsingGamepad,
		TFunction<void(bool /*bSubmitted*/, const FString& /*Text*/)> OnText);

	/** Suelta lo que haya pendiente del teclado (al cerrar el juego o la pantalla que lo pidió). */
	TORTUNABO_API void CancelPendingKeyboard();
}
