#include "World/Beach/TN_RaceItemSynth.h"
#include "Audio/TN_AudioVoices.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"
#include <atomic>
#include <cmath>

/**
 * Motor de efectos de los objetos de carrera. Hilos: la cola la llena el hilo de juego (Play) y la vacía el hilo de render de
 * audio al principio de cada bloque; todo lo demás (voces, filtros, osciladores) vive solo en el hilo de audio, sin
 * asignaciones ni bloqueos. Las envolventes lentas se calculan una vez por bloque de 16 muestras; las rápidas (ataques de
 * milisegundos, chasquidos), por muestra. Cada voz se dibuja en un búfer propio y al final se le aplica la ganancia del
 * disparo y un fundido de salida común, de modo que ningún efecto se corta con un chasquido al llegar a su duración.
 */
namespace TNRaceItemDSP
{
	constexpr float RacePi = 3.14159265358979323846f;
	constexpr float RaceTwoPi = 6.28318530717958647692f;
	constexpr int32 RaceMaxVoices = 12;
	constexpr int32 RaceBlock = 16;
	/** Notas de campanilla que puede llevar a la vez una voz (arpegios). */
	constexpr int32 RaceMaxNotes = 8;
	/** Fundido de salida común (s). */
	constexpr float RaceTail = 0.03f;

	/** Tipos de efecto (el orden es el de ETNRaceSound). */
	constexpr uint8 KindBoxOpen = 0;
	constexpr uint8 KindTurbo = 1;
	constexpr uint8 KindGolden = 2;
	constexpr uint8 KindStarUp = 3;
	constexpr uint8 KindStarDown = 4;
	constexpr uint8 KindWhistle = 5;
	constexpr uint8 KindSquawk = 6;
	constexpr uint8 KindFlap = 7;
	constexpr uint8 KindScuttle = 8;
	constexpr uint8 KindBoing = 9;
	constexpr uint8 KindBonk = 10;
	constexpr uint8 KindSplat = 11;
	constexpr uint8 KindFall = 12;
	constexpr uint8 KindThrow = 13;
	constexpr uint8 KindWhirr = 14;
	constexpr uint8 KindCatch = 15;
	constexpr uint8 KindRumble = 16;
	constexpr uint8 KindZap = 17;
	constexpr uint8 KindNope = 18;
	constexpr uint8 KindBeep = 19;
	constexpr uint8 KindLand = 20;
	constexpr uint8 KindCount = 21;

	/** Duración de cada efecto (s), en el orden de los tipos. El graznido se alarga después con el tono. */
	constexpr float RaceDurations[] =
	{
		0.50f, // BoxOpen
		0.90f, // Turbo
		1.40f, // Golden
		0.70f, // StarUp
		0.90f, // StarDown
		1.00f, // Whistle
		0.50f, // Squawk
		0.35f, // Flap
		0.50f, // Scuttle
		0.45f, // Boing
		0.30f, // Bonk
		0.40f, // Splat
		0.90f, // Fall
		0.25f, // Throw
		0.90f, // Whirr
		0.15f, // Catch
		2.20f, // Rumble
		0.35f, // Zap
		0.45f, // Nope
		0.12f, // Beep
		0.60f, // Land
	};
	static_assert(sizeof(RaceDurations) / sizeof(float) == KindCount, "Falta o sobra una duración en RaceDurations");

	/** Un disparo: tipo, multiplicador de tono y ganancia. */
	struct FRaceSfxEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FRaceSfxQueue
	{
		static constexpr uint32 Capacity = 32;

		FRaceSfxEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FRaceSfxEvent& InEvent)
		{
			const uint32 W = WriteIndex.load(std::memory_order_relaxed);
			const uint32 R = ReadIndex.load(std::memory_order_acquire);
			if (W - R >= Capacity)
			{
				return false;
			}
			Events[W % Capacity] = InEvent;
			WriteIndex.store(W + 1, std::memory_order_release);
			return true;
		}

		/** Hilo de audio. */
		bool Pop(FRaceSfxEvent& OutEvent)
		{
			const uint32 R = ReadIndex.load(std::memory_order_relaxed);
			const uint32 W = WriteIndex.load(std::memory_order_acquire);
			if (R == W)
			{
				return false;
			}
			OutEvent = Events[R % Capacity];
			ReadIndex.store(R + 1, std::memory_order_release);
			return true;
		}
	};

	/** Ruido blanco barato (xorshift32) en [-1, 1). */
	inline float RaceNoise(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Número al azar en [0, 1). */
	inline float RaceUnit(uint32& State)
	{
		return 0.5f + 0.5f * RaceNoise(State);
	}

	/** Coeficiente del filtro de estado variable (Chamberlin) para un corte en Hz; estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float RaceSvfCoef(float CutHz, float Rate)
	{
		return 2.f * std::sin(RacePi * FMath::Clamp(CutHz, 20.f, Rate * 0.16f) / Rate);
	}

	/** Coeficiente de un paso bajo de un polo para un corte en Hz. */
	inline float RaceLpCoef(float CutHz, float Rate)
	{
		return 1.f - std::exp(-RaceTwoPi * FMath::Clamp(CutHz, 5.f, Rate * 0.4f) / Rate);
	}

	/** Hermite 0..1. */
	inline float RaceSmooth(float X)
	{
		const float T = FMath::Clamp(X, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Avanza una fase en vueltas (0..1). */
	inline void RaceAdvance(float& Phase, float Inc)
	{
		Phase += Inc;
		Phase -= std::floor(Phase);
	}

	/** Seno de una fase en vueltas. */
	inline float RaceSin(float Phase)
	{
		return std::sin(RaceTwoPi * Phase);
	}

	/** Sierra con corrección PolyBLEP (sin aliasing fuerte). Phase en [0, 1) e Inc = frecuencia / tasa de muestreo. */
	inline float RaceSaw(float Phase, float Inc)
	{
		float Value = 2.f * Phase - 1.f;
		if (Inc <= 0.f || Inc >= 0.5f)
		{
			return Value;
		}
		if (Phase < Inc)
		{
			const float X = Phase / Inc;
			Value -= X + X - X * X - 1.f;
		}
		else if (Phase > 1.f - Inc)
		{
			const float X = (Phase - 1.f) / Inc;
			Value -= X * X + X + X + 1.f;
		}
		return Value;
	}

	/** Filtro de estado variable de Chamberlin: devuelve el paso banda normalizado (ganancia ~1 en el corte). */
	struct FRaceSvf
	{
		float Low = 0.f;
		float Band = 0.f;

		float Process(float In, float G, float Damp)
		{
			Low += G * Band;
			const float High = In - Low - Damp * Band;
			Band += G * High;
			return Band * Damp;
		}
	};

	/** Resonador de dos polos (madera, chasquidos, gotas) excitado por impulsos. */
	struct FRaceResonator
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Tune(float FreqHz, float BandwidthHz, float Rate)
		{
			const float W = RaceTwoPi * FMath::Clamp(FreqHz, 20.f, Rate * 0.45f) / Rate;
			const float R = std::exp(-RacePi * BandwidthHz / Rate);
			C = 2.f * R * std::cos(W);
			R2 = R * R;
			Norm = std::sin(W);
		}

		float Process(float Excite)
		{
			float Y = Excite + C * Y1 - R2 * Y2;
			if (std::fabs(Y) < 1.0e-18f)
			{
				// Sin números subnormales cuando el resonador ya calló.
				Y = 0.f;
			}
			Y2 = Y1;
			Y1 = Y;
			return Y * Norm;
		}
	};

	/** Una nota de campanilla: instante de arranque (s), frecuencia (Hz), constante de caída (s) y nivel. */
	struct FRaceNote
	{
		float Start;
		float Freq;
		float Tau;
		float Level;
	};

	/** Número de notas de una tabla. */
	template <int32 N>
	constexpr int32 RaceNoteCount(const FRaceNote (&)[N])
	{
		return N;
	}

	/** Abrir caja: arpegio mayor corto que sube (do, mi, sol, do). */
	constexpr FRaceNote RaceBoxNotes[] =
	{
		{ 0.030f, 1046.5f, 0.090f, 0.34f },
		{ 0.085f, 1318.5f, 0.090f, 0.34f },
		{ 0.140f, 1568.0f, 0.095f, 0.36f },
		{ 0.195f, 2093.0f, 0.130f, 0.40f },
	};

	/** Protector puesto: seis notas rápidas que suben, la última larga. */
	constexpr FRaceNote RaceStarUpNotes[] =
	{
		{ 0.000f, 1046.5f, 0.100f, 0.30f },
		{ 0.050f, 1318.5f, 0.100f, 0.30f },
		{ 0.100f, 1568.0f, 0.100f, 0.32f },
		{ 0.150f, 2093.0f, 0.110f, 0.32f },
		{ 0.200f, 2637.0f, 0.120f, 0.32f },
		{ 0.250f, 3136.0f, 0.200f, 0.34f },
	};

	/** Protector que se acaba: las mismas notas, más lentas y cayendo. */
	constexpr FRaceNote RaceStarDownNotes[] =
	{
		{ 0.000f, 1568.0f, 0.130f, 0.30f },
		{ 0.090f, 1318.5f, 0.130f, 0.30f },
		{ 0.180f, 1046.5f, 0.130f, 0.30f },
		{ 0.270f, 784.0f, 0.140f, 0.30f },
		{ 0.360f, 659.3f, 0.150f, 0.30f },
		{ 0.450f, 523.3f, 0.160f, 0.32f },
	};

	/** Coco dorado: campanillas que se encienden una tras otra sobre el barrido. */
	constexpr FRaceNote RaceGoldenNotes[] =
	{
		{ 0.220f, 2093.0f, 0.220f, 0.24f },
		{ 0.360f, 2637.0f, 0.220f, 0.22f },
		{ 0.500f, 3136.0f, 0.220f, 0.22f },
		{ 0.620f, 2637.0f, 0.200f, 0.20f },
		{ 0.740f, 3136.0f, 0.200f, 0.20f },
		{ 0.860f, 4186.0f, 0.240f, 0.22f },
		{ 1.000f, 3136.0f, 0.160f, 0.18f },
	};

	/** Aterrizar: trino que baja por la escala pentatónica y acaba en una nota larga. */
	constexpr FRaceNote RaceLandNotes[] =
	{
		{ 0.060f, 2093.0f, 0.055f, 0.30f },
		{ 0.105f, 1760.0f, 0.055f, 0.30f },
		{ 0.150f, 1568.0f, 0.055f, 0.30f },
		{ 0.195f, 1318.5f, 0.055f, 0.30f },
		{ 0.240f, 1174.7f, 0.060f, 0.32f },
		{ 0.285f, 1046.5f, 0.070f, 0.32f },
		{ 0.335f, 784.0f, 0.140f, 0.36f },
	};

	static_assert(RaceNoteCount(RaceBoxNotes) <= RaceMaxNotes, "Demasiadas notas en RaceBoxNotes");
	static_assert(RaceNoteCount(RaceStarUpNotes) <= RaceMaxNotes, "Demasiadas notas en RaceStarUpNotes");
	static_assert(RaceNoteCount(RaceStarDownNotes) <= RaceMaxNotes, "Demasiadas notas en RaceStarDownNotes");
	static_assert(RaceNoteCount(RaceGoldenNotes) <= RaceMaxNotes, "Demasiadas notas en RaceGoldenNotes");
	static_assert(RaceNoteCount(RaceLandNotes) <= RaceMaxNotes, "Demasiadas notas en RaceLandNotes");

	/** Una voz sonando. */
	struct FRaceVoice
	{
		bool bActive = false;
		uint8 Kind = 0;
		float Age = 0.f;
		float Duration = 1.f;
		float Pitch = 1.f;
		float Gain = 1.f;
		float PhaseA = 0.f;
		float PhaseB = 0.f;
		float PhaseC = 0.f;
		float PhaseD = 0.f;
		FRaceSvf Filt[3];
		FRaceResonator ResA;
		FRaceResonator ResB;
		/** Dos pasos bajos de un polo en serie (retumbo, soplidos, sonidos apagados). */
		float Lp1 = 0.f;
		float Lp2 = 0.f;
		/** Reloj de los impulsos (clics, gotas, crepitar). */
		float PulseClock = 0.f;
		/** Envolvente del último chasquido (1 al arrancar: el primer chasquido es el de arranque). */
		float Burst = 1.f;
		int32 Pulses = 0;
		/** Fases de las notas de campanilla y de su parcial. */
		float NotePhase[RaceMaxNotes] = {};
		float NotePartial[RaceMaxNotes] = {};
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces con limitador suave al final. */
	class FRaceSfxCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FRaceSfxQueue& Queue)
		{
			FRaceSfxEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FRaceVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += RaceBlock)
			{
				const int32 Count = FMath::Min(RaceBlock, Frames - Frame);
				float MixBuf[RaceBlock] = {};
				for (FRaceVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					// Saturación suave (aproximación racional de tanh): varios efectos a la vez no recortan.
					const float X = FMath::Clamp(MixBuf[i] * MasterGain, -3.f, 3.f);
					const float Soft = X * (27.f + X * X) / (27.f + 9.f * X * X);
					for (int32 Ch = 0; Ch < Channels; ++Ch)
					{
						Out[(Frame + i) * Channels + Ch] = Soft;
					}
				}
			}
		}

	private:
		float Rate = 48000.f;
		float InvRate = 1.f / 48000.f;
		FRaceVoice Voices[RaceMaxVoices];
		uint32 SeedState = 0x9E3779B9u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FRaceSfxEvent& InEvent)
		{
			if (InEvent.Kind >= KindCount)
			{
				return;
			}
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < RaceMaxVoices; ++i)
			{
				if (!Voices[i].bActive)
				{
					Best = i;
					break;
				}
				if (Voices[i].Age > Oldest)
				{
					Oldest = Voices[i].Age;
					Best = i;
				}
			}
			FRaceVoice& Voice = Voices[Best];
			Voice = FRaceVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.25f, 4.f);
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			Voice.Duration = RaceDurations[InEvent.Kind];
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			switch (InEvent.Kind)
			{
			case KindStarUp:
				// Resonancia de purpurina del destello final (un filtro de estado variable pierde precisión tan arriba).
				Voice.ResA.Tune(6800.f * Voice.Pitch, 900.f, Rate);
				break;
			case KindSquawk:
				// Más grave, más largo: con Pitch 0,55 (pelícano) dura unos 0,75 s.
				Voice.Duration *= FMath::Clamp(std::pow(1.f / Voice.Pitch, 0.7f), 0.6f, 2.f);
				break;
			case KindScuttle:
				Voice.ResB.Tune(950.f * Voice.Pitch, 260.f, Rate);
				break;
			case KindBonk:
				Voice.ResA.Tune(590.f * Voice.Pitch, 90.f, Rate);
				Voice.ResB.Tune(1400.f * Voice.Pitch, 220.f, Rate);
				break;
			case KindCatch:
				Voice.ResA.Tune(880.f * Voice.Pitch, 220.f, Rate);
				break;
			default:
				break;
			}
		}

		/** Dibuja un bloque de una voz en su propio búfer y lo suma a la mezcla con la ganancia y el fundido de salida. */
		void RenderVoice(FRaceVoice& Voice, float* MixBuf, int32 Count)
		{
			float Buf[RaceBlock] = {};
			switch (Voice.Kind)
			{
			case KindBoxOpen: RenderBoxOpen(Voice, Buf, Count); break;
			case KindTurbo: RenderTurbo(Voice, Buf, Count); break;
			case KindGolden: RenderGolden(Voice, Buf, Count); break;
			case KindStarUp: RenderStarUp(Voice, Buf, Count); break;
			case KindStarDown: RenderStarDown(Voice, Buf, Count); break;
			case KindWhistle: RenderWhistle(Voice, Buf, Count); break;
			case KindSquawk: RenderSquawk(Voice, Buf, Count); break;
			case KindFlap: RenderFlap(Voice, Buf, Count); break;
			case KindScuttle: RenderScuttle(Voice, Buf, Count); break;
			case KindBoing: RenderBoing(Voice, Buf, Count); break;
			case KindBonk: RenderBonk(Voice, Buf, Count); break;
			case KindSplat: RenderSplat(Voice, Buf, Count); break;
			case KindFall: RenderFall(Voice, Buf, Count); break;
			case KindThrow: RenderThrow(Voice, Buf, Count); break;
			case KindWhirr: RenderWhirr(Voice, Buf, Count); break;
			case KindCatch: RenderCatch(Voice, Buf, Count); break;
			case KindRumble: RenderRumble(Voice, Buf, Count); break;
			case KindZap: RenderZap(Voice, Buf, Count); break;
			case KindNope: RenderNope(Voice, Buf, Count); break;
			case KindBeep: RenderBeep(Voice, Buf, Count); break;
			case KindLand: RenderLand(Voice, Buf, Count); break;
			default: break;
			}
			// Fundido de salida común, interpolado dentro del bloque.
			const float BlockLength = static_cast<float>(Count) * InvRate;
			const float FadeStart = RaceSmooth((Voice.Duration - Voice.Age) / RaceTail);
			const float FadeEnd = RaceSmooth((Voice.Duration - Voice.Age - BlockLength) / RaceTail);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Alpha = static_cast<float>(i) / static_cast<float>(Count);
				MixBuf[i] += Buf[i] * Voice.Gain * (FadeStart + (FadeEnd - FadeStart) * Alpha);
			}
			Voice.Age += BlockLength;
			if (Voice.Age >= Voice.Duration)
			{
				Voice.bActive = false;
			}
		}

		/** Suma un grupo de notas de campanilla (seno más un parcial) con ataque suave y caída exponencial. */
		void RenderNotes(FRaceVoice& Voice, float* Buf, int32 Count, const FRaceNote* Notes, int32 NumNotes, float PartialRatio, float PartialLevel) const
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			const int32 NoteLimit = FMath::Min(NumNotes, RaceMaxNotes);
			for (int32 n = 0; n < NoteLimit; ++n)
			{
				const FRaceNote& Note = Notes[n];
				const float Age0 = T - Note.Start;
				if (Age0 + static_cast<float>(Count) * Dt <= 0.f || Age0 > Note.Tau * 9.f)
				{
					continue;
				}
				const float Freq = Note.Freq * Voice.Pitch;
				const float Inc = Freq * Dt;
				const float PartInc = Freq * PartialRatio * Dt;
				const bool bPartial = PartialLevel > 0.f && Freq * PartialRatio < Rate * 0.45f;
				const float Level = Note.Level * std::exp(-FMath::Max(0.f, Age0) / Note.Tau);
				float Phase = Voice.NotePhase[n];
				float PartPhase = Voice.NotePartial[n];
				for (int32 i = 0; i < Count; ++i)
				{
					const float Since = Age0 + static_cast<float>(i) * Dt;
					if (Since < 0.f)
					{
						continue;
					}
					RaceAdvance(Phase, Inc);
					float Wave = RaceSin(Phase);
					if (bPartial)
					{
						RaceAdvance(PartPhase, PartInc);
						Wave += PartialLevel * RaceSin(PartPhase);
					}
					Buf[i] += Wave * RaceSmooth(Since / 0.003f) * Level;
				}
				Voice.NotePhase[n] = Phase;
				Voice.NotePartial[n] = PartPhase;
			}
		}

		void RenderBoxOpen(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// «¡Pop!» de corcho (seno que cae de tono con un soplido de aire) y arpegio brillante que sube.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float PopHz = (300.f + 950.f * std::exp(-T / 0.016f)) * Voice.Pitch;
			const float PopEnv = RaceSmooth(T / 0.0012f) * std::exp(-T / 0.035f);
			const float SnapK = std::exp(-Dt / 0.0018f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseA, PopHz * Dt);
				Voice.Burst *= SnapK;
				const float Snap = RaceNoise(Voice.NoiseState) * Voice.Burst;
				Buf[i] += 0.75f * RaceSin(Voice.PhaseA) * PopEnv + 0.22f * Snap;
			}
			RenderNotes(Voice, Buf, Count, RaceBoxNotes, RaceNoteCount(RaceBoxNotes), 2.f, 0.28f);
		}

		void RenderTurbo(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// «¡Fiuuum!»: ruido filtrado que sube y se abre, con un tono que sube debajo y un golpe seco de arranque.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float Sweep = RaceSmooth(T / 0.62f);
			const float NoiseG = RaceSvfCoef(320.f * std::pow(15.f, Sweep) * Voice.Pitch, Rate);
			const float NoiseEnv = RaceSmooth(T / 0.09f) * RaceSmooth((0.9f - T) / 0.32f);
			const float ToneHz = 190.f * std::pow(5.5f, Sweep) * Voice.Pitch;
			const float ToneEnv = RaceSmooth(T / 0.12f) * RaceSmooth((0.9f - T) / 0.3f);
			const float ThumpHz = (46.f + 95.f * std::exp(-T / 0.05f)) * Voice.Pitch;
			const float ThumpEnv = RaceSmooth(T / 0.003f) * std::exp(-T / 0.075f);
			const float SnapK = std::exp(-Dt / 0.004f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseA, ToneHz * Dt);
				RaceAdvance(Voice.PhaseB, ThumpHz * Dt);
				Voice.Burst *= SnapK;
				const float Nz = RaceNoise(Voice.NoiseState);
				const float Whoosh = Voice.Filt[0].Process(Nz, NoiseG, 0.4f) * NoiseEnv;
				const float Tone = (RaceSin(Voice.PhaseA) + 0.3f * RaceSin(2.f * Voice.PhaseA)) * ToneEnv;
				const float Thump = RaceSin(Voice.PhaseB) * ThumpEnv;
				Buf[i] += 1.0f * Whoosh + 0.14f * Tone + 0.45f * Thump + 0.22f * Nz * Voice.Burst;
			}
		}

		void RenderGolden(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Como el turbo, más largo y brillante: barrido de ruido, dos tonos (quinta) que suben y campanillas encima.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float Sweep = RaceSmooth(T / 0.95f);
			const float NoiseG = RaceSvfCoef(450.f * std::pow(13.f, Sweep) * Voice.Pitch, Rate);
			const float NoiseEnv = RaceSmooth(T / 0.1f) * RaceSmooth((1.4f - T) / 0.55f);
			const float ToneHz = 240.f * std::pow(6.f, Sweep) * Voice.Pitch;
			const float ToneEnv = RaceSmooth(T / 0.14f) * RaceSmooth((1.4f - T) / 0.5f);
			const float ThumpHz = (52.f + 110.f * std::exp(-T / 0.05f)) * Voice.Pitch;
			const float ThumpEnv = RaceSmooth(T / 0.003f) * std::exp(-T / 0.085f);
			const float SnapK = std::exp(-Dt / 0.004f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseA, ToneHz * Dt);
				RaceAdvance(Voice.PhaseC, ToneHz * 1.5f * Dt);
				RaceAdvance(Voice.PhaseB, ThumpHz * Dt);
				Voice.Burst *= SnapK;
				const float Nz = RaceNoise(Voice.NoiseState);
				const float Whoosh = Voice.Filt[0].Process(Nz, NoiseG, 0.35f) * NoiseEnv;
				const float Tone = (RaceSin(Voice.PhaseA) + 0.3f * RaceSin(2.f * Voice.PhaseA) + 0.6f * RaceSin(Voice.PhaseC)) * ToneEnv;
				const float Thump = RaceSin(Voice.PhaseB) * ThumpEnv;
				Buf[i] += 0.8f * Whoosh + 0.11f * Tone + 0.45f * Thump + 0.2f * Nz * Voice.Burst;
			}
			RenderNotes(Voice, Buf, Count, RaceGoldenNotes, RaceNoteCount(RaceGoldenNotes), 2.76f, 0.2f);
		}

		void RenderStarUp(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Arpegio brillante y rápido que sube, con un destello agudo al final.
			const float T = Voice.Age;
			const float Dt = InvRate;
			RenderNotes(Voice, Buf, Count, RaceStarUpNotes, RaceNoteCount(RaceStarUpNotes), 2.f, 0.25f);
			const float Glint = T - 0.27f;
			if (Glint > -0.02f)
			{
				const float GlintEnv = RaceSmooth(Glint / 0.012f) * std::exp(-FMath::Max(0.f, Glint) / 0.13f);
				for (int32 i = 0; i < Count; ++i)
				{
					RaceAdvance(Voice.PhaseA, 4186.f * Voice.Pitch * Dt);
					RaceAdvance(Voice.PhaseC, 5274.f * Voice.Pitch * Dt);
					RaceAdvance(Voice.PhaseB, 22.f * Dt);
					const float Shimmer = 0.6f + 0.4f * RaceSin(Voice.PhaseB);
					const float Sparkle = 0.12f * Voice.ResA.Process(RaceNoise(Voice.NoiseState));
					Buf[i] += ((0.22f * RaceSin(Voice.PhaseA) + 0.16f * RaceSin(Voice.PhaseC)) * Shimmer + Sparkle) * GlintEnv;
				}
			}
		}

		void RenderStarDown(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// El mismo arpegio, más lento y cayendo, y un «pof» blando cuando se acaba.
			const float T = Voice.Age;
			const float Dt = InvRate;
			RenderNotes(Voice, Buf, Count, RaceStarDownNotes, RaceNoteCount(RaceStarDownNotes), 2.f, 0.2f);
			const float Puff = T - 0.5f;
			if (Puff > -0.01f)
			{
				const float PuffAge = FMath::Max(0.f, Puff);
				const float ThumpHz = (60.f + 110.f * std::exp(-PuffAge / 0.03f)) * Voice.Pitch;
				const float ThumpEnv = RaceSmooth(Puff / 0.005f) * std::exp(-PuffAge / 0.07f);
				const float AirEnv = RaceSmooth(Puff / 0.008f) * std::exp(-PuffAge / 0.06f);
				const float AirK = RaceLpCoef(1400.f * Voice.Pitch, Rate);
				for (int32 i = 0; i < Count; ++i)
				{
					RaceAdvance(Voice.PhaseA, ThumpHz * Dt);
					Voice.Lp1 += AirK * (RaceNoise(Voice.NoiseState) - Voice.Lp1);
					Voice.Lp2 += AirK * (Voice.Lp1 - Voice.Lp2);
					Buf[i] += 0.4f * RaceSin(Voice.PhaseA) * ThumpEnv + 1.3f * Voice.Lp2 * AirEnv;
				}
			}
		}

		void RenderWhistle(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Silbato del sargento: dos pitidos (el segundo más largo y algo más agudo), cada uno con el trino de la bolita.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const bool bSecond = T >= 0.45f;
			const float BlastStart = bSecond ? 0.50f : 0.02f;
			const float BlastEnd = bSecond ? 0.96f : 0.40f;
			const float BaseHz = (bSecond ? 3080.f : 2860.f) * Voice.Pitch;
			const float Chirp = 1.f + 0.07f * std::exp(-FMath::Max(0.f, T - BlastStart) / 0.04f);
			const float BreathG = RaceSvfCoef(BaseHz, Rate);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Tl = T + static_cast<float>(i) * Dt;
				const float Env = RaceSmooth((Tl - BlastStart) / 0.015f) * RaceSmooth((BlastEnd - Tl) / 0.035f);
				RaceAdvance(Voice.PhaseB, 27.f * Dt);
				RaceAdvance(Voice.PhaseA, BaseHz * Chirp * (1.f + 0.05f * RaceSin(Voice.PhaseB)) * Dt);
				const float Pea = 1.f - 0.25f * (0.5f + 0.5f * RaceSin(Voice.PhaseB + 0.25f));
				const float Tone = (RaceSin(Voice.PhaseA) + 0.14f * RaceSin(2.f * Voice.PhaseA)) * Pea;
				const float Breath = Voice.Filt[0].Process(RaceNoise(Voice.NoiseState), BreathG, 0.09f);
				Buf[i] += (0.48f * Tone + 0.8f * Breath) * Env;
			}
		}

		void RenderSquawk(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Graznido: sierra con vibrato y aspereza pasada por tres formantes que se abren, con un barrido de tono que sube
			// rápido y cae. El pelícano (Pitch bajo) es el mismo dibujo más grave y más largo.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float U = FMath::Clamp(T / Voice.Duration, 0.f, 1.f);
			const float Contour = (0.72f + 0.6f * RaceSmooth(U / 0.14f)) * (1.f - 0.5f * RaceSmooth((U - 0.22f) / 0.78f));
			const float BaseHz = 950.f * Voice.Pitch * Contour;
			const float Open = RaceSmooth(U / 0.25f);
			const float G1 = RaceSvfCoef((600.f + 320.f * Open) * Voice.Pitch, Rate);
			const float G2 = RaceSvfCoef((1450.f + 650.f * Open) * Voice.Pitch, Rate);
			const float G3 = RaceSvfCoef(2900.f * Voice.Pitch, Rate);
			const float Env = RaceSmooth(U / 0.035f) * (0.55f + 0.45f * std::exp(-U / 0.3f)) * RaceSmooth((1.f - U) / 0.25f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseB, 30.f * Dt);
				RaceAdvance(Voice.PhaseC, 78.f * Dt);
				const float Inc = BaseHz * (1.f + 0.04f * RaceSin(Voice.PhaseB)) * Dt;
				RaceAdvance(Voice.PhaseA, Inc);
				const float Source = RaceSaw(Voice.PhaseA, Inc) + 0.12f * RaceNoise(Voice.NoiseState);
				const float Voiced = 0.9f * Voice.Filt[0].Process(Source, G1, 0.5f)
					+ 0.7f * Voice.Filt[1].Process(Source, G2, 0.5f)
					+ 0.4f * Voice.Filt[2].Process(Source, G3, 0.6f);
				const float Rasp = 0.72f + 0.28f * RaceSin(Voice.PhaseC);
				Buf[i] += Voiced * Rasp * Env * 0.8f;
			}
		}

		void RenderFlap(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Un batido grande: empujón de aire grave, latido de bajos y un soplo de plumas que baja.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float LpK = RaceLpCoef(300.f * Voice.Pitch, Rate);
			const float WindEnv = RaceSmooth(T / 0.03f) * std::exp(-T / 0.085f);
			const float BodyHz = (48.f + 70.f * std::exp(-T / 0.09f)) * Voice.Pitch;
			const float BodyEnv = RaceSmooth(T / 0.02f) * std::exp(-T / 0.11f);
			const float AirG = RaceSvfCoef((950.f - 500.f * RaceSmooth(T / 0.3f)) * Voice.Pitch, Rate);
			const float AirEnv = RaceSmooth(T / 0.045f) * std::exp(-T / 0.09f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseA, BodyHz * Dt);
				const float Nz = RaceNoise(Voice.NoiseState);
				Voice.Lp1 += LpK * (Nz - Voice.Lp1);
				Voice.Lp2 += LpK * (Voice.Lp1 - Voice.Lp2);
				const float Air = Voice.Filt[0].Process(Nz, AirG, 0.6f);
				Buf[i] += 5.f * Voice.Lp2 * WindEnv + 0.5f * RaceSin(Voice.PhaseA) * BodyEnv + 0.6f * Air * AirEnv;
			}
		}

		void RenderScuttle(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Ráfaga de unos diez clics secos, algo irregulares, cada uno con su tono y un golpecito hueco debajo.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float SnapK = std::exp(-Dt / 0.0012f);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Tl = T + static_cast<float>(i) * Dt;
				float Excite = 0.f;
				Voice.PulseClock -= Dt;
				if (Voice.PulseClock <= 0.f && Voice.Pulses < 10 && Tl < Voice.Duration - 0.06f)
				{
					Voice.PulseClock = 0.030f + 0.024f * RaceUnit(Voice.NoiseState);
					Voice.Burst = 0.65f + 0.35f * RaceUnit(Voice.NoiseState);
					Excite = Voice.Burst;
					Voice.ResA.Tune((2300.f + 2200.f * RaceUnit(Voice.NoiseState)) * Voice.Pitch, 900.f, Rate);
					++Voice.Pulses;
				}
				Voice.Burst *= SnapK;
				const float Snap = RaceNoise(Voice.NoiseState) * Voice.Burst;
				const float Tick = Voice.ResA.Process(Excite * 0.5f + Snap * 0.5f);
				const float Hollow = Voice.ResB.Process(Excite * 0.4f);
				Buf[i] += 0.65f * Tick + 0.18f * Snap + 0.55f * Hollow;
			}
		}

		void RenderBoing(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Muelle: un seno cuyo tono oscila con fuerza al principio y se va calmando, con un clic de arranque.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float Swing = std::exp(-T / 0.15f);
			const float Drift = 1.f + 0.15f * T / 0.45f;
			const float Env = RaceSmooth(T / 0.003f) * std::exp(-T / 0.16f);
			const float SnapK = std::exp(-Dt / 0.002f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseB, 10.5f * Dt);
				RaceAdvance(Voice.PhaseA, 410.f * Voice.Pitch * Drift * (1.f + 0.7f * Swing * RaceSin(Voice.PhaseB)) * Dt);
				Voice.Burst *= SnapK;
				const float Wave = RaceSin(Voice.PhaseA) + 0.3f * RaceSin(3.f * Voice.PhaseA);
				Buf[i] += 0.7f * Wave * Env + 0.15f * RaceNoise(Voice.NoiseState) * Voice.Burst;
			}
		}

		void RenderBonk(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Golpe seco y hueco: seno grave que cae de tono y dos resonancias de madera excitadas por un chasquido.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float BodyHz = (230.f + 260.f * std::exp(-T / 0.022f)) * Voice.Pitch;
			const float BodyEnv = RaceSmooth(T / 0.0015f) * std::exp(-T / 0.085f);
			const float SnapK = std::exp(-Dt / 0.002f);
			for (int32 i = 0; i < Count; ++i)
			{
				float Excite = 0.f;
				if (Voice.Pulses == 0)
				{
					Excite = 1.f;
					++Voice.Pulses;
				}
				RaceAdvance(Voice.PhaseA, BodyHz * Dt);
				Voice.Burst *= SnapK;
				const float Snap = RaceNoise(Voice.NoiseState) * Voice.Burst;
				const float Ring = 0.5f * Voice.ResA.Process(Excite * 0.6f + Snap * 0.3f) + 0.3f * Voice.ResB.Process(Excite * 0.5f + Snap * 0.3f);
				Buf[i] += 0.6f * RaceSin(Voice.PhaseA) * BodyEnv + 0.75f * Ring + 0.12f * Snap;
			}
		}

		void RenderSplat(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Pegote en el suelo: golpe grave blando, chapoteo de ruido que se cierra y unas gotas que rebotan después.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float ThumpHz = (55.f + 120.f * std::exp(-T / 0.035f)) * Voice.Pitch;
			const float ThumpEnv = RaceSmooth(T / 0.003f) * std::exp(-T / 0.08f);
			const float SquelchG = RaceSvfCoef((420.f + 2600.f * std::exp(-T / 0.045f)) * Voice.Pitch, Rate);
			const float SquelchEnv = RaceSmooth(T / 0.003f) * std::exp(-T / 0.085f);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Tl = T + static_cast<float>(i) * Dt;
				float Excite = 0.f;
				Voice.PulseClock -= Dt;
				if (Voice.PulseClock <= 0.f && Voice.Pulses < 5 && Tl >= 0.05f)
				{
					Voice.PulseClock = 0.035f + 0.05f * RaceUnit(Voice.NoiseState);
					Excite = 0.7f + 0.3f * RaceUnit(Voice.NoiseState);
					Voice.ResA.Tune((650.f + 900.f * RaceUnit(Voice.NoiseState)) * Voice.Pitch, 90.f, Rate);
					++Voice.Pulses;
				}
				RaceAdvance(Voice.PhaseA, ThumpHz * Dt);
				RaceAdvance(Voice.PhaseB, 46.f * Dt);
				const float Wet = 0.65f + 0.35f * RaceSin(Voice.PhaseB);
				const float Squelch = Voice.Filt[0].Process(RaceNoise(Voice.NoiseState), SquelchG, 0.55f) * SquelchEnv * Wet;
				Buf[i] += 0.65f * RaceSin(Voice.PhaseA) * ThumpEnv + 0.9f * Squelch + 0.4f * Voice.ResA.Process(Excite);
			}
		}

		void RenderFall(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Silbido descendente de dibujos animados: el tono baja de golpe con un vibrato leve y el volumen se va apagando.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float Glide = std::exp(-1.966f * FMath::Min(T, 0.85f) / 0.85f);
			const float Env = RaceSmooth(T / 0.03f) * (1.f - 0.55f * T / 0.9f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseB, 6.5f * Dt);
				const float Hz = 2500.f * Glide * Voice.Pitch * (1.f + 0.02f * RaceSin(Voice.PhaseB));
				RaceAdvance(Voice.PhaseA, Hz * Dt);
				const float Air = Voice.Filt[0].Process(RaceNoise(Voice.NoiseState), RaceSvfCoef(Hz, Rate), 0.12f);
				Buf[i] += (0.55f * (RaceSin(Voice.PhaseA) + 0.18f * RaceSin(2.f * Voice.PhaseA)) + 0.5f * Air) * Env;
			}
		}

		void RenderThrow(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Soplido corto: ruido cuyo color se abre al pasar y un soplo grave debajo.
			const float T = Voice.Age;
			const float G = RaceSvfCoef((900.f + 1500.f * RaceSmooth(T / 0.13f)) * Voice.Pitch, Rate);
			const float Env = RaceSmooth(T / 0.025f) * RaceSmooth((0.25f - T) / 0.12f);
			const float SweepK = RaceLpCoef((550.f + 1800.f * RaceSmooth(T / 0.12f)) * Voice.Pitch, Rate);
			const float PuffEnv = RaceSmooth(T / 0.02f) * std::exp(-T / 0.06f);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Nz = RaceNoise(Voice.NoiseState);
				Voice.Lp1 += SweepK * (Nz - Voice.Lp1);
				Voice.Lp2 += SweepK * (Voice.Lp1 - Voice.Lp2);
				Buf[i] += 1.3f * Voice.Lp2 * Env + 0.7f * Voice.Filt[0].Process(Nz, G, 0.7f) * PuffEnv;
			}
		}

		void RenderWhirr(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Disco que gira: dos sierras algo desafinadas (que baten) suavizadas, con vibrato, aleteo de giro y un soplo de aire.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float BaseHz = (235.f + 95.f * RaceSmooth(T / 0.25f)) * Voice.Pitch;
			const float Env = RaceSmooth(T / 0.08f) * RaceSmooth((0.9f - T) / 0.3f);
			const float LpK = RaceLpCoef(1800.f * Voice.Pitch, Rate);
			const float AirG = RaceSvfCoef(2600.f * Voice.Pitch, Rate);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseC, 5.5f * Dt);
				RaceAdvance(Voice.PhaseD, 23.f * Dt);
				const float Hz = BaseHz * (1.f + 0.04f * RaceSin(Voice.PhaseC));
				RaceAdvance(Voice.PhaseA, Hz * Dt);
				RaceAdvance(Voice.PhaseB, Hz * 1.011f * Dt);
				const float Buzz = 0.5f * (RaceSaw(Voice.PhaseA, Hz * Dt) + RaceSaw(Voice.PhaseB, Hz * 1.011f * Dt));
				Voice.Lp1 += LpK * (Buzz - Voice.Lp1);
				Voice.Lp2 += LpK * (Voice.Lp1 - Voice.Lp2);
				const float Flutter = 0.72f + 0.28f * RaceSin(Voice.PhaseD);
				const float Air = Voice.Filt[0].Process(RaceNoise(Voice.NoiseState), AirG, 0.2f);
				Buf[i] += (0.9f * Voice.Lp2 * Flutter + 0.3f * Air) * Env;
			}
		}

		void RenderCatch(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// «¡Clap!» seco: tres golpes de ruido casi pegados, un cuerpo hueco en el último y un rabo cortísimo.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float SnapK = std::exp(-Dt / 0.0045f);
			const float G = RaceSvfCoef(1900.f * Voice.Pitch, Rate);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Tl = T + static_cast<float>(i) * Dt;
				float Excite = 0.f;
				if (Voice.Pulses < 3 && Tl >= 0.0085f * static_cast<float>(Voice.Pulses))
				{
					Voice.Burst = Voice.Pulses == 2 ? 1.f : 0.8f;
					Excite = Voice.Pulses == 2 ? 0.9f : 0.3f;
					++Voice.Pulses;
				}
				Voice.Burst *= SnapK;
				const float Tail = 0.45f * RaceSmooth((Tl - 0.012f) / 0.006f) * std::exp(-FMath::Max(0.f, Tl - 0.018f) / 0.028f);
				const float Band = Voice.Filt[0].Process(RaceNoise(Voice.NoiseState), G, 0.55f);
				Buf[i] += 1.3f * Band * (Voice.Burst + Tail) + 0.5f * Voice.ResA.Process(Excite);
			}
		}

		void RenderRumble(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Trueno: ruido muy grave que rueda (modulación lenta del volumen y del corte), un latido de bajos y un par de
			// chasquidos lejanos al principio.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float LpK = RaceLpCoef((190.f + 100.f * std::sin(RaceTwoPi * 0.9f * T + 0.6f)) * Voice.Pitch, Rate);
			const float CrackAttack = RaceLpCoef(70.f, Rate);
			const float Roll = 0.62f + 0.26f * std::sin(RaceTwoPi * 1.25f * T) + 0.12f * std::sin(RaceTwoPi * 3.3f * T + 1.3f);
			const float Env = RaceSmooth(T / 0.18f) * (0.15f + 0.85f * std::exp(-T / 1.2f)) * RaceSmooth((2.2f - T) / 0.6f);
			const float SubHz = 42.f * Voice.Pitch * (1.f + 0.15f * std::sin(RaceTwoPi * 0.7f * T));
			const float CrackG = RaceSvfCoef(700.f * Voice.Pitch, Rate);
			const float CrackK = std::exp(-Dt / 0.035f);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Tl = T + static_cast<float>(i) * Dt;
				if (Voice.Pulses < 3)
				{
					const float HitTime = Voice.Pulses == 0 ? 0.f : (Voice.Pulses == 1 ? 0.46f : 1.1f);
					if (Tl >= HitTime)
					{
						Voice.Burst = Voice.Pulses == 0 ? 0.5f : (Voice.Pulses == 1 ? 0.22f : 0.14f);
						++Voice.Pulses;
					}
				}
				Voice.Burst *= CrackK;
				RaceAdvance(Voice.PhaseA, SubHz * Dt);
				const float Nz = RaceNoise(Voice.NoiseState);
				Voice.Lp1 += LpK * (Nz - Voice.Lp1);
				Voice.Lp2 += LpK * (Voice.Lp1 - Voice.Lp2);
				// Los chasquidos entran con un ataque de unos milisegundos (sin «tic» al dispararse).
				Voice.PhaseC += CrackAttack * (Voice.Burst - Voice.PhaseC);
				const float Crack = Voice.Filt[0].Process(Nz, CrackG, 0.5f) * Voice.PhaseC * 2.f;
				Buf[i] += (4.5f * Voice.Lp2 + 0.4f * RaceSin(Voice.PhaseA)) * Roll * Env + Crack;
			}
		}

		void RenderZap(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Rayo: chasquido muy agudo, un barrido de tono que cae rápido y un crepitar de impulsos al azar que se espacian.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float SnapK = std::exp(-Dt / 0.0035f);
			const float ChirpHz = (1800.f + 5200.f * std::exp(-T / 0.022f)) * Voice.Pitch;
			const float ChirpEnv = RaceSmooth(T / 0.001f) * std::exp(-T / 0.03f);
			const float CrackleEnv = std::exp(-T / 0.1f);
			const float Density = 1.f + 3.f * T / 0.35f;
			for (int32 i = 0; i < Count; ++i)
			{
				float Excite = 0.f;
				Voice.PulseClock -= Dt;
				if (Voice.PulseClock <= 0.f)
				{
					Voice.PulseClock = (0.0009f + 0.0045f * RaceUnit(Voice.NoiseState)) * Density;
					const float Sign = RaceNoise(Voice.NoiseState) >= 0.f ? 1.f : -1.f;
					Excite = Sign * (0.45f + 0.55f * RaceUnit(Voice.NoiseState));
					Voice.ResA.Tune((3800.f + 4200.f * RaceUnit(Voice.NoiseState)) * Voice.Pitch, 1600.f, Rate);
				}
				RaceAdvance(Voice.PhaseA, ChirpHz * Dt);
				Voice.Burst *= SnapK;
				const float Snap = RaceNoise(Voice.NoiseState) * Voice.Burst;
				Buf[i] += 0.45f * Snap + 0.22f * RaceSin(Voice.PhaseA) * ChirpEnv + 1.1f * Voice.ResA.Process(Excite) * CrackleEnv;
			}
		}

		void RenderNope(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Dos notas graves que bajan (un «uh-oh» apagado): sierra muy filtrada con un seno debajo y una ligera caída de tono.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const bool bSecond = T >= 0.21f;
			const float NoteStart = bSecond ? 0.22f : 0.f;
			const float NoteEnd = bSecond ? 0.44f : 0.2f;
			const float NoteHz = (bSecond ? 185.f : 220.f) * Voice.Pitch;
			const float LpK = RaceLpCoef(520.f * Voice.Pitch, Rate);
			for (int32 i = 0; i < Count; ++i)
			{
				const float Tl = T + static_cast<float>(i) * Dt;
				const float Env = RaceSmooth((Tl - NoteStart) / 0.02f) * RaceSmooth((NoteEnd - Tl) / 0.05f);
				const float Hz = NoteHz * (1.f - 0.06f * RaceSmooth((Tl - NoteStart) / 0.2f));
				RaceAdvance(Voice.PhaseA, Hz * Dt);
				RaceAdvance(Voice.PhaseB, Hz * 0.5f * Dt);
				Voice.Lp1 += LpK * (RaceSaw(Voice.PhaseA, Hz * Dt) - Voice.Lp1);
				Voice.Lp2 += LpK * (Voice.Lp1 - Voice.Lp2);
				Buf[i] += (0.6f * Voice.Lp2 + 0.2f * RaceSin(Voice.PhaseB)) * Env;
			}
		}

		void RenderBeep(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Pitido electrónico de aviso: casi cuadrado, agudo y con los bordes suaves.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float Env = RaceSmooth(T / 0.004f) * RaceSmooth((Voice.Duration - T) / 0.02f);
			const float Hz = 1975.f * Voice.Pitch;
			const bool bFifth = Hz * 5.f < Rate * 0.45f;
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseA, Hz * Dt);
				float Wave = RaceSin(Voice.PhaseA) + 0.33f * RaceSin(3.f * Voice.PhaseA);
				if (bFifth)
				{
					Wave += 0.2f * RaceSin(5.f * Voice.PhaseA);
				}
				Buf[i] += Wave * Env * 0.33f;
			}
		}

		void RenderLand(FRaceVoice& Voice, float* Buf, int32 Count)
		{
			// Despedida del pelícano: «pop» que sube de tono y un trino alegre que baja por la escala.
			const float T = Voice.Age;
			const float Dt = InvRate;
			const float PopHz = 250.f * (1.f + 2.8f * (1.f - std::exp(-T / 0.012f))) * Voice.Pitch;
			const float PopEnv = RaceSmooth(T / 0.0015f) * std::exp(-T / 0.03f);
			const float SnapK = std::exp(-Dt / 0.0015f);
			for (int32 i = 0; i < Count; ++i)
			{
				RaceAdvance(Voice.PhaseA, PopHz * Dt);
				Voice.Burst *= SnapK;
				Buf[i] += 0.75f * RaceSin(Voice.PhaseA) * PopEnv + 0.18f * RaceNoise(Voice.NoiseState) * Voice.Burst;
			}
			RenderNotes(Voice, Buf, Count, RaceLandNotes, RaceNoteCount(RaceLandNotes), 2.f, 0.25f);
		}
	};

	/** Generador del hilo de render de audio: solo C++ puro y la cola compartida. */
	class FRaceSfxGenerator final : public ISoundGenerator
	{
	public:
		FRaceSfxGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FRaceSfxQueue, ESPMode::ThreadSafe>& InQueue)
			: Queue(InQueue)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			Core.Init(InSampleRate);
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			if (Queue.IsValid())
			{
				Core.Render(OutAudio, Frames, OutChannels, *Queue);
			}
			else
			{
				FMemory::Memzero(OutAudio, sizeof(float) * Frames * OutChannels);
			}
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i)
			{
				OutAudio[i] = 0.f;
			}
			return NumSamples;
		}

	private:
		TSharedPtr<FRaceSfxQueue, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		FRaceSfxCore Core;
	};
}

static_assert(static_cast<uint8>(ETNRaceSound::BoxOpen) == TNRaceItemDSP::KindBoxOpen, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Turbo) == TNRaceItemDSP::KindTurbo, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Golden) == TNRaceItemDSP::KindGolden, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::StarUp) == TNRaceItemDSP::KindStarUp, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::StarDown) == TNRaceItemDSP::KindStarDown, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Whistle) == TNRaceItemDSP::KindWhistle, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Squawk) == TNRaceItemDSP::KindSquawk, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Flap) == TNRaceItemDSP::KindFlap, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Scuttle) == TNRaceItemDSP::KindScuttle, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Boing) == TNRaceItemDSP::KindBoing, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Bonk) == TNRaceItemDSP::KindBonk, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Splat) == TNRaceItemDSP::KindSplat, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Fall) == TNRaceItemDSP::KindFall, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Throw) == TNRaceItemDSP::KindThrow, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Whirr) == TNRaceItemDSP::KindWhirr, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Catch) == TNRaceItemDSP::KindCatch, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Rumble) == TNRaceItemDSP::KindRumble, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Zap) == TNRaceItemDSP::KindZap, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Nope) == TNRaceItemDSP::KindNope, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Beep) == TNRaceItemDSP::KindBeep, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Land) == TNRaceItemDSP::KindLand, "ETNRaceSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNRaceSound::Count) == TNRaceItemDSP::KindCount, "ETNRaceSound y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_RaceItemSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_RaceItemSynthComponent::UTN_RaceItemSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	SfxQueue = MakeShared<TNRaceItemDSP::FRaceSfxQueue, ESPMode::ThreadSafe>();
}

void UTN_RaceItemSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_RaceItemSynthComponent::ConfigureSpatial()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Volumen pleno dentro de InnerRadius, caída natural hasta FalloffDistance más allá y agudos que se apagan lejos.
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
	Att.LPFFrequencyAtMax = 2500.f;
	Att.NonSpatializedRadiusStart = InnerRadius * 0.3f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.1f;
}

bool UTN_RaceItemSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_RaceItemSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNRaceItemDSP::FRaceSfxGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SfxQueue);
}

bool UTN_RaceItemSynthComponent::IsListenerNear() const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 300.f;
}

void UTN_RaceItemSynthComponent::Play(ETNRaceSound Sound, float Pitch, float Volume)
{
	if (!SfxQueue.IsValid() || Volume <= 0.f || Sound >= ETNRaceSound::Count || !IsListenerNear())
	{
		return;
	}
	if (!IsPlaying())
	{
		// Lo que usa la tortuga propia (UTN_RaceItemComponent cuelga de ella) tiene voz reservada.
		TNAudioVoices::Apply(*this, TNAudioVoices::RankForOwner(GetOwner()));
		Start();
	}
	SfxQueue->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNRaceItemDSP::FRaceSfxEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	SfxQueue->Push(Shot);
	// El efecto más largo (el trueno) dura 2,2 s: con 3 s de margen no se corta ninguna cola.
	SilenceLeft = 3.f;
	SetComponentTickEnabled(true);
}

void UTN_RaceItemSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SilenceLeft -= DeltaTime;
	if (SilenceLeft <= 0.f)
	{
		// Callado: se libera la voz del mezclador hasta el próximo disparo.
		if (IsPlaying())
		{
			Stop();
		}
		SetComponentTickEnabled(false);
	}
}

UTN_RaceItemSynthComponent* UTN_RaceItemSynthComponent::AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius, float InFalloff)
{
	if (!InOwner)
	{
		return nullptr;
	}
	const UWorld* OwnerWorld = InOwner->GetWorld();
	if (!OwnerWorld || !OwnerWorld->IsGameWorld() || OwnerWorld->GetNetMode() == NM_DedicatedServer || !FApp::CanEverRenderAudio())
	{
		return nullptr;
	}

	UTN_RaceItemSynthComponent* Comp = NewObject<UTN_RaceItemSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloff);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
		// Colocado antes de registrar: la distancia al oyente ya sale del sitio bueno desde el primer disparo.
		Comp->SetRelativeLocation(RootComp->GetComponentTransform().InverseTransformPosition(InWorldLocation));
	}
	Comp->RegisterComponent();
	if (!Comp->GetAttachParent())
	{
		Comp->SetWorldLocation(InWorldLocation);
	}
	InOwner->AddInstanceComponent(Comp);
	return Comp;
}
