// Paso de quads en red (issue #353): la posición del quad sale de FTNQuadPass con el reloj de trampa compartido
// (FTNTrapClock). Con la misma hora del servidor, anfitrión y cliente ponen el quad en el mismo sitio aunque vayan a
// fotogramas distintos; una corrección de la hora del servidor no lo hace saltar; la hora de salida no pierde precisión
// con el mundo encendido muchas horas, y el servidor juzga a la tortuga de un cliente contra el quad que ese cliente veía.
// Correr desde Session Frontend (categoría "Tortunabo.Beach.QuadLane") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.QuadLane; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachQuadLane.h"
#include "World/Beach/TN_BeachTrapCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachQuadLaneTest
{
	FTNQuadPass MakePass(double PassTime, int8 Dir)
	{
		FTNQuadPass Pass;
		Pass.PassTime = PassTime;
		Pass.Dir = Dir;
		Pass.HalfLength = 14000.f;
		Pass.QuadHalfLen = 2800.f;
		return Pass;
	}

	/** Hace avanzar un reloj de trampa de From a To con pasos de Dt, alimentado con la hora del servidor exacta. */
	double RunClock(FTNTrapClock& Clock, double From, double To, double Dt)
	{
		double Server = From;
		Clock.AdvanceTo(Server, 0.f);
		while (Server < To - 1e-9)
		{
			const double Step = FMath::Min(Dt, To - Server);
			Server += Step;
			Clock.AdvanceTo(Server, static_cast<float>(Step));
		}
		return Clock.Now();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachQuadLaneSameServerTimeTest, "Tortunabo.Beach.QuadLane.SameServerTimeSamePosition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachQuadLaneSameServerTimeTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachQuadLaneTest;
	const FTNQuadPass Pass = MakePass(103.0, 1);

	// Anfitrión a 120 fps y cliente a 30 fps (con un último paso irregular): al llegar a la misma hora del servidor, mismo quad.
	FTNTrapClock Host;
	FTNTrapClock Client;
	const double HostNow = RunClock(Host, 100.0, 105.0, 1.0 / 120.0);
	const double ClientNow = RunClock(Client, 100.0, 105.0, 1.0 / 30.0 + 0.0007);
	TestTrue(TEXT("Los dos relojes marcan la hora del servidor"), FMath::IsNearlyEqual(HostNow, 105.0, 1e-6) && FMath::IsNearlyEqual(ClientNow, 105.0, 1e-6));

	float HostX = 0.f;
	float ClientX = 0.f;
	TestTrue(TEXT("El quad está pasando en el anfitrión"), Pass.QuadXAt(HostNow, HostX));
	TestTrue(TEXT("El quad está pasando en el cliente"), Pass.QuadXAt(ClientNow, ClientX));
	TestTrue(FString::Printf(TEXT("Desfase anfitrión-cliente < 10 cm (%.3f cm)"), FMath::Abs(HostX - ClientX)), FMath::Abs(HostX - ClientX) < 10.f);

	// A los 2 s de pasada, 84 m recorridos desde la palmera de salida.
	const float StartX = -(Pass.HalfLength + Pass.QuadHalfLen + FTNQuadPass::PalmMargin);
	TestTrue(TEXT("Recorre 42 m/s desde la palmera de salida"), FMath::IsNearlyEqual(HostX, StartX + 2.f * FTNQuadPass::Speed, 1.f));

	// En sentido contrario, la misma hora da la posición reflejada.
	float BackX = 0.f;
	TestTrue(TEXT("Pasada hacia -X"), MakePass(103.0, -1).QuadXAt(HostNow, BackX));
	TestTrue(TEXT("La pasada hacia -X es la reflejada"), FMath::IsNearlyEqual(BackX, -HostX, 1.f));

	// Fuera de la pasada (antes de salir y al acabar) no hay quad.
	float Unused = 0.f;
	TestFalse(TEXT("Antes de la hora de salida no hay quad"), Pass.QuadXAt(102.99, Unused));
	TestFalse(TEXT("Después de meterse en las palmeras no hay quad"), Pass.QuadXAt(103.0 + Pass.TravelSeconds() + 0.01, Unused));
	TestFalse(TEXT("Sin pasada programada no hay quad"), MakePass(-1.0, 1).QuadXAt(105.0, Unused));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachQuadLaneClockCorrectionTest, "Tortunabo.Beach.QuadLane.ClockCorrectionNoJump",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachQuadLaneClockCorrectionTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachQuadLaneTest;
	const FTNQuadPass Pass = MakePass(100.0, 1);
	constexpr double Dt = 1.0 / 60.0;
	// El GameState del cliente corrige su hora del servidor 150 ms (lo que tarda un paquete con PktLag 150).
	constexpr double Correction = 0.15;
	const float MaxStep = static_cast<float>(1.5 * FTNQuadPass::Speed * Dt);

	FTNTrapClock Clock;
	double Server = 101.0;
	Clock.AdvanceTo(Server, 0.f);
	float PrevX = 0.f;
	float PrevRawX = 0.f;
	Pass.QuadXAt(Server, PrevX);
	Pass.QuadXAt(Server, PrevRawX);
	float WorstStep = 0.f;
	float WorstRawStep = 0.f;
	for (int32 Frame = 1; Frame <= 240; ++Frame)
	{
		Server += Dt + (Frame == 30 ? Correction : 0.0);
		float X = 0.f;
		float RawX = 0.f;
		Pass.QuadXAt(Clock.AdvanceTo(Server, static_cast<float>(Dt)), X);
		// Cálculo anterior: la hora del servidor sin suavizar.
		Pass.QuadXAt(Server, RawX);
		WorstStep = FMath::Max(WorstStep, FMath::Abs(X - PrevX));
		WorstRawStep = FMath::Max(WorstRawStep, FMath::Abs(RawX - PrevRawX));
		PrevX = X;
		PrevRawX = RawX;
	}
	TestTrue(FString::Printf(TEXT("Con el reloj de trampa el quad no salta (%.1f cm por fotograma, tope %.1f)"), WorstStep, MaxStep), WorstStep <= MaxStep);
	TestTrue(FString::Printf(TEXT("Sin suavizar saltaba (%.1f cm en un fotograma)"), WorstRawStep), WorstRawStep > MaxStep);
	TestTrue(FString::Printf(TEXT("A los 3,5 s alcanza a la hora del servidor (< 10 cm: %.2f cm)"), FMath::Abs(PrevX - PrevRawX)), FMath::Abs(PrevX - PrevRawX) < 10.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachQuadLanePrecisionTest, "Tortunabo.Beach.QuadLane.PassTimePrecision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachQuadLanePrecisionTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachQuadLaneTest;
	// 20 h de mundo: en float la hora de salida se redondea a 7,8 ms (16 cm de quad).
	constexpr double PassTime = 72000.0039;
	const FTNQuadPass Pass = MakePass(PassTime, 1);
	const float StartX = -(Pass.HalfLength + Pass.QuadHalfLen + FTNQuadPass::PalmMargin);
	const float Expected = StartX + FTNQuadPass::Speed;

	float X = 0.f;
	TestTrue(TEXT("El quad está pasando"), Pass.QuadXAt(PassTime + 1.0, X));
	TestTrue(FString::Printf(TEXT("Hora de salida en double: error < 1 cm (%.3f cm)"), FMath::Abs(X - Expected)), FMath::Abs(X - Expected) < 1.f);

	// Cálculo anterior: PassTime replicada en float (volatile, como el miembro del actor: que el compilador no se salte el redondeo).
	volatile float OldPassTime = static_cast<float>(PassTime);
	const float OldT = static_cast<float>((PassTime + 1.0) - static_cast<double>(OldPassTime));
	const float OldX = StartX + FTNQuadPass::Speed * OldT;
	TestTrue(FString::Printf(TEXT("En float se desfasaba más de 10 cm (%.1f cm)"), FMath::Abs(OldX - Expected)), FMath::Abs(OldX - Expected) > 10.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachQuadLaneHitEvalTest, "Tortunabo.Beach.QuadLane.HitEvalTime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachQuadLaneHitEvalTest::RunTest(const FString& Parameters)
{
	constexpr double Now = 500.0;
	TestTrue(TEXT("La tortuga del anfitrión se juzga en el instante actual"), FTNQuadPass::HitEvalTime(Now, true, 150.f) == Now);
	TestTrue(TEXT("La de un cliente con 150 ms de ping, un ping antes"), FMath::IsNearlyEqual(FTNQuadPass::HitEvalTime(Now, false, 150.f), Now - 0.15, 1e-9));
	TestTrue(TEXT("Con un ping enorme, como mucho 0,35 s"), FMath::IsNearlyEqual(FTNQuadPass::HitEvalTime(Now, false, 2000.f), Now - FTNQuadPass::MaxLagCompensation, 1e-9));
	TestTrue(TEXT("Sin ping, el instante actual"), FTNQuadPass::HitEvalTime(Now, false, 0.f) == Now);
	return true;
}

#endif
