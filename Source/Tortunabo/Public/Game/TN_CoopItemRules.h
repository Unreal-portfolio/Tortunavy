#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"

/**
 * Objetos del cooperativo y de los modos de a pie definidos solo desde código (GDD oficial, hoja ObjectsData del Excel; regla
 * del director: sin vida numérica, veneno ni curas, que se traducen a estados). Cada uno es un FTN_InventoryItem con UseType
 * CoopItem y el objeto y su cuenta en el ItemId («Coop_<Code>_<Cuenta>»): sin filas en DT_Items ni assets. La cuenta es lo
 * que hay apilado en el hueco (los que se apilan) o los usos que le quedan (las herramientas de varios usos).
 *
 * Lógica pura, sin mundo ni red (catálogo, apilado, tabla de botín del coop y charco de pesca): la recorren las pruebas
 * Tortunabo.Coop.Items.*. El objeto en el juego (malla, icono, uso) está en TN_CoopItems.h.
 */
enum class ETNCoopItem : uint8
{
	None,
	/** Pez globo: al comerlo, 5 s sin derribo ni aturdimiento («protección x2» sin vida) y un mareo corto al acabar. */
	PufferFish,
	/** Cáscara resbaladiza: se lanza al suelo (8 m como mucho) y deja un parche; quien lo pisa resbala un instante. */
	SlipperyPeel,
	/**
	 * Concha: se lanza hasta 10 m y aturde al enemigo al que da (nunca a una tortuga). Distinta de la concha trampa de DT_Items
	 * (UseType Conch, que se deja en el suelo): por eso su ItemId es «Coop_StunShell_<n>».
	 */
	StunShell,
	Count
};

/** Lo fijo de cada objeto. */
struct FTNCoopItemSpec
{
	ETNCoopItem Kind = ETNCoopItem::None;
	/** Nombre para el ItemId, el registro y la consola. */
	const TCHAR* Code = TEXT("");
	/** Cuántos caben en un hueco del inventario (límite de apilado del Excel). */
	int32 MaxStack = 1;
	/** Usos de una unidad (1 = se gasta al usarla; más = herramienta con usos). */
	int32 Uses = 1;
	/** Peso en la tabla de botín del coop (el «Peso (%)» del Excel). */
	float LootWeight = 0.f;
};

/** Qué pasa al recibir un objeto del coop teniendo ya uno en un hueco. */
enum class ETNCoopStack : uint8
{
	/** Son objetos distintos (o no son del coop): va a otro hueco como siempre. */
	Separate,
	/** Mismo objeto con sitio: se suma al hueco que ya lo tiene (o recarga sus usos). */
	Merge,
	/** Mismo objeto y ya está al máximo: no se coge. */
	Full
};

/** Valores del charco de pesca y de la tabla de botín. */
namespace TNCoopItemTuning
{
	/** Charco: segundos que hay que mantener la tecla, respiro entre capturas y objetos sin recoger a la vez como mucho. */
	inline constexpr float FishSeconds = 2.f;
	inline constexpr float FishCooldownSeconds = 12.f;
	inline constexpr int32 FishMaxLootLying = 2;
	/** Radio (cm) del agua del charco. */
	inline constexpr float PoolRadius = 120.f;

	/**
	 * Peso en la tabla del coop de una fila de DT_Items con peso 1 (las filas no tienen fila en el Excel con los mismos
	 * nombres): 15, el más repetido del Excel. Con el tótem a 0,3 queda en 4,5.
	 */
	inline constexpr float CatalogWeightScale = 15.f;

	/**
	 * Pez globo: segundos de protección (nada la derriba ni la aturde), segundos del mareo de después y tope de velocidad
	 * (cm/s) mientras dura el mareo (el del mareo de la cabezota).
	 */
	inline constexpr float PufferSeconds = 5.f;
	inline constexpr float PufferDizzySeconds = 2.f;
	inline constexpr float PufferDizzySpeedCap = 250.f;

	/**
	 * Cáscara resbaladiza: alcance (cm, en planta), radio del parche, diferencia de altura máxima para pisarlo, segundos hasta
	 * que resbala (al caer), vida máxima del parche, velocidad del resbalón (por el suelo y hacia arriba: en el aire no hay
	 * agarre ni apenas control) y mareo de los enemigos que lo pisan.
	 */
	inline constexpr float PeelRange = 800.f;
	inline constexpr float PeelRadius = 70.f;
	inline constexpr float PeelStepHeight = 120.f;
	inline constexpr float PeelArmSeconds = 0.35f;
	inline constexpr float PeelLifeSeconds = 45.f;
	inline constexpr float PeelSlipSpeed = 700.f;
	inline constexpr float PeelSlipUp = 280.f;
	inline constexpr float PeelEnemyStunSeconds = 1.5f;

	/**
	 * Concha: alcance (cm), grosor de la mira para encontrar al enemigo, segundos de aturdimiento, distancia a la que el
	 * enemigo puede haberse movido mientras vuela y aún le da, y segundos que se queda en el suelo si no iba a nadie.
	 */
	inline constexpr float ShellRange = 1000.f;
	inline constexpr float ShellAimRadius = 80.f;
	inline constexpr float ShellStunSeconds = 4.f;
	inline constexpr float ShellHitSlack = 200.f;
	inline constexpr float ShellRestSeconds = 1.5f;

	/** Objetos lanzados en arco: velocidad media (cm/s) para el tiempo de vuelo, mínimo de vuelo (s) y altura del arco. */
	inline constexpr float ThrowSpeed = 1600.f;
	inline constexpr float ThrowMinSeconds = 0.25f;
	inline constexpr float ThrowArcBase = 90.f;
	inline constexpr float ThrowArcPerCm = 0.12f;
}

/** Lo que el pez globo deja en una tortuga ahora (horas del servidor; 0 = nunca). */
struct FTNPufferState
{
	double ProtectEnd = 0.0;
	double DizzyEnd = 0.0;

	/** Protegida ahora: ni derribo ni aturdimiento. */
	bool IsProtected(double Now) const { return ProtectEnd > 0.0 && Now < ProtectEnd; }

	/** Mareada por el pez globo ahora (el rato de después de la protección). */
	bool IsDizzy(double Now) const { return !IsProtected(Now) && DizzyEnd > 0.0 && Now >= ProtectEnd && Now < DizzyEnd; }

	/** Se puede comer otro: no se apila ni se alarga mientras dura la protección. */
	bool CanStart(double Now) const { return !IsProtected(Now); }

	/** Come uno a la hora Now: protección y, al acabar, el mareo. false (sin cambios) si aún dura el anterior. */
	bool Start(double Now, float ProtectSeconds, float DizzySeconds)
	{
		if (!CanStart(Now))
		{
			return false;
		}
		ProtectEnd = Now + FMath::Max(0.f, ProtectSeconds);
		DizzyEnd = ProtectEnd + FMath::Max(0.f, DizzySeconds);
		return true;
	}
};

/** Lo que sale de la tabla del coop: una fila de DT_Items (por su índice en la lista de pesos) o un objeto de código. */
struct FTNCoopLootPick
{
	int32 CatalogIndex = INDEX_NONE;
	ETNCoopItem Kind = ETNCoopItem::None;

	bool IsValid() const { return CatalogIndex != INDEX_NONE || Kind != ETNCoopItem::None; }
};

namespace TNCoopItemRules
{
	/** Ficha de Kind (la de None si no es ninguno). */
	TORTUNABO_API const FTNCoopItemSpec& Spec(ETNCoopItem Kind);

	/** Todos los objetos de verdad (sin None ni Count). */
	TORTUNABO_API TArray<ETNCoopItem> AllKinds();

	/** Cuenta con la que sale un objeto nuevo: sus usos si es una herramienta; si no, una unidad. */
	TORTUNABO_API int32 InitialCount(ETNCoopItem Kind);

	/** ItemId de Kind con esa cuenta: «Coop_<Code>_<Cuenta>». NAME_None si no es un objeto o la cuenta no vale. */
	TORTUNABO_API FName MakeItemId(ETNCoopItem Kind, int32 Count);

	/** El objeto y su cuenta de un ItemId de MakeItemId. false si no lo es. */
	TORTUNABO_API bool ParseItemId(FName ItemId, ETNCoopItem& OutKind, int32& OutCount);

	/** Lo que queda en la mano tras usar uno: el mismo con uno menos, o nada (NAME_None) si era el último. */
	TORTUNABO_API FName ItemIdAfterUse(FName ItemId);

	/**
	 * Apilado con las medidas a mano: un hueco con HeldCount recibe IncomingCount del mismo objeto. Los que se apilan
	 * (MaxStack > 1) suman hasta MaxStack; las herramientas de varios usos (Uses > 1) se quedan con los usos del que tenga
	 * más (coger otra recarga). Merge si la cuenta sube (OutCount, la nueva); Full si no.
	 */
	TORTUNABO_API ETNCoopStack DecideStackCounts(int32 MaxStack, int32 Uses, int32 HeldCount, int32 IncomingCount, int32& OutCount);

	/** Lo mismo con los objetos: Separate si no son el mismo objeto del coop. */
	TORTUNABO_API ETNCoopStack DecideStack(ETNCoopItem Held, int32 HeldCount, ETNCoopItem Incoming, int32 IncomingCount, int32& OutCount);

	/**
	 * Peso en la tabla del coop de una fila de DT_Items de uso Use con el peso BaseWeight de quien sortea (1 por defecto):
	 * BaseWeight x CatalogWeightScale. Las filas sin uso o de los otros catálogos de código, 0.
	 */
	TORTUNABO_API float CatalogWeight(ETN_ItemUseType Use, float BaseWeight);

	/** Índice elegido de Weights con Roll en [0, 1) (los pesos de 0 o menos no salen); INDEX_NONE si no hay peso. */
	TORTUNABO_API int32 PickWeighted(const TArray<float>& Weights, float Roll);

	/**
	 * Tabla de botín del coop: las filas de DT_Items con los pesos CatalogWeights (ya con CatalogWeight) y después los objetos
	 * de código con su LootWeight. Roll en [0, 1). Nada si no hay peso.
	 */
	TORTUNABO_API FTNCoopLootPick PickLoot(const TArray<float>& CatalogWeights, float Roll);

	/** Probabilidad (0-1) de que la tabla dé el objeto de código Kind con esos pesos de DT_Items. */
	TORTUNABO_API float LootChance(const TArray<float>& CatalogWeights, ETNCoopItem Kind);

	// ── Lanzar en arco (cáscara y concha) ──────────────────────────────────────────────────────────────────────────────

	/** Desired recortado a MaxRange (cm) en planta desde Origin; la altura de Desired se queda. */
	TORTUNABO_API FVector ClampThrowTarget(const FVector& Origin, const FVector& Desired, float MaxRange);

	/** Punto del arco de From a To a Alpha (0-1, recortado) con Height cm de altura sobre la recta en la mitad. */
	TORTUNABO_API FVector ArcPoint(const FVector& From, const FVector& To, float Alpha, float Height);

	/** Segundos de vuelo y altura del arco para una distancia (cm). */
	TORTUNABO_API float ThrowFlightSeconds(float Distance);
	TORTUNABO_API float ThrowArcHeight(float Distance);

	// ── Cáscara resbaladiza ────────────────────────────────────────────────────────────────────────────────────────

	/** Where está pisando el parche de Patch: dentro del radio en planta y a menos de PeelStepHeight de altura. */
	TORTUNABO_API bool IsOnPatch(const FVector& Patch, const FVector& Where);

	/**
	 * Resbalón de quien pisa la cáscara yendo a Velocity y mirando a Facing: sigue hacia donde iba (o hacia donde mira si
	 * estaba casi parada), algo más rápido que iba y nunca menos de PeelSlipSpeed, con PeelSlipUp hacia arriba.
	 */
	TORTUNABO_API FVector SlipVelocity(const FVector& Velocity, const FVector& Facing);

	// ── Concha ─────────────────────────────────────────────────────────────────────────────────────────────────────

	/** La concha aturde a algo solo si es un enemigo que se deja aturdir y no es una tortuga. */
	TORTUNABO_API bool CanShellStun(bool bIsTurtle, bool bIsEnemy, bool bAcceptsStun);

	/** El enemigo sigue donde iba la concha al llegar (a menos de ShellHitSlack del punto). */
	TORTUNABO_API bool IsShellHit(const FVector& AimedAt, const FVector& EnemyNow);
}
