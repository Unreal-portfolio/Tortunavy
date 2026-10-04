// Red de la plataforma tambaleante (issue #20): la pose de la tabla del servidor viaja en siete bytes (TNWobblyPlatformNet,
// TN_BeachWobblyPlatform.h). Correr desde Session Frontend (categoría "Tortunabo.Beach.WobblyPlatform") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Beach.WobblyPlatform; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachWobblyPlatform.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNWobblyPlatformNetTestHelpers
{
	/** Esquina de la tabla (semilargo X, semiancho Y) con su pose: alabeo y cabeceo en grados y hundimiento en cm. */
	FVector BoardCorner(double HalfL, double HalfW, float Roll, float Pitch, float Sag)
	{
		return FRotator(Pitch, 0.f, Roll).RotateVector(FVector(HalfL, HalfW, 0.0)) - FVector(0.0, 0.0, Sag);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNWobblyPlatformPoseNetTest,
	"Tortunabo.Beach.WobblyPlatform.PoseNet",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNWobblyPlatformPoseNetTest::RunTest(const FString& Parameters)
{
	using namespace TNWobblyPlatformNet;

	// Quieta: todo a cero.
	TestEqual(TEXT("Alabeo 0"), QuantizeRoll(0.f), static_cast<int8>(0));
	TestEqual(TEXT("Cabeceo 0"), QuantizePitch(0.f), static_cast<int8>(0));
	TestEqual(TEXT("Hundimiento 0"), QuantizeSag(0.f), static_cast<uint8>(0));

	// Lo que puede dar el muelle cabe sin recortar: alabeo hasta MaxRollDeg (15 como mucho) x 1,4, cabeceo hasta 3° y
	// hundimiento hasta 18 cm (ocho tortugas y la grieta a punto).
	for (const float Roll : { -21.f, -7.f, -0.33f, 0.1f, 4.5f, 9.8f, 21.f })
	{
		TestEqual(FString::Printf(TEXT("Alabeo %.2f ida y vuelta"), Roll), DequantizeRoll(QuantizeRoll(Roll)), Roll, RollStepDeg * 0.5f + KINDA_SMALL_NUMBER);
	}
	for (const float Pitch : { -3.f, -1.2f, 0.01f, 2.f, 3.f })
	{
		TestEqual(FString::Printf(TEXT("Cabeceo %.2f ida y vuelta"), Pitch), DequantizePitch(QuantizePitch(Pitch)), Pitch, PitchStepDeg * 0.5f + KINDA_SMALL_NUMBER);
	}
	for (const float Sag : { 0.f, 1.5f, 3.f, 9.f, 18.f })
	{
		TestEqual(FString::Printf(TEXT("Hundimiento %.1f ida y vuelta"), Sag), DequantizeSag(QuantizeSag(Sag)), Sag, SagStepCm * 0.5f + KINDA_SMALL_NUMBER);
	}

	// Fuera de rango se recorta (nunca da la vuelta en el byte).
	TestEqual(TEXT("Alabeo enorme: tope positivo"), QuantizeRoll(90.f), static_cast<int8>(127));
	TestEqual(TEXT("Alabeo enorme negativo: tope negativo"), QuantizeRoll(-90.f), static_cast<int8>(-127));
	TestEqual(TEXT("Cabeceo enorme: tope"), QuantizePitch(10.f), static_cast<int8>(127));
	TestEqual(TEXT("Hundimiento negativo: 0"), QuantizeSag(-4.f), static_cast<uint8>(0));
	TestEqual(TEXT("Hundimiento enorme: 255"), QuantizeSag(100.f), static_cast<uint8>(255));

	// La esquina de la tabla más grande (434 x 150 cm de semiejes) con la pose recibida, contra la del servidor: menos de
	// 5 mm de error en todo el recorrido del muelle (el desfase pedido es menos de 5 cm).
	double WorstCorner = 0.0;
	for (float Roll = -21.f; Roll <= 21.f; Roll += 0.37f)
	{
		for (float Pitch = -3.f; Pitch <= 3.f; Pitch += 0.29f)
		{
			const float Sag = FMath::Abs(Roll) * 0.4f;
			const FVector Server = TNWobblyPlatformNetTestHelpers::BoardCorner(434.0, 150.0, Roll, Pitch, Sag);
			const FVector Client = TNWobblyPlatformNetTestHelpers::BoardCorner(434.0, 150.0, DequantizeRoll(QuantizeRoll(Roll)),
				DequantizePitch(QuantizePitch(Pitch)), DequantizeSag(QuantizeSag(Sag)));
			WorstCorner = FMath::Max(WorstCorner, FVector::Dist(Server, Client));
		}
	}
	TestTrue(FString::Printf(TEXT("Esquina de la tabla: %.2f cm de error como mucho (< 0,5)"), WorstCorner), WorstCorner < 0.5);
	return true;
}

namespace TNWobblyPoseFollowTestHelpers
{
	using namespace TNWobblyPlatformNet;

	constexpr float ServerStep = 1.f / 60.f;
	constexpr float ClientStep = 1.f / 75.f;
	constexpr float SendInterval = 1.f / ATN_BeachWobblyPlatform::PoseNetFrequency;
	constexpr float MaxRollDeg = 7.f;
	/** Ciclo de la QA (#20): cae en una esquina, se queda encima andando un poco y se baja. */
	constexpr float CycleSeconds = 2.f;
	constexpr float OnBoardSeconds = 1.2f;
	constexpr int32 Cycles = 8;
	/** Caída de 2,5 m: el golpe que da el aterrizaje (0,4 + 0,64). */
	constexpr float LandingKick = 1.04f;
	/** Hundimiento con una tortuga encima (cm). */
	constexpr float RiderSagCm = 1.5f;
	/** Rapidez (1/s) con que el cliente de antes suavizaba hacia la muestra. */
	constexpr float LegacyInterpSpeed = 15.f;

	/** Cómo sigue el cliente la pose del servidor. */
	enum class EFollow : uint8
	{
		/** El mismo muelle desde la última muestra (el arreglo). */
		Spring,
		/** Suavizar hacia la muestra con FInterpTo a 15/s (lo de antes, para ver que el escenario lo pilla). */
		LegacyInterp,
	};

	struct FPacket
	{
		double ArriveAt = 0.0;
		FTNWobblyPoseNet Net;
	};

	double WorstCornerGap(const FPoseState& A, const FPoseState& B)
	{
		double Worst = 0.0;
		for (const double Sx : { -1.0, 1.0 })
		{
			for (const double Sy : { -1.0, 1.0 })
			{
				const FVector Ca = TNWobblyPlatformNetTestHelpers::BoardCorner(434.0 * Sx, 150.0 * Sy, A.Roll, A.Pitch, A.Sag);
				const FVector Cb = TNWobblyPlatformNetTestHelpers::BoardCorner(434.0 * Sx, 150.0 * Sy, B.Roll, B.Pitch, B.Sag);
				Worst = FMath::Max(Worst, FVector::Dist(Ca, Cb));
			}
		}
		return Worst;
	}

	/** Lo que hay encima en el instante T del escenario (el golpe, solo en el fotograma del aterrizaje). */
	FRiderInput RiderAt(double T, bool bLandingFrame)
	{
		FRiderInput Input;
		const int32 Cycle = FMath::FloorToInt32(T / CycleSeconds);
		const double InCycle = T - Cycle * CycleSeconds;
		if (InCycle >= OnBoardSeconds)
		{
			return Input;
		}
		Input.SumY = (Cycle % 2 == 0) ? 0.6f : -0.6f;
		Input.SumX = (Cycle % 4 < 2) ? 0.6f : -0.6f;
		Input.Kick = bLandingFrame ? LandingKick : 0.f;
		Input.Motion = InCycle > OnBoardSeconds * 0.5 ? 0.6f : 0.f;
		return Input;
	}

	/** Servidor y cliente de la prueba, con lo que va por la red. */
	struct FSim
	{
		FPoseState Server;
		FPoseState Client;
		/** LegacyInterp: la última pose recibida (hacia ella se suaviza). */
		FPoseState Received;
		FTNWobblyPoseNet LastSent;
		TArray<FPacket> InFlight;
		double NextSend = 0.0;
		double ServerTime = 0.0;
	};

	/** El servidor avanza a 60 Hz hasta T; manda a 15 Hz lo que cambia, y en el acto al aterrizar. */
	void AdvanceServer(FSim& Sim, double T, float Latency)
	{
		while (Sim.ServerTime + ServerStep <= T)
		{
			Sim.ServerTime += ServerStep;
			const double InCycle = FMath::Fmod(Sim.ServerTime, static_cast<double>(CycleSeconds));
			const bool bLanding = InCycle < ServerStep;
			StepServerPose(Sim.Server, RiderAt(Sim.ServerTime, bLanding), Sim.ServerTime, ServerStep, MaxRollDeg);
			Sim.Server.Sag = InCycle < OnBoardSeconds ? RiderSagCm : 0.f;
			const FTNWobblyPoseNet Net = EncodePose(Sim.Server);
			if ((bLanding || Sim.ServerTime >= Sim.NextSend) && !(Net == Sim.LastSent))
			{
				Sim.NextSend = Sim.ServerTime + SendInterval;
				Sim.LastSent = Net;
				Sim.InFlight.Add({ Sim.ServerTime + Latency, Net });
			}
		}
	}

	/** Un fotograma del cliente en T: recibe lo que ha llegado antes y sigue la pose a su manera. */
	void TickClient(FSim& Sim, EFollow Mode, double T, float Latency)
	{
		bool bSeeded = false;
		while (Sim.InFlight.Num() > 0 && Sim.InFlight[0].ArriveAt < T)
		{
			Sim.Received = DecodePose(Sim.InFlight[0].Net);
			Sim.InFlight.RemoveAt(0);
			if (Mode == EFollow::Spring)
			{
				Sim.Client = Sim.Received;
				AdvancePose(Sim.Client, Latency, MaxRollDeg);
				bSeeded = true;
			}
		}
		if (Mode == EFollow::Spring)
		{
			if (!bSeeded)
			{
				AdvancePose(Sim.Client, ClientStep, MaxRollDeg);
			}
			return;
		}
		Sim.Client.Roll = FMath::FInterpTo(Sim.Client.Roll, Sim.Received.Roll, ClientStep, LegacyInterpSpeed);
		Sim.Client.Pitch = FMath::FInterpTo(Sim.Client.Pitch, Sim.Received.Pitch, ClientStep, LegacyInterpSpeed);
		Sim.Client.Sag = FMath::FInterpTo(Sim.Client.Sag, Sim.Received.Sag, ClientStep, LegacyInterpSpeed);
	}

	/** Peor esquina (cm) entre la tabla del servidor y la del cliente en el mismo instante, con Latency de ida. */
	double SimulateWorstGap(EFollow Mode, float Latency)
	{
		FSim Sim;
		double Worst = 0.0;
		for (double T = 0.0; T < Cycles * CycleSeconds; T += ClientStep)
		{
			AdvanceServer(Sim, T, Latency);
			TickClient(Sim, Mode, T, Latency);
			Worst = FMath::Max(Worst, WorstCornerGap(Sim.Server, Sim.Client));
		}
		return Worst;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNWobblyPlatformPoseFollowTest,
	"Tortunabo.Beach.WobblyPlatform.PoseFollow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNWobblyPlatformPoseFollowTest::RunTest(const FString& Parameters)
{
	using namespace TNWobblyPlatformNet;
	using namespace TNWobblyPoseFollowTestHelpers;

	// Velocidades del muelle: ida y vuelta dentro de medio paso y recortadas en el byte.
	for (const float Rate : { -49.f, -3.3f, 0.f, 12.7f, 49.f })
	{
		TestEqual(FString::Printf(TEXT("Velocidad de alabeo %.1f ida y vuelta"), Rate), DequantizeRate(QuantizeRate(Rate, RollRateStepDeg), RollRateStepDeg), Rate,
			RollRateStepDeg * 0.5f + KINDA_SMALL_NUMBER);
	}
	for (const float Rate : { -11.2f, -0.4f, 0.f, 6.f, 11.2f })
	{
		TestEqual(FString::Printf(TEXT("Velocidad de cabeceo %.1f ida y vuelta"), Rate), DequantizeRate(QuantizeRate(Rate, PitchRateStepDeg), PitchRateStepDeg), Rate,
			PitchRateStepDeg * 0.5f + KINDA_SMALL_NUMBER);
	}
	TestEqual(TEXT("Velocidad enorme: tope"), QuantizeRate(1000.f, RollRateStepDeg), static_cast<int8>(127));

	// La prueba de la QA (aterrizajes en las esquinas, 2P sin retardo): desfase en la peor esquina de la tabla más grande.
	// Suavizando la muestra pasaba de 5 cm al aterrizar; con el muelle, por debajo.
	const double Legacy = SimulateWorstGap(EFollow::LegacyInterp, 0.f);
	const double Spring = SimulateWorstGap(EFollow::Spring, 0.f);
	TestTrue(FString::Printf(TEXT("El escenario pilla el retraso de suavizar: %.2f cm (> 5)"), Legacy), Legacy > 5.0);
	TestTrue(FString::Printf(TEXT("Con el muelle: %.2f cm en la peor esquina (< 5)"), Spring), Spring < 5.0);

	// Con 50 ms de ida, la sacudida de un aterrizaje llega tarde se haga como se haga; lo pedido es no perder contra suavizar.
	const double LegacyFar = SimulateWorstGap(EFollow::LegacyInterp, 0.05f);
	const double SpringFar = SimulateWorstGap(EFollow::Spring, 0.05f);
	AddInfo(FString::Printf(TEXT("Peor esquina sin retardo: %.2f cm (suavizando, %.2f); con 50 ms de ida: %.2f cm (suavizando, %.2f)"), Spring, Legacy,
		SpringFar, LegacyFar));
	TestTrue(FString::Printf(TEXT("Con 50 ms de ida, el muelle (%.2f cm) no queda peor que suavizar (%.2f)"), SpringFar, LegacyFar), SpringFar < LegacyFar);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
