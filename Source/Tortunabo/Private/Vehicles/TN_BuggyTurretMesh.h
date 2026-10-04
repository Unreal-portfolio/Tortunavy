// Torreta del buggy construida en ejecución (#435): aro fijo a la altura de la cadera de la artillera, sobre dos patas
// que bajan al suelo de la carrocería trasera; carro que gira solo en guiñada con un poste que sube por detrás de la artillera hasta el cubo del eje de
// cabeceo, por encima de su cabeza; cañón con cuerpo, escudo y brazo lateral que gira y cabecea con el apuntado, y caña
// con boca ensanchada que toma el tinte de la munición. Estilo de juguete a juego con SM_TN_BuggyBody
// (Art/Source/Vehicles/Buggy/build_buggy.py).
//
// Ejes de la torreta (los de UTN_BuggyTurretComponent): origen en el pivote, X hacia el apuntado, Y a su derecha, Z arriba.
// El pivote va UTN_BuggyTurretComponent::PivotRaiseCm por encima de Muzzle_Gunner: el cañón queda por encima de la
// cabeza de la artillera, a su derecha (MuzzleSideCm), y apuntando atrás pasa por encima del arco trasero. Las cotas
// salen de la artillera sentada medida en el juego (TN.Rally.DebugTurretFit, TN_Buggy_TurretFit.cpp). Los tests en
// Tortunabo.Rally.Turret.
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "World/ProcMap/TN_ProcMapMeshKit.h"

namespace TNBuggyTurretMesh
{
	/** Cotas de SM_TN_BuggyBody respecto al pivote (build_buggy.py: ROLL_Y, GUNNER_RAIL_Z, REAR_HOOP_X/TOP, BAR_R). */
	constexpr double SeatZ = -static_cast<double>(UTN_BuggyTurretComponent::PivotAboveSeatCm);
	constexpr double RailY = 55.0;
	constexpr double RailZ = SeatZ + 145.0 - 127.38;
	constexpr double BarRadius = 4.0;
	/** Travesaño del arco trasero: 42 cm detrás del pivote. */
	constexpr double RearBarX = -42.0;
	constexpr double RearBarZ = SeatZ + 175.0 - 127.38;
	/** Respaldo de la artillera: hasta 35 cm del eje y 27,4 cm por encima del asiento. */
	constexpr double BackrestRadius = 35.0;
	constexpr double BackrestTopZ = SeatZ + 14.6;
	/** Suelo de la carrocería trasera bajo la artillera (GUNNER_FLOOR_Z): ahí apoyan las patas del aro. */
	constexpr double FloorZ = SeatZ + 90.0 - 127.38;

	/**
	 * Aro fijo RingDropCm por debajo del asiento: por fuera del respaldo y de la cadera y el caparazón de la artillera (33
	 * cm del eje), por debajo de sus brazos también encogida en el salto (#435) y por dentro de las barandillas. Se apoya
	 * en dos patas a los lados de la cadera que bajan al suelo: los tirantes a las barandillas quedaban donde la
	 * artillera apoya los antebrazos.
	 */
	constexpr double RingDropCm = 4.0;
	constexpr double RingZ = SeatZ - RingDropCm;
	constexpr double RingRadius = 44.0;
	constexpr double RingTube = 2.2;

	/**
	 * Poste del carro: sale del aro detrás del apuntado (MountAngleDeg, un poco a la derecha), lejos de la cabeza, que mira
	 * hacia el apuntado; sube por fuera del respaldo, se mete a PostInnerRadius solo a la altura del travesaño del arco
	 * trasero (entre la nuca y el travesaño) y llega al cubo por encima de la cabeza. PostInnerRadius no puede crecer (el
	 * travesaño corta todo círculo de más radio a esa altura): el ángulo, el codo alto y el grosor dejan libre la cara de
	 * la artillera cuando apunta atrás y el poste le queda delante (#435, TN.Rally.DebugTurretFit).
	 */
	constexpr double MountAngleDeg = 166.0;
	constexpr double PostHalf = 1.6;
	constexpr double PostInnerRadius = 35.0;
	constexpr double PostBendLowZ = SeatZ + 29.0;
	constexpr double PostInnerLowZ = SeatZ + 42.0;
	constexpr double PostInnerHighZ = SeatZ + 54.0;
	constexpr double PostTopRadius = 39.0;

	/** Cubo del eje de cabeceo (pasa por el pivote) a la derecha del cañón. */
	constexpr double HubInnerY = 36.0;
	constexpr double HubOuterY = 44.0;
	constexpr double HubRadius = 5.5;

	/** Cuerpo del cañón, escudo, brazo lateral y caña (cm, ejes de la torreta). */
	constexpr double BodyMinX = 36.0;
	constexpr double BodyMaxX = 46.0;
	constexpr double ShieldX = 47.0;
	constexpr double ShieldMinY = 10.0;
	constexpr double ArmY = 35.0;
	/**
	 * Brazo lateral (ArmZ) y cuerpo, culata, tolva y escudo (BodyZ) por encima del eje de la caña: libres de la mano
	 * derecha levantada también con el cabeceo mínimo (sus dedos llegan a 0,3 cm sobre el eje; TN.Rally.DebugTurretFit).
	 */
	constexpr double ArmZ = 3.0;
	constexpr double BodyZ = 8.5;
	constexpr double BarrelRadius = 4.2;
	constexpr double BellStartX = 56.0;
	constexpr double MouthRadius = 7.5;

	/** Punto del poste del carro (ejes de la torreta sin cabeceo) a Radius del eje y altura Z. */
	FVector PostPoint(double Radius, double Z);

	/** Aro y sus dos patas al suelo: va fijo en el chasis, no gira. */
	void BuildRing(TNProcMesh::FTNProcMeshBuffers& Out);

	/** Carro sobre el aro, poste, brazo hasta el cubo y cubo del eje de cabeceo: gira solo en guiñada. */
	void BuildMount(TNProcMesh::FTNProcMeshBuffers& Out);

	/** Brazo lateral, cuerpo, mira y escudo: giran y cabecean con el apuntado. Colores propios (sin tinte). */
	void BuildGun(TNProcMesh::FTNProcMeshBuffers& Out);

	/** Caña y boca ensanchada, centradas en (MuzzleDistanceCm, MuzzleSideCm, 0): llevan el tinte de la munición. */
	void BuildBarrel(TNProcMesh::FTNProcMeshBuffers& Out);
}
