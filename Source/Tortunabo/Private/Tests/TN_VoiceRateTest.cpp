// Frecuencia real de la captura de voz (issue #154): TNVoiceRate (TN_VoiceRate.h), la que usa UProximityVoiceComponent
// para reducir y etiquetar lo que envía (medidor de la frecuencia real, plan de envío y reducción). Correr desde Session
// Frontend (categoría "Tortunabo.Voice") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Voice; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Voice/TN_VoiceRate.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNVoiceRateTestHelpers
{
	/** Simula Seconds segundos de una captura que entrega RealRate muestras por segundo en bloques de 60 por segundo. */
	static int32 Feed(TNVoiceRate::FCaptureRateMeter& Meter, double& Now, double Seconds, double RealRate)
	{
		const double Step = 1.0 / 60.0;
		double Owed = 0.0;
		for (double Elapsed = 0.0; Elapsed < Seconds; Elapsed += Step)
		{
			Now += Step;
			Owed += RealRate * Step;
			const int32 Block = static_cast<int32>(Owed);
			Owed -= Block;
			Meter.Add(Block, Now);
		}
		return Meter.Rate;
	}

	/**
	 * Segundos que tarda la primera medida con una captura de RealRate muestras por segundo, sin suavizar: las muestras
	 * llegan a ráfagas de BlockFrames (los callbacks del dispositivo) y el juego las recoge 60 veces por segundo. El
	 * medidor empieza en el segundo 0 y Meter queda con lo medido. -1 si en 10 s no hay medida.
	 */
	static double SecondsToFirstRate(TNVoiceRate::FCaptureRateMeter& Meter, double RealRate, int32 BlockFrames)
	{
		const double Step = 1.0 / 60.0;
		int64 Delivered = 0;
		double Now = 0.0;
		Meter.Add(0, Now);
		while (Now < 10.0)
		{
			Now += Step;
			const int64 Produced = static_cast<int64>(Now * RealRate / BlockFrames) * BlockFrames;
			Meter.Add(static_cast<int32>(Produced - Delivered), Now);
			Delivered = Produced;
			if (Meter.Rate > 0)
			{
				return Now;
			}
		}
		return -1.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVoiceRateSnapTest,
	"Tortunabo.Voice.SnapToStandardRate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVoiceRateSnapTest::RunTest(const FString& Parameters)
{
	using namespace TNVoiceRate;
	TestEqual(TEXT("48 000 exactos"), SnapToStandardRate(48000.0), 48000);
	TestEqual(TEXT("47 700 (hilo con prisa): 48 000"), SnapToStandardRate(47700.0), 48000);
	TestEqual(TEXT("44 000: 44 100, no 48 000"), SnapToStandardRate(44000.0), 44100);
	TestEqual(TEXT("16 200: 16 000"), SnapToStandardRate(16200.0), 16000);
	TestEqual(TEXT("24 000 (48 kHz estéreo contados como mono a la mitad)"), SnapToStandardRate(24100.0), 24000);
	TestEqual(TEXT("Nada: 0"), SnapToStandardRate(0.0), 0);
	TestEqual(TEXT("Lejos de todas (5 000): 0"), SnapToStandardRate(5000.0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVoiceRateMeterTest,
	"Tortunabo.Voice.CaptureRateMeter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVoiceRateMeterTest::RunTest(const FString& Parameters)
{
	using namespace TNVoiceRate;
	using namespace TNVoiceRateTestHelpers;
	{
		// El caso de la issue: el dispositivo dice 48 kHz, pero la captura (con cancelación de eco) entrega 16 kHz.
		// La primera medida llega con ~0,5 s de audio, no a los 4-6 s; antes no se sabe con qué frecuencia etiquetar.
		FCaptureRateMeter Meter;
		double Now = 100.0;
		TestEqual(TEXT("Con 0,3 s de audio todavía no hay medida"), Feed(Meter, Now, 0.3, 16000.0), 0);
		TestEqual(TEXT("Antes de la primera medida no se puede enviar"), ResolveCaptureRate(Meter.Rate, Meter.bGaveUp, 48000), 0);
		TestEqual(TEXT("Con ~0,5 s la primera medida: 16 000"), Feed(Meter, Now, 0.4, 16000.0), 16000);
		TestEqual(TEXT("Ya se puede enviar, con la medida y no con la del dispositivo"), ResolveCaptureRate(Meter.Rate, Meter.bGaveUp, 48000), 16000);
		TestEqual(TEXT("Y las ventanas largas la mantienen"), Feed(Meter, Now, 5.0, 16000.0), 16000);
	}
	{
		FCaptureRateMeter Meter;
		double Now = 0.0;
		TestEqual(TEXT("48 kHz de verdad: 48 000"), Feed(Meter, Now, 6.0, 48000.0), 48000);
		TestEqual(TEXT("44,1 kHz: 44 100"), [] { FCaptureRateMeter M; double T = 0.0; return Feed(M, T, 6.0, 44100.0); }(), 44100);
	}
	{
		// La primera medida es rápida con cualquier frecuencia, suavizada o a ráfagas de callback (1024 muestras, 10 ms...).
		const double Rates[] = { 8000.0, 16000.0, 22050.0, 44100.0, 48000.0, 96000.0 };
		for (const double Rate : Rates)
		{
			FCaptureRateMeter Smooth;
			const double SmoothSeconds = SecondsToFirstRate(Smooth, Rate, 1);
			TestTrue(*FString::Printf(TEXT("%.0f Hz suave: primera medida en menos de 0,6 s"), Rate), SmoothSeconds > 0.0 && SmoothSeconds < 0.6);
			TestEqual(*FString::Printf(TEXT("%.0f Hz suave: bien medida"), Rate), Smooth.Rate, static_cast<int32>(Rate));

			FCaptureRateMeter Bursty;
			const double BurstSeconds = SecondsToFirstRate(Bursty, Rate, 1024);
			TestTrue(*FString::Printf(TEXT("%.0f Hz a ráfagas de 1024: primera medida en menos de 2 s"), Rate), BurstSeconds > 0.0 && BurstSeconds < 2.0);

			FCaptureRateMeter Fine;
			const double FineSeconds = SecondsToFirstRate(Fine, Rate, FMath::Max(1, static_cast<int32>(Rate / 100.0)));
			TestTrue(*FString::Printf(TEXT("%.0f Hz a ráfagas de 10 ms: primera medida en menos de 0,7 s"), Rate), FineSeconds > 0.0 && FineSeconds < 0.7);
			TestEqual(*FString::Printf(TEXT("%.0f Hz a ráfagas de 10 ms: bien medida"), Rate), Fine.Rate, static_cast<int32>(Rate));
		}
	}
	{
		// Una primera medida rápida puede ser un estándar vecino (44,1 frente a 48 kHz, con ráfagas grandes): las ventanas
		// largas la afinan. Aquí, un cambio de dispositivo a media partida hace lo mismo: dos ventanas seguidas con otra.
		FCaptureRateMeter Meter;
		double Now = 0.0;
		TestEqual(TEXT("Empieza a 48 kHz"), Feed(Meter, Now, 3.0, 48000.0), 48000);
		TestEqual(TEXT("Dos ventanas seguidas a 16 kHz la cambian"), Feed(Meter, Now, 6.0, 16000.0), 16000);
	}
	{
		// Un tirón que pierde muestras en una ventana no cambia una frecuencia ya confirmada.
		FCaptureRateMeter Meter;
		double Now = 0.0;
		Feed(Meter, Now, 6.0, 48000.0);
		Feed(Meter, Now, 2.1, 20000.0);
		TestEqual(TEXT("Una ventana rara no la cambia"), Feed(Meter, Now, 2.1, 48000.0), 48000);
	}
	{
		// Sin audio (micrófono desconectado): no se inventa ninguna medida; a los 4 s se da por no medible.
		FCaptureRateMeter Meter;
		double Now = 0.0;
		TestEqual(TEXT("Sin muestras: 0"), Feed(Meter, Now, 3.0, 0.0), 0);
		TestFalse(TEXT("A los 3 s aún no se ha dado por no medible"), Meter.bGaveUp);
		Feed(Meter, Now, 2.0, 0.0);
		TestEqual(TEXT("Sin muestras sigue sin haber medida"), Meter.Rate, 0);
	}
	{
		// Dispositivo raro: 13 000 muestras por segundo no cuadran con ninguna frecuencia estándar. No hay medida; a los
		// GiveUpSeconds se usa la del dispositivo (y antes, nada: no se envía voz).
		FCaptureRateMeter Meter;
		double Now = 0.0;
		Feed(Meter, Now, 3.0, 13000.0);
		TestEqual(TEXT("Raro, a los 3 s: sin medida"), Meter.Rate, 0);
		TestFalse(TEXT("Raro, a los 3 s: aún no se da por no medible"), Meter.bGaveUp);
		TestEqual(TEXT("Raro, a los 3 s: todavía no se puede enviar"), ResolveCaptureRate(Meter.Rate, Meter.bGaveUp, 48000), 0);
		Feed(Meter, Now, 2.0, 13000.0);
		TestEqual(TEXT("Raro, a los 5 s: sigue sin medida"), Meter.Rate, 0);
		TestTrue(TEXT("Raro, a los 5 s: se da por no medible"), Meter.bGaveUp);
		TestEqual(TEXT("Raro, a los 5 s: se envía con la del dispositivo"), ResolveCaptureRate(Meter.Rate, Meter.bGaveUp, 48000), 48000);
		TestEqual(TEXT("Raro y sin frecuencia de dispositivo: no se envía"), ResolveCaptureRate(Meter.Rate, Meter.bGaveUp, 0), 0);
	}
	{
		// Quién manda: la medida sobre la del dispositivo, aunque ya se hubiera dado por no medible.
		TestEqual(TEXT("Sin medida ni rendirse: 0"), ResolveCaptureRate(0, false, 48000), 0);
		TestEqual(TEXT("Con medida: la medida"), ResolveCaptureRate(16000, false, 48000), 16000);
		TestEqual(TEXT("Rendido sin medida: la del dispositivo"), ResolveCaptureRate(0, true, 44100), 44100);
		TestEqual(TEXT("Rendido pero con medida: la medida"), ResolveCaptureRate(16000, true, 48000), 16000);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVoiceSendPlanTest,
	"Tortunabo.Voice.SendPlan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVoiceSendPlanTest::RunTest(const FString& Parameters)
{
	using namespace TNVoiceRate;

	// Frecuencia de la captura -> factor de reducción y frecuencia con la que se envía (y se etiqueta).
	struct FCase
	{
		int32 Capture;
		int32 Factor;
		int32 Send;
	};
	const FCase Cases[] = {
		{ 8000, 1, 8000 },
		{ 11025, 1, 11025 },
		{ 16000, 1, 16000 }, // el caso de la revisión: con el factor fijo 3 salía a 5333 Hz, el destino la acotaba a 8000 y se oía 1,5 veces más rápida
		{ 22050, 1, 22050 },
		{ 44100, 2, 22050 },
		{ 48000, 3, 16000 },
		{ 96000, 6, 16000 },
	};
	for (const FCase& Case : Cases)
	{
		const FSendPlan Plan = MakeSendPlan(Case.Capture);
		const FString Name = FString::Printf(TEXT("%d Hz"), Case.Capture);
		TestEqual(*(Name + TEXT(": factor")), Plan.Factor, Case.Factor);
		TestEqual(*(Name + TEXT(": frecuencia enviada")), Plan.SendRate, Case.Send);
		TestEqual(*(Name + TEXT(": el factor del plan es max(1, captura / 16000)")), DownsampleFactorFor(Case.Capture), Plan.Factor);
		TestEqual(*(Name + TEXT(": la etiqueta es la de las muestras (captura / factor)")), Plan.SendRate, Plan.CaptureRate / Plan.Factor);
		TestTrue(*(Name + TEXT(": el plan es válido")), Plan.IsValid());
		TestTrue(*(Name + TEXT(": no baja del mínimo que aceptan servidor y receptor")), Plan.SendRate >= MinVoiceRate);
		TestEqual(*(Name + TEXT(": el destino no la acota a otro valor")), FMath::Clamp(Plan.SendRate, MinVoiceRate, MaxVoiceRate), Plan.SendRate);
	}

	// Cualquier frecuencia estándar de micrófono da un plan que servidor y receptor aceptan tal cual, y nunca por debajo
	// de la objetivo (salvo que la propia captura ya sea más baja: entonces no se reduce).
	for (const int32 Rate : StandardRates())
	{
		const FSendPlan Plan = MakeSendPlan(Rate);
		TestTrue(*FString::Printf(TEXT("%d Hz: plan válido"), Rate), Plan.IsValid());
		TestTrue(*FString::Printf(TEXT("%d Hz: envía al menos min(captura, 16000)"), Rate), Plan.SendRate >= FMath::Min(Rate, DefaultTargetRate));
		TestTrue(*FString::Printf(TEXT("%d Hz: envía como mucho la captura"), Rate), Plan.SendRate <= Rate);
	}

	// Otro objetivo: el factor sigue saliendo de la frecuencia real.
	TestEqual(TEXT("Objetivo 24000, captura 48000: factor 2"), MakeSendPlan(48000, 24000).Factor, 2);
	TestEqual(TEXT("Objetivo 24000, captura 48000: 24000 Hz"), MakeSendPlan(48000, 24000).SendRate, 24000);

	// Lo que no se puede enviar sin acotar: plan no válido (no se envía en vez de etiquetar mal).
	TestFalse(TEXT("Sin captura: plan no válido"), MakeSendPlan(0).IsValid());
	TestFalse(TEXT("4000 Hz queda por debajo del mínimo aceptado: plan no válido"), MakeSendPlan(4000).IsValid());
	TestTrue(TEXT("8000 Hz se acepta"), IsRateAccepted(8000));
	TestFalse(TEXT("7999 Hz no se acepta"), IsRateAccepted(7999));
	TestTrue(TEXT("96000 Hz se acepta"), IsRateAccepted(96000));
	TestFalse(TEXT("96001 Hz no se acepta"), IsRateAccepted(96001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNVoiceDecimateTest,
	"Tortunabo.Voice.Decimate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNVoiceDecimateTest::RunTest(const FString& Parameters)
{
	using namespace TNVoiceRate;

	TArray<float> Whole;
	for (int32 i = 0; i < 11; ++i)
	{
		Whole.Add(static_cast<float>(i));
	}

	// Todo de una vez: 11 muestras a 1/3 son 3 (las medias de 0-2, 3-5 y 6-8); sobran 2.
	TArray<float> CarryWhole;
	TArray<float> OutWhole;
	Decimate(Whole, 3, CarryWhole, OutWhole);
	TestEqual(TEXT("11 muestras a 1/3: 3 de salida"), OutWhole.Num(), 3);
	TestEqual(TEXT("Sobran 2"), CarryWhole.Num(), 2);
	if (OutWhole.Num() == 3)
	{
		TestEqual(TEXT("Media de 0, 1 y 2"), OutWhole[0], 1.f);
		TestEqual(TEXT("Media de 3, 4 y 5"), OutWhole[1], 4.f);
		TestEqual(TEXT("Media de 6, 7 y 8"), OutWhole[2], 7.f);
	}

	// En bloques de 4, 2 y 5 (ninguno múltiplo de 3): sale lo mismo. Con la reducción por bloque, cada uno redondeaba hacia
	// arriba (y uno corto salía sin reducir pero etiquetado como reducido).
	TArray<float> CarryBlocks;
	TArray<float> OutBlocks;
	const int32 BlockSizes[] = { 4, 2, 5 };
	int32 Start = 0;
	for (const int32 Size : BlockSizes)
	{
		TArray<float> Block;
		Block.Append(Whole.GetData() + Start, Size);
		Start += Size;
		Decimate(Block, 3, CarryBlocks, OutBlocks);
	}
	TestTrue(TEXT("En bloques sale lo mismo que de una vez"), OutBlocks == OutWhole);
	TestEqual(TEXT("En bloques también sobran 2"), CarryBlocks.Num(), 2);

	// Un bloque más corto que el factor no sale sin reducir: se guarda para el siguiente.
	{
		TArray<float> Carry;
		TArray<float> Out;
		Decimate({ 5.f, 7.f }, 3, Carry, Out);
		TestEqual(TEXT("Dos muestras a 1/3: no sale ninguna"), Out.Num(), 0);
		TestEqual(TEXT("Y se guardan las dos"), Carry.Num(), 2);
		Decimate({ 9.f }, 3, Carry, Out);
		TestEqual(TEXT("Con la tercera sale una"), Out.Num(), 1);
		if (Out.Num() == 1)
		{
			TestEqual(TEXT("La media de 5, 7 y 9"), Out[0], 7.f);
		}
		TestEqual(TEXT("Y no sobra nada"), Carry.Num(), 0);
	}

	// Factor 1 (captura a 16 kHz o menos): pasa tal cual.
	{
		TArray<float> Carry;
		TArray<float> Out;
		Decimate(Whole, 1, Carry, Out);
		TestTrue(TEXT("Factor 1: sin cambios"), Out == Whole);
		TestEqual(TEXT("Factor 1: no sobra nada"), Carry.Num(), 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
