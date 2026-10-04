// Objetos de los karts (#304), al estilo de las carreras de karts y con tema de playa y tortuga: se sacan de las cajas del
// camino (ATN_KartItemBox) con una ruleta y se usan con un botón (la artillera si va alguien detrás; si no, la conductora).
// Lógica pura (reparto por puesto, cargas, decisión de los bots y guiado de la concha), con tests Tortunabo.Kart.Items.*.
// Los efectos los aplica UTN_KartItemComponent en el servidor.
#pragma once

#include "CoreMinimal.h"
#include "Kart/TN_KartShellLogic.h"
#include "TN_KartItems.generated.h"

UENUM(BlueprintType)
enum class ETNKartItem : uint8
{
	None          UMETA(DisplayName = "Ninguno"),
	/** Acelerón de 1,3 s. */
	Coco          UMETA(DisplayName = "Coco turbo"),
	/** Tres acelerones, uno por pulsación. */
	TripleCoco    UMETA(DisplayName = "Triple coco turbo"),
	/** Concha que sale recta (hacia delante o, con atrás pulsado, hacia atrás) y hace trompear al primero que toca. */
	Concha        UMETA(DisplayName = "Concha"),
	/** Concha que persigue al kart de delante. */
	ConchaGuiada  UMETA(DisplayName = "Concha teledirigida"),
	/** Charco de alga resbaladiza que se deja detrás. */
	Alga          UMETA(DisplayName = "Alga resbaladiza"),
	/** Tinta de calamar en la pantalla de todos los que van por delante. */
	Tinta         UMETA(DisplayName = "Tinta de calamar"),
	/** Invulnerable y algo más rápida unos segundos; aparta a los karts que toca. */
	Estrella      UMETA(DisplayName = "Estrella de mar"),
	Count         UMETA(Hidden)
};

namespace TNKart
{
	inline constexpr int32 ItemKinds = static_cast<int32>(ETNKartItem::Count);

	// ---- Ajuste ----

	/** Ruleta del HUD al coger una caja (s): hasta que acaba no se puede usar. */
	inline constexpr float RouletteSeconds = 1.1f;
	/** Espera mínima entre dos usos (s), para no gastar el triple coco de una pulsación. */
	inline constexpr float UseCooldownSeconds = 0.4f;
	/** Acelerón del coco: impulso al usarlo (cm/s), empuje (cm/s²) durante BoostSeconds y velocidad hasta la que empuja. */
	inline constexpr float BoostImpulseCms = 500.f;
	inline constexpr float BoostSeconds = 1.3f;
	inline constexpr float BoostAccelCms2 = 1500.f;
	inline constexpr float BoostTopSpeedCms = 3400.f;
	/** Estrella de mar: duración (s), empuje (cm/s²), velocidad hasta la que empuja y radio con el que aparta a los demás (cm). */
	inline constexpr float StarSeconds = 7.f;
	inline constexpr float StarAccelCms2 = 700.f;
	inline constexpr float StarTopSpeedCms = 3200.f;
	inline constexpr float StarBumpRadiusCm = 380.f;
	/** Velocidad de las conchas (cm/s); su vida, su guiado y el trompo están en TN_KartShellLogic.h (también de la torreta). */
	inline constexpr float ShellSpeedCms = 4600.f;
	/** El charco de alga cae a esta distancia detrás del kart (cm). */
	inline constexpr float AlgaBehindCm = 500.f;

	// ---- Reparto ----

	/** Peso de cada objeto (índice = ETNKartItem); None siempre 0. */
	struct FItemWeights
	{
		float Weights[ItemKinds] = {};

		float Total() const;
	};

	/**
	 * Reparto según el puesto (1 = primera de NumKarts): delante, alga, conchas rectas y cocos; detrás, conchas
	 * teledirigidas, triple coco, tinta y estrella de mar. La primera nunca saca tinta (no tiene a nadie delante) ni estrella.
	 */
	TORTUNABO_API FItemWeights ItemWeightsForPlace(int32 Place, int32 NumKarts);

	/** El objeto que toca con Roll01 en [0, 1) según los pesos (None si todos son 0). */
	TORTUNABO_API ETNKartItem PickItem(const FItemWeights& Weights, float Roll01);

	/** Usos de un objeto recién sacado (3 el triple coco; 1 el resto; 0 None). */
	TORTUNABO_API int32 ItemCharges(ETNKartItem Item);

	/** Nombre en pantalla (HUD). */
	TORTUNABO_API FText ItemName(ETNKartItem Item);

	// ---- Uso (guiado de la concha en TN_KartShellLogic.h) ----

	/**
	 * Bot: ¿usa ya el objeto? HeldSeconds desde que acabó la ruleta; AheadCm y BehindCm, distancia al kart de delante y al
	 * de detrás (negativa si no hay). Las conchas, con alguien delante a menos de 60 m; el alga, con alguien detrás a menos
	 * de 40 m; el resto, al rato. Pasados 8 s lo usa igual.
	 */
	TORTUNABO_API bool ShouldBotUseItem(ETNKartItem Item, float HeldSeconds, float AheadCm, float BehindCm);
}
