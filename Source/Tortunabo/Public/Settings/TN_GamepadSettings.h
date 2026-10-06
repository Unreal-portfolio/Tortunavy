#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UI/TN_InputGlyphs.h"
#include "TN_GamepadSettings.generated.h"

/** Eje de DirectInput (DIJOYSTATE2) del que sale un eje del mando. */
UENUM()
enum class ETNPadAxis : uint8
{
	None,
	X,
	Y,
	Z,
	RX,
	RY,
	RZ,
	Slider0,
	Slider1,
};

/** De qué eje de DirectInput se lee un stick o un gatillo, y si va al revés. */
USTRUCT()
struct FTNPadAxisBinding
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	ETNPadAxis Axis = ETNPadAxis::None;

	/** Al revés: en HID la Y de los sticks crece hacia abajo y Gamepad_LeftY/RightY crecen hacia arriba. */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	bool bInvert = false;
};

/**
 * Cómo se traduce un mando que no es de Xbox (lo lee DirectInput) a las teclas Gamepad_* de serie, para que valgan los IMC_*
 * sin tocarlos. Se elige por VendorID/ProductID (Docs/Mandos.md); el primero de la lista que encaja gana.
 */
USTRUCT()
struct FTNPadProfile
{
	GENERATED_BODY()

	/** Nombre para el registro y para TN.Input.Pads. */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FString Name;

	/**
	 * Identificador del aparato para el motor (FInputDeviceScope): DualShock4, DualSense, DualSenseEdge, SwitchPro,
	 * GenericGamepad... Va también en [InputPlatformSettings_Windows InputPlatformSettings] de DefaultInput.ini como mando, y
	 * de él sale la familia de los avisos de botones (TNInputGlyphs::FamilyFromHardwareName).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FString HardwareId;

	/** VendorID en hexadecimal («054C» o «0x054C»). Vacío: cualquier fabricante. */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FString VendorId;

	/** ProductID en hexadecimal. Vacío: cualquier producto del fabricante. */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	TArray<FString> ProductIds;

	/** Tecla de cada botón de DirectInput, por su número (empieza en 0): «Gamepad_FaceButton_Bottom»... None: no hace nada. */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	TArray<FName> Buttons;

	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FTNPadAxisBinding LeftX;

	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FTNPadAxisBinding LeftY;

	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FTNPadAxisBinding RightX;

	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FTNPadAxisBinding RightY;

	/** Gatillo analógico (de 0 a 1). Sin eje, el gatillo es el botón Gamepad_LeftTrigger de Buttons (y su eje vale 0 o 1). */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FTNPadAxisBinding LeftTrigger;

	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	FTNPadAxisBinding RightTrigger;

	/** El primer «hat» (POV) de DirectInput es la cruceta. */
	UPROPERTY(Config, EditAnywhere, Category = "Mando")
	bool bHatIsDPad = true;
};

/**
 * @brief Mandos que no son de Xbox (#743): DualShock 4, DualSense, Switch Pro y mandos HID genéricos, en el editor y en el
 * juego sin Steam Input. Se editan en Config/DefaultInput.ini ([/Script/Tortunabo.TN_GamepadSettings]) o en Ajustes del
 * proyecto > Tortunavy - Mandos. Los de Xbox los sigue leyendo XInput; los lee FTNGamepadDevice (DirectInput). Docs/Mandos.md.
 */
UCLASS(Config = Input, DefaultConfig, meta = (DisplayName = "Tortunavy - Mandos"))
class TORTUNABO_API UTN_GamepadSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Lee los mandos que no son de Xbox. Sin esto, solo XInput (y Steam Input, si el juego va por Steam). */
	UPROPERTY(Config, EditAnywhere, Category = "Mandos")
	bool bEnabled = true;

	/**
	 * Deja de leer un mando que Steam Input ya traduce a mando de Xbox (si no, cada pulsación llegaría dos veces): los de
	 * SDL_GAMECONTROLLER_IGNORE_DEVICES, que pone Steam al lanzar el juego, y, sin esa lista, los de la familia que Steam Input
	 * dice tener mientras hay un mando XInput conectado.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Mandos")
	bool bSkipSteamInputControllers = true;

	/** Perfiles por VendorID/ProductID, en orden: gana el primero que encaja. El último, sin IDs, vale para cualquier mando. */
	UPROPERTY(Config, EditAnywhere, Category = "Mandos")
	TArray<FTNPadProfile> Profiles;
};

/** Lo que se lee de un mando por DirectInput, con los ejes en -32768..32767 (DIPROP_RANGE). */
struct FTNPadRawState
{
	static constexpr int32 NumAxes = 8;
	static constexpr int32 MaxButtons = 128;

	/** X, Y, Z, RX, RY, RZ, Slider0 y Slider1 (el orden de ETNPadAxis sin None). */
	int32 Axes[NumAxes] = {};
	/** Si el mando tiene ese eje (los que no tiene leen 0, que en un gatillo sería «medio apretado»). */
	bool bHasAxis[NumAxes] = {};
	/** Primer hat en centésimas de grado (0 arriba, 9000 derecha); centrado si la palabra baja es 0xFFFF. */
	uint32 Pov = 0xFFFFFFFFu;
	bool Buttons[MaxButtons] = {};
};

/** Un fotograma del mando ya en teclas del motor. */
struct FTNPadFrame
{
	float LeftX = 0.f;
	float LeftY = 0.f;
	float RightX = 0.f;
	float RightY = 0.f;
	float LeftTrigger = 0.f;
	float RightTrigger = 0.f;
	/** Teclas Gamepad_* apretadas (botones, cruceta, gatillos y sticks pasado el umbral), sin repetir. */
	TArray<FName, TInlineAllocator<16>> Pressed;
};

/**
 * Reglas de los mandos que no son de Xbox (#743), sin aparato ni mundo: las prueban los tests de Automation
 * (Tortunabo.Input.Gamepad).
 */
namespace TNGamepadRules
{
	/** Por debajo de esto, un stick vale 0 (el reposo de un mando barato baila un 2-5 %). */
	inline constexpr float StickSnap = 0.06f;
	/** Stick como botón (Gamepad_LeftStick_Up...): el umbral de XInput (7849/32767). */
	inline constexpr float StickButtonThreshold = 0.2395f;
	/** Gatillo como botón (Gamepad_LeftTrigger): el umbral de XInput (30/255). */
	inline constexpr float TriggerButtonThreshold = 0.1176f;

	/** VendorID o ProductID en hexadecimal («054C», «0x054c»); -1 si no lo es o no cabe en 16 bits. */
	TORTUNABO_API int32 ParseHexId(const FString& Text);

	/** El primer perfil de la lista que encaja con el mando, o INDEX_NONE. */
	TORTUNABO_API int32 FindProfile(const TArray<FTNPadProfile>& Profiles, uint16 VendorId, uint16 ProductId);

	/** El perfil de serie para un mando que no encaja con ninguno (la distribución más común de DirectInput). */
	TORTUNABO_API FTNPadProfile MakeGenericProfile();

	/** La familia de los avisos de botones de un perfil (por su HardwareId). */
	TORTUNABO_API ETNPadFamily FamilyOf(const FTNPadProfile& Profile);

	/** Stick de -32768..32767 a -1..1 (al revés si se pide), con el reposo a 0. */
	TORTUNABO_API float NormalizeStick(int32 Raw, bool bInvert);

	/** Gatillo de -32768..32767 a 0..1 (al revés si se pide). */
	TORTUNABO_API float NormalizeTrigger(int32 Raw, bool bInvert);

	/** La cruceta según el hat: arriba, derecha, abajo, izquierda (con las diagonales, dos a la vez). */
	TORTUNABO_API void HatToDPad(uint32 Pov, bool& bUp, bool& bRight, bool& bDown, bool& bLeft);

	/** Traduce lo leído de un mando con su perfil a ejes y teclas del motor. */
	TORTUNABO_API FTNPadFrame Translate(const FTNPadProfile& Profile, const FTNPadRawState& Raw);

	/**
	 * Lista de SDL_GAMECONTROLLER_IGNORE_DEVICES (o su _EXCEPT): «0xVVVV/0xPPPP» separados por comas. Cada mando, como
	 * (VendorID << 16) | ProductID. Lo que no se entiende se salta.
	 */
	TORTUNABO_API TArray<uint32> ParseDeviceList(const FString& List);

	/**
	 * true si Steam pide no leer este mando: con lista _EXCEPT, todo lo que no está en ella; si no, lo que está en la lista
	 * de ignorados (las reglas de SDL, que Steam usa para los mandos que traduce).
	 */
	TORTUNABO_API bool IsInSteamIgnoreList(uint16 VendorId, uint16 ProductId, const TArray<uint32>& Ignore, const TArray<uint32>& Except);

	/**
	 * true si Steam Input ya traduce un mando de esta familia: algún tipo de Steam Input (ESteamInputType como entero) es de
	 * la familia (PlayStation, Switch o, para el resto, el mando genérico de DirectInput). Los tipos de Xbox no cuentan:
	 * XInput nunca llega por aquí.
	 */
	TORTUNABO_API bool IsFamilyTranslatedBySteam(ETNPadFamily Family, const TArray<int32>& SteamInputTypes);
}
