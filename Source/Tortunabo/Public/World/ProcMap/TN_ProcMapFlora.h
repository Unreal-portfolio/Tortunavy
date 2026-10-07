#pragma once

#include "CoreMinimal.h"

/**
 * Formas de la vegetación y de los objetos sueltos (lógica pura): qué malla es cada especie (EFloraShape, EPropKind),
 * cuántas variantes tiene y en qué zona respecto al agua crece. Lo usan las mallas de vegetación
 * (TN_ProcMapFloraMeshes.h), el valle del lobby y la vegetación de los mapas de terreno fijo (ATN_MapPlacementSpawner).
 */
namespace TNProcMap
{
	/** Forma (malla) de una especie. */
	enum class EFloraShape : uint8
	{
		BroadTree,     ///< Árbol de copa redonda (selva, lagunas, parques).
		Ceiba,         ///< Gigante de la selva: raíces tabulares y copa en parasol.
		Palm,          ///< Palmera de tronco curvo.
		MangroveTree,  ///< Mangle: raíces zancudas en arco y copa ancha y baja.
		YoungSequoia,  ///< Secuoya joven.
		Cypress,       ///< Ciprés de pantano, cónico y esbelto.
		Pine,          ///< Pino de pisos.
		Fir,           ///< Abeto estrecho y denso.
		Willow,        ///< Sauce llorón.
		Acacia,        ///< Acacia de copa plana (desierto).
		DeadTree,      ///< Árbol seco sin hojas.
		CharredTree,   ///< Árbol calcinado (volcán), con brasas.
		Ornamental,    ///< Árbol de parque de copa esférica.
		Fern,          ///< Helecho de frondas arqueadas.
		Bush,          ///< Arbusto de varias masas.
		Grass,         ///< Mata de hierba.
		Flowers,       ///< Mata con flores de colores.
		Reeds,         ///< Juncos y eneas (orilla y agua somera).
		Saguaro,       ///< Cactus columnar con brazos.
		Barrel,        ///< Cactus barril con flor.
		DryBush,       ///< Matojo seco de ramitas.
		AshBush,       ///< Arbusto de ceniza (volcán).
		Hedge,         ///< Seto recortado (zona humana).
		Umbrella,      ///< Sombrilla de playa con mástil (zona humana).
		Creeper,       ///< Enredadera o musgo: manta de hojas pegada a la pared (sigue su normal).
		BananaPlant,   ///< Platanera de hojas enormes (selva).
		Bamboo,        ///< Mata de bambú con nudos y penachos.
		TreeFern,      ///< Helecho arbóreo.
		SeaGrape,      ///< Uva de playa: arbolito bajo de hojas redondas.
		Pandanus,      ///< Pándano de raíces zancudas y penachos de hojas afiladas.
		FanPalm,       ///< Palmito de hojas en abanico.
		Casuarina,     ///< Casuarina de ramillas colgantes (costa).
		JoshuaTree,    ///< Árbol de Josué (desierto).
		Birch,         ///< Abedul de tronco blanco.
		Prop,          ///< Objeto suelto (EPropKind dice cuál).
		Rock,          ///< Peñasco suelto.
		Stones,        ///< Corro de piedras pequeñas.
		Count
	};

	/** Objetos sueltos junto al camino (se reparten como la vegetación, en grupos). */
	enum class EPropKind : uint8
	{
		Crate, WoodBarrel, Barricade, TrafficCone, HayBale, Bench, LampPost, Mailbox, Sacks, FlowerPot,
		Shell, Starfish, SandBucket, BeachTowel, Surfboard, Parasol, Driftwood, Coconuts, Lifebuoy,
		Mushrooms, ClayPot, TikiTorch, SkullPost, CattleSkull, Bones, Amphora, WagonWheel, Signpost, Tumbleweed,
		Crystals, Stump, Cairn, Lantern, CrabTrap,
		Count
	};

	/** Variantes de malla por especie (forma y tono distintos). */
	constexpr int32 FloraVariants = 3;

	/** Dónde puede crecer una especie según la cota respecto al agua. */
	namespace FloraZone
	{
		constexpr uint8 Land = 1u << 0;      ///< Tierra firme (40 cm o más sobre el agua).
		constexpr uint8 Shore = 1u << 1;     ///< Orilla, a ras de agua.
		constexpr uint8 Shallows = 1u << 2;  ///< Agua somera (hasta 1,8 m de fondo).
		constexpr uint8 Deep = 1u << 3;      ///< Agua honda (hasta 4,5 m): mangles y cipreses de las pozas.
	}
}
