#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"
#include "Game/TN_CoopItemRules.h"

class APawn;
class ATortugaCharacter;
class UDataTable;

/**
 * Objetos del cooperativo y de los modos de a pie en el juego (GDD oficial; reglas puras en TN_CoopItemRules.h): la fila del
 * inventario de cada uno, su malla y su icono, el apilado en el inventario, la tabla de botín del coop y lo que hacen al
 * usarlos (servidor).
 *
 * Como los de la carrera y los de Todos contra Todos: UseType CoopItem, el objeto y su cuenta en el ItemId («Coop_<Code>_<n>»),
 * sin filas en DT_Items; cada máquina pone malla e icono por el ItemId (ResolveVisuals), así que no se replica ni se guarda
 * ningún asset. El icono lleva pintada la cuenta (apilados o usos que quedan) cuando pasa de uno.
 *
 * Tabla de botín del coop (RollLoot): los rebuscables del cooperativo (ATN_ProcSearchSpot) y el charco de pesca sortean las
 * filas de DT_Items de siempre (con el peso de quien sortea x TNCoopItemTuning::CatalogWeightScale) y los objetos de código
 * con su peso del Excel.
 */
namespace TNCoopItems
{
	/** Nombre que se ve (español, localizable). */
	TORTUNABO_API FText DisplayName(ETNCoopItem Kind);

	/** El objeto del coop de una fila del inventario (None si no es CoopItem). */
	TORTUNABO_API ETNCoopItem KindOf(const FTN_InventoryItem& Item);

	/** Cuenta de una fila del coop: apilados o usos que le quedan (0 si no lo es). */
	TORTUNABO_API int32 CountOf(const FTN_InventoryItem& Item);

	/** true si Kind se apunta (se lanza o se dispara hacia la mira): el HUD enseña la mira mientras se lleva en la mano. */
	TORTUNABO_API bool IsAimed(ETNCoopItem Kind);

	/** Lo que hay que buscar en la consola: el código («Harpoon»), sin mayúsculas, o el número. false si no es ninguno. */
	TORTUNABO_API bool ParseKind(const FString& Text, ETNCoopItem& OutKind);

	/** La fila del inventario de Kind con Count (0 = la cuenta con la que sale: sus usos o una unidad). Inválida si no es un objeto. */
	TORTUNABO_API FTN_InventoryItem MakeItem(ETNCoopItem Kind, int32 Count = 0);

	/** Si Item es del coop, le pone malla, escala, giro e icono (con la cuenta) de esta máquina. No hace nada con otros. */
	TORTUNABO_API void ResolveVisuals(FTN_InventoryItem& Item);

	/**
	 * Qué pasa si un hueco con Held recibe Incoming (TNCoopItemRules::DecideStack). Con Merge, OutMerged es la fila del hueco
	 * con la cuenta nueva (sin malla ni icono: ResolveVisuals).
	 */
	TORTUNABO_API ETNCoopStack DecideStack(const FTN_InventoryItem& Held, const FTN_InventoryItem& Incoming, FTN_InventoryItem& OutMerged);

	/** Servidor: da Kind a la tortuga (en la mano o, si está llena, en el caparazón; si no, sustituye lo de la mano). */
	TORTUNABO_API bool GiveItem(ATortugaCharacter* Turtle, ETNCoopItem Kind);

	/**
	 * Servidor: sortea de la tabla del coop con Roll en [0, 1). CatalogWeight da el peso de quien sortea a cada fila de
	 * DT_Items (1 por defecto); Catalog nulo = solo los objetos de código. false si no sale nada.
	 */
	TORTUNABO_API bool RollLoot(const UDataTable* Catalog, TFunctionRef<float(FName, const FTN_InventoryItem&)> CatalogWeight, float Roll,
		FTN_InventoryItem& OutItem);

	/**
	 * Servidor: la tabla del modo en el que está Picker: en la carrera de la playa, la de la carrera (por su puesto, como un
	 * rebuscable); en el resto, la del coop (RollLoot con FMath::FRand).
	 */
	TORTUNABO_API bool RollModeLoot(const APawn* Picker, const UDataTable* Catalog, TFunctionRef<float(FName, const FTN_InventoryItem&)> CatalogWeight,
		FTN_InventoryItem& OutItem);

	/**
	 * Servidor: la tortuga usa el objeto del coop Item (el equipado). Si no se puede ahora, suena un «nop» y el objeto se
	 * queda; si se usa, se gasta una unidad o un uso. Lo llama ATortugaCharacter::ServerUseEquippedItem.
	 */
	TORTUNABO_API void ServerUse(ATortugaCharacter* Turtle, const FTN_InventoryItem& Item);
}
