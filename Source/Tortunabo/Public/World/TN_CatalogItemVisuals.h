#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"

class UObject;

/**
 * Aspecto de cada objeto de siempre (las filas de DT_Items) dibujado en código, con el estilo de los objetos de la carrera y
 * de Todos contra Todos (issue #787). Sale del uso de la fila (UseType), como en los sorteos de los modos, y del ItemId en la
 * fila sin uso. Inventario completo en Docs/Objetos_DT_Items.md.
 */
enum class ETNCatalogLook : uint8
{
	None,
	/** Energía sin fin (SelfStaminaBoost, fila StaminaBoost): barrita naranja con un rayo. */
	StaminaBoost,
	/** Barra llena de golpe (SelfStaminaFull): barrita turquesa. Hoy no tiene fila. */
	StaminaFull,
	/** Bola (Throwable, fila ThrowableBall): la piedra gris de su malla, Piedra1. */
	Ball,
	/** Cabezota (BigHead): cabeza de tortuga grandota. */
	BigHead,
	/** Concha trampa (Conch): concha cerrada malva de su malla, ConchaCerrada. */
	Conch,
	/** Tinta de calamar (InkThrower, fila Tinta): el calamar morado de su malla, Calamar. */
	Ink,
	/** Tótem (Totem): la tortuga de peluche verde de su malla, Peluche1. */
	Totem,
	/** Fila de relleno Score (sin uso): concha de puntos melocotón. */
	Score,
	Count
};

/**
 * El icono de la mochila es siempre una pegatina pintada en ejecución (128x128, como TNRaceItemArt y TNTctItemArt). La malla
 * solo se cambia si la fila trae una de relleno del motor (/Engine/BasicShapes...): las del proyecto se conservan, y la de un
 * lanzable también, porque el proyectil la manda a las demás máquinas por un multicast y una malla construida en ejecución
 * no viaja por la red. No se toca DT_Items.uasset.
 *
 * Cada máquina lo pone por su cuenta: TNRaceItems::ResolveVisuals lo llama desde el inventario (al recibir el objeto en el
 * servidor y al replicarse en los clientes) y desde los pickups, igual que con los objetos de carrera.
 */
namespace TNCatalogItemVisuals
{
	/** El aspecto de un objeto de DT_Items (None si es de carrera, de Todos contra Todos o una fila sin uso desconocida). */
	TORTUNABO_API ETNCatalogLook LookOf(const FTN_InventoryItem& Item);

	/** Nombre para el registro y las claves de caché («StaminaBoost», «Ball»...). */
	TORTUNABO_API const TCHAR* CodeName(ETNCatalogLook Look);

	/**
	 * true si Asset es un recurso de relleno del motor o de VREditor (/Engine/..., /VREditor/...). Lo construido en
	 * ejecución vive en /Engine/Transient y no cuenta.
	 */
	TORTUNABO_API bool IsEnginePlaceholder(const UObject* Asset);

	/** true si la malla de Item se sustituye por la construida en código (falta o es del motor, y no es un lanzable). */
	TORTUNABO_API bool ShouldReplaceMesh(const FTN_InventoryItem& Item);

	/**
	 * Pone a Item el icono (y la malla, si toca) de su aspecto. true si Item es un objeto de DT_Items con aspecto propio,
	 * aunque esta máquina no dibuje nada (servidor dedicado o sin motor gráfico).
	 */
	TORTUNABO_API bool ResolveVisuals(FTN_InventoryItem& Item);

	/** Lo mismo, también en una máquina sin pantalla: para los tests y las herramientas que inspeccionan el catálogo. */
	TORTUNABO_API bool ResolveVisualsHeadless(FTN_InventoryItem& Item);
}
