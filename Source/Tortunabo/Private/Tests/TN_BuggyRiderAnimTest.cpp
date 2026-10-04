// Lógica pura de la animación de las ocupantes del buggy (TNRiderAnim en TN_BuggyRiderAnimComponent.h). Sin mundo ni
// actores. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.RiderAnim; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_BuggyRiderAnimComponent.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRiderAnimTest
{
	constexpr float FrameSeconds = 1.f / 60.f;

	/** Resultado de simular un canal: estado final y extremos alcanzados. */
	struct FRun
	{
		TNRiderAnim::FSpring Final;
		float MaxValue = -std::numeric_limits<float>::max();
		float MinValue = std::numeric_limits<float>::max();
	};

	FRun Simulate(TNRiderAnim::FSpring State, float Target, const FTNRiderSpringTuning& Tuning, float Seconds,
		float Step = FrameSeconds)
	{
		FRun Run;
		const int32 Frames = FMath::CeilToInt(Seconds / Step);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			State = TNRiderAnim::StepSpring(State, Target, Tuning, Step);
			Run.MaxValue = FMath::Max(Run.MaxValue, State.Value);
			Run.MinValue = FMath::Min(Run.MinValue, State.Value);
		}
		Run.Final = State;
		return Run;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimStepResponseTest,
	"Tortunabo.Rally.RiderAnim.StepResponse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimStepResponseTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnimTest;
	const FTNRiderSpringTuning Critical(4.f, 1.f, 0.f);
	const FRun Run = Simulate(TNRiderAnim::FSpring(), 10.f, Critical, 1.5f);
	TestTrue(TEXT("con amortiguamiento crítico llega al escalón en 1,5 s (1 %)"), FMath::IsNearlyEqual(Run.Final.Value, 10.f, 0.1f));
	// La integración discreta se pasa un 0,1 % como mucho (comprobado con 60 FPS).
	TestTrue(TEXT("con amortiguamiento crítico no se pasa del escalón (0,5 %)"), Run.MaxValue <= 10.f + 0.05f);
	TestTrue(TEXT("con amortiguamiento crítico no va hacia el lado contrario"), Run.MinValue >= 0.f);

	const FRun Half = Simulate(TNRiderAnim::FSpring(), 10.f, Critical, 0.05f);
	TestTrue(TEXT("a los 0,05 s se ha movido, pero no ha llegado"), Half.Final.Value > 0.f && Half.Final.Value < 10.f);

	const FRun Back = Simulate(TNRiderAnim::FSpring{ 10.f, 0.f }, 0.f, Critical, 1.5f);
	TestTrue(TEXT("vuelve a cero desde el escalón"), FMath::Abs(Back.Final.Value) < 0.1f);

	const FRun Snap = Simulate(TNRiderAnim::FSpring(), 7.f, FTNRiderSpringTuning(0.f, 1.f, 0.f), FrameSeconds);
	TestEqual(TEXT("frecuencia 0: sigue al objetivo sin muelle"), Snap.Final.Value, 7.f);
	TestEqual(TEXT("frecuencia 0: sin velocidad"), Snap.Final.Velocity, 0.f);

	const TNRiderAnim::FSpring Still = TNRiderAnim::StepSpring(TNRiderAnim::FSpring{ 3.f, 0.f }, 9.f, Critical, 0.f);
	TestEqual(TEXT("paso de 0 s: no cambia"), Still.Value, 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimDecayTest,
	"Tortunabo.Rally.RiderAnim.Decay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimDecayTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnimTest;
	// Poco amortiguado (el del rebote al aterrizar): se pasa, pero se apaga.
	const FTNRiderSpringTuning Bouncy(5.f, 0.3f, 0.f);
	const FRun Early = Simulate(TNRiderAnim::FSpring(), 1.f, Bouncy, 0.5f);
	TestTrue(TEXT("poco amortiguado se pasa del objetivo (rebote)"), Early.MaxValue > 1.05f);
	const FRun Late = Simulate(TNRiderAnim::FSpring(), 1.f, Bouncy, 4.f);
	TestTrue(TEXT("a los 4 s ya está en el objetivo (1 %)"), FMath::Abs(Late.Final.Value - 1.f) < 0.01f);
	TestTrue(TEXT("a los 4 s ya casi no se mueve"), FMath::Abs(Late.Final.Velocity) < 0.05f);

	// Una sacudida sin objetivo (cabezazo, disparo) vuelve a reposo.
	const TNRiderAnim::FSpring Kicked = TNRiderAnim::KickSpring(TNRiderAnim::FSpring(), 600.f);
	TestEqual(TEXT("la sacudida suma velocidad"), Kicked.Velocity, 600.f);
	const FRun AfterKick = Simulate(Kicked, 0.f, FTNRiderSpringTuning(6.f, 0.35f, 0.f), 3.f);
	TestTrue(TEXT("tras la sacudida se mueve hacia el lado del golpe"), AfterKick.MaxValue > 1.f);
	TestTrue(TEXT("a los 3 s de la sacudida está en reposo"), FMath::Abs(AfterKick.Final.Value) < 0.01f
		&& FMath::Abs(AfterKick.Final.Velocity) < 0.05f);

	// Ni con el amortiguamiento mínimo oscila para siempre.
	const FRun Minimum = Simulate(TNRiderAnim::FSpring(), 1.f, FTNRiderSpringTuning(5.f, 0.f, 0.f), 10.f);
	TestTrue(TEXT("con amortiguamiento 0 se aplica el mínimo y se apaga"), FMath::Abs(Minimum.Final.Value - 1.f) < 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimLimitsTest,
	"Tortunabo.Rally.RiderAnim.Limits",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimLimitsTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnimTest;
	const FTNRiderSpringTuning Head(6.f, 0.35f, 28.f);
	const FRun Far = Simulate(TNRiderAnim::FSpring(), 1000.f, Head, 2.f);
	TestTrue(TEXT("un objetivo enorme no pasa del límite"), Far.MaxValue <= 28.f);
	TestTrue(TEXT("y se queda en el límite"), FMath::IsNearlyEqual(Far.Final.Value, 28.f, 0.01f));
	const FRun Negative = Simulate(TNRiderAnim::FSpring(), -1000.f, Head, 2.f);
	TestTrue(TEXT("el límite es simétrico"), Negative.MinValue >= -28.f);

	const TNRiderAnim::FSpring Kicked = TNRiderAnim::KickSpring(TNRiderAnim::FSpring{ 27.f, 0.f }, 1.0e5f);
	const TNRiderAnim::FSpring AfterKick = TNRiderAnim::StepSpring(Kicked, 0.f, Head, FrameSeconds);
	TestTrue(TEXT("una sacudida enorme tampoco pasa del límite"), AfterKick.Value <= 28.f);
	TestTrue(TEXT("al tocar el límite pierde la velocidad hacia fuera"), AfterKick.Velocity <= 0.f);

	const FRun Free = Simulate(TNRiderAnim::FSpring(), 100.f, FTNRiderSpringTuning(6.f, 1.f, 0.f), 2.f);
	TestTrue(TEXT("límite 0 = sin límite"), Free.Final.Value > 99.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimRobustTest,
	"Tortunabo.Rally.RiderAnim.Robust",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimRobustTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnimTest;
	// Tirón de 1 s con el muelle más rígido: subpasos y amortiguador implícito, sin explotar.
	const FTNRiderSpringTuning Stiff(30.f, 3.f, 0.f);
	const FRun Hitch = Simulate(TNRiderAnim::FSpring(), 5.f, Stiff, 3.f, 1.f);
	TestTrue(TEXT("con pasos de 1 s sigue siendo finito"), FMath::IsFinite(Hitch.Final.Value) && FMath::IsFinite(Hitch.Final.Velocity));
	TestTrue(TEXT("con pasos de 1 s no se dispara"), Hitch.MaxValue <= 5.f + 0.5f && Hitch.MinValue >= -0.5f);

	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const TNRiderAnim::FSpring FromNaN = TNRiderAnim::StepSpring(TNRiderAnim::FSpring{ NaN, NaN }, 1.f, Stiff, FrameSeconds);
	TestTrue(TEXT("un estado no finito se reinicia"), FMath::IsFinite(FromNaN.Value) && FMath::IsFinite(FromNaN.Velocity));
	const TNRiderAnim::FSpring NaNTarget = TNRiderAnim::StepSpring(TNRiderAnim::FSpring{ 2.f, 0.f }, NaN, Stiff, FrameSeconds);
	TestTrue(TEXT("un objetivo no finito no contamina el estado"), FMath::IsFinite(NaNTarget.Value));
	const TNRiderAnim::FSpring NaNKick = TNRiderAnim::KickSpring(TNRiderAnim::FSpring(), NaN);
	TestEqual(TEXT("una sacudida no finita se ignora"), NaNKick.Velocity, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimReactionsTest,
	"Tortunabo.Rally.RiderAnim.Reactions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimReactionsTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnim;
	const FTNRiderReactionTuning Tuning;
	TestEqual(TEXT("zona muerta: dentro del umbral, 0"), DeadZone(300.f, 500.f), 0.f);
	TestEqual(TEXT("zona muerta: lo que sobra, con signo"), DeadZone(-800.f, 500.f), -300.f);

	// Ejes de la malla: X = izquierda de la tortuga, Y = delante.
	const FTargets Brake = ReactionTargets(FVector(0.0, -1500.0, 0.0), 0.f, Tuning);
	TestTrue(TEXT("frenar fuerte: cabezazo hacia delante"), Brake.HeadPitchDeg > 5.f);
	const FTargets Throttle = ReactionTargets(FVector(0.0, 1500.0, 0.0), 0.f, Tuning);
	TestTrue(TEXT("acelerar fuerte: cabeza hacia atrás"), Throttle.HeadPitchDeg < -5.f);
	const FTargets Soft = ReactionTargets(FVector(0.0, -300.0, 0.0), 0.f, Tuning);
	TestEqual(TEXT("frenar suave: la cabeza no reacciona"), Soft.HeadPitchDeg, 0.f);

	// Curva a la derecha: la aceleración centrípeta apunta a su derecha (-X) y el cuerpo se va a la izquierda.
	const FTargets RightTurn = ReactionTargets(FVector(-1000.0, 0.0, 0.0), 0.f, Tuning);
	TestTrue(TEXT("curva a la derecha: se inclina hacia fuera (izquierda, +)"), RightTurn.LeanRollDeg > 3.f);
	const FTargets LeftTurn = ReactionTargets(FVector(1000.0, 0.0, 0.0), 0.f, Tuning);
	TestTrue(TEXT("curva a la izquierda: se inclina hacia la derecha (-)"), LeftTurn.LeanRollDeg < -3.f);

	const FTargets Gripped = ReactionTargets(FVector(-1000.0, -1500.0, 0.0), 1.f, Tuning);
	TestTrue(TEXT("agarrada: reacciona menos al frenar"), Gripped.HeadPitchDeg < Brake.HeadPitchDeg * 0.5f);
	TestTrue(TEXT("agarrada: se inclina menos"), Gripped.LeanRollDeg < RightTurn.LeanRollDeg * 0.5f && Gripped.LeanRollDeg > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimSteerTest,
	"Tortunabo.Rally.RiderAnim.SteerFromYaw",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimSteerTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnim;
	// 20 m/s, batalla 2,5 m, 30 grados a tope. Guiñada de un ángulo de 10 grados: v · tan(10) / L.
	const float YawFor10 = FMath::RadiansToDegrees(2000.f * FMath::Tan(FMath::DegreesToRadians(10.f)) / 250.f);
	TestTrue(TEXT("a la derecha (+): un tercio de la dirección"), FMath::IsNearlyEqual(SteerFromYawRate(YawFor10, 2000.f, 250.f, 30.f, 150.f), 1.f / 3.f, 0.01f));
	TestTrue(TEXT("a la izquierda (-)"), SteerFromYawRate(-YawFor10, 2000.f, 250.f, 30.f, 150.f) < -0.3f);
	TestTrue(TEXT("marcha atrás invierte el signo"), SteerFromYawRate(YawFor10, -2000.f, 250.f, 30.f, 150.f) < -0.3f);
	TestEqual(TEXT("parado: 0"), SteerFromYawRate(90.f, 50.f, 250.f, 30.f, 150.f), 0.f);
	TestEqual(TEXT("trompo: se queda en el tope"), SteerFromYawRate(2000.f, 300.f, 250.f, 30.f, 150.f), 1.f);
	TestEqual(TEXT("batalla 0: 0"), SteerFromYawRate(30.f, 2000.f, 0.f, 30.f, 150.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimShotSignalTest,
	"Tortunabo.Rally.RiderAnim.ShotSignal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimShotSignalTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnim;
	TestTrue(TEXT("el calor sube un disparo (1/6): disparo"), IsShotSignal(0.f, 1.f / 6.f, 0, 0, true, 0.1f));
	TestFalse(TEXT("el calor baja (enfriado): no"), IsShotSignal(0.5f, 0.45f, 0, 0, true, 0.1f));
	TestFalse(TEXT("el calor sube poco: no"), IsShotSignal(0.2f, 0.25f, 0, 0, true, 0.1f));
	TestTrue(TEXT("una carga menos con la misma munición: disparo"), IsShotSignal(0.f, 0.f, 3, 2, true, 0.1f));
	TestTrue(TEXT("la última carga (cambia a ninguna): disparo"), IsShotSignal(0.f, 0.f, 1, 0, false, 0.1f));
	TestFalse(TEXT("caja nueva con otra munición y menos cargas: no"), IsShotSignal(0.f, 0.f, 3, 2, false, 0.1f));
	TestFalse(TEXT("recoger una caja (más cargas): no"), IsShotSignal(0.f, 0.f, 0, 3, false, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimGunnerAimTest,
	"Tortunabo.Rally.RiderAnim.GunnerAim",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimGunnerAimTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnim;
	const FTNGunnerAimTuning Tuning;
	const FGunnerAim Front = GunnerAimTargets(0.f, 0.f, false, Tuning);
	TestEqual(TEXT("al frente: sin giro"), Front.YawDeg, 0.f);
	TestEqual(TEXT("al frente: sin cabeceo"), Front.PitchDeg, 0.f);
	TestEqual(TEXT("60° a la derecha: gira 60°"), GunnerAimTargets(60.f, 0.f, false, Tuning).YawDeg, 60.f);
	TestEqual(TEXT("60° a la izquierda: gira -60°"), GunnerAimTargets(-60.f, 0.f, false, Tuning).YawDeg, -60.f);
	TestEqual(TEXT("casi detrás por la derecha: se queda en el tope"), GunnerAimTargets(179.f, 0.f, false, Tuning).YawDeg, Tuning.MaxYawDeg);
	TestEqual(TEXT("casi detrás por la izquierda: tope del otro lado"), GunnerAimTargets(-179.f, 0.f, false, Tuning).YawDeg, -Tuning.MaxYawDeg);
	TestEqual(TEXT("una guiñada sin normalizar (400°) es 40°"), GunnerAimTargets(400.f, 0.f, false, Tuning).YawDeg, 40.f);
	TestEqual(TEXT("cabeceo de 20°: la cabeza sigue el 80 %"), GunnerAimTargets(0.f, 20.f, false, Tuning).PitchDeg, 16.f);
	TestEqual(TEXT("cabeceo de 45° (máximo de la torreta): tope de la cabeza"), GunnerAimTargets(0.f, 45.f, false, Tuning).PitchDeg,
		Tuning.MaxPitchUpDeg);
	TestEqual(TEXT("cabeceo de -10°: hacia abajo"), GunnerAimTargets(0.f, -10.f, false, Tuning).PitchDeg, -8.f);

	// Casos negativos: noqueada o con un apuntado roto, mira al frente.
	const FGunnerAim Knocked = GunnerAimTargets(90.f, 30.f, true, Tuning);
	TestTrue(TEXT("noqueada no sigue el apuntado"), Knocked.YawDeg == 0.f && Knocked.PitchDeg == 0.f);
	const float NaN = std::numeric_limits<float>::quiet_NaN();
	const FGunnerAim Broken = GunnerAimTargets(NaN, 10.f, false, Tuning);
	TestTrue(TEXT("apuntado no finito: al frente"), Broken.YawDeg == 0.f && Broken.PitchDeg == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRiderAnimGunnerGesturesTest,
	"Tortunabo.Rally.RiderAnim.GunnerGestures",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRiderAnimGunnerGesturesTest::RunTest(const FString& Parameters)
{
	using namespace TNRiderAnimTest;
	using namespace TNRiderAnim;
	TestTrue(TEXT("cambia la munición seleccionada: gesto"), IsAmmoSwapSignal(true, 1, 2));
	TestFalse(TEXT("misma munición: sin gesto"), IsAmmoSwapSignal(true, 2, 2));
	TestFalse(TEXT("primera muestra (sin anterior): sin gesto"), IsAmmoSwapSignal(false, 0, 3));

	// Retroceso con los valores por defecto: se ve (más de 12° y más de 0,1 s por encima de 5°) y vuelve a reposo.
	FSpring Recoil = KickSpring(FSpring(), DefaultRecoilKickDegPerSec);
	float Peak = 0.f;
	float VisibleSeconds = 0.f;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Recoil = StepSpring(Recoil, 0.f, DefaultRecoilSpring(), FrameSeconds);
		Peak = FMath::Max(Peak, Recoil.Value);
		VisibleSeconds += Recoil.Value > 5.f ? FrameSeconds : 0.f;
	}
	TestTrue(FString::Printf(TEXT("el retroceso llega a más de 12° (%.1f)"), Peak), Peak > 12.f);
	TestTrue(TEXT("el retroceso no pasa de su límite"), Peak <= DefaultRecoilSpring().Limit);
	TestTrue(FString::Printf(TEXT("el retroceso se ve más de 0,1 s (%.2f)"), VisibleSeconds), VisibleSeconds > 0.1f);
	TestTrue(TEXT("al segundo del disparo está en reposo"), FMath::Abs(Recoil.Value) < 0.5f);

	// Gesto de cambio de munición: casi completo y breve.
	const FRun SwapRun = Simulate(KickSpring(FSpring(), DefaultSwapKickPerSec), 0.f, DefaultSwapSpring(), 1.5f);
	TestTrue(FString::Printf(TEXT("el gesto llega casi a 1 (%.2f)"), SwapRun.MaxValue), SwapRun.MaxValue > 0.7f);
	TestTrue(TEXT("a los 1,5 s el gesto ha acabado"), FMath::Abs(SwapRun.Final.Value) < 0.05f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
