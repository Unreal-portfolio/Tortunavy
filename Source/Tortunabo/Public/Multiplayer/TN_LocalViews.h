#pragma once

#include "CoreMinimal.h"

class APawn;
class APlayerController;
class UWorld;

/**
 * Los jugadores que miran en esta máquina. En red es uno; en el modo local (#311), hasta cuatro a pantalla partida. Para lo
 * que antes miraba «el primer jugador» (GetFirstPlayerController, GetPlayerCameraManager(0)): qué cámara está cerca de un
 * sonido o de un efecto, a qué tortuga mira un tendero, qué tortuga ve un marcador... Así cada vista funciona igual.
 */
namespace TNLocalViews
{
	/** Los PlayerController locales de World, el del jugador 1 primero (el orden de la GameInstance). */
	TORTUNABO_API void GetLocalControllers(const UWorld* World, TArray<APlayerController*>& Out);

	/** Cuántos jugadores locales hay (vistas en pantalla). */
	TORTUNABO_API int32 NumLocalPlayers(const UWorld* World);

	/** La cámara local más cercana a At: su posición (y giro, y su jugador, si se piden). false sin ninguna. */
	TORTUNABO_API bool ClosestCamera(const UWorld* World, const FVector& At, FVector& OutLocation, FRotator* OutRotation = nullptr,
		APlayerController** OutController = nullptr);

	/** Distancia de At a la cámara local más cercana (1e9 si no hay ninguna). */
	TORTUNABO_API double ClosestCameraDistance(const UWorld* World, const FVector& At);

	/** La tortuga (el peón) de un jugador local más cercana a At; null si no hay. OutDistance: su distancia (1e9 sin ninguna). */
	TORTUNABO_API APawn* ClosestLocalPawn(const UWorld* World, const FVector& At, double* OutDistance = nullptr);

	/** true si Pawn es de un jugador de esta máquina. */
	TORTUNABO_API bool IsLocalPawn(const APawn* Pawn);
}
