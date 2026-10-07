// Lógica pura de quién mueve a la tortuga en la carrera (ronda 4, tarea 1: «segunda gaviota + caparazón = torbellino»):
// el árbitro (TNBeach::ResolveMover, TNBeach::CanStunOver, TN_BeachStun.h) y la sujeción de los enemigos
// (ATN_BeachEnemy::CanHoldTurtle). Sin mundo ni actores: se recorre lo que ve cada sistema en dos (y cinco) agarres
// seguidos, con y sin meterse en el caparazón colgando del pico, y se comprueba que nunca hay dos que la mueven a la vez y
// que cada agarre deja todo como estaba (el segundo es igual que el primero).
// Correr desde Session Frontend (categoría "Tortunabo.Beach.Hold") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.Hold; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachStun.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachHoldTest
{
	using TNBeach::ETNBeachMover;
	using TNBeach::FTNMoverView;

	/** Lo que se ve de la tortuga en una máquina: lo del árbitro y lo que mira la sujeción. */
	struct FTurtleSeen
	{
		FTNMoverView Mover;
		/** Su caja del caparazón enganchada en esta máquina (puede llegar antes o después que bInShell). */
		bool bHasLocalBody = false;
		bool bRagdoll = false;
		bool bDead = false;
	};

	bool CanHold(const FTurtleSeen& Seen)
	{
		return ATN_BeachEnemy::CanHoldTurtle(Seen.Mover.bInShell, Seen.bHasLocalBody, Seen.bRagdoll, Seen.Mover.bCarried, Seen.bDead);
	}

	/** Dos que la mueven a la vez: un enemigo la coloca en su pico y su caja la arrastra (el torbellino). */
	bool TwoDrivers(const FTurtleSeen& Seen)
	{
		return Seen.Mover.bHeld && (Seen.Mover.bInShell || Seen.bHasLocalBody || Seen.bRagdoll);
	}

	/**
	 * Un agarre de gaviota entero, como lo hace ahora el servidor: la coge (CanHold), la lleva, y o bien la suelta al acabar
	 * el vuelo (primero EndHoldTurtle, luego StunTurtle: la bola), o bien la tortuga se mete en el caparazón colgando
	 * (SlipFromHolder: primero la suelta y luego nace la bola). Después la bola acaba y sale. Devuelve false (con Why) en
	 * cuanto algo no cuadra.
	 */
	bool RunGrab(FTurtleSeen& Seen, bool bEntersShellWhileHeld, FString& Why)
	{
		if (!CanHold(Seen) || TNBeach::ResolveMover(Seen.Mover) != ETNBeachMover::None)
		{
			Why = TEXT("antes del agarre no estaba libre");
			return false;
		}
		// La coge.
		Seen.Mover.bHeld = true;
		if (TNBeach::ResolveMover(Seen.Mover) != ETNBeachMover::Held)
		{
			Why = TEXT("sujeta, el árbitro no dice «un enemigo»");
			return false;
		}
		// Mientras cuelga, nada la mete en bola ni la derriba (una mina, una patada, un mareo).
		if (TNBeach::CanStunOver(TNBeach::ResolveMover(Seen.Mover)))
		{
			Why = TEXT("sujeta, se la podía aturdir (bola con el enemigo aún colocándola)");
			return false;
		}
		// Tanto si se mete en el caparazón colgando (SlipFromHolder) como al acabar el vuelo (EndHoldTurtle y luego StunTurtle),
		// quien la sujeta la suelta ANTES de que nazca la bola: el mismo camino las dos veces.
		(void)bEntersShellWhileHeld;
		Seen.Mover.bHeld = false;
		if (!TNBeach::CanStunOver(TNBeach::ResolveMover(Seen.Mover)))
		{
			Why = TEXT("soltada, no se la podía aturdir");
			return false;
		}
		// La bola (en esta máquina, la caja puede llegar antes que el estado replicado o después).
		Seen.bHasLocalBody = true;
		if (TwoDrivers(Seen) || CanHold(Seen))
		{
			Why = TEXT("con la caja enganchada, un enemigo aún podía colocarla");
			return false;
		}
		Seen.Mover.bInShell = true;
		if (TNBeach::ResolveMover(Seen.Mover) != ETNBeachMover::Ball || TwoDrivers(Seen) || CanHold(Seen))
		{
			Why = TEXT("en bola, el árbitro no dice «su bola» o un enemigo aún podía cogerla");
			return false;
		}
		// Se para y sale.
		Seen.Mover.bInShell = false;
		Seen.bHasLocalBody = false;
		return true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Árbitro
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachHoldArbiterTest,
	"Tortunabo.Beach.Hold.Arbiter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachHoldArbiterTest::RunTest(const FString& Parameters)
{
	using namespace TNBeach;

	FTNMoverView View;
	TestEqual(TEXT("Nada: su propio movimiento"), static_cast<int32>(ResolveMover(View)), static_cast<int32>(ETNBeachMover::None));

	View.bInShell = true;
	TestEqual(TEXT("En el caparazón: su bola"), static_cast<int32>(ResolveMover(View)), static_cast<int32>(ETNBeachMover::Ball));
	View.bHeld = true;
	TestEqual(TEXT("Sujeta y en el caparazón a la vez (un cliente con la bola por delante): manda el enemigo"),
		static_cast<int32>(ResolveMover(View)), static_cast<int32>(ETNBeachMover::Held));
	View.Claim = ETNBeachMover::SafetyNet;
	TestEqual(TEXT("La reserva de la red de seguridad manda sobre el enemigo"), static_cast<int32>(ResolveMover(View)),
		static_cast<int32>(ETNBeachMover::SafetyNet));

	{
		FTNMoverView Launched;
		Launched.bFallImmune = true;
		TestEqual(TEXT("Por el aire con la caída inmune: un lanzamiento"), static_cast<int32>(ResolveMover(Launched)),
			static_cast<int32>(ETNBeachMover::Launch));
		Launched.bKnockedDown = true;
		TestEqual(TEXT("Derribada manda sobre el lanzamiento"), static_cast<int32>(ResolveMover(Launched)),
			static_cast<int32>(ETNBeachMover::Knockdown));
	}

	TestTrue(TEXT("Aturdir puede con su movimiento"), CanStunOver(ETNBeachMover::None));
	TestTrue(TEXT("Aturdir puede con su bola (se alarga)"), CanStunOver(ETNBeachMover::Ball));
	TestTrue(TEXT("Aturdir puede con el derribo (se levanta)"), CanStunOver(ETNBeachMover::Knockdown));
	TestTrue(TEXT("Aturdir puede con un lanzamiento (catapulta)"), CanStunOver(ETNBeachMover::Launch));
	TestTrue(TEXT("Aturdir puede con otra que la lleva (la suelta)"), CanStunOver(ETNBeachMover::Carried));
	TestFalse(TEXT("Aturdir no puede con un enemigo que la sujeta (la suelta él antes)"), CanStunOver(ETNBeachMover::Held));
	TestFalse(TEXT("Aturdir no puede con la patada de la tormenta"), CanStunOver(ETNBeachMover::StormKick));
	TestFalse(TEXT("Aturdir no puede con la red de seguridad"), CanStunOver(ETNBeachMover::SafetyNet));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sujeción: nunca a la vez que otra cosa la mueve
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachHoldCanHoldTest,
	"Tortunabo.Beach.Hold.CanHold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachHoldCanHoldTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Libre: se la puede sujetar"), ATN_BeachEnemy::CanHoldTurtle(false, false, false, false, false));
	TestFalse(TEXT("En su caparazón (estado replicado): no"), ATN_BeachEnemy::CanHoldTurtle(true, false, false, false, false));
	TestFalse(TEXT("Con su caja enganchada aquí aunque el estado aún no haya llegado: no"),
		ATN_BeachEnemy::CanHoldTurtle(false, true, false, false, false));
	TestFalse(TEXT("En ragdoll: no"), ATN_BeachEnemy::CanHoldTurtle(false, false, true, false, false));
	TestFalse(TEXT("En brazos de otra: no"), ATN_BeachEnemy::CanHoldTurtle(false, false, false, true, false));
	TestFalse(TEXT("Muerta: no"), ATN_BeachEnemy::CanHoldTurtle(false, false, false, false, true));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dos (y cinco) agarres seguidos, con y sin caparazón
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachHoldRepeatedGrabsTest,
	"Tortunabo.Beach.Hold.RepeatedGrabs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachHoldRepeatedGrabsTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachHoldTest;

	// El caso del fallo: la primera suelta al acabar el vuelo, la segunda metiéndose en el caparazón colgando.
	{
		FTurtleSeen Seen;
		FString Why;
		const bool bFirst = RunGrab(Seen, false, Why);
		TestTrue(FString::Printf(TEXT("Primer agarre (suelta al acabar el vuelo) %s"), *Why), bFirst);
		const bool bSecond = RunGrab(Seen, true, Why);
		TestTrue(FString::Printf(TEXT("Segundo agarre, metiéndose en el caparazón colgando del pico %s"), *Why), bSecond);
		TestTrue(TEXT("Tras los dos, libre como al principio"),
			CanHold(Seen) && TNBeach::ResolveMover(Seen.Mover) == TNBeach::ETNBeachMover::None && !Seen.bHasLocalBody);
	}

	// Cinco seguidos alternando: ninguno deja nada a medias (da igual cuántas veces te cojan).
	{
		FTurtleSeen Seen;
		for (int32 Grab = 0; Grab < 5; ++Grab)
		{
			FString Why;
			const bool bShell = (Grab % 2) == 1;
			const bool bOk = RunGrab(Seen, bShell, Why);
			TestTrue(FString::Printf(TEXT("Agarre %d (%s) %s"), Grab + 1, bShell ? TEXT("con caparazón") : TEXT("sin caparazón"), *Why), bOk);
		}
	}

	// En un cliente la caja puede llegar antes que la suelta del enemigo: en cuanto la hay, la sujeción ya no puede seguir.
	{
		FTurtleSeen Seen;
		Seen.Mover.bHeld = true;
		Seen.bHasLocalBody = true;
		TestTrue(TEXT("Cliente con la caja por delante: dos que la mueven…"), TwoDrivers(Seen));
		TestFalse(TEXT("…y la sujeción la suelta (CanHoldTurtle falso: PlaceHeldTurtle la deja)"), CanHold(Seen));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
