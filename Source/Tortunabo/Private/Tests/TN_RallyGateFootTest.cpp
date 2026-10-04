// Patas de las puertas del Rally sobre el suelo (#665): el pórtico de una pieza se inclina hasta que pisan las dos patas y el
// arco provisional alarga cada pata hasta su suelo. Sin mundo: suelo plano inclinado (peralte de 15° y cuesta de 10°).
// Correr con -ExecCmds="Automation RunTests Tortunabo.Rally.Gate; Quit".

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyGate.h"
#include "Rally/TN_RallyTrackDressing.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyGateFootTest
{
	/** Suelo plano: sube BankDeg hacia la derecha de la carrera (+Y) y SlopeDeg hacia delante (+X), con 0 en el origen. */
	double GroundZ(const FVector& At, double BankDeg, double SlopeDeg)
	{
		return At.Y * FMath::Tan(FMath::DegreesToRadians(BankDeg)) + At.X * FMath::Tan(FMath::DegreesToRadians(SlopeDeg));
	}

	/** Distancia vertical de cada pie del pórtico apoyado con FitArchToFeet a su suelo (izquierda, derecha). */
	FVector2D ArchFootGaps(double BankDeg, double SlopeDeg, double FootOffset, double MaxRollDeg)
	{
		const double Left = GroundZ(FVector(0.0, -FootOffset, 0.0), BankDeg, SlopeDeg);
		const double Right = GroundZ(FVector(0.0, FootOffset, 0.0), BankDeg, SlopeDeg);
		const TNRallyDressing::FArchFit Fit = TNRallyDressing::FitArchToFeet(Left, Right, FootOffset, MaxRollDeg);
		const FVector Base(0.0, 0.0, Fit.BaseZ);
		// El pórtico derecho apoyado en Base: sus pies, a FootOffset a cada lado y a la cota de Base.
		const FTransform Upright(FQuat::Identity, Base);
		const FTransform Placed = TNRallyDressing::RollAboutBase(Upright, Base, FVector::ForwardVector, Fit.RollDeg);
		const FVector LeftFoot = Placed.TransformPosition(FVector(0.0, -FootOffset, 0.0));
		const FVector RightFoot = Placed.TransformPosition(FVector(0.0, FootOffset, 0.0));
		return FVector2D(LeftFoot.Z - GroundZ(LeftFoot, BankDeg, SlopeDeg), RightFoot.Z - GroundZ(RightFoot, BankDeg, SlopeDeg));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyGateArchFeetTest, "Tortunabo.Rally.Gate.ArchFeetOnGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyGateArchFeetTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyGateFootTest;
	// Medio vano de la puerta (12 m + 1 m de margen) por la fracción de las patas del pórtico.
	const double FootOffset = 1300.0 * 0.92;
	const struct { const TCHAR* Name; double Bank; double Slope; } Cases[] = {
		{ TEXT("llano"), 0.0, 0.0 }, { TEXT("peralte 15°"), 15.0, 0.0 }, { TEXT("peralte -15°"), -15.0, 0.0 },
		{ TEXT("cuesta 10°"), 0.0, 10.0 }, { TEXT("peralte 15° y cuesta 10°"), 15.0, 10.0 } };
	for (const auto& Case : Cases)
	{
		const FVector2D Gaps = ArchFootGaps(Case.Bank, Case.Slope, FootOffset, 20.0);
		TestTrue(FString::Printf(TEXT("%s: pata izquierda a menos de 5 cm del suelo (%.1f cm)"), Case.Name, Gaps.X), FMath::Abs(Gaps.X) < 5.0);
		TestTrue(FString::Printf(TEXT("%s: pata derecha a menos de 5 cm del suelo (%.1f cm)"), Case.Name, Gaps.Y), FMath::Abs(Gaps.Y) < 5.0);
	}
	// Más desnivel del que admite la inclinación: ninguna pata flota (la alta queda enterrada).
	const FVector2D Steep = ArchFootGaps(35.0, 0.0, FootOffset, 20.0);
	TestTrue(TEXT("Desnivel de 35°: ninguna pata flota"), Steep.X <= 0.5 && Steep.Y <= 0.5);
	TestTrue(TEXT("Desnivel de 35°: la pata baja pisa"), FMath::Min(FMath::Abs(Steep.X), FMath::Abs(Steep.Y)) < 5.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyGatePostDropTest, "Tortunabo.Rally.Gate.PostReachesGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyGatePostDropTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyGateFootTest;
	// Arco provisional girado con el peralte de 15°: el pie de cada pata (a 1290 cm del centro) sobre un talud que sigue en
	// llano fuera de la calzada (suelo a 0) y en una cuesta de 10° hacia delante.
	const double Bank = 15.0;
	const FQuat Rotation(FVector::ForwardVector, FMath::DegreesToRadians(Bank));
	for (const double Sign : { -1.0, 1.0 })
	{
		const FVector Foot = Rotation.RotateVector(FVector(0.0, Sign * 1290.0, 0.0));
		const double Ground = GroundZ(FVector(Foot.X, Foot.Y, 0.0), 0.0, 10.0);
		const double UpZ = Rotation.GetUpVector().Z;
		const double Drop = TNRallyGateFit::FootDropAlongPost(Foot.Z, Ground, UpZ, 1500.0);
		const double ReachedZ = Foot.Z - Drop * UpZ;
		const bool bFloating = Foot.Z > Ground;
		TestTrue(FString::Printf(TEXT("Pata %s: su pie llega a menos de 5 cm del suelo (%.1f cm)"), Sign < 0.0 ? TEXT("izquierda") : TEXT("derecha"),
			ReachedZ - Ground), bFloating ? FMath::Abs(ReachedZ - Ground) < 5.0 : Drop == 0.0);
	}
	TestEqual(TEXT("Suelo por encima del pie: no se alarga"), TNRallyGateFit::FootDropAlongPost(0.0, 30.0, 1.0, 1500.0), 0.0);
	TestEqual(TEXT("Nunca más del tope"), TNRallyGateFit::FootDropAlongPost(5000.0, 0.0, 1.0, 1500.0), 1500.0);
	return true;
}

#endif
