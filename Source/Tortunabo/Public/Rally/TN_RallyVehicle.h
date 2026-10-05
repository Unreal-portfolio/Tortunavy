// Contrato entre el buggy (Vehicles/) y la lógica de carrera del Rally (Rally/).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TN_RallyVehicle.generated.h"

class AController;

/** Plazas del buggy biplaza: la conductora posee el buggy y la artillera maneja la torreta. */
UENUM(BlueprintType)
enum class ETNRallySeat : uint8
{
	Driver,
	Gunner
};

/**
 * Munición de la torreta. Coco es la básica (infinita, con calentamiento); el resto son cargas
 * especiales que dan las cajas «?» (ATN_KartItemBox, #629) según el puesto. Todas empujan al buggy propio
 * con su retroceso. Concha y ConchaGuiada no vuelan: corren pegadas al suelo (ATN_KartShell). Los valores
 * se añaden al final para no cambiar los que ya están guardados o replicados.
 */
UENUM(BlueprintType)
enum class ETNRallyAmmo : uint8
{
	None,
	Coco,
	Alga,
	Burbuja,
	Mortero,
	Tinta,
	Ancla,
	/** Concha que corre recta por el suelo, rebota en las paredes y hace trompear al primero que toca. */
	Concha,
	/** Concha que persigue al buggy de justo delante. */
	ConchaGuiada,
	/** Ráfaga de erizos (#715): una carga son 12 púas rápidas mientras se mantiene el gatillo. */
	Erizos
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTN_RallyVehicle : public UInterface
{
	GENERATED_BODY()
};

/**
 * Lo que la carrera necesita del vehículo. Las funciones que cambian estado solo se llaman en el
 * servidor; los getters valen en cualquier máquina.
 */
class TORTUNABO_API ITN_RallyVehicle
{
	GENERATED_BODY()

public:
	/** Sienta al controlador en la plaza pedida (Driver = posee el buggy). False si está ocupada. */
	virtual bool SeatController(AController* Controller, ETNRallySeat Seat) = 0;

	/** Saca al controlador de su plaza. Si sale la conductora y hay artillera, la artillera pasa a conducir. */
	virtual void UnseatController(AController* Controller) = 0;

	virtual AController* GetSeatController(ETNRallySeat Seat) const = 0;
	virtual bool HasFreeSeat(ETNRallySeat Seat) const = 0;

	/** Reaparición: coloca el buggy, anula velocidades, lo deja inmóvil LockSeconds y fantasma GhostSeconds. */
	virtual void RallyTeleport(const FTransform& Where, float LockSeconds, float GhostSeconds) = 0;

	/** Corta el motor (semáforo, salida anticipada, fin de carrera). */
	virtual void SetEngineLocked(bool bLocked) = 0;

	virtual float GetForwardSpeedCms() const = 0;
	virtual bool IsFlipped() const = 0;

	/** Ángulo máximo de la rueda (grados) con el que el piloto IA acota la dirección para no volcar; 0 = sin dato (no la acota). */
	virtual float GetMaxSteerAngleDeg() const { return 0.f; }

	/** Índice del equipo (un buggy = un equipo) que asigna el GameMode; replicado para colores y HUD. */
	virtual int32 GetRallyTeamIndex() const = 0;
	virtual void SetRallyTeamIndex(int32 Index) = 0;

	/** Carga de munición especial al pasar por una caja; sustituye a la que hubiera. */
	virtual void GiveSpecialAmmo(ETNRallyAmmo Ammo, int32 Charges) = 0;
	virtual ETNRallyAmmo GetSpecialAmmo() const = 0;

	/** Mando del piloto IA (sin Enhanced Input): acelerador y freno 0..1, dirección -1..1. */
	virtual void SetAIDriveInput(float Throttle, float Brake, float Steer, bool bHandbrake) = 0;

	/** Disparo del piloto IA o de la conductora sola: dirección de apuntado en mundo. */
	virtual void AIFire(const FVector& AimWorldDir, bool bSpecial) = 0;

	/**
	 * Solo servidor: true una sola vez cuando una ocupante ha mantenido R (o Y) 1,5 s para pedir la reaparición. La
	 * carrera lo consulta en su bucle y hace la reaparición con RallyTeleport. Por defecto, nunca.
	 */
	virtual bool ConsumeRespawnRequest() { return false; }

	/**
	 * Solo servidor: true una sola vez cuando el vehículo ha caído bajo el KillZ del mundo. La carrera lo devuelve a la
	 * pista (último arco o parrilla) en cualquier fase y sin esperar a la inmunidad de la reaparición.
	 */
	virtual bool ConsumeFellOutOfWorld() { return false; }

	/**
	 * Solo servidor: true una sola vez cuando el vehículo ha reventado (vida 0). La carrera lo hace reaparecer siempre, en
	 * cualquier fase y sin esperar a la inmunidad, con el motivo ETNRallyRespawnReason::Destroyed.
	 */
	virtual bool ConsumeDestroyed() { return false; }

	/** Solo servidor: bloquea la torreta (calentamiento, semáforo, resultados o equipo retirado). Por defecto, nada. */
	virtual void SetWeaponsLocked(bool bLocked) {}

	/**
	 * Solo servidor: freno de carrera (parrilla durante el semáforo). Mientras está puesto, el vehículo no acelera ni usa
	 * el turbo y frena a fondo; la salida lo suelta. Por defecto, nada.
	 */
	virtual void SetRaceBrakeHeld(bool bHeld) {}
};
