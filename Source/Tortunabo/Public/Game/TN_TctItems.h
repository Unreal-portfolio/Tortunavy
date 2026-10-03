#pragma once

#include "CoreMinimal.h"
#include "Core/TN_InventoryTypes.h"
#include "Game/TN_TctItemRules.h"
#include "World/Beach/TN_RaceItemSynth.h"

class ACharacter;
class ATortugaCharacter;
class UStaticMesh;

/**
 * Objetos de combate de Todos contra Todos (#651) en el juego: la fila del inventario de cada uno, su malla y su icono, y lo
 * que hacen al usarlos (servidor). Las reglas puras (cargas, sorteo, cuánto empuja cada golpe) están en TN_TctItemRules.h.
 *
 * Los de código (UseType TctItem) llevan el objeto y las cargas en el ItemId («Tct_Shovel_4»): no tienen fila en DT_Items y
 * cada máquina pone su malla e icono por el ItemId (ResolveVisuals), como los de la carrera. Sus mallas son las de la
 * biblioteca IA (#600, /Game/Art/IA/todos_contra_todos) por ruta blanda: si el asset no está en el proyecto, se usa una forma
 * básica del motor y el objeto funciona igual. Los reutilizados son la fila de DT_Items de su UseType (bola, concha trampa,
 * cabezota) o el objeto de la carrera (mina de arena, disco volador), y se usan como siempre.
 *
 * Efectos (servidor; los empujones con UTN_TurtleMovementComponent::LaunchFromServer, que el dueño estrena en su propio
 * movimiento: sin corrección). El caparazón protege de los empujones (trabuco, pala, balón, garfio), no del derribo.
 */
namespace TNTctItems
{
	/** Nombre que se ve (español, localizable). */
	TORTUNABO_API FText DisplayName(ETNTctItem Kind);

	/** El objeto de código de una fila del inventario (None si no es TctItem). */
	TORTUNABO_API ETNTctItem KindOf(const FTN_InventoryItem& Item);

	/** Cargas que le quedan a una fila de código (0 si no lo es). */
	TORTUNABO_API int32 ChargesOf(const FTN_InventoryItem& Item);

	/** Ruta de la malla IA de #600 (en la mano o como proyectil); vacía si no tiene. */
	TORTUNABO_API FString MeshPath(ETNTctItem Kind, bool bProjectile = false);

	/** La malla del objeto: la IA si está en el proyecto, si no una forma básica del motor. null en máquinas sin pantalla. */
	TORTUNABO_API UStaticMesh* LoadMesh(ETNTctItem Kind, bool bProjectile = false);

	/** Carga ya las mallas de todos los objetos de código (al cargar la arena), para no hacerlo en plena ronda. */
	TORTUNABO_API void PreloadMeshes();

	/** Escala de la malla del objeto (en la mano o como proyectil); distinta si es la forma básica de reserva. */
	TORTUNABO_API FVector MeshScale(ETNTctItem Kind, bool bProjectile, bool bFallback);

	/** Servidor: la fila del inventario de Kind con todas sus cargas. false si ahora no se puede dar (falta su fila en DT_Items). */
	TORTUNABO_API bool MakeItem(ETNTctItem Kind, FTN_InventoryItem& OutItem);

	/** Los objetos que MakeItem puede dar ahora (los de código siempre; los de DT_Items si está su fila). */
	TORTUNABO_API TArray<ETNTctItem> AvailableKinds();

	/** Si Item es de código, le pone malla, escala e icono de esta máquina (no hace nada con otros). */
	TORTUNABO_API void ResolveVisuals(FTN_InventoryItem& Item);

	/** Servidor: da Kind a la tortuga (en la mano o, si está llena, en el caparazón; si no, sustituye lo de la mano). */
	TORTUNABO_API bool GiveItem(ATortugaCharacter* Turtle, ETNTctItem Kind);

	/** Servidor: quita los dos objetos que lleva (al empezar cada ronda). */
	TORTUNABO_API void ClearInventory(ATortugaCharacter* Turtle);

	/**
	 * Servidor: la tortuga usa el objeto de código Item (el equipado). Si no se puede ahora (nadie a quien apuntar con el
	 * garfio...), suena un «nop» y el objeto se queda; si se usa, se gasta una carga. Lo llama
	 * ATortugaCharacter::ServerUseEquippedItem.
	 */
	TORTUNABO_API void ServerUse(ATortugaCharacter* Turtle, const FTN_InventoryItem& Item);

	/** true si un golpe de TcT puede mover ahora a Turtle (viva, sin ir en brazos de otra; bPush: además, fuera del caparazón). */
	TORTUNABO_API bool CanAffect(const ATortugaCharacter* Turtle, bool bPush);

	/** Servidor: un sonido de los objetos (los sintetizados de la carrera) en la tortuga, para todas las máquinas. */
	TORTUNABO_API void PlayCue(ACharacter* Turtle, ETNRaceSound Sound, float Pitch = 1.f);

	/** Las tortugas vivas del mundo, sin Except. */
	TORTUNABO_API void GatherTurtles(const UObject* WorldContext, const AActor* Except, TArray<ATortugaCharacter*>& Out);
}
