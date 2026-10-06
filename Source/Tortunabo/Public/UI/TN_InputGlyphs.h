#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/**
 * Último aparato con el que ha jugado el jugador: decide si los avisos enseñan la tecla, el botón del mando o, con gafas, el
 * botón de los mandos Touch (#644).
 */
enum class ETNInputDevice : uint8
{
	KeyboardMouse,
	Gamepad,
	/** Modo VR (con gafas o simulado): los avisos nombran los botones de los mandos Meta Quest Touch. */
	VR,
};

/** Familia del mando, para dibujar sus botones como los lleva impresos. */
enum class ETNPadFamily : uint8
{
	/** Xbox y cualquier mando que no se reconozca (lo que llega por XInput y los mandos genéricos de DirectInput). */
	Xbox,
	/** DualShock y DualSense: cruz, círculo, cuadrado y triángulo; L1, R1, L2, R2. */
	PlayStation,
	/** Steam Deck: A, B, X e Y como el de Xbox, pero L1, R1, L2 y R2. */
	SteamDeck,
	/** Switch Pro y Joy-Con: B abajo, A a la derecha, Y a la izquierda y X arriba; L, R, ZL, ZR; + y −. */
	Switch,
};

/** Forma del dibujo de un botón (UTN_ButtonGlyphWidget). */
enum class ETNGlyphShape : uint8
{
	/** Sin dibujo: no es un botón del mando. */
	None,
	/** Botón redondo de la cara (A, B, X, Y o los símbolos de PlayStation). */
	Face,
	/** Botón superior (LB, RB, L1, R1). */
	Shoulder,
	/** Gatillo (LT, RT, L2, R2). */
	Trigger,
	/** Stick (moverse o la cámara) o su clic. */
	Stick,
	/** Una dirección de la cruceta. */
	DPad,
	/** Botón del menú (Menú, Options, ☰). */
	Menu,
	/** Botón de vista (Vista, Create, Share). */
	View,
};

/** Símbolo dentro de un botón de la cara de PlayStation (las letras de Xbox van como texto). */
enum class ETNGlyphSymbol : uint8
{
	None,
	Cross,
	Circle,
	Square,
	Triangle,
};

/** Cómo se dibuja un botón del mando. */
struct FTNGlyphSpec
{
	ETNGlyphShape Shape = ETNGlyphShape::None;
	ETNGlyphSymbol Symbol = ETNGlyphSymbol::None;
	/** Texto dentro del botón («A», «LB», «R2», «L»...): nombres de marca, iguales en todos los idiomas. */
	FText Label;
	/** Color de la letra o del símbolo (los botones de la cara llevan el suyo). */
	FLinearColor Accent = FLinearColor::White;
	/** Cruceta: dirección que se resalta (0 arriba, 1 derecha, 2 abajo, 3 izquierda). Stick: 0 izquierdo, 1 derecho. */
	int32 Index = 0;
	/** Stick: true si es el clic (L3, R3) y no el eje. */
	bool bClick = false;
};

/**
 * Reglas de los avisos de botones (#347): qué aparato cuenta como el último usado, qué tecla de una acción se enseña con
 * cada aparato y cómo se dibuja cada botón según la familia del mando. Funciones puras: las prueban los tests de
 * Automation (Tortunabo.UI.InputGlyphs) sin mundo ni mando conectado.
 */
namespace TNInputGlyphs
{
	/** Por debajo de esto, un stick o un gatillo no cambia de aparato (deriva del stick o un roce). */
	inline constexpr float AnalogSwitchThreshold = 0.35f;
	/** Movimiento del ratón (px) que cuenta como usar el ratón: menos es un temblor de la mesa. */
	inline constexpr float MouseSwitchThreshold = 3.f;

	/** El aparato de una tecla (los botones y ejes del mando son del mando; el resto, teclado y ratón). */
	TORTUNABO_API ETNInputDevice DeviceOfKey(const FKey& Key);

	/** true si este evento analógico cuenta como usar el mando (ejes del mando por encima del umbral). */
	TORTUNABO_API bool IsAnalogDeviceSwitch(const FKey& Key, float Value);

	/** true si este movimiento del ratón cuenta como usar el ratón. */
	TORTUNABO_API bool IsMouseDeviceSwitch(const FVector2D& CursorDelta);

	/** El gatillo como botón (IMC_Player lo lleva como eje): así el dibujo y el nombre son los del botón. */
	TORTUNABO_API FKey AsButton(const FKey& Key);

	/**
	 * La tecla que se enseña de una acción con ese aparato: la primera válida de ese aparato (ya reasignada en Ajustes, que
	 * es lo que devuelve el sistema de entrada) y, si la acción no tiene ninguna, la primera del otro aparato. Los gatillos
	 * salen como botón. Sin ninguna tecla válida, devuelve una vacía. Los botones de los Touch (FTNVRKeys) solo cuentan para
	 * el aparato VR: con teclado o mando son «del otro aparato».
	 */
	TORTUNABO_API FKey PickKey(const TArray<FKey>& Keys, ETNInputDevice Device);

	/** Familia del mando según el tipo de Steam Input (ESteamInputType, como entero: 0 si Steam no lo sabe). */
	TORTUNABO_API ETNPadFamily FamilyFromSteamInputType(int32 SteamInputType);

	/**
	 * Familia según el nombre del aparato que da el motor (DualSense, DualShock, PS4, PS5... SwitchPro, Nintendo, Joy-Con):
	 * Xbox si no lo reconoce.
	 */
	TORTUNABO_API ETNPadFamily FamilyFromHardwareName(const FString& HardwareName);

	/** Cómo se dibuja Key con esa familia de mando (Shape = None si no es un botón del mando). */
	TORTUNABO_API FTNGlyphSpec GlyphFor(const FKey& Key, ETNPadFamily Family);
}
