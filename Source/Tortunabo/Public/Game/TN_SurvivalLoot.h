#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"
#include "World/Beach/TN_RaceItems.h"

class APawn;
class UDataTable;

/**
 * Objetos de los rebuscables de Supervivencia (#724): los de siempre de DT_Items salvo el tótem (energía sin fin, bola,
 * cabezota, concha trampa y tinta) y, de la carrera de la playa, los cocos (turbo, triple y dorado), el cangrejo
 * teledirigido, la mina de arena, la nube de tormenta y el disco volador. Cada uno con el peso de la carrera según el
 * puesto (TNRaceItems::RollLoot, con el avance por el camino del nivel), salvo la cabezota, que aquí sí salva de una
 * gaviota y sale como cualquier otro.
 *
 * Al empezar cada nivel desde el 2, las que llegaron antes a la meta del anterior salen con cocos (StartItemFor).
 *
 * Cada decorado se rellena RefillSeconds después de cada resultado, para que quien va primera no deje vacíos los de las
 * demás. La densidad de rebuscables es la de la carrera (TNBeachLoot): 70 % de suerte, casi todo el decorado elegible y 9 m de
 * centro a centro y 3 m de borde a borde como mínimo. La reparte ATN_ProcMapGenerator::SpawnSearchSpots.
 *
 * Lógica pura salvo Roll; tests Tortunabo.Survival.Loot.
 */
namespace TNSurvivalLoot
{
	/** Probabilidad de que salga algo al rebuscar (la de la playa, TNBeachLoot::SearchLuck). */
	inline constexpr float SearchLuck = 0.7f;

	/**
	 * Cada decorado se vuelve a poder rebuscar estos segundos después de cada resultado: a las de detrás no les llegan
	 * vacíos. Y como mucho deja MaxLootLying objetos sin recoger a la vez (al pasarse se quita el más viejo).
	 */
	inline constexpr float RefillSeconds = 5.f;
	inline constexpr int32 MaxLootLying = 1;

	/** Separación entre rebuscables (cm), la de la playa: de centro a centro y de borde a borde. */
	inline constexpr double MinSpacing = 900.0;
	inline constexpr double MinRimGap = 300.0;

	/** Peso de la cabezota (en la carrera, 0,3: allí no protege de nada). */
	inline constexpr float BigHeadWeight = 1.f;

	/** Si un objeto de siempre (por su uso) sale en Supervivencia. */
	TORTUNABO_API bool AllowsCatalogUse(ETN_ItemUseType Use);

	/**
	 * Si un objeto de carrera sale en Supervivencia para quien va en el puesto Place (0 = la primera) con Racers tortugas en
	 * juego. La nube necesita a otra a la que atacar; el cangrejo, a otra por delante (aquí no hay enemigos de la playa a
	 * los que mandarlo): ni en solitario ni a la primera. Los triples de 2 y 1 usos no salen nunca de un sorteo (son lo que
	 * queda del de 3).
	 */
	TORTUNABO_API bool AllowsRaceItem(ETNRaceItem Kind, int32 Place, int32 Racers);

	/** Peso final en el sorteo a partir del de la carrera (los argumentos de TNRaceItems::RollLoot); 0 = no sale. */
	TORTUNABO_API float Weight(ETN_ItemUseType Use, ETNRaceItem Kind, float RaceWeight, int32 Place, int32 Racers);

	/**
	 * Probabilidad de que un decorado candidato sea rebuscable, con la de la playa según su tamaño: lo grande (Priority 0,
	 * las formaciones), siempre; lo mediano (1-2, objetos del camino y agujas), casi siempre; los peñascos (3), a menudo.
	 */
	TORTUNABO_API double SpotChance(int32 Priority);

	/** Cocos con los que se empieza el nivel según el puesto de llegada, de la última a la primera premiada. */
	inline constexpr ETNRaceItem StartItemLadder[] = { ETNRaceItem::Coconut, ETNRaceItem::TripleCoconut2,
		ETNRaceItem::TripleCoconut3, ETNRaceItem::GoldenCoconut };

	/**
	 * Lo que recibe al empezar el nivel quien llegó en el puesto Place (0 = la primera) de Finishers que llegaron a la meta
	 * del anterior. La última no recibe nada; de las demás, la penúltima un coco, la anterior dos (triple coco de 2 usos),
	 * la anterior tres (triple coco) y la primera, con 5 o más, el coco dorado. Desde 6, las de detrás de la 4.ª, nada.
	 * Ej.: con 3, dos cocos, un coco y nada; con 5, dorado, tres, dos, uno y nada.
	 */
	TORTUNABO_API ETNRaceItem StartItemFor(int32 Place, int32 Finishers);

	/** Servidor: sortea el objeto que le sale a Picker de Catalog (DT_Items) y de los de carrera. false si no sale nada. */
	TORTUNABO_API bool Roll(const APawn* Picker, const UDataTable* Catalog, FTN_InventoryItem& OutItem);
}
