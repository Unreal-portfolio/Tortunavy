// Derribo de la tortuga que va en brazos de otra (#68): la regla de TN_CarryRules.h que usa ATortugaCharacter::ApplyKnockdown.
// Sin mundo ni actores: se recorre lo que hace el servidor con dos y tres tortugas (coger, el golpe de un lanzable de un
// tercero, levantarse, volver a cogerla, el DBNO, el portador derribado) y se comprueba en cada paso que portador y llevada
// quedan en un estado coherente: nadie anda enganchado a otra, el enganche es de los dos lados y una derribada no va en brazos.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Carry; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/EngineTypes.h"
#include "Player/TN_CarryRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNCarryKnockdownTest
{
	/** Lo que el servidor sabe de cada tortuga (UTN_CarryComponent, UTN_ShellComponent, el derribo y el movimiento). */
	struct FTurtle
	{
		/** CarriedBy y CarriedTurtle, como índices (INDEX_NONE = nadie). */
		int32 CarriedBy = INDEX_NONE;
		int32 Carrying = INDEX_NONE;
		bool bInShell = false;
		bool bExitLocked = false;
		bool bKnockedDown = false;
		/** Enganchada encima del portador (AttachToActor). */
		bool bAttached = false;
		EMovementMode Move = MOVE_Walking;
	};

	using FTurtles = TArray<FTurtle>;

	/** UTN_CarryComponent::Release: suelta a la que lleva (dejada, sin lanzar). En el caparazón cae como bola. */
	void Release(FTurtles& T, int32 Carrier)
	{
		const int32 Carried = T[Carrier].Carrying;
		if (Carried == INDEX_NONE)
		{
			return;
		}
		T[Carrier].Carrying = INDEX_NONE;
		FTurtle& Other = T[Carried];
		Other.CarriedBy = INDEX_NONE;
		Other.bAttached = false;
		Other.bExitLocked = false;
		// En el caparazón la mueve su caja (movimiento apagado); fuera, cae.
		Other.Move = Other.bInShell ? MOVE_None : MOVE_Falling;
	}

	/** ATortugaCharacter::RecoverFromKnockdown. */
	void Recover(FTurtles& T, int32 Who)
	{
		T[Who].bKnockedDown = false;
		T[Who].Move = MOVE_Walking;
	}

	/** UTN_CarryComponent::ServerGrab: solo en el caparazón o derribada; la derribada se levanta y se mete en él. */
	bool Grab(FTurtles& T, int32 Carrier, int32 Target)
	{
		FTurtle& Self = T[Carrier];
		FTurtle& Other = T[Target];
		if (Self.bKnockedDown || Self.bInShell || Self.Carrying != INDEX_NONE || Self.CarriedBy != INDEX_NONE
			|| !(Other.bInShell || Other.bKnockedDown) || Other.CarriedBy != INDEX_NONE || Other.Carrying != INDEX_NONE)
		{
			return false;
		}
		if (Other.bKnockedDown)
		{
			Recover(T, Target);
		}
		Other.bInShell = true;
		Other.bExitLocked = true;
		Self.Carrying = Target;
		Other.CarriedBy = Carrier;
		Other.bAttached = true;
		Other.Move = MOVE_None;
		return true;
	}

	/** ATortugaCharacter::ApplyKnockdown, con la regla de verdad. */
	void Knockdown(FTurtles& T, int32 Who)
	{
		FTurtle& Self = T[Who];
		// ForceExitShell.
		Self.bInShell = false;
		Self.bExitLocked = false;
		if (TNCarryRules::KnockdownDropsFromCarrier(Self.CarriedBy != INDEX_NONE))
		{
			Release(T, T[Who].CarriedBy);
		}
		if (T[Who].bKnockedDown)
		{
			return;
		}
		T[Who].bKnockedDown = true;
		// El ragdoll apaga el movimiento.
		T[Who].Move = MOVE_None;
	}

	/** UTN_CarryComponent::TickComponent del portador: derribado, suelta lo que lleva. */
	void CarrierTick(FTurtles& T, int32 Carrier)
	{
		if (T[Carrier].Carrying != INDEX_NONE && T[Carrier].bKnockedDown)
		{
			Release(T, Carrier);
		}
	}

	/** Estado coherente de todas; si no, Why dice qué falla. */
	bool IsConsistent(const FTurtles& T, FString& Why)
	{
		for (int32 i = 0; i < T.Num(); ++i)
		{
			const FTurtle& Self = T[i];
			if (Self.CarriedBy != INDEX_NONE && T[Self.CarriedBy].Carrying != i)
			{
				Why = FString::Printf(TEXT("%d lleva CarriedBy = %d, pero esa no la lleva"), i, Self.CarriedBy);
				return false;
			}
			if (Self.Carrying != INDEX_NONE && T[Self.Carrying].CarriedBy != i)
			{
				Why = FString::Printf(TEXT("%d lleva a %d, pero esa no tiene CarriedBy = %d"), i, Self.Carrying, i);
				return false;
			}
			if (Self.CarriedBy != INDEX_NONE && Self.Move != MOVE_None)
			{
				Why = FString::Printf(TEXT("%d va en brazos con movimiento propio (modo %d)"), i, static_cast<int32>(Self.Move));
				return false;
			}
			if (Self.bAttached != (Self.CarriedBy != INDEX_NONE))
			{
				Why = FString::Printf(TEXT("%d: enganchada sin ir en brazos o al revés"), i);
				return false;
			}
			if (Self.bKnockedDown && Self.CarriedBy != INDEX_NONE)
			{
				Why = FString::Printf(TEXT("%d está derribada y sigue en brazos de %d"), i, Self.CarriedBy);
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNCarryKnockdownTest,
	"Tortunabo.Carry.KnockdownWhileCarried",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNCarryKnockdownTest::RunTest(const FString& Parameters)
{
	using namespace TNCarryKnockdownTest;
	constexpr int32 A = 0;
	constexpr int32 B = 1;
	FTurtles T;
	auto CheckConsistent = [this, &T](const TCHAR* Step)
	{
		FString Why;
		const bool bOk = IsConsistent(T, Why);
		TestTrue(bOk ? FString(Step) : FString::Printf(TEXT("%s: %s"), Step, *Why), bOk);
	};

	TestTrue(TEXT("En brazos de otra, el derribo la suelta"), TNCarryRules::KnockdownDropsFromCarrier(true));
	TestFalse(TEXT("Sin nadie que la lleve, nada que soltar"), TNCarryRules::KnockdownDropsFromCarrier(false));

	// A coge a B (metida en el caparazón) y el lanzable de C la derriba en sus brazos.
	T.SetNum(3);
	T[B].bInShell = true;
	T[B].Move = MOVE_None;
	TestTrue(TEXT("A coge a B en su caparazón"), Grab(T, A, B));
	CheckConsistent(TEXT("Llevándola"));
	Knockdown(T, B);
	CheckConsistent(TEXT("Derribada en brazos"));
	TestEqual(TEXT("A ya no lleva a nadie"), T[A].Carrying, static_cast<int32>(INDEX_NONE));
	TestTrue(TEXT("B está derribada, fuera del caparazón y sin salida bloqueada"), T[B].bKnockedDown && !T[B].bInShell && !T[B].bExitLocked);

	// Se levanta: anda por su cuenta, sin nadie que la lleve (antes: MOVE_Walking con CarriedBy puesto).
	CarrierTick(T, A);
	Recover(T, B);
	CheckConsistent(TEXT("Levantada"));
	TestTrue(TEXT("B anda suelta"), T[B].Move == MOVE_Walking && T[B].CarriedBy == INDEX_NONE && !T[B].bAttached);

	// Derribada otra vez, A puede volver a cogerla (se levanta y se mete en el caparazón) y otro golpe la vuelve a soltar.
	Knockdown(T, B);
	TestTrue(TEXT("A vuelve a coger a B derribada"), Grab(T, A, B));
	CheckConsistent(TEXT("Llevándola otra vez"));
	Knockdown(T, B);
	CheckConsistent(TEXT("Segundo golpe en brazos"));

	// El DBNO (un derribo largo) también la suelta.
	Recover(T, B);
	T[B].bInShell = true;
	T[B].Move = MOVE_None;
	TestTrue(TEXT("A coge a B para el DBNO"), Grab(T, A, B));
	Knockdown(T, B);
	CheckConsistent(TEXT("DBNO en brazos"));

	// El portador derribado suelta a la que lleva (como antes, en su tick): cae como bola.
	Recover(T, B);
	T[B].bInShell = true;
	T[B].Move = MOVE_None;
	TestTrue(TEXT("A coge a B"), Grab(T, A, B));
	Knockdown(T, A);
	CarrierTick(T, A);
	CheckConsistent(TEXT("Portador derribado"));
	TestTrue(TEXT("B cae como bola, suelta"), T[B].bInShell && T[B].CarriedBy == INDEX_NONE && T[B].Move == MOVE_None);
	return true;
}

#endif
