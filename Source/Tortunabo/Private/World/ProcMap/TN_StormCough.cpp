#include "World/ProcMap/TN_StormCough.h"
#include "Audio/TN_AudioVoices.h"
#include "Core/TN_Log.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"
#include <atomic>

/**
 * Motor DSP de la tos. C++ puro (solo CoreMinimal y <atomic>) para poder probarlo fuera del motor.
 *
 * Modelo fuente-filtro: el aire (ruido) y la voz (derivada de un pulso glotal de Rosenberg con jitter, shimmer y ciclos
 * flojos alternos para la voz rota) pasan por tres formantes en paralelo (el tracto: la vocal del gesto). Cada golpe de
 * tos suma un estallido de agudos y un golpe de pecho grave en el arranque, y un troceado casi periódico hace el raspado
 * de la úvula y las flemas. Los gestos (golpe de tos, raspado, «hm», inspiración) se deciden en el propio hilo de audio
 * con la intensidad que manda el juego y suenan en unas pocas voces que se solapan.
 *
 * Hilos: FSharedParams lo escribe el hilo de juego (atómicos relajados) y lo lee el de audio una vez por bloque de
 * control; Busy va al revés. Todo lo demás solo lo toca el hilo de audio: sin asignaciones, sin UObjects, sin logs ni
 * bloqueos; el azar sale de un xorshift propio.
 */
namespace TNStormCough
{
	constexpr float Pi = 3.14159265f;

	/** Muestras por bloque de control: envolventes, formantes, tono y planificación se recalculan a este ritmo. */
	constexpr int32 BlockFrames = 32;

	/** Voces a la vez: la cola de un golpe se solapa con el siguiente y con la inspiración de después. */
	constexpr int32 MaxVoices = 4;

	/** Gestos esperando su turno (un ataque de cuatro golpes con sus inspiraciones cabe de sobra). */
	constexpr int32 MaxPending = 12;

	/** Por debajo de esta intensidad solo hay carraspeos sueltos (el principio de la exposición y la prueba «1»). */
	constexpr float ClearOnlySeverity = 0.14f;

	/** Ganancia de cada fuente con su envolvente a 1: aire, voz, estallido, siseo del aire, golpe de pecho y pitido. */
	constexpr float AirNorm = 6.f;
	constexpr float VoiceNorm = 0.8f;
	constexpr float CrackNorm = 2.f;
	constexpr float AspNorm = 0.5f;
	constexpr float ThumpNorm = 5.f;
	constexpr float WheezeNorm = 0.3f;

	/**
	 * Nivel de cada tipo de gesto (tos, raspado, «hm», inspiración), calibrado en un arnés fuera del motor con Master 1:
	 * un golpe de tos fuerte da picos de -7 a -1,5 dBFS (media -4) y ronda -19 dBFS RMS mientras suena; el primer golpe
	 * flojo, -28; un carraspeo, -24/-26 (unos 5-7 dB por debajo de la tos fuerte); una inspiración, -27/-31.
	 */
	constexpr float GestureTrim[4] = { 0.4f, 0.245f, 0.21f, 0.25f };

	/** Umbral del limitador de salida (0,8 ≈ -2 dBFS): solo actúa con el refuerzo de la tortuga local en los golpes más fuertes. */
	constexpr float LimThreshold = 0.8f;

	/** Tipos de gesto; cada uno tiene su envolvente y su mezcla de fuentes. */
	namespace Gesture
	{
		constexpr int32 Cough = 0;   ///< Golpe de tos: estallido seco, aire con los formantes de una vocal y cola con voz.
		constexpr int32 Scrape = 1;  ///< Raspado de garganta: aire troceado por la úvula («khrrr») con un gruñido debajo.
		constexpr int32 Hum = 2;     ///< «Hm» o «ejem»: voz grave y algo rota que se cierra en «m».
		constexpr int32 Inhale = 3;  ///< Inspiración: aire que entra; con la tos ya fuerte, jadeo con pitido.
	}

	/** Parámetros compartidos entre hilos (solo atómicos). */
	struct FSharedParams
	{
		/** Intensidad objetivo: 0 = callada; 1 = la peor tos. Al volver a 0, un último carraspeo y silencio. */
		std::atomic<float> Severity{ 0.f };
		/** Ganancia final (volumen y refuerzo de la tortuga local). */
		std::atomic<float> Master{ 1.f };
		/** 1 = callar en seco (la tortuga ha muerto): fundido de 20 ms, se descarta lo pendiente y no carraspea. */
		std::atomic<int32> Hush{ 0 };
		/** Lo escribe el hilo de audio: 1 mientras suena algo, queda algo pendiente o sigue tosiendo. */
		std::atomic<int32> Busy{ 0 };
		/** Se fijan antes de arrancar: la voz (una por tortuga) y el azar de cada arranque. */
		std::atomic<uint32> VoiceSeed{ 1u };
		std::atomic<uint32> RunSeed{ 1u };

		static float Get(const std::atomic<float>& Value) { return Value.load(std::memory_order_relaxed); }
		static void Set(std::atomic<float>& Value, float NewValue) { Value.store(NewValue, std::memory_order_relaxed); }
	};

	// ─────────────────────────────────────────────────────────────────────────
	// Utilidades
	// ─────────────────────────────────────────────────────────────────────────

	/** xorshift32 propio: barato, determinista con la semilla y sin estado global. */
	struct FRandom
	{
		uint32 State = 0x9E3779B9u;

		void SetSeed(uint32 InSeed)
		{
			State = InSeed != 0u ? InSeed : 0x9E3779B9u;
			// Unas vueltas para que semillas parecidas no den secuencias parecidas.
			for (int32 k = 0; k < 6; ++k) { NextU(); }
		}

		uint32 NextU()
		{
			uint32 X = State;
			X ^= X << 13;
			X ^= X >> 17;
			X ^= X << 5;
			State = X;
			return X;
		}

		/** [0, 1). */
		float Unit() { return static_cast<float>(NextU() >> 8) * (1.f / 16777216.f); }
		/** [-1, 1). */
		float Bipolar() { return Unit() * 2.f - 1.f; }
		float Range(float A, float B) { return A + (B - A) * Unit(); }
		bool Chance(float P) { return Unit() < P; }
	};

	/** Semilla derivada: que cada parte del azar tenga su propia secuencia. */
	inline uint32 MixSeed(uint32 InSeed, uint32 Salt)
	{
		uint32 H = InSeed ^ (Salt * 0x9E3779B9u);
		H ^= H >> 16;
		H *= 0x85EBCA6Bu;
		H ^= H >> 13;
		H *= 0xC2B2AE35u;
		H ^= H >> 16;
		return H != 0u ? H : 0x1234567u;
	}

	/** sin(2π·P) para P en [0, 1): parábola con corrección (error < 0,1 %), sin llamadas a la librería. */
	inline float FastSin01(float P)
	{
		const float U = 2.f * P - 1.f;
		const float Y = 4.f * U * (1.f - FMath::Abs(U));
		return -(0.225f * (Y * FMath::Abs(Y) - Y) + Y);
	}

	/** cos(2π·P) para cualquier P. */
	inline float FastCos01(float P)
	{
		const float Q = P + 0.25f;
		return FastSin01(Q - FMath::FloorToFloat(Q));
	}

	inline float SmoothStep01(float X)
	{
		const float C = FMath::Clamp(X, 0.f, 1.f);
		return C * C * (3.f - 2.f * C);
	}

	/** Saturación suave (aproximación racional de tanh, ±1 como mucho). */
	inline float SoftClip(float X)
	{
		const float C = FMath::Clamp(X, -3.f, 3.f);
		return C * (27.f + C * C) / (27.f + 9.f * C * C);
	}

	/** Coeficiente de acercamiento exponencial con constante de tiempo Seconds en un paso de Dt segundos. */
	inline float TimeCoef(float Seconds, float Dt)
	{
		return Seconds <= 1e-4f ? 1.f : 1.f - FMath::Exp(-Dt / Seconds);
	}

	/** Filtro de un polo (paso bajo y, restando, paso alto). */
	struct FOnePole
	{
		float Z = 0.f;
		float K = 1.f;

		void SetHz(float Hz, float InvRate) { K = 1.f - FMath::Exp(-2.f * Pi * FMath::Max(1.f, Hz) * InvRate); }
		void Reset() { Z = 0.f; }
		float Low(float X) { Z += K * (X - Z); return Z; }
		float High(float X) { Z += K * (X - Z); return X - Z; }
		void Flush() { if (FMath::Abs(Z) < 1e-15f) { Z = 0.f; } }
	};

	/** Paso banda de estado variable TPT (Zavalishin/Simper) con 0 dB en el centro: estable aunque su centro se mueva. */
	struct FBandPass
	{
		float Ic1 = 0.f;
		float Ic2 = 0.f;
		float K = 1.f;
		float A1 = 1.f;
		float A2 = 0.f;
		float A3 = 0.f;

		void Set(float Hz, float Q, float InvRate)
		{
			const float Norm = FMath::Clamp(Hz * InvRate, 1e-5f, 0.45f);
			const float G = FMath::Tan(Pi * Norm);
			K = 1.f / FMath::Max(0.05f, Q);
			A1 = 1.f / (1.f + G * (G + K));
			A2 = G * A1;
			A3 = G * A2;
		}

		void Reset() { Ic1 = Ic2 = 0.f; }

		float Process(float X)
		{
			const float V3 = X - Ic2;
			const float V1 = A1 * Ic1 + A2 * V3;
			const float V2 = Ic2 + A2 * Ic1 + A3 * V3;
			Ic1 = 2.f * V1 - Ic1;
			Ic2 = 2.f * V2 - Ic2;
			return V1 * K;
		}

		void Flush()
		{
			if (FMath::Abs(Ic1) < 1e-15f) { Ic1 = 0.f; }
			if (FMath::Abs(Ic2) < 1e-15f) { Ic2 = 0.f; }
		}
	};

	/**
	 * Fuente glotal: derivada de un pulso de Rosenberg (la glotis se abre en el 45 % del ciclo y se cierra de golpe en el
	 * 17 % siguiente), con jitter y shimmer por ciclo y, con Creak, ciclos alternos flojos: voz rota («fry»).
	 */
	struct FGlottis
	{
		float Phase = 0.f;
		float CycleInc = 0.f;
		float CycleAmp = 1.f;
		float Prev = 0.f;
		bool bOdd = false;

		void Reset(float Inc)
		{
			Phase = 0.f;
			CycleInc = Inc;
			CycleAmp = 1.f;
			Prev = 0.f;
			bOdd = false;
		}

		float Next(float Inc, float Jitter, float Shimmer, float Creak, FRandom& Rng)
		{
			Phase += CycleInc;
			if (Phase >= 1.f)
			{
				Phase -= FMath::FloorToFloat(Phase);
				CycleInc = Inc * (1.f + Jitter * Rng.Bipolar());
				CycleAmp = 1.f - Shimmer * Rng.Unit();
				bOdd = !bOdd;
				if (bOdd) { CycleAmp *= 1.f - Creak; }
			}
			float Flow = 0.f;
			if (Phase < 0.45f)
			{
				Flow = 0.5f - 0.5f * FastCos01(Phase * (0.5f / 0.45f));
			}
			else if (Phase < 0.62f)
			{
				Flow = FastCos01((Phase - 0.45f) * (0.25f / 0.17f));
			}
			// Derivada por fase, no por muestra: la misma forma suena igual de fuerte a cualquier tono.
			const float Slope = (Flow - Prev) / FMath::Max(1e-4f, CycleInc);
			Prev = Flow;
			return Slope * CycleAmp;
		}
	};

	/** Troceado casi periódico del aire (úvula que vibra, flemas): ganancia entre 1 - Depth y 1 con periodos al azar. */
	struct FTrill
	{
		float Phase = 0.f;
		float CycleInc = 0.f;
		float CycleDepth = 0.f;

		void Reset(float Inc, float Depth)
		{
			Phase = 0.f;
			CycleInc = Inc;
			CycleDepth = Depth;
		}

		float Next(float Inc, float Depth, FRandom& Rng)
		{
			Phase += CycleInc;
			if (Phase >= 1.f)
			{
				Phase -= FMath::FloorToFloat(Phase);
				CycleInc = Inc * Rng.Range(0.75f, 1.3f);
				CycleDepth = Depth * Rng.Range(0.6f, 1.f);
			}
			const float Open = 0.5f + 0.5f * FastCos01(Phase);
			return 1.f - CycleDepth * (1.f - Open * Open);
		}
	};

	// ─────────────────────────────────────────────────────────────────────────
	// Gestos
	// ─────────────────────────────────────────────────────────────────────────

	/** Formantes de una vocal (Hz, tracto adulto medio; se escalan con el de cada tortuga). */
	struct FVowel
	{
		float F[3];
	};

	/** Vocales de los golpes de tos: «a», «a» oscura, «e» abierta, schwa y «o» abierta. */
	constexpr int32 NumCoughVowels = 5;
	constexpr FVowel CoughVowels[NumCoughVowels] = {
		{ { 750.f, 1200.f, 2600.f } },
		{ { 650.f, 1250.f, 2550.f } },
		{ { 560.f, 1750.f, 2550.f } },
		{ { 520.f, 1450.f, 2500.f } },
		{ { 600.f, 950.f, 2500.f } } };

	/** «m» (boca cerrada), la «e» del «ejem», el aire de la inspiración y la fricativa del raspado («j» áspera). */
	constexpr FVowel HumVowel = { { 290.f, 1150.f, 2450.f } };
	constexpr FVowel EhVowel = { { 560.f, 1750.f, 2550.f } };
	constexpr FVowel BreathVowel = { { 450.f, 1500.f, 2600.f } };
	constexpr FVowel ScrapeVowel = { { 1100.f, 1900.f, 2700.f } };

	/** Un gesto ya decidido: cuándo empieza, cuánto dura y con qué voz. */
	struct FEvent
	{
		int32 Type = Gesture::Cough;
		/** Inicio (s del reloj del motor) y duración. */
		float At = 0.f;
		float Dur = 0.25f;
		/** Volumen de pico relativo. */
		float Amp = 1.f;
		/** Tono de la voz (o del pitido de la inspiración) al principio y al final (Hz). */
		float F0Start = 180.f;
		float F0End = 150.f;
		/** Formantes al principio y al final (Hz), su Q y su ganancia. */
		float FormStart[3] = { 700.f, 1200.f, 2600.f };
		float FormEnd[3] = { 600.f, 1100.f, 2500.f };
		float Q[3] = { 4.5f, 6.f, 7.f };
		float Gain[3] = { 1.f, 0.6f, 0.35f };
		/** Cuánta voz lleva (0 = solo aire); en la inspiración, cuánto pitido. */
		float Voice = 0.5f;
		/** Aspereza: troceado del aire (flemas, úvula) y voz rota. */
		float Rough = 0.f;
		/** Ritmo del troceado (Hz). */
		float TrillHz = 30.f;
		/** Estallido y agudos del aire (0..1). */
		float Bright = 0.5f;
		/** Paso bajo de la boca (Hz; 0 = sin él): cerrada en el «hm», suave en el raspado y el aire. */
		float Muffle = 0.f;
		/** Paso bajo del golpe de pecho (Hz). */
		float ThumpHz = 180.f;
	};

	/**
	 * Envolventes de un gesto T segundos después de empezar: [0] aire, [1] voz, [2] estallido, [3] golpe de pecho y
	 * [4] pitido. Todas valen 0 en T = 0 y en T = Dur: los gestos entran y salen sin clics.
	 */
	inline void GestureEnvelopes(const FEvent& Ev, float T, float* OutEnv)
	{
		for (int32 k = 0; k < 5; ++k) { OutEnv[k] = 0.f; }
		if (T <= 0.f || T >= Ev.Dur) { return; }
		const float Left = Ev.Dur - T;
		switch (Ev.Type)
		{
		case Gesture::Cough:
		{
			// Ataque seco de 4-8 ms; el estallido cae en ~35 ms y el aire sigue con la cola; la voz entra pegada al golpe
			// (sin hueco entre los dos: suena a un solo «khuh»).
			const float Attack = SmoothStep01(T / FMath::Lerp(0.008f, 0.004f, Ev.Bright));
			const float Release = SmoothStep01(Left / 0.05f);
			OutEnv[0] = Attack * Release * (0.65f * FMath::Exp(-T / 0.035f) + 0.35f * FMath::Exp(-T / (0.35f * Ev.Dur)));
			OutEnv[1] = Ev.Voice * SmoothStep01((T - 0.006f) / 0.022f) * FMath::Exp(-FMath::Max(0.f, T - 0.03f) / (0.4f * Ev.Dur)) * Release;
			OutEnv[2] = Attack * FMath::Exp(-T / 0.007f);
			OutEnv[3] = Attack * FMath::Exp(-T / 0.03f);
			break;
		}
		case Gesture::Scrape:
		{
			// Raspado sostenido que entra en ~30 ms y se apaga en ~60 ms, con algo de gruñido debajo.
			const float Shape = SmoothStep01(T / 0.03f) * SmoothStep01(Left / 0.06f);
			OutEnv[0] = Shape;
			OutEnv[1] = Ev.Voice * Shape;
			break;
		}
		case Gesture::Hum:
		{
			// «h» corta de arranque y voz grave que se cierra en «m».
			const float Shape = SmoothStep01(T / 0.025f) * SmoothStep01(Left / 0.07f);
			OutEnv[0] = 0.35f * SmoothStep01(T / 0.01f) * FMath::Exp(-T / 0.04f) + 0.05f * Shape;
			OutEnv[1] = Shape;
			break;
		}
		default:
		{
			// Inspiración: el aire crece mientras entra y se corta casi de golpe; el pitido aparece a mitad.
			const float Cut = SmoothStep01(Left / 0.035f);
			const float U = T / Ev.Dur;
			OutEnv[0] = SmoothStep01(T / 0.05f) * (0.4f + 0.6f * U) * Cut;
			OutEnv[4] = Ev.Voice * SmoothStep01((U - 0.2f) / 0.35f) * Cut;
			break;
		}
		}
	}

	/** Voz propia de una tortuga (sale de su semilla). */
	struct FTraits
	{
		/** Tono base (Hz): de ~140 a ~245 según la tortuga. */
		float F0 = 185.f;
		/** Escala de formantes: tracto algo más corto que el de un adulto (tortuga de dibujos) y más aún si es aguda. */
		float Tract = 1.12f;
		/** Aspereza propia (multiplica la de cada gesto). */
		float Rasp = 1.f;
		/** Vocales favoritas de sus golpes. */
		int32 VowelA = 0;
		int32 VowelB = 1;
	};

	/** Una voz que dice un gesto: fuente glotal, troceado, tres formantes y los extras de cada tipo. */
	struct FVoice
	{
		FEvent Ev;
		bool bActive = false;
		/** Segundos desde que empezó. */
		float Time = 0.f;
		/** Envolventes al final del bloque anterior (GestureEnvelopes). */
		float Env[5] = {};
		FGlottis Glottis;
		FTrill Trill;
		FBandPass Form[3];
		/** Agudos del estallido y del aire (paso alto de 12 dB/oct). */
		FOnePole HissA;
		FOnePole HissB;
		/** Golpe de pecho (paso bajo de 12 dB/oct). */
		FOnePole ThumpA;
		FOnePole ThumpB;
		/** Paso bajo de la boca (FEvent::Muffle). */
		FOnePole Lips;
		/** La inspiración no tiene graves. */
		FOnePole AirCut;
		float WheezePhase = 0.f;
	};

	/**
	 * Motor de una tortuga: decide los gestos con la intensidad (carraspeos al principio, ataques cada vez más seguidos y
	 * fuertes, inspiraciones entre ataques y el carraspeo de salida), los dice en unas pocas voces solapadas y deja una
	 * salida mono limitada.
	 */
	class FEngine
	{
	public:
		void Init(float InRate, uint32 InVoiceSeed, uint32 InRunSeed)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
			Rng.SetSeed(MixSeed(InRunSeed, 1u));
			NoiseRng.SetSeed(MixSeed(InRunSeed, 2u));
			BuildTraits(InVoiceSeed);
			for (FVoice& Slot : Voices) { Slot.bActive = false; }
			NumPending = 0;
			Clock = 0.f;
			GroupEnd = 0.f;
			FirstWait = 0.f;
			Ready = 0.f;
			GapJitter = 1.f;
			Played = 0;
			bEngaged = false;
			bFirstBlock = true;
			Master = 0.f;
			HushGain = 1.f;
			PrevGain = 0.f;
			LimEnv = 0.f;
			LimAttack = 1.f - FMath::Exp(-1.f / (0.001f * Rate));
			LimRelease = 1.f - FMath::Exp(-1.f / (0.15f * Rate));
			DcCut.SetHz(30.f, InvRate);
			DcCut.Reset();
		}

		/** Rellena NumFrames tramas intercaladas de OutChannels canales (mono; si llegan más, el mismo valor en todos). */
		void Render(float* Out, int32 NumFrames, int32 OutChannels, FSharedParams& P)
		{
			const int32 Channels = FMath::Max(1, OutChannels);
			int32 Done = 0;
			while (Done < NumFrames)
			{
				const int32 N = FMath::Min(BlockFrames, NumFrames - Done);
				RenderBlock(N, P);
				float* Dst = Out + Done * Channels;
				for (int32 i = 0; i < N; ++i)
				{
					for (int32 c = 0; c < Channels; ++c) { Dst[i * Channels + c] = Mix[i]; }
				}
				Done += N;
			}
			P.Busy.store(IsBusy() ? 1 : 0, std::memory_order_relaxed);
		}

		/** Depuración y arnés fuera del motor: gestos dichos, el último, la voz y el reloj. */
		int32 GetTriggeredCount() const { return TriggeredCount; }
		const FEvent& GetLastTriggered() const { return LastTriggered; }
		const FTraits& GetTraits() const { return Traits; }
		float GetClock() const { return Clock; }

	private:
		void BuildTraits(uint32 InVoiceSeed)
		{
			FRandom VoiceRng;
			VoiceRng.SetSeed(MixSeed(InVoiceSeed, 0xC0u));
			// ±5 semitonos alrededor de 185 Hz; los formantes siguen al tono a medias (tortugas grandes y pequeñas).
			const float Pitch = FMath::Pow(2.f, VoiceRng.Range(-0.42f, 0.42f));
			Traits.F0 = 185.f * Pitch;
			Traits.Tract = 1.12f * FMath::Pow(Pitch, 0.45f) * VoiceRng.Range(0.97f, 1.03f);
			Traits.Rasp = VoiceRng.Range(0.75f, 1.25f);
			Traits.VowelA = FMath::Min(static_cast<int32>(VoiceRng.Unit() * NumCoughVowels), NumCoughVowels - 1);
			const int32 Other = FMath::Min(static_cast<int32>(VoiceRng.Unit() * (NumCoughVowels - 1)), NumCoughVowels - 2);
			Traits.VowelB = (Traits.VowelA + 1 + Other) % NumCoughVowels;
		}

		bool IsBusy() const
		{
			if (bEngaged || NumPending > 0) { return true; }
			for (const FVoice& Slot : Voices)
			{
				if (Slot.bActive) { return true; }
			}
			return false;
		}

		/** Reloj del motor en el que acaba lo que ya suena. */
		float VoicesEnd() const
		{
			float EndAt = Clock;
			for (const FVoice& Slot : Voices)
			{
				if (Slot.bActive) { EndAt = FMath::Max(EndAt, Clock + Slot.Ev.Dur - Slot.Time); }
			}
			return EndAt;
		}

		/** Pausa media entre grupos (s): larga con la intensidad baja, casi nada con la peor tos. */
		static float GapFor(float Severity)
		{
			return FMath::Lerp(2.4f, 0.3f, FMath::Pow(FMath::Clamp(Severity, 0.f, 1.f), 0.65f));
		}

		// ── Planificación ────────────────────────────────────────────────────

		void RenderBlock(int32 N, FSharedParams& P)
		{
			const float Dt = static_cast<float>(N) * InvRate;
			Clock += Dt;
			const float Target = FMath::Clamp(FSharedParams::Get(P.Severity), 0.f, 1.f);
			const bool bHushNow = P.Hush.load(std::memory_order_relaxed) != 0;
			const float MasterGoal = FMath::Max(0.f, FSharedParams::Get(P.Master));
			Master = bFirstBlock ? MasterGoal : Master + (MasterGoal - Master) * TimeCoef(0.1f, Dt);
			bFirstBlock = false;
			HushGain += ((bHushNow ? 0.f : 1.f) - HushGain) * TimeCoef(bHushNow ? 0.02f : 0.01f, Dt);

			if (bHushNow)
			{
				// Muerte: calla en seco y sin carraspeo; lo que suena se apaga con el fundido y luego se corta.
				NumPending = 0;
				bEngaged = false;
				Played = 0;
				if (HushGain < 1e-3f)
				{
					for (FVoice& Slot : Voices) { Slot.bActive = false; }
				}
			}
			else
			{
				Schedule(Dt, Target);
			}

			for (int32 i = 0; i < N; ++i) { Mix[i] = 0.f; }
			bool bAnyVoice = false;
			for (FVoice& Slot : Voices)
			{
				if (Slot.bActive)
				{
					RenderVoice(Slot, N);
					bAnyVoice = true;
				}
			}

			// Salida: volumen con el fundido de silencio, sin continua y limitador suave.
			const float GainEnd = Master * HushGain;
			if (!bAnyVoice && FMath::Abs(DcCut.Z) < 1e-6f)
			{
				// En reposo no se calcula nada (la mezcla ya está a cero).
				DcCut.Reset();
				LimEnv = 0.f;
				PrevGain = GainEnd;
				return;
			}
			const float InvN = 1.f / static_cast<float>(N);
			float G = PrevGain;
			const float DG = (GainEnd - PrevGain) * InvN;
			PrevGain = GainEnd;
			for (int32 i = 0; i < N; ++i)
			{
				G += DG;
				const float X = DcCut.High(Mix[i]) * G;
				const float Peak = FMath::Abs(X);
				LimEnv += (Peak > LimEnv ? LimAttack : LimRelease) * (Peak - LimEnv);
				const float LimGain = LimEnv > LimThreshold ? LimThreshold / LimEnv : 1.f;
				Mix[i] = SoftClip(X * LimGain);
			}
			DcCut.Flush();
		}

		void Schedule(float Dt, float Severity)
		{
			if (Severity > 0.001f)
			{
				if (!bEngaged)
				{
					// Entra: el primer gesto llega enseguida (con la intensidad baja, un carraspeo suelto).
					bEngaged = true;
					Played = 0;
					FirstWait = Rng.Range(0.18f, 0.5f);
				}
			}
			else if (bEngaged)
			{
				// Sale: acaba el golpe que suena, se descarta lo pendiente y, si ha llegado a toser, un último carraspeo.
				bEngaged = false;
				NumPending = 0;
				if (Played > 0)
				{
					GroupEnd = AddClear(FMath::Max(Clock, VoicesEnd()) + Rng.Range(0.25f, 0.6f), Rng.Range(0.34f, 0.44f));
				}
				Played = 0;
			}

			// Lo que ya toca.
			while (NumPending > 0 && Pending[0].At <= Clock)
			{
				Trigger(Pending[0]);
				for (int32 k = 1; k < NumPending; ++k) { Pending[k - 1] = Pending[k]; }
				--NumPending;
			}

			// El siguiente grupo: tras el primer respiro al entrar o tras una pausa que se acorta según empeora (la pausa se
			// va consumiendo con la intensidad de cada momento: si sube mientras espera, llega antes).
			if (bEngaged && NumPending == 0 && Clock >= GroupEnd)
			{
				if (FirstWait > 0.f)
				{
					FirstWait -= Dt;
					if (FirstWait <= 0.f) { PlanGroup(Severity); }
				}
				else
				{
					Ready += Dt / (GapFor(Severity) * GapJitter);
					if (Ready >= 1.f) { PlanGroup(Severity); }
				}
			}
		}

		/** Decide el siguiente grupo de gestos (un carraspeo o un ataque de tos con sus inspiraciones). */
		void PlanGroup(float Severity)
		{
			FirstWait = 0.f;
			Ready = 0.f;
			GapJitter = Rng.Range(0.75f, 1.25f);
			float T = Clock + Rng.Range(0.f, 0.04f);

			// Al principio (intensidad baja) solo carraspeos sueltos; enseguida, ataques (con la muerte a 5 s no hay tiempo
			// para más): a media intensidad ya no hay carraspeos sueltos, solo los de después de un ataque.
			const float ClearChance = Severity < ClearOnlySeverity ? 1.f
				: FMath::Lerp(0.25f, 0.f, SmoothStep01((Severity - ClearOnlySeverity) / 0.35f));
			if (Rng.Chance(ClearChance))
			{
				GroupEnd = AddClear(T, FMath::Lerp(0.42f, 0.6f, Severity) * Rng.Range(0.85f, 1.1f));
				return;
			}

			// Ataque: más golpes, más seguidos y más fuertes cuanto peor (1-2 al empezar a toser, 3-4 al final).
			const int32 MinHits = Severity < 0.3f ? 1 : 2;
			const int32 Hits = FMath::Clamp(FMath::RoundToInt(1.3f + 2.7f * Severity + Rng.Range(-0.6f, 0.6f)), MinHits, 4);
			const float Force = FMath::Clamp(FMath::Lerp(0.5f, 1.f, Severity) * Rng.Range(0.88f, 1.05f), 0.f, 1.f);
			float EndAt = T;
			if (Severity > 0.4f && Rng.Chance(FMath::Lerp(0.1f, 0.55f, Severity)))
			{
				// Jadeo rápido antes del ataque.
				EndAt = Push(BuildInhale(T, Severity, true));
				T = EndAt + Rng.Range(0.02f, 0.06f);
			}
			const float Spacing = FMath::Lerp(0.34f, 0.2f, Severity);
			for (int32 k = 0; k < Hits; ++k)
			{
				const float Fade = 1.f - 0.12f * static_cast<float>(k) * Rng.Range(0.4f, 1.3f);
				EndAt = FMath::Max(EndAt, Push(BuildCough(T, Force * Fade, Severity, k == Hits - 1)));
				T += Spacing * Rng.Range(0.82f, 1.2f);
			}

			// Entre ataques: a veces una inspiración (larga y con pitido si ya es fuerte), a veces un carraspeo (menos
			// cuanto peor: ya no da tiempo).
			if (Rng.Chance(FMath::Lerp(0.2f, 0.8f, Severity)))
			{
				EndAt = Push(BuildInhale(EndAt + Rng.Range(0.06f, 0.18f), Severity, false));
			}
			else if (Rng.Chance(0.3f * (1.f - 0.7f * Severity)))
			{
				EndAt = AddClear(EndAt + Rng.Range(0.25f, 0.5f), 0.45f * Force);
			}
			GroupEnd = EndAt;
		}

		/** Mete un gesto en la cola (en orden de tiempo) y devuelve cuándo acaba. */
		float Push(const FEvent& Ev)
		{
			if (NumPending >= MaxPending) { return Ev.At; }
			int32 Index = NumPending;
			while (Index > 0 && Pending[Index - 1].At > Ev.At)
			{
				Pending[Index] = Pending[Index - 1];
				--Index;
			}
			Pending[Index] = Ev;
			++NumPending;
			return Ev.At + Ev.Dur;
		}

		/** Carraspeo desde At: «khhh... hm», «ejem» o doble raspado. Devuelve cuándo acaba. */
		float AddClear(float At, float Amp)
		{
			const float Pick = Rng.Unit();
			if (Pick < 0.5f)
			{
				const float ScrapeEnd = Push(BuildScrape(At, Amp, 1.f));
				return Push(BuildHum(ScrapeEnd + Rng.Range(0.02f, 0.07f), Amp * Rng.Range(0.7f, 0.9f), false, Rng.Range(0.12f, 0.22f)));
			}
			if (Pick < 0.8f)
			{
				const float EhEnd = Push(BuildHum(At, Amp * 0.6f, true, Rng.Range(0.08f, 0.12f)));
				return Push(BuildHum(EhEnd + Rng.Range(0.04f, 0.09f), Amp, true, Rng.Range(0.16f, 0.26f)));
			}
			const float FirstEnd = Push(BuildScrape(At, Amp * 0.75f, 0.6f));
			return Push(BuildScrape(FirstEnd + Rng.Range(0.05f, 0.12f), Amp, 1.f));
		}

		/** Un golpe de tos con la voz de la tortuga (Force 0..1; Strain: lo forzada que va la garganta). */
		FEvent BuildCough(float At, float Force, float Strain, bool bLastOfBout)
		{
			FEvent Ev;
			Ev.Type = Gesture::Cough;
			Ev.At = At;
			Ev.Dur = FMath::Lerp(0.19f, 0.32f, Force) * Rng.Range(0.85f, 1.15f) * (bLastOfBout ? 1.2f : 1.f);
			Ev.Amp = FMath::Pow(FMath::Max(0.05f, Force), 1.25f) * Rng.Range(0.85f, 1.f);
			const float Pick = Rng.Unit();
			const int32 VowelIndex = Pick < 0.6f ? Traits.VowelA
				: (Pick < 0.9f ? Traits.VowelB : FMath::Min(static_cast<int32>(Rng.Unit() * NumCoughVowels), NumCoughVowels - 1));
			const FVowel& Vowel = CoughVowels[VowelIndex];
			const float Shift = Traits.Tract * Rng.Range(0.95f, 1.05f);
			for (int32 f = 0; f < 3; ++f)
			{
				Ev.FormStart[f] = Vowel.F[f] * Shift * Rng.Range(0.97f, 1.03f);
			}
			// Al final se cierra la boca y aprieta la glotis: los formantes bajan, sobre todo el primero.
			Ev.FormEnd[0] = Ev.FormStart[0] * Rng.Range(0.72f, 0.85f);
			Ev.FormEnd[1] = Ev.FormStart[1] * Rng.Range(0.9f, 0.97f);
			Ev.FormEnd[2] = Ev.FormStart[2] * Rng.Range(0.95f, 1.f);
			Ev.Q[0] = 4.5f;
			Ev.Q[1] = 6.f;
			Ev.Q[2] = 7.f;
			Ev.Gain[0] = 1.f;
			Ev.Gain[1] = 1.f;
			Ev.Gain[2] = 0.7f;
			// Con la garganta forzada, el golpe sale más agudo, con más voz y más flemas.
			Ev.F0Start = Traits.F0 * Rng.Range(1.05f, 1.25f) * (1.f + 0.25f * Strain);
			Ev.F0End = Ev.F0Start * Rng.Range(0.68f, 0.82f);
			Ev.Voice = FMath::Clamp(FMath::Lerp(0.25f, 0.75f, Strain) * Rng.Range(0.7f, 1.2f) + (bLastOfBout ? 0.15f : 0.f), 0.f, 1.f);
			Ev.Rough = FMath::Clamp(FMath::Lerp(0.05f, 0.5f, Strain) * Traits.Rasp * Rng.Range(0.5f, 1.3f), 0.f, 0.9f);
			Ev.TrillHz = Rng.Range(28.f, 48.f);
			Ev.Bright = FMath::Clamp(FMath::Lerp(0.35f, 1.f, Force) * Rng.Range(0.85f, 1.1f), 0.f, 1.f);
			Ev.ThumpHz = 180.f * Traits.Tract;
			return Ev;
		}

		/** Raspado de garganta («khhh», úvula que vibra) con un gruñido grave debajo; LengthScale acorta el primero de dos. */
		FEvent BuildScrape(float At, float Amp, float LengthScale)
		{
			FEvent Ev;
			Ev.Type = Gesture::Scrape;
			Ev.At = At;
			Ev.Dur = Rng.Range(0.14f, 0.28f) * LengthScale;
			Ev.Amp = Amp;
			// El raspado depende poco del tamaño de la tortuga.
			const float Size = FMath::Sqrt(Traits.Tract);
			for (int32 f = 0; f < 3; ++f)
			{
				Ev.FormStart[f] = ScrapeVowel.F[f] * Size * Rng.Range(0.9f, 1.1f);
				Ev.FormEnd[f] = Ev.FormStart[f] * Rng.Range(0.9f, 1.02f);
			}
			Ev.Q[0] = 2.8f;
			Ev.Q[1] = 3.2f;
			Ev.Q[2] = 4.f;
			Ev.Gain[0] = 1.f;
			Ev.Gain[1] = 0.9f;
			Ev.Gain[2] = 0.35f;
			Ev.F0Start = Traits.F0 * Rng.Range(0.45f, 0.6f);
			Ev.F0End = Ev.F0Start * Rng.Range(0.85f, 1.f);
			Ev.Voice = Rng.Range(0.15f, 0.4f);
			Ev.Rough = FMath::Clamp(Rng.Range(0.6f, 0.9f) * Traits.Rasp, 0.4f, 0.95f);
			Ev.TrillHz = Rng.Range(22.f, 34.f);
			Ev.Bright = 0.f;
			Ev.Muffle = 3800.f;
			return Ev;
		}

		/** «Hm» con la boca cerrada o, con bOpen, «eh» que se cierra en «m» (las dos partes del «ejem»). */
		FEvent BuildHum(float At, float Amp, bool bOpen, float Dur)
		{
			FEvent Ev;
			Ev.Type = Gesture::Hum;
			Ev.At = At;
			Ev.Dur = Dur;
			Ev.Amp = Amp;
			for (int32 f = 0; f < 3; ++f)
			{
				Ev.FormStart[f] = (bOpen ? EhVowel.F[f] : HumVowel.F[f] * 1.05f) * Traits.Tract;
				Ev.FormEnd[f] = HumVowel.F[f] * Traits.Tract;
			}
			Ev.Q[0] = 5.f;
			Ev.Q[1] = 6.f;
			Ev.Q[2] = 6.f;
			Ev.Gain[0] = 1.f;
			Ev.Gain[1] = bOpen ? 0.5f : 0.4f;
			Ev.Gain[2] = bOpen ? 0.25f : 0.1f;
			Ev.F0Start = Traits.F0 * Rng.Range(0.7f, 0.85f);
			Ev.F0End = Ev.F0Start * Rng.Range(0.8f, 0.92f);
			Ev.Voice = 1.f;
			Ev.Rough = Rng.Range(0.15f, 0.45f) * Traits.Rasp;
			Ev.TrillHz = Rng.Range(20.f, 30.f);
			Ev.Bright = 0.f;
			Ev.Muffle = bOpen ? 2000.f : 1400.f;
			return Ev;
		}

		/** Inspiración: jadeo corto antes de un ataque (bGasp) o aire largo después, con pitido si la tos ya es fuerte. */
		FEvent BuildInhale(float At, float Strain, bool bGasp)
		{
			FEvent Ev;
			Ev.Type = Gesture::Inhale;
			Ev.At = At;
			Ev.Dur = bGasp ? Rng.Range(0.14f, 0.24f) : Rng.Range(0.34f, 0.6f) * FMath::Lerp(0.85f, 1.1f, Strain);
			Ev.Amp = (bGasp ? Rng.Range(0.3f, 0.42f) : Rng.Range(0.2f, 0.32f)) * FMath::Lerp(0.8f, 1.15f, Strain);
			for (int32 f = 0; f < 3; ++f)
			{
				Ev.FormStart[f] = BreathVowel.F[f] * Traits.Tract * Rng.Range(0.94f, 1.06f);
				Ev.FormEnd[f] = Ev.FormStart[f] * Rng.Range(1.f, 1.12f);
			}
			Ev.Q[0] = 2.5f;
			Ev.Q[1] = 3.2f;
			Ev.Q[2] = 4.f;
			Ev.Gain[0] = 0.5f;
			Ev.Gain[1] = 1.f;
			Ev.Gain[2] = 0.5f;
			Ev.Muffle = 5000.f;
			// Pitido al coger aire con la garganta cerrada (solo con la tos ya fuerte): sube un poco mientras entra.
			const float Whoop = FMath::Clamp((Strain - 0.55f) / 0.45f, 0.f, 1.f);
			Ev.Voice = Whoop > 0.f && Rng.Chance(0.35f + 0.5f * Whoop) ? Whoop * Rng.Range(0.5f, 1.f) : 0.f;
			Ev.F0Start = Traits.F0 * Rng.Range(2.6f, 3.6f);
			Ev.F0End = Ev.F0Start * Rng.Range(1.08f, 1.3f);
			Ev.Rough = 0.f;
			Ev.Bright = 0.f;
			return Ev;
		}

		// ── Voces ────────────────────────────────────────────────────────────

		void Trigger(const FEvent& Ev)
		{
			// Una voz libre o, si no queda, la que esté más cerca de acabar.
			FVoice* Chosen = &Voices[0];
			float BestLeft = TNumericLimits<float>::Max();
			for (FVoice& Slot : Voices)
			{
				if (!Slot.bActive)
				{
					Chosen = &Slot;
					break;
				}
				const float Left = Slot.Ev.Dur - Slot.Time;
				if (Left < BestLeft)
				{
					BestLeft = Left;
					Chosen = &Slot;
				}
			}
			FVoice& Voice = *Chosen;
			Voice.Ev = Ev;
			Voice.bActive = Ev.Dur > 0.f;
			Voice.Time = 0.f;
			for (float& Value : Voice.Env) { Value = 0.f; }
			Voice.Glottis.Reset(FMath::Min(Ev.F0Start * InvRate, 0.45f));
			Voice.Trill.Reset(Ev.TrillHz * InvRate, Ev.Rough);
			for (int32 f = 0; f < 3; ++f)
			{
				Voice.Form[f].Reset();
				Voice.Form[f].Set(Ev.FormStart[f], Ev.Q[f], InvRate);
			}
			Voice.HissA.SetHz(1800.f, InvRate);
			Voice.HissB.SetHz(1800.f, InvRate);
			Voice.ThumpA.SetHz(Ev.ThumpHz, InvRate);
			Voice.ThumpB.SetHz(Ev.ThumpHz, InvRate);
			Voice.Lips.SetHz(FMath::Max(100.f, Ev.Muffle), InvRate);
			Voice.AirCut.SetHz(500.f, InvRate);
			Voice.HissA.Reset();
			Voice.HissB.Reset();
			Voice.ThumpA.Reset();
			Voice.ThumpB.Reset();
			Voice.Lips.Reset();
			Voice.AirCut.Reset();
			Voice.WheezePhase = 0.f;
			++Played;
			++TriggeredCount;
			LastTriggered = Ev;
		}

		void RenderVoice(FVoice& Voice, int32 N)
		{
			const FEvent& Ev = Voice.Ev;
			const float BlockDt = static_cast<float>(N) * InvRate;
			const float TEnd = Voice.Time + BlockDt;
			float EnvEnd[5];
			GestureEnvelopes(Ev, TEnd, EnvEnd);

			// Formantes y tono al centro del bloque: se deslizan del principio al final del gesto.
			const float U = FMath::Clamp((Voice.Time + 0.5f * BlockDt) / Ev.Dur, 0.f, 1.f);
			const float Glide = SmoothStep01(U);
			for (int32 f = 0; f < 3; ++f)
			{
				Voice.Form[f].Set(FMath::Lerp(Ev.FormStart[f], Ev.FormEnd[f], Glide), Ev.Q[f], InvRate);
			}
			const float Tone = Ev.F0Start * FMath::Pow(Ev.F0End / Ev.F0Start, U);
			const float ToneInc = FMath::Min(Tone * InvRate, 0.45f);
			const float TrillInc = Ev.TrillHz * InvRate;
			const float Jitter = 0.01f + 0.07f * Ev.Rough;
			const float Shimmer = 0.06f + 0.3f * Ev.Rough;
			const float Creak = (Ev.Type == Gesture::Cough ? 0.25f : 0.6f) * Ev.Rough;
			const bool bChop = Ev.Rough > 0.01f;
			const bool bClosedLips = Ev.Muffle > 0.f;
			const bool bInhale = Ev.Type == Gesture::Inhale;
			const float Level = Ev.Amp * GestureTrim[FMath::Clamp(Ev.Type, 0, 3)];

			const float InvN = 1.f / static_cast<float>(N);
			float E[5];
			float DE[5];
			for (int32 k = 0; k < 5; ++k)
			{
				E[k] = Voice.Env[k];
				DE[k] = (EnvEnd[k] - Voice.Env[k]) * InvN;
			}
			for (int32 i = 0; i < N; ++i)
			{
				for (int32 k = 0; k < 5; ++k) { E[k] += DE[k]; }
				const float Noise = NoiseRng.Bipolar();
				const float Chop = bChop ? Voice.Trill.Next(TrillInc, Ev.Rough, NoiseRng) : 1.f;
				float Exc = E[0] * AirNorm * Noise * Chop;
				if (E[1] > 1e-5f)
				{
					Exc += E[1] * VoiceNorm * Voice.Glottis.Next(ToneInc, Jitter, Shimmer, Creak, NoiseRng) * (0.5f + 0.5f * Chop);
				}
				float Y = Ev.Gain[0] * Voice.Form[0].Process(Exc) + Ev.Gain[1] * Voice.Form[1].Process(Exc) + Ev.Gain[2] * Voice.Form[2].Process(Exc);
				if (bClosedLips) { Y = Voice.Lips.Low(Y); }
				const float Hiss = Ev.Bright * (E[2] * CrackNorm + E[0] * AspNorm);
				if (Hiss > 1e-5f) { Y += Hiss * Voice.HissB.High(Voice.HissA.High(Noise)); }
				if (E[3] > 1e-5f) { Y += E[3] * ThumpNorm * Voice.ThumpB.Low(Voice.ThumpA.Low(Noise)); }
				if (bInhale)
				{
					Y = Voice.AirCut.High(Y);
					if (E[4] > 1e-5f)
					{
						// Pitido: casi un seno con su segundo armónico y un temblor de aire.
						Voice.WheezePhase += ToneInc * (1.f + 0.02f * NoiseRng.Bipolar());
						Voice.WheezePhase -= FMath::FloorToFloat(Voice.WheezePhase);
						const float Twice = 2.f * Voice.WheezePhase;
						Y += E[4] * WheezeNorm * (FastSin01(Voice.WheezePhase) + 0.3f * FastSin01(Twice - FMath::FloorToFloat(Twice)));
					}
				}
				Mix[i] += Y * Level;
			}
			for (int32 k = 0; k < 5; ++k) { Voice.Env[k] = EnvEnd[k]; }
			Voice.Time = TEnd;
			for (FBandPass& Band : Voice.Form) { Band.Flush(); }
			Voice.HissA.Flush();
			Voice.HissB.Flush();
			Voice.ThumpA.Flush();
			Voice.ThumpB.Flush();
			Voice.Lips.Flush();
			Voice.AirCut.Flush();
			if (Voice.Time >= Ev.Dur) { Voice.bActive = false; }
		}

		float Rate = 48000.f;
		float InvRate = 1.f / 48000.f;
		FRandom Rng;
		FRandom NoiseRng;
		FTraits Traits;
		FVoice Voices[MaxVoices];
		FEvent Pending[MaxPending];
		int32 NumPending = 0;
		/** Segundos desde que arrancó el generador. */
		float Clock = 0.f;
		/** Cuándo acaba el último grupo decidido. */
		float GroupEnd = 0.f;
		/** Respiro antes del primer gesto al entrar (s). */
		float FirstWait = 0.f;
		/** Pausa consumida hacia el siguiente grupo (0..1) y su variación al azar. */
		float Ready = 0.f;
		float GapJitter = 1.f;
		/** Gestos dichos desde que empezó a toser (si es 0 al salir, no carraspea). */
		int32 Played = 0;
		bool bEngaged = false;
		bool bFirstBlock = true;
		float Master = 0.f;
		float HushGain = 1.f;
		float PrevGain = 0.f;
		float LimEnv = 0.f;
		float LimAttack = 0.f;
		float LimRelease = 0.f;
		FOnePole DcCut;
		int32 TriggeredCount = 0;
		FEvent LastTriggered;
		float Mix[BlockFrames] = {};
	};
}

namespace TNStormCough
{
	/**
	 * Generador del hilo de render de audio: solo el motor en C++ puro y los parámetros atómicos, que comparte con el
	 * componente por un puntero compartido: si el componente se destruye mientras suena, no queda nada colgando.
	 */
	class FGenerator final : public ISoundGenerator
	{
	public:
		FGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FSharedParams, ESPMode::ThreadSafe>& InParams)
			: Params(InParams)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			DspEngine.Init(InSampleRate, Params->VoiceSeed.load(std::memory_order_relaxed), Params->RunSeed.load(std::memory_order_relaxed));
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			DspEngine.Render(OutAudio, Frames, OutChannels, *Params);
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i)
			{
				OutAudio[i] = 0.f;
			}
			return NumSamples;
		}

	private:
		TSharedPtr<FSharedParams, ESPMode::ThreadSafe> Params;
		int32 OutChannels = 1;
		FEngine DspEngine;
	};

	/** Un parpadeo en el borde del frente (menos de esto fuera) no cuenta como salir (s). */
	constexpr double ExitHoldSeconds = 0.35;

	/** Sin noticias de la tormenta durante esto (destruida o sin tick), la tortuga cuenta como fuera (s). */
	constexpr double StaleSeconds = 1.0;

	/** Intensidad mínima dentro: el primer gesto al entrar llega enseguida (y es un carraspeo). */
	constexpr float MinInsideSeverity = 0.03f;

	/** Arranca a esta distancia del oyente (cm, más allá del alcance) y se para a esta otra (margen para no titubear). */
	constexpr float StartMargin = 500.f;
	constexpr float StopMargin = 1500.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// UTN_StormCoughComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_StormCoughComponent::UTN_StormCoughComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo corre mientras tose o le queda algo por decir, diez veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
	bAutoActivate = false;
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	SharedParams = MakeShared<TNStormCough::FSharedParams, ESPMode::ThreadSafe>();
}

void UTN_StormCoughComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateCough();
}

UTN_StormCoughComponent* UTN_StormCoughComponent::FindOrAddTo(AActor* InOwner)
{
	if (!IsValid(InOwner) || InOwner->IsActorBeingDestroyed()) { return nullptr; }
	if (UTN_StormCoughComponent* Existing = InOwner->FindComponentByClass<UTN_StormCoughComponent>())
	{
		return Existing;
	}
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}

	UTN_StormCoughComponent* Comp = NewObject<UTN_StormCoughComponent>(InOwner, NAME_None, RF_Transient);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
	}
	Comp->RegisterComponent();
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}

void UTN_StormCoughComponent::SetStormExposure(bool bInside, float DeathFraction)
{
	const UWorld* CompWorld = GetWorld();
	if (!CompWorld) { return; }
	const double Now = CompWorld->GetTimeSeconds();
	LastExposureTime = Now;
	bStormInside = bInside;
	if (bInside)
	{
		LastInsideTime = Now;
		StormFraction = FMath::Clamp(DeathFraction, 0.f, 1.f);
	}
	if (bHushed)
	{
		// Vuelve a estar viva (o era otra ronda): se deshace el silencio.
		bHushed = false;
		SharedParams->Hush.store(0, std::memory_order_relaxed);
	}
	// Las que están fuera y calladas no gastan tick.
	if (bInside || bExposed || Severity > 0.f || IsActive())
	{
		SetComponentTickEnabled(true);
	}
}

void UTN_StormCoughComponent::Hush()
{
	bStormInside = false;
	bExposed = false;
	StormFraction = 0.f;
	if (bHushed) { return; }
	bHushed = true;
	SharedParams->Hush.store(1, std::memory_order_relaxed);
	TNStormCough::FSharedParams::Set(SharedParams->Severity, 0.f);
	Severity = 0.f;
	if (const UWorld* CompWorld = GetWorld())
	{
		HushTime = CompWorld->GetTimeSeconds();
	}
	if (IsActive())
	{
		// Para pararlo cuando se haya apagado el fundido.
		SetComponentTickEnabled(true);
	}
}

void UTN_StormCoughComponent::SetDebugLevel(int32 InLevel)
{
	DebugLevel = FMath::Clamp(InLevel, 0, 2);
	if (DebugLevel > 0 && bHushed)
	{
		bHushed = false;
		SharedParams->Hush.store(0, std::memory_order_relaxed);
	}
	SetComponentTickEnabled(true);
	UpdateCough();
}

void UTN_StormCoughComponent::UpdateCough()
{
	const UWorld* CompWorld = GetWorld();
	if (!CompWorld || !SharedParams.IsValid()) { return; }
	const double Now = CompWorld->GetTimeSeconds();

	// Sin noticias de la tormenta (destruida o parada del todo) cuenta como fuera.
	if (bStormInside && Now - LastExposureTime > TNStormCough::StaleSeconds)
	{
		bStormInside = false;
	}
	// Dentro con margen de salida: un parpadeo en el borde del frente no corta la tos ni la reinicia.
	const bool bNowExposed = !bHushed && (bStormInside || (bExposed && Now - LastInsideTime < TNStormCough::ExitHoldSeconds));
	if (bNowExposed && !bExposed)
	{
		ExposedSince = Now;
	}
	bExposed = bNowExposed;

	// Intensidad: lo que más pese entre lo cerca que está de morir y el tiempo que lleva dentro.
	float NewSeverity = 0.f;
	if (!bHushed)
	{
		if (DebugLevel == 1)
		{
			NewSeverity = TNStormCough::ClearOnlySeverity * 0.6f;
		}
		else if (DebugLevel >= 2)
		{
			NewSeverity = 1.f;
		}
		else if (bExposed)
		{
			const float Ramp = static_cast<float>(Now - ExposedSince) / FMath::Max(1.f, SecondsToWorstCough);
			NewSeverity = FMath::Clamp(FMath::Max(StormFraction, Ramp), TNStormCough::MinInsideSeverity, 1.f);
		}
	}
	if (NewSeverity <= 0.f && Severity > 0.f)
	{
		QuietSince = Now;
	}
	Severity = NewSeverity;

	const bool bLocal = IsLocalTurtle();
	TNStormCough::FSharedParams& P = *SharedParams;
	TNStormCough::FSharedParams::Set(P.Severity, Severity);
	TNStormCough::FSharedParams::Set(P.Master, Loudness * (bLocal ? LocalPlayerBoost : 1.f));

	// Arranque y parada: suena si tose y el oyente está a tiro; se para tras callar del todo o lejos del oyente.
	const float Reach = InnerRadius + FMath::Max(100.f, FalloffDistance);
	if (bHushed)
	{
		if (IsActive() && Now - HushTime > 0.3)
		{
			Stop();
		}
	}
	else if (Severity > 0.f)
	{
		if (!IsActive())
		{
			if (bLocal || GetListenerDistance() < Reach + TNStormCough::StartMargin)
			{
				ConfigureAttenuation();
				// La tos de la tortuga propia tiene voz reservada; la de las demás compite con el resto del mundo.
				TNAudioVoices::Apply(*this, TNAudioVoices::RankForOwner(GetOwner()));
				Start();
			}
		}
		else if (!bLocal && GetListenerDistance() > Reach + TNStormCough::StopMargin)
		{
			Stop();
		}
	}
	else if (IsActive() && Now - QuietSince > 0.3 && P.Busy.load(std::memory_order_relaxed) == 0)
	{
		// Ya ha dicho su último carraspeo.
		Stop();
	}

	if (!IsActive() && Severity <= 0.f && !bExposed)
	{
		SetComponentTickEnabled(false);
	}
}

bool UTN_StormCoughComponent::IsLocalTurtle() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn && OwnerPawn->IsLocallyControlled();
}

float UTN_StormCoughComponent::GetListenerDistance() const
{
	const FAudioDevice* Device = GetAudioDevice();
	return Device ? Device->GetDistanceToNearestListener(GetComponentLocation()) : 0.f;
}

void UTN_StormCoughComponent::ConfigureAttenuation()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Como las fuentes puntuales del ambiente: volumen pleno dentro de InnerRadius, caída natural hasta FalloffDistance
	// más allá y agudos que se apagan con la distancia. Pegada al oyente (primera persona) deja de ser un punto.
	FSoundAttenuationSettings& Att = AttenuationOverrides;
	Att.bAttenuate = true;
	Att.bSpatialize = true;
	Att.AttenuationShape = EAttenuationShape::Sphere;
	Att.AttenuationShapeExtents = FVector(InnerRadius, 0.f, 0.f);
	Att.FalloffDistance = FMath::Max(100.f, FalloffDistance);
	Att.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Att.dBAttenuationAtMax = -50.f;
	Att.bAttenuateWithLPF = true;
	Att.LPFRadiusMin = InnerRadius;
	Att.LPFRadiusMax = InnerRadius + Att.FalloffDistance;
	Att.LPFFrequencyAtMin = 20000.f;
	Att.LPFFrequencyAtMax = 3000.f;
	Att.NonSpatializedRadiusStart = InnerRadius * 0.5f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.2f;
}

bool UTN_StormCoughComponent::Init(int32& /*SampleRate*/)
{
	// En el hilo de juego al arrancar (Start -> Initialize), antes de crear el generador: mono y las semillas. La voz sale
	// del jugador (PlayerId, igual en todas las máquinas); el azar cambia en cada arranque para no repetir la secuencia.
	NumChannels = 1;
	uint32 VoiceSeed = 0x7A11u;
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerState* OwnerState = OwnerPawn->GetPlayerState())
		{
			VoiceSeed = HashCombine(GetTypeHash(OwnerState->GetPlayerId()), 0x7A11u);
		}
		else
		{
			VoiceSeed = GetTypeHash(OwnerPawn->GetFName());
		}
	}
	++StartCount;
	SharedParams->VoiceSeed.store(VoiceSeed != 0u ? VoiceSeed : 1u, std::memory_order_relaxed);
	SharedParams->RunSeed.store(HashCombine(HashCombine(VoiceSeed, StartCount), FPlatformTime::Cycles()), std::memory_order_relaxed);
	return true;
}

ISoundGeneratorPtr UTN_StormCoughComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNStormCough::FGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SharedParams);
}

// ─────────────────────────────────────────────────────────────────────────────
// Comando de prueba
// ─────────────────────────────────────────────────────────────────────────────

namespace TNStormCough
{
	/** TN.Storm.Cough <0|1|2>: tos de prueba en la tortuga local, sin tormenta. */
	static void RunCoughCommand(const TArray<FString>& Args, UWorld* InWorld)
	{
		const int32 Level = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2) : 2;
		const APlayerController* PC = InWorld ? InWorld->GetFirstPlayerController() : nullptr;
		APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
		UTN_StormCoughComponent* Cough = UTN_StormCoughComponent::FindOrAddTo(LocalPawn);
		if (!Cough)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[StormCough] TN.Storm.Cough: no hay tortuga local o esta máquina no tiene audio."));
			return;
		}
		Cough->SetDebugLevel(Level);
		UE_LOG(LogTortunabo, Log, TEXT("[StormCough] Prueba en %s: %s."), *LocalPawn->GetName(),
			Level == 0 ? TEXT("apagada (manda la tormenta)") : (Level == 1 ? TEXT("carraspeos sueltos") : TEXT("tos fuerte")));
	}

	static FAutoConsoleCommandWithWorldAndArgs CoughCommand(
		TEXT("TN.Storm.Cough"),
		TEXT("Tos de la tormenta en la tortuga local, sin tormenta: 0 = apagada (vuelve a mandar la tormenta), 1 = carraspeos sueltos, 2 = tos fuerte."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunCoughCommand),
		ECVF_Cheat);
}
