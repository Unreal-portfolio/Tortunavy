#pragma once

#include "CoreMinimal.h"

/**
 * Tabla de todas las piezas de arte sustituibles (Docs/Arte_Assets.md). Se escribe en TN_ArtSlots_<Parte>.inl (Lobby:
 * castillo; LobbyValley: valle; LobbyPlayground: parque y puestos; ProcMap; Beach; Turtle: piezas pegadas a la
 * tortuga), una pieza por
 * TN_ART_SLOT(...). La leen TN.Art.Slots, los tests
 * (Tortunabo.Art.*) y Scripts/arte/rellenar_catalogos.py (que mete cada pieza vacía en su catálogo y genera la lista del
 * documento), así que cada campo es una cadena entre comillas dobles, sin comillas dentro.
 */
namespace TNArt
{
	/** Cómo se sustituye la pieza (Docs/Arte_Assets.md, «Tipos de pieza»). */
	namespace SlotKind
	{
		/** Parte de una malla combinada: la de arte va en cada copia, como instancias en el sitio de la pieza. */
		inline const TCHAR* Piece = TEXT("Pieza");
		/** Malla de un componente suelto: la de arte va de hija y se mueve, se esconde y se enseña con él. */
		inline const TCHAR* Component = TEXT("Componente");
		/** Malla de un ISM/HISM: cambia la de todas sus instancias. */
		inline const TCHAR* Instances = TEXT("Instancias");
		/** Malla estática nueva pegada a un hueso de la tortuga (TNTurtleArt): sigue su animación; sin ella no hay nada. */
		inline const TCHAR* Bone = TEXT("Hueso");
	}

	struct FSlotInfo
	{
		/** «Lobby.Castle.Tower». */
		const TCHAR* Name;
		/** SlotKind. */
		const TCHAR* Kind;
		/** Fichero de Source/Tortunabo que la genera. */
		const TCHAR* Source;
		/** Qué es. */
		const TCHAR* What;
		/** Tamaño aproximado (cm). */
		const TCHAR* Size;
		/** Dónde está el pivote de la pieza generada y hacia dónde mira. */
		const TCHAR* Pivot;
	};

	/** Todas las piezas, en el orden de los .inl (Lobby, ProcMap, Beach, Turtle). */
	TArrayView<const FSlotInfo> GetSlotTable();

	/** La pieza de la tabla o nullptr. */
	const FSlotInfo* FindSlotInfo(FName Slot);
}
