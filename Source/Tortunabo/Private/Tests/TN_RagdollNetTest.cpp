// Ragdoll del derribo en red (#153, Player/TN_RagdollNet.h), sin mundo ni actores: dónde se levanta (fase 1, el punto
// del servidor), la pose raíz que viaja (precisión y tamaño), cuándo la manda el servidor, el asentado, la corrección de
// los clientes (también con 150 ms de ida y vuelta y envíos a 15 Hz) y el empuje de otra tortuga.
// Correr desde Session Frontend (categoría "Tortunabo.RagdollNet") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.RagdollNet; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Engine/NetSerialization.h"
#include "Player/TN_RagdollNet.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRagdollNetTestDetail
{
	/** Ida y vuelta por la red de una pose; devuelve los bits que ocupa. */
	int64 RoundTrip(const FTNRagdollRootPose& In, FTNRagdollRootPose& Out, bool& bOk)
	{
		FTNRagdollRootPose Copy = In;
		FBitWriter Writer(512, true);
		bool bWriteOk = false;
		Copy.NetSerialize(Writer, nullptr, bWriteOk);
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		bool bReadOk = false;
		Out.NetSerialize(Reader, nullptr, bReadOk);
		bOk = bWriteOk && bReadOk && !Reader.IsError() && !Writer.IsError();
		return Writer.GetNumBits();
	}

	/** Un cliente que persigue al servidor: un punto con velocidad que solo cambia con las correcciones. */
	struct FClientBody
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		bool bAsleep = false;

		void Apply(const TNRagdollNet::FCorrection& Correction)
		{
			switch (Correction.Mode)
			{
			case TNRagdollNet::ECorrectionMode::Nudge:
				Velocity += Correction.DeltaVelocity;
				break;
			case TNRagdollNet::ECorrectionMode::Snap:
				Location += Correction.Translation;
				Velocity += Correction.DeltaVelocity;
				break;
			case TNRagdollNet::ECorrectionMode::SnapAndSleep:
				Location += Correction.Translation;
				Velocity = FVector::ZeroVector;
				bAsleep = true;
				break;
			default:
				break;
			}
		}

		void Integrate(float Dt)
		{
			if (!bAsleep)
			{
				Location += Velocity * Dt;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRagdollNetStandTest,
	"Tortunabo.RagdollNet.StandLocation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRagdollNetStandTest::RunTest(const FString& Parameters)
{
	using namespace TNRagdollNet;
	const FVector Server(1234.56, -789.01, 210.04);
	const FVector Local(1250.0, -760.0, 205.0);

	// Fase 1: el servidor se levanta donde calcula; un cliente, donde dice el servidor, aunque su ragdoll esté en otro sitio.
	TestEqual(TEXT("El servidor usa su punto"), ResolveStandLocation(true, Server, Local), Local);
	TestEqual(TEXT("El cliente usa el punto del servidor"), ResolveStandLocation(false, Server, Local), Server);
	TestEqual(TEXT("Sin punto del servidor, el cliente usa el suyo"), ResolveStandLocation(false, FVector::ZeroVector, Local), Local);

	// El servidor redondea a la décima: el punto que viaja (FVector_NetQuantize10) es exactamente el mismo.
	const FVector Quantized = QuantizeLocation(Server);
	TestTrue(TEXT("Redondeo a la décima de centímetro"), Quantized.Equals(FVector(1234.6, -789.0, 210.0), 1e-6));
	FVector_NetQuantize10 OnWire(Quantized);
	FBitWriter Writer(256, true);
	bool bOk = false;
	OnWire.NetSerialize(Writer, nullptr, bOk);
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	FVector_NetQuantize10 Received;
	Received.NetSerialize(Reader, nullptr, bOk);
	TestTrue(TEXT("El cliente recibe el mismo punto que usa el servidor (±0,001 cm)"), FVector(Received).Equals(Quantized, 1e-3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRagdollNetPoseWireTest,
	"Tortunabo.RagdollNet.PoseWire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRagdollNetPoseWireTest::RunTest(const FString& Parameters)
{
	using namespace TNRagdollNetTestDetail;
	FTNRagdollRootPose Pose;
	Pose.Location = TNRagdollNet::QuantizeLocation(FVector(-15234.57, 8421.33, 1520.08));
	Pose.Rotation = FRotator(37.4, -121.9, 88.2);
	Pose.Velocity = FVector(812.4, -305.6, -940.2);
	Pose.bActive = true;

	FTNRagdollRootPose Out;
	bool bOk = false;
	const int64 Bits = RoundTrip(Pose, Out, bOk);
	TestTrue(TEXT("Serializa sin errores"), bOk);
	TestTrue(TEXT("Posición exacta (ya redondeada a la décima)"), Out.Location.Equals(Pose.Location, 1e-3));
	TestTrue(TEXT("Giro con menos de 0,01° de error"), Out.Rotation.Equals(Pose.Rotation, 0.01f));
	TestTrue(TEXT("Velocidad con menos de 0,5 cm/s de error"), Out.Velocity.Equals(Pose.Velocity, 0.5));
	TestTrue(TEXT("Activo"), Out.bActive);
	TestFalse(TEXT("En marcha"), Out.bSettled);
	TestTrue(FString::Printf(TEXT("Cabe en 30 bytes (%lld bits)"), Bits), Bits <= 240);

	// Asentado: sin velocidad (no viaja) y más corto.
	Pose.bSettled = true;
	const int64 SettledBits = RoundTrip(Pose, Out, bOk);
	TestTrue(TEXT("Asentado: serializa sin errores"), bOk);
	TestTrue(TEXT("Asentado: llega asentado"), Out.bSettled);
	TestTrue(TEXT("Asentado: velocidad cero"), Out.Velocity.IsZero());
	TestTrue(TEXT("Asentado: ocupa menos"), SettledBits < Bits);

	// Fin del derribo.
	Pose = FTNRagdollRootPose();
	RoundTrip(Pose, Out, bOk);
	TestFalse(TEXT("Inactivo: llega inactivo"), Out.bActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRagdollNetSendTest,
	"Tortunabo.RagdollNet.SendAndSettle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRagdollNetSendTest::RunTest(const FString& Parameters)
{
	using namespace TNRagdollNet;
	const FTNRagdollNetTuning Tuning;
	const float Interval = 1.f / Tuning.SendRateHz;

	FTNRagdollRootPose Last;
	Last.bActive = true;
	Last.Location = FVector(0.0, 0.0, 50.0);
	FTNRagdollRootPose Moved = Last;
	Moved.Location.X += 20.0;

	TestFalse(TEXT("Antes del intervalo no se manda"), ShouldSendPose(Interval * 0.5f, Last, Moved, Tuning));
	TestTrue(TEXT("Pasado el intervalo y movido, se manda"), ShouldSendPose(Interval, Last, Moved, Tuning));
	TestFalse(TEXT("Quieto (menos de SendMinMove) no se manda"), ShouldSendPose(1.f, Last, Last, Tuning));

	FTNRagdollRootPose Settled = Last;
	Settled.bSettled = true;
	TestTrue(TEXT("Al asentarse se manda en el acto"), ShouldSendPose(0.f, Last, Settled, Tuning));
	TestFalse(TEXT("Asentado y ya mandado, no se repite"), ShouldSendPose(5.f, Settled, Settled, Tuning));
	TestTrue(TEXT("Al despertarse se manda en el acto"), ShouldSendPose(0.f, Settled, Last, Tuning));
	FTNRagdollRootPose Ended = Last;
	Ended.bActive = false;
	TestTrue(TEXT("Al acabar el derribo se manda en el acto"), ShouldSendPose(0.f, Last, Ended, Tuning));

	// Asentado: SettleSeconds seguidos por debajo de los dos umbrales; un movimiento lo pone a cero.
	float Timer = 0.f;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		Timer = AdvanceSettleTimer(Timer, Tuning.SettleLinearSpeed * 0.5f, Tuning.SettleAngularSpeedDeg * 0.5f, 1.f / 60.f, Tuning);
	}
	TestFalse(TEXT("Un tercio de segundo quieto aún no asienta"), IsSettled(Timer, Tuning));
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Timer = AdvanceSettleTimer(Timer, 0.f, 0.f, 1.f / 60.f, Tuning);
	}
	TestTrue(TEXT("Medio segundo quieto asienta"), IsSettled(Timer, Tuning));
	TestEqual(TEXT("Rodando, el asentado vuelve a cero"),
		AdvanceSettleTimer(Timer, Tuning.SettleLinearSpeed * 3.f, 0.f, 1.f / 60.f, Tuning), 0.f);
	TestEqual(TEXT("Girando, el asentado vuelve a cero"),
		AdvanceSettleTimer(Timer, 0.f, Tuning.SettleAngularSpeedDeg * 3.f, 1.f / 60.f, Tuning), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRagdollNetCorrectionTest,
	"Tortunabo.RagdollNet.Correction",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRagdollNetCorrectionTest::RunTest(const FString& Parameters)
{
	using namespace TNRagdollNet;
	using namespace TNRagdollNetTestDetail;
	const FTNRagdollNetTuning Tuning;
	constexpr float Dt = 1.f / 60.f;

	// Lejos (entrar tarde): de golpe a la pose del servidor y con su velocidad.
	{
		const FCorrection C = ComputeCorrection(FVector::ZeroVector, FVector::ZeroVector, FVector(500.0, 0.0, 0.0),
			FVector(100.0, 0.0, 0.0), false, Dt, Tuning);
		TestTrue(TEXT("Error de 5 m: de golpe"), C.Mode == ECorrectionMode::Snap);
		TestTrue(TEXT("Error de 5 m: traslada todo el error"), C.Translation.Equals(FVector(500.0, 0.0, 0.0)));
		TestTrue(TEXT("Error de 5 m: toma la velocidad del servidor"), C.DeltaVelocity.Equals(FVector(100.0, 0.0, 0.0)));
	}
	// Dentro de las zonas muertas y con la misma velocidad: nada (sin temblores).
	{
		const FCorrection C = ComputeCorrection(FVector::ZeroVector, FVector::ZeroVector, FVector(2.0, 0.0, 15.0),
			FVector::ZeroVector, false, Dt, Tuning);
		TestTrue(TEXT("2 cm de lado y 15 cm de alto: nada"), C.Mode == ECorrectionMode::None);
	}
	// Asentado en el servidor: al mismo punto exacto y a dormir; ya en su sitio y quieto, nada.
	{
		const FCorrection C = ComputeCorrection(FVector(10.0, 5.0, 40.0), FVector(3.0, 0.0, 0.0), FVector(0.0, 0.0, 30.0),
			FVector::ZeroVector, true, Dt, Tuning);
		TestTrue(TEXT("Asentado a 15 cm: colocar y dormir"), C.Mode == ECorrectionMode::SnapAndSleep);
		TestTrue(TEXT("Asentado: el raíz acaba en el punto del servidor"), (FVector(10.0, 5.0, 40.0) + C.Translation).Equals(FVector(0.0, 0.0, 30.0)));
		const FCorrection Still = ComputeCorrection(FVector(0.0, 0.0, 30.5), FVector::ZeroVector, FVector(0.0, 0.0, 30.0),
			FVector::ZeroVector, true, Dt, Tuning);
		TestTrue(TEXT("Asentado a 0,5 cm y quieto: nada"), Still.Mode == ECorrectionMode::None);
	}
	// Sin red: un error de 80 cm quieto baja de 10 cm en menos de medio segundo, sin pasarse de largo.
	{
		FClientBody Body;
		const FVector Target(80.0, -30.0, 0.0);
		float Time = 0.f;
		double MaxOvershoot = 0.0;
		while (Time < 0.5f && FVector::Dist(Body.Location, Target) > 10.0)
		{
			Body.Apply(ComputeCorrection(Body.Location, Body.Velocity, Target, FVector::ZeroVector, false, Dt, Tuning));
			Body.Integrate(Dt);
			MaxOvershoot = FMath::Max(MaxOvershoot, FVector::DotProduct(Body.Location - Target, Target.GetSafeNormal()));
			Time += Dt;
		}
		TestTrue(FString::Printf(TEXT("80 cm de error: < 10 cm en %.2f s"), Time), FVector::Dist(Body.Location, Target) <= 10.0);
		TestTrue(TEXT("Sin pasarse más de 10 cm"), MaxOvershoot <= 10.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRagdollNetLagTest,
	"Tortunabo.RagdollNet.LaggedFollow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRagdollNetLagTest::RunTest(const FString& Parameters)
{
	using namespace TNRagdollNet;
	using namespace TNRagdollNetTestDetail;
	const FTNRagdollNetTuning Tuning;
	constexpr float Dt = 1.f / 60.f;
	constexpr float OneWay = 0.075f;  // NetEmulation.PktLag 150 (ida y vuelta)
	const float SendInterval = 1.f / Tuning.SendRateHz;

	// El servidor: el cuerpo resbala a 300 cm/s, frena entre 1,5 y 2 s y se asienta a los 2,5 s. El cliente empieza a
	// 40 cm con la velocidad de su propia simulación (algo distinta) y recibe cada envío 75 ms después.
	auto ServerLocation = [](float T) -> FVector
	{
		const float Slide = FMath::Min(T, 1.5f);
		const float Brake = FMath::Clamp(T - 1.5f, 0.f, 0.5f);
		return FVector(300.0 * Slide + (300.0 * Brake - 300.0 * Brake * Brake), 0.0, 20.0);
	};
	auto ServerVelocity = [](float T) -> FVector
	{
		if (T < 1.5f) { return FVector(300.0, 0.0, 0.0); }
		return FVector(FMath::Max(0.0, 300.0 - 600.0 * (T - 1.5f)), 0.0, 0.0);
	};

	FClientBody Client;
	Client.Location = FVector(-40.0, 25.0, 20.0);
	Client.Velocity = FVector(260.0, 20.0, 0.0);

	struct FInFlight { float ArriveAt; FTNRagdollRootPose Pose; };
	TArray<FInFlight> InFlight;
	FTNRagdollRootPose Received;
	float ReceivedAt = -1.f;
	float NextSend = 0.f;
	double MaxSlidingError = 0.0;
	double FinalError = TNumericLimits<double>::Max();
	bool bSentSettled = false;

	for (float T = 0.f; T < 3.5f; T += Dt)
	{
		// Envíos del servidor (15 Hz y uno al asentarse), con su retraso.
		const bool bSettled = T >= 2.5f;
		if ((T >= NextSend && !bSettled) || (bSettled && !bSentSettled))
		{
			FTNRagdollRootPose Pose;
			Pose.bActive = true;
			Pose.bSettled = bSettled;
			Pose.Location = QuantizeLocation(ServerLocation(T));
			Pose.Velocity = bSettled ? FVector::ZeroVector : ServerVelocity(T);
			InFlight.Add({T + OneWay, Pose});
			NextSend = T + SendInterval;
			bSentSettled = bSettled;
		}
		for (int32 Idx = InFlight.Num() - 1; Idx >= 0; --Idx)
		{
			if (InFlight[Idx].ArriveAt <= T)
			{
				Received = InFlight[Idx].Pose;
				ReceivedAt = T;
				InFlight.RemoveAt(Idx);
				break;
			}
		}
		if (ReceivedAt < 0.f || Client.bAsleep)
		{
			Client.Integrate(Dt);
			continue;
		}
		const FVector Target = ExtrapolateTarget(Received, T - ReceivedAt, OneWay, Tuning);
		Client.Apply(ComputeCorrection(Client.Location, Client.Velocity, Target, Received.Velocity, Received.bSettled, Dt, Tuning));
		Client.Integrate(Dt);

		// Error contra el servidor en el mismo instante, ya enganchado (desde 0,6 s) y mientras resbala.
		if (T > 0.6f && T < 1.5f)
		{
			MaxSlidingError = FMath::Max(MaxSlidingError, FVector::Dist(Client.Location, ServerLocation(T)));
		}
		FinalError = FVector::Dist(Client.Location, ServerLocation(T));
	}

	TestTrue(FString::Printf(TEXT("Resbalando con 150 ms: error < 10 cm (máx. %.1f cm)"), MaxSlidingError), MaxSlidingError < 10.0);
	TestTrue(TEXT("Asentado: el cliente duerme el cuerpo"), Client.bAsleep);
	TestTrue(FString::Printf(TEXT("Asentado: en el punto del servidor (%.3f cm)"), FinalError), FinalError < 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRagdollNetPushTest,
	"Tortunabo.RagdollNet.Push",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRagdollNetPushTest::RunTest(const FString& Parameters)
{
	using namespace TNRagdollNet;
	const FTNRagdollNetTuning Tuning;
	constexpr float Dt = 1.f / 60.f;
	const FVector Body(0.0, 0.0, 20.0);
	const FVector Walk(400.0, 0.0, 0.0);
	const FVector Behind(-50.0, 0.0, 70.0);

	const FVector Push = ComputePushVelocityChange(Behind, Walk, Body, FVector::ZeroVector, Dt, Tuning);
	TestTrue(TEXT("Caminando contra el cuerpo: lo empuja hacia delante"), Push.X > 0.0 && FMath::IsNearlyZero(Push.Y) && Push.Z == 0.0);
	TestEqual(TEXT("Con la aceleración del empuje"), Push.X, static_cast<double>(Tuning.PushAcceleration * Dt), 1e-3);

	TestTrue(TEXT("Alejándose: nada"), ComputePushVelocityChange(Behind, -Walk, Body, FVector::ZeroVector, Dt, Tuning).IsZero());
	TestTrue(TEXT("Casi quieta: nada"), ComputePushVelocityChange(Behind, FVector(20.0, 0.0, 0.0), Body, FVector::ZeroVector, Dt, Tuning).IsZero());
	TestTrue(TEXT("Lejos: nada"), ComputePushVelocityChange(FVector(-200.0, 0.0, 70.0), Walk, Body, FVector::ZeroVector, Dt, Tuning).IsZero());
	TestTrue(TEXT("Mucho más arriba (en una roca): nada"), ComputePushVelocityChange(FVector(-50.0, 0.0, 300.0), Walk, Body, FVector::ZeroVector, Dt, Tuning).IsZero());
	TestTrue(TEXT("El cuerpo ya va tan rápido como ella: nada"), ComputePushVelocityChange(Behind, Walk, Body, Walk, Dt, Tuning).IsZero());
	const FVector Last = ComputePushVelocityChange(Behind, Walk, Body, FVector(395.0, 0.0, 0.0), Dt, Tuning);
	TestEqual(TEXT("Nunca más rápido que quien empuja"), Last.X, 5.0, 1e-3);
	const FVector OnTop = ComputePushVelocityChange(FVector(0.0, 0.0, 70.0), Walk, Body, FVector::ZeroVector, Dt, Tuning);
	TestTrue(TEXT("Encima del cuerpo: hacia donde camina"), OnTop.X > 0.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
