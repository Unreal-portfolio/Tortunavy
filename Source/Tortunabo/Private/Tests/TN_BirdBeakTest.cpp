// El pico de las gaviotas y del pelícano de la playa (#738, TNBeachMeshes::BuildBirdParts): la cabeza lleva la mitad de
// arriba y la pieza que se abre, la de abajo; cerrado es el mismo pico de una pieza que el de las aves de la fauna. Antes
// iban el pico entero y, debajo, una mandíbula suelta que se veía como un segundo pico mal colocado.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.Birds; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachEnemyMeshes.h"
#include "World/ProcMap/TN_ProcMapFaunaMeshes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBirdBeakTest
{
	using TNProcMesh::FTNProcMeshBuffers;

	const FTNProcMeshBuffers* HeadMesh(const TArray<TNFauna::FTNFaunaPart>& Parts)
	{
		for (const TNFauna::FTNFaunaPart& Part : Parts)
		{
			if (Part.Bone == TNFauna::ETNFaunaBone::Head) { return &Part.Mesh; }
		}
		return nullptr;
	}

	FBox BoxOf(const TArray<FVector>& Verts, const FVector& Offset)
	{
		FBox Box(ForceInit);
		for (const FVector& V : Verts) { Box += V + Offset; }
		return Box;
	}

	/** Altura del eje del pico (de la base a la punta) en X. */
	double AxisZ(const TNBeachMeshes::FBirdGeom& G, double X)
	{
		const double T = FMath::Clamp((X - G.BeakBase.X) / FMath::Max(G.BeakTip.X - G.BeakBase.X, 1e-3), 0.0, 1.0);
		return FMath::Lerp(G.BeakBase.Z, G.BeakTip.Z, T);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBirdBeakTest,
	"Tortunabo.Beach.Birds.OneBeak",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBirdBeakTest::RunTest(const FString& Parameters)
{
	using namespace TNBirdBeakTest;
	for (const bool bPelican : { false, true })
	{
		const TCHAR* Bird = bPelican ? TEXT("Pelícano") : TEXT("Gaviota");
		const TNBeachMeshes::FBirdGeom G = TNBeachMeshes::BirdGeom(bPelican);
		TArray<TNFauna::FTNFaunaPart> Parts;
		TNFauna::FTNFaunaRig Rig;
		TNFauna::FTNFaunaBirdJaw Jaw;
		TNBeachMeshes::BuildBirdParts(bPelican, Parts, Rig, Jaw);
		TArray<TNFauna::FTNFaunaPart> WholeParts;
		TNFauna::FTNFaunaRig WholeRig;
		TNFauna::TNFaunaBuildSpecies(bPelican ? TNFauna::ETNFaunaSpecies::Pelican : TNFauna::ETNFaunaSpecies::Gull, WholeParts, WholeRig);
		const FTNProcMeshBuffers* Head = HeadMesh(Parts);
		const FTNProcMeshBuffers* WholeHead = HeadMesh(WholeParts);
		if (!TestNotNull(FString::Printf(TEXT("%s: cabeza"), Bird), Head) || !TestNotNull(FString::Printf(TEXT("%s: cabeza de la fauna"), Bird), WholeHead))
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("%s: tiene pico de abajo"), Bird), Jaw.Mesh.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s: el pico de abajo gira en la base del pico (donde lo colocan las aves, BirdGeom)"), Bird),
			Jaw.Pivot.Equals(G.BeakBase, 1e-4));

		// Cerrado: cabeza y pico de abajo ocupan lo mismo que la cabeza de una pieza (el pico y la bolsa, en su sitio).
		FBox Closed = BoxOf(Head->Verts, FVector::ZeroVector);
		Closed += BoxOf(Jaw.Mesh.Verts, Jaw.Pivot);
		const FBox Whole = BoxOf(WholeHead->Verts, FVector::ZeroVector);
		TestTrue(FString::Printf(TEXT("%s: cerrado, el mismo bulto que el pico de una pieza (%s / %s)"), Bird, *Closed.ToString(), *Whole.ToString()),
			Closed.Min.Equals(Whole.Min, 1e-3) && Closed.Max.Equals(Whole.Max, 1e-3));

		// En la parte de delante del pico (ya fuera de la cabeza, con el anillo de en medio): arriba solo la cabeza, abajo solo
		// la pieza que se abre.
		const double FrontX = FMath::Lerp(G.BeakBase.X, G.BeakTip.X, 0.45);
		const double Slack = G.BeakR * 0.3;
		bool bHeadUpperOnly = true;
		bool bWholeHasLower = false;
		for (const FVector& V : Head->Verts)
		{
			bHeadUpperOnly &= V.X <= FrontX || V.Z >= AxisZ(G, V.X) - Slack;
		}
		for (const FVector& V : WholeHead->Verts)
		{
			bWholeHasLower |= V.X > FrontX && V.Z < AxisZ(G, V.X) - Slack;
		}
		bool bJawLowerOnly = true;
		for (const FVector& V : Jaw.Mesh.Verts)
		{
			const FVector P = V + Jaw.Pivot;
			bJawLowerOnly &= P.X <= FrontX || P.Z <= AxisZ(G, P.X) + Slack;
		}
		TestTrue(FString::Printf(TEXT("%s: la cabeza solo lleva la mitad de arriba del pico"), Bird), bHeadUpperOnly);
		TestTrue(FString::Printf(TEXT("%s: la pieza que se abre solo lleva la mitad de abajo"), Bird), bJawLowerOnly);
		TestTrue(FString::Printf(TEXT("%s: las aves de la fauna (sin abrir el pico) lo siguen llevando entero"), Bird), bWholeHasLower);
	}

	// Las aves que no abren el pico (la fauna, ATN_EnemySeagull) no piden pico de abajo y la cabeza no cambia.
	TArray<TNFauna::FTNFaunaPart> Parts;
	TNFauna::FTNFaunaRig Rig;
	TNFauna::FTNFaunaBirdJaw Unused;
	TNFauna::TNFaunaBuildSpecies(TNFauna::ETNFaunaSpecies::Crab, Parts, Rig, &Unused);
	TestTrue(TEXT("Un animal sin pico deja vacío el pico de abajo"), Unused.Mesh.IsEmpty());
	return true;
}

#endif
