#include "Settings/TN_HazardTuning.h"

UTN_HazardTuning::UTN_HazardTuning()
{
	// Valores de la hoja EnemyAndObstacleData (plan maestro §3). Los supuestos (duración del veneno, altura de la caída de la
	// gaviota) están anotados en #871. Config/DefaultGame.ini los repite: manda el .ini.
	GullDrop.bKills = true;
	GullDrop.DeathCause = ETNDeathCause::Seagull;

	SeagullDropping.Damage = 30.f;
	SeagullDropping.DeathCause = ETNDeathCause::SeagullDropping;

	CrabContact.Damage = 20.f;
	CrabContact.DeathCause = ETNDeathCause::Crab;

	DragCrab.bKills = true;
	DragCrab.DeathCause = ETNDeathCause::Crab;

	JellyfishTentacles.PoisonDamagePerSecond = 5.f;
	JellyfishTentacles.PoisonSeconds = 4.f;
	JellyfishTentacles.DeathCause = ETNDeathCause::Poison;

	Urchin.Damage = 15.f;
	Urchin.PoisonDamagePerSecond = 5.f;
	Urchin.PoisonSeconds = 4.f;

	BurrowCrab.Damage = 40.f;
	BurrowCrab.DeathCause = ETNDeathCause::Crab;

	Quicksand.bKills = true;
	Quicksand.DeathCause = ETNDeathCause::Quicksand;

	Quad.bKills = true;
	Quad.DeathCause = ETNDeathCause::Quad;

	TankTrap.Damage = 15.f;
}
