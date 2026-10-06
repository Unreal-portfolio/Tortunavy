#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "Audio/TN_AudioVoices.h"
#include "Game/TN_CoopItems.h"
#include "Multiplayer/TN_LocalViews.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "TN_ProcMapAmbientFX.h"
#include "../TN_LootGlowKit.h"
#include "Core/TN_InventoryTypes.h"
#include "Core/TN_Log.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_PickupInteractableBase.h"
#include "AudioDevice.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"
#include "TimerManager.h"
#include <atomic>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Ajustes y variables de consola
// ─────────────────────────────────────────────────────────────────────────────

namespace TNSearchSpotDetail
{
	TAutoConsoleVariable<float> CVarSearchLuck(TEXT("tn.Search.Luck"), -1.f,
		TEXT("Rebuscar decorados: fuerza la probabilidad de que salga un objeto (0-1; 1 = siempre, 0 = nunca). -1 = la del actor (55 %)."));
	TAutoConsoleVariable<float> CVarSearchSeconds(TEXT("tn.Search.Seconds"), -1.f,
		TEXT("Rebuscar decorados: segundos que hay que mantener la tecla. -1 = los del actor (1,3 s)."));
	TAutoConsoleVariable<int32> CVarSearchShow(TEXT("tn.Search.Show"), 0,
		TEXT("Rebuscar decorados: 1 dibuja una baliza y la huella de cada decorado buscable a menos de 300 m ")
		TEXT("(dorado = por buscar, naranja = rebuscando, verde = salió algo, gris = vacío)."));

	/** Saltito del objeto: duración (s), altura (cm) y escala con la que asoma. */
	constexpr float HopSeconds = 0.5f;
	constexpr float HopApex = 110.f;
	constexpr float HopStartScale = 0.3f;
	/** Los efectos de un resultado solo se ven si llegan con menos de esto (s) desde que pasó en el servidor. */
	constexpr double FreshOutcomeSeconds = 1.5;
	/** Perímetro (cm) de huella por cada chispita de «aquí se puede rebuscar» de más (los decorados grandes, más). */
	constexpr float HintPerimeterPerSparkle = 3000.f;
	constexpr int32 MaxHintSparkles = 4;
	/** Alcance de interacción (cm): el del escaneo de la tortuga (ATortugaCharacter::MaxInteractionDistance; 250, antes 350). */
	constexpr float Reach = ATortugaCharacter::DefaultInteractionDistance;
	/** Intervalo del tick sin nada que hacer (s): solo mira si la cámara se acerca. */
	constexpr float IdleTickInterval = 0.3f;
	/** Segundos que siguen moviéndose las partículas tras el último estallido. */
	constexpr double FxTail = 2.5;

	/** Anillo fijo: margen (cm) entre el borde de la huella y donde empiezan los guiones; lo que tarda en salir y en irse (1/s). */
	constexpr float MarkerMargin = 25.f;
	constexpr float MarkerAppearSpeed = 3.f;
	constexpr float MarkerHideSpeed = 4.f;
	/** Anillo fijo: distancia (cm) de la cámara hasta la que se anima cada fotograma (la AnimRange de los objetos; más lejos, al ritmo lento del tick). */
	constexpr float MarkerAnimDistance = 4000.f;
	/** Anillo fijo mientras alguien rebusca: cuánto más deprisa gira y cuánto late (fracción del radio). */
	constexpr float MarkerSearchSpinScale = 5.f;
	constexpr float MarkerSearchPulse = 0.1f;
	/** Anillo fijo: cuántas veces se busca el suelo como mucho y cada cuánto (s) si aún no tiene colisión. */
	constexpr int32 MarkerMaxTraces = 6;
	constexpr double MarkerRetraceSeconds = 1.0;

	bool DebugInteraction()
	{
		static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("TN.Debug.Interaction"));
		return CVar && CVar->GetInt() != 0;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Motor de sonido (hilo de render de audio)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Hilos como en UTN_PlaygroundSynthComponent: la cola la llena el hilo de juego (TriggerSound) y la vacía el hilo de
 * audio al principio de cada bloque; voces, filtros y osciladores viven solo en el hilo de audio, sin asignaciones ni
 * bloqueos. Los parámetros lentos se calculan una vez por bloque de 16 muestras.
 */
namespace TNSearchSynthDSP
{
	constexpr float SfxPi = 3.14159265358979323846f;
	constexpr float SfxTwoPi = 6.28318530717958647692f;
	constexpr int32 SfxMaxVoices = 10;
	constexpr int32 SfxBlock = 16;

	/** Tipos de efecto (el orden es el de ETNSearchSound). */
	constexpr uint8 KindRummage = 0;
	constexpr uint8 KindPuff = 1;
	constexpr uint8 KindPof = 2;
	constexpr uint8 KindLidCreak = 3;
	constexpr uint8 KindLidThump = 4;

	struct FSfxEvent
	{
		uint8 Kind = 0;
		float Pitch = 1.f;
		float Gain = 1.f;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FSfxShared
	{
		static constexpr uint32 Capacity = 32;

		FSfxEvent Events[Capacity];
		std::atomic<uint32> WriteIndex{ 0 };
		std::atomic<uint32> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		bool Push(const FSfxEvent& InEvent)
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

		bool Pop(FSfxEvent& OutEvent)
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
	inline float SfxNoise(uint32& Seed)
	{
		Seed ^= Seed << 13;
		Seed ^= Seed >> 17;
		Seed ^= Seed << 5;
		return static_cast<float>(Seed >> 8) * (1.f / 8388608.f) - 1.f;
	}

	/** Azar en [0, 1). */
	inline float SfxUnit(uint32& Seed)
	{
		return 0.5f * (SfxNoise(Seed) + 1.f);
	}

	/** Coeficiente del filtro de estado variable (Chamberlin); estable por debajo de ~1/6 de la frecuencia de muestreo. */
	inline float SfxSvfCoef(float CutHz, float SampleRate)
	{
		return 2.f * std::sin(SfxPi * FMath::Clamp(CutHz, 20.f, SampleRate * 0.16f) / SampleRate);
	}

	struct FSfxVoice
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
		/** Filtro de estado variable: salidas paso bajo y paso banda. */
		float SvfLow = 0.f;
		float SvfBand = 0.f;
		/** Pasos bajos de un polo: siseo (agudo) y retumbo (grave). */
		float HissLp = 0.f;
		float RumbleLp = 0.f;
		/** Resonador de la chinita (y el grave de la madera de la tapa). */
		float Res1 = 0.f;
		float Res2 = 0.f;
		/** Segundo resonador (el agudo de la madera de la tapa). */
		float ResB1 = 0.f;
		float ResB2 = 0.f;
		float ClickAt = -1.f;
		float ClickHz = 2600.f;
		/** Centro del paso banda de los granos de arena. */
		float BandHz = 2200.f;
		float Jitter = 0.f;
		uint32 NoiseState = 0x2545F491u;
	};

	/** Mezclador de voces con limitador suave al final. */
	class FSfxCore
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32 Frames, int32 Channels, FSfxShared& Queue)
		{
			FSfxEvent Pending;
			while (Queue.Pop(Pending))
			{
				StartVoice(Pending);
			}
			bool bAnyVoice = false;
			for (const FSfxVoice& Voice : Voices)
			{
				bAnyVoice |= Voice.bActive;
			}
			if (!bAnyVoice)
			{
				FMemory::Memzero(Out, sizeof(float) * Frames * Channels);
				return;
			}
			const float MasterGain = Queue.Master.load(std::memory_order_relaxed);
			for (int32 Frame = 0; Frame < Frames; Frame += SfxBlock)
			{
				const int32 Count = FMath::Min(SfxBlock, Frames - Frame);
				float MixBuf[SfxBlock] = {};
				for (FSfxVoice& Voice : Voices)
				{
					if (Voice.bActive)
					{
						RenderVoice(Voice, MixBuf, Count);
					}
				}
				for (int32 i = 0; i < Count; ++i)
				{
					// Saturación suave (aproximación racional de tanh): varios puñados a la vez no recortan.
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
		FSfxVoice Voices[SfxMaxVoices];
		uint32 SeedState = 0x7F4A7C15u;

		/** Coge una voz libre (o la más vieja) y la prepara. */
		void StartVoice(const FSfxEvent& InEvent)
		{
			int32 Best = 0;
			float Oldest = -1.f;
			for (int32 i = 0; i < SfxMaxVoices; ++i)
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
			FSfxVoice& Voice = Voices[Best];
			Voice = FSfxVoice();
			Voice.bActive = true;
			Voice.Kind = InEvent.Kind;
			Voice.Pitch = FMath::Clamp(InEvent.Pitch, 0.25f, 4.f);
			Voice.Gain = FMath::Clamp(InEvent.Gain, 0.f, 2.f);
			SeedState = SeedState * 1664525u + 1013904223u;
			Voice.NoiseState = SeedState | 1u;
			Voice.Jitter = static_cast<float>((SeedState >> 9) & 1023u) / 1023.f;
			switch (InEvent.Kind)
			{
			case KindRummage:
				// Cada puñado, distinto: largo, color de la arena y si suena una chinita (y cuándo).
				Voice.Duration = 0.17f + 0.12f * Voice.Jitter;
				Voice.BandHz = 1500.f + 1700.f * SfxUnit(Voice.NoiseState);
				Voice.ClickAt = SfxUnit(Voice.NoiseState) < 0.45f ? Voice.Duration * (0.15f + 0.6f * SfxUnit(Voice.NoiseState)) : -1.f;
				Voice.ClickHz = 1900.f + 1800.f * SfxUnit(Voice.NoiseState);
				break;
			case KindPuff:
				Voice.Duration = 0.85f;
				break;
			case KindLidCreak:
				// Cada crujido, de un largo distinto.
				Voice.Duration = 0.4f + 0.2f * Voice.Jitter;
				break;
			case KindLidThump:
				// El golpe arranca con un impulso en la primera muestra.
				Voice.Duration = 0.42f;
				Voice.ClickAt = 0.f;
				break;
			default:
				Voice.Duration = 0.62f;
				break;
			}
		}

		void RenderVoice(FSfxVoice& Voice, float* MixBuf, int32 Count)
		{
			const float T = Voice.Age;
			const float Dt = InvRate;
			switch (Voice.Kind)
			{
			case KindRummage:
			{
				// Un puñado de arena y piedrecitas: granos de fricción al azar (más densos al principio) por un paso
				// banda, siseo agudo, retumbo grave de roca y, a veces, el clic de una chinita.
				const float X = FMath::Clamp(T / FMath::Max(0.05f, Voice.Duration), 0.f, 1.f);
				const float Env = FMath::Min(1.f, T / 0.012f) * std::pow(1.f - X, 1.3f)
					* (0.75f + 0.25f * std::sin(SfxTwoPi * 21.f * T + 6.f * Voice.Jitter));
				const float GrainChance = (650.f * Env + 40.f) * Dt;
				const float G = SfxSvfCoef(Voice.BandHz * Voice.Pitch * (0.9f + 0.25f * X), Rate);
				const float Wc = SfxTwoPi * FMath::Min(Voice.ClickHz * Voice.Pitch, Rate * 0.4f) * Dt;
				const float Rc = std::exp(-Dt / 0.011f);
				const float Cc = 2.f * Rc * std::cos(Wc);
				// Normalización: la respuesta al impulso de un resonador de dos polos llega a 1/sin(w).
				const float Nc = std::sin(Wc);
				for (int32 i = 0; i < Count; ++i)
				{
					float Grain = 0.f;
					if (SfxUnit(Voice.NoiseState) < GrainChance)
					{
						Grain = 1.6f * SfxNoise(Voice.NoiseState);
					}
					Voice.SvfLow += G * Voice.SvfBand;
					const float High = Grain - Voice.SvfLow - 0.8f * Voice.SvfBand;
					Voice.SvfBand += G * High;
					const float White = SfxNoise(Voice.NoiseState);
					Voice.HissLp += 0.3f * (White - Voice.HissLp);
					const float Hiss = (White - Voice.HissLp) * 0.07f * Env;
					Voice.RumbleLp += 0.015f * (White - Voice.RumbleLp);
					const float Rumble = Voice.RumbleLp * 1.2f * Env;
					float Excite = 0.f;
					if (Voice.ClickAt >= 0.f && T + static_cast<float>(i) * Dt >= Voice.ClickAt)
					{
						Excite = 1.f;
						Voice.ClickAt = -1.f;
					}
					const float Y = Excite + Cc * Voice.Res1 - Rc * Rc * Voice.Res2;
					Voice.Res2 = Voice.Res1;
					Voice.Res1 = Y;
					MixBuf[i] += (1.1f * Voice.SvfBand + Hiss + Rumble + 0.35f * Y * Nc) * Voice.Gain;
				}
				break;
			}
			case KindPuff:
			{
				// ¡Puf!: golpe de aire (ruido por un paso bajo que se cierra deprisa), «pop» grave y dos notas de
				// campanita (sol y do agudos) de premio.
				const float AirEnv = T < 0.004f ? T / 0.004f : std::exp(-(T - 0.004f) / 0.085f);
				const float G = SfxSvfCoef((420.f + 5200.f * std::exp(-T / 0.06f)) * Voice.Pitch, Rate);
				const float PopHz = 175.f * Voice.Pitch * (1.f + 1.3f * std::exp(-T / 0.012f));
				const float PopEnv = std::exp(-T / 0.05f);
				auto Bell = [T](float Start)
				{
					const float Tb = T - Start;
					return Tb <= 0.f ? 0.f : FMath::Min(1.f, Tb / 0.003f) * std::exp(-Tb / 0.24f);
				};
				const float BellA = Bell(0.05f);
				const float BellB = Bell(0.13f);
				const float HzA = 1568.f * Voice.Pitch;
				const float HzB = 2093.f * Voice.Pitch;
				for (int32 i = 0; i < Count; ++i)
				{
					const float White = SfxNoise(Voice.NoiseState);
					Voice.SvfLow += G * Voice.SvfBand;
					const float High = White - Voice.SvfLow - 1.1f * Voice.SvfBand;
					Voice.SvfBand += G * High;
					const float Air = Voice.SvfLow * AirEnv;
					Voice.PhaseA += PopHz * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Pop = std::sin(SfxTwoPi * Voice.PhaseA) * PopEnv;
					Voice.PhaseB += HzA * Dt;
					Voice.PhaseB -= std::floor(Voice.PhaseB);
					Voice.PhaseC += HzB * Dt;
					Voice.PhaseC -= std::floor(Voice.PhaseC);
					const float Chime = (std::sin(SfxTwoPi * Voice.PhaseB) + 0.3f * std::sin(2.f * SfxTwoPi * Voice.PhaseB)) * BellA
						+ (std::sin(SfxTwoPi * Voice.PhaseC) + 0.3f * std::sin(2.f * SfxTwoPi * Voice.PhaseC)) * BellB;
					MixBuf[i] += (0.8f * Air + 0.55f * Pop + 0.16f * Chime) * Voice.Gain;
				}
				break;
			}
			case KindLidCreak:
			{
				// Crujido de madera: roce a tirones (impulsos irregulares cuya frecuencia sube y vuelve a bajar) por dos
				// resonancias de tabla, una grave y otra aguda (resonadores de dos polos normalizados como el de la chinita).
				const float X = FMath::Clamp(T / FMath::Max(0.05f, Voice.Duration), 0.f, 1.f);
				const float Env = FMath::Min(1.f, T / 0.02f) * std::pow(1.f - X, 0.7f);
				const float RubHz = Voice.Pitch * (45.f + 150.f * std::pow(FMath::Max(0.f, std::sin(SfxPi * X)), 1.5f)) * (0.85f + 0.3f * Voice.Jitter);
				const float Wa = SfxTwoPi * FMath::Min(540.f * Voice.Pitch, Rate * 0.4f) * Dt;
				const float Ra = std::exp(-Dt / 0.009f);
				const float Ca = 2.f * Ra * std::cos(Wa);
				const float Na = std::sin(Wa);
				const float Wb = SfxTwoPi * FMath::Min(1380.f * Voice.Pitch, Rate * 0.4f) * Dt;
				const float Rb = std::exp(-Dt / 0.005f);
				const float Cb = 2.f * Rb * std::cos(Wb);
				const float Nb = std::sin(Wb);
				for (int32 i = 0; i < Count; ++i)
				{
					float Excite = 0.f;
					Voice.PhaseA += RubHz * Dt;
					if (Voice.PhaseA >= 1.f)
					{
						// Un tirón, de fuerza al azar; el siguiente llega algo tarde (el roce no es regular).
						Excite = 0.55f + 0.45f * SfxUnit(Voice.NoiseState);
						Voice.PhaseA = -0.35f * SfxUnit(Voice.NoiseState);
					}
					const float In = Excite + 0.004f * SfxNoise(Voice.NoiseState);
					const float Ya = In + Ca * Voice.Res1 - Ra * Ra * Voice.Res2;
					Voice.Res2 = Voice.Res1;
					Voice.Res1 = Ya;
					const float Yb = In + Cb * Voice.ResB1 - Rb * Rb * Voice.ResB2;
					Voice.ResB2 = Voice.ResB1;
					Voice.ResB1 = Yb;
					MixBuf[i] += (0.6f * Ya * Na + 0.4f * Yb * Nb) * Env * Voice.Gain;
				}
				break;
			}
			case KindLidThump:
			{
				// ¡Clonc!: golpe grave cuya altura cae, la caja de madera que resuena (dos modos excitados por un impulso
				// y una pizca de ruido) y el tintineo corto de los herrajes.
				const float BodyHz = 128.f * Voice.Pitch * (1.f + 0.7f * std::exp(-T / 0.012f));
				const float BodyEnv = std::exp(-T / 0.075f);
				const float GritEnv = T < 0.006f ? 1.f : 0.f;
				const float Wa = SfxTwoPi * FMath::Min(360.f * Voice.Pitch, Rate * 0.4f) * Dt;
				const float Ra = std::exp(-Dt / 0.04f);
				const float Ca = 2.f * Ra * std::cos(Wa);
				const float Na = std::sin(Wa);
				const float Wb = SfxTwoPi * FMath::Min(880.f * Voice.Pitch, Rate * 0.4f) * Dt;
				const float Rb = std::exp(-Dt / 0.02f);
				const float Cb = 2.f * Rb * std::cos(Wb);
				const float Nb = std::sin(Wb);
				const float TinkEnv = std::exp(-T / 0.06f);
				const float TinkHz = 2650.f * Voice.Pitch;
				for (int32 i = 0; i < Count; ++i)
				{
					float Excite = 0.06f * GritEnv * SfxNoise(Voice.NoiseState);
					if (Voice.ClickAt >= 0.f)
					{
						Excite += 1.f;
						Voice.ClickAt = -1.f;
					}
					Voice.PhaseA += BodyHz * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Body = std::sin(SfxTwoPi * Voice.PhaseA) * BodyEnv;
					const float Ya = Excite + Ca * Voice.Res1 - Ra * Ra * Voice.Res2;
					Voice.Res2 = Voice.Res1;
					Voice.Res1 = Ya;
					const float Yb = Excite + Cb * Voice.ResB1 - Rb * Rb * Voice.ResB2;
					Voice.ResB2 = Voice.ResB1;
					Voice.ResB1 = Yb;
					Voice.PhaseC += TinkHz * Dt;
					Voice.PhaseC -= std::floor(Voice.PhaseC);
					const float Tink = std::sin(SfxTwoPi * Voice.PhaseC) * TinkEnv;
					MixBuf[i] += (0.6f * Body + 0.75f * Ya * Na + 0.45f * Yb * Nb + 0.1f * Tink) * Voice.Gain;
				}
				break;
			}
			default:
			{
				// ¡Pof!: golpe sordo, polvo que se posa (ruido por un paso bajo que va bajando) y un «buuu» bajito
				// que cae: no había nada.
				const float ThumpHz = 105.f * Voice.Pitch * (1.f + 0.9f * std::exp(-T / 0.015f));
				const float ThumpEnv = std::exp(-T / 0.085f);
				const float DustEnv = T < 0.008f ? T / 0.008f : std::exp(-(T - 0.008f) / 0.13f);
				const float G = SfxSvfCoef((240.f + 1100.f * std::exp(-T / 0.12f)) * Voice.Pitch, Rate);
				const float U = (T - 0.09f) / 0.34f;
				const float WompEnv = (U > 0.f && U < 1.f) ? FMath::Square(std::sin(SfxPi * U)) : 0.f;
				const float WompHz = FMath::Lerp(330.f, 220.f, FMath::Clamp(U, 0.f, 1.f)) * Voice.Pitch;
				for (int32 i = 0; i < Count; ++i)
				{
					Voice.PhaseA += ThumpHz * Dt;
					Voice.PhaseA -= std::floor(Voice.PhaseA);
					const float Thump = std::sin(SfxTwoPi * Voice.PhaseA) * ThumpEnv;
					const float White = SfxNoise(Voice.NoiseState);
					Voice.SvfLow += G * Voice.SvfBand;
					const float High = White - Voice.SvfLow - 1.2f * Voice.SvfBand;
					Voice.SvfBand += G * High;
					const float Dust = Voice.SvfLow * DustEnv;
					Voice.PhaseB += WompHz * Dt;
					Voice.PhaseB -= std::floor(Voice.PhaseB);
					const float Womp = (std::sin(SfxTwoPi * Voice.PhaseB) + 0.25f * std::sin(2.f * SfxTwoPi * Voice.PhaseB)) * WompEnv;
					MixBuf[i] += (0.65f * Thump + 0.9f * Dust + 0.14f * Womp) * Voice.Gain;
				}
				break;
			}
			}
			Voice.Age += static_cast<float>(Count) * Dt;
			if (Voice.Age >= Voice.Duration)
			{
				Voice.bActive = false;
			}
		}
	};

	/** Generador del hilo de render de audio: solo C++ puro y la cola compartida. */
	class FSfxGenerator final : public ISoundGenerator
	{
	public:
		FSfxGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FSfxShared, ESPMode::ThreadSafe>& InQueue)
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
		TSharedPtr<FSfxShared, ESPMode::ThreadSafe> Queue;
		int32 OutChannels = 1;
		FSfxCore Core;
	};
}

static_assert(static_cast<uint8>(ETNSearchSound::Rummage) == TNSearchSynthDSP::KindRummage, "ETNSearchSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNSearchSound::Puff) == TNSearchSynthDSP::KindPuff, "ETNSearchSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNSearchSound::Pof) == TNSearchSynthDSP::KindPof, "ETNSearchSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNSearchSound::LidCreak) == TNSearchSynthDSP::KindLidCreak, "ETNSearchSound y el motor DSP deben coincidir");
static_assert(static_cast<uint8>(ETNSearchSound::LidThump) == TNSearchSynthDSP::KindLidThump, "ETNSearchSound y el motor DSP deben coincidir");

// ─────────────────────────────────────────────────────────────────────────────
// UTN_SearchSynthComponent
// ─────────────────────────────────────────────────────────────────────────────

UTN_SearchSynthComponent::UTN_SearchSynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// El tick solo cuenta el silencio para parar el sintetizador, dos veces por segundo.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.5f;
	bAutoActivate = false;
	NumChannels = 1;
	SfxQueue = MakeShared<TNSearchSynthDSP::FSfxShared, ESPMode::ThreadSafe>();
}

void UTN_SearchSynthComponent::OnRegister()
{
	// Antes de que el padre cree el componente de audio.
	ConfigureSpatial();
	Super::OnRegister();
}

void UTN_SearchSynthComponent::ConfigureSpatial()
{
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	// Como las piezas del parque del lobby: volumen pleno cerca, caída natural y agudos que se apagan con la distancia.
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
	Att.LPFFrequencyAtMax = 3500.f;
	Att.NonSpatializedRadiusStart = InnerRadius * 0.6f;
	Att.NonSpatializedRadiusEnd = InnerRadius * 0.25f;
}

bool UTN_SearchSynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_SearchSynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNSearchSynthDSP::FSfxGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SfxQueue);
}

bool UTN_SearchSynthComponent::IsListenerNear() const
{
	FAudioDevice* Device = GetAudioDevice();
	if (!Device)
	{
		return false;
	}
	return Device->GetDistanceToNearestListener(GetComponentLocation()) < InnerRadius + FalloffDistance + 300.f;
}

void UTN_SearchSynthComponent::TriggerSound(ETNSearchSound Sound, float Pitch, float Volume)
{
	if (!SfxQueue.IsValid() || Volume <= 0.f || !IsListenerNear())
	{
		return;
	}
	if (!IsPlaying())
	{
		TNAudioVoices::Apply(*this, TNAudioVoices::ERank::World);
		Start();
	}
	SfxQueue->Master.store(FMath::Clamp(Loudness, 0.f, 2.f), std::memory_order_relaxed);
	TNSearchSynthDSP::FSfxEvent Shot;
	Shot.Kind = static_cast<uint8>(Sound);
	Shot.Pitch = Pitch;
	Shot.Gain = Volume;
	SfxQueue->Push(Shot);
	// El «¡puf!» dura 0,85 s: con 3 s de margen no se corta ninguna cola.
	SilenceLeft = 3.f;
	SetComponentTickEnabled(true);
}

void UTN_SearchSynthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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

UTN_SearchSynthComponent* UTN_SearchSynthComponent::AttachTo(AActor* InOwner, const FVector& InWorldLocation, float InInnerRadius, float InFalloff)
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

	UTN_SearchSynthComponent* Comp = NewObject<UTN_SearchSynthComponent>(InOwner, NAME_None, RF_Transient);
	Comp->InnerRadius = FMath::Max(0.f, InInnerRadius);
	Comp->FalloffDistance = FMath::Max(100.f, InFalloff);
	if (USceneComponent* RootComp = InOwner->GetRootComponent())
	{
		Comp->SetupAttachment(RootComp);
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

// ─────────────────────────────────────────────────────────────────────────────
// ATN_ProcSearchSpot
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcSearchSpot::ATN_ProcSearchSpot()
{
	// Sin nada que hacer, el tick solo mira de vez en cuando si la cámara se acerca (chispitas) o hay que dibujar las
	// balizas; mientras alguien rebusca, o quedan efectos, va a cada fotograma.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = TNSearchSpotDetail::IdleTickInterval;

	PromptText = NSLOCTEXT("Tortunabo", "SearchSpotPrompt", "Mantén para rebuscar");
	InteractionDistance = TNSearchSpotDetail::Reach;

	// Sin malla propia: el decorado ya está en las mallas del mapa.
	if (Mesh)
	{
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetHiddenInGame(true);
	}
	// El aviso lo da el HUD: el widget 3D de la base sobra (ni tick ni render target en cada decorado).
	if (PromptWidgetComponent)
	{
		PromptWidgetComponent->SetTickMode(ETickMode::Disabled);
		PromptWidgetComponent->SetHiddenInGame(true);
		PromptWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Solo consultas (el escaneo de interactuables busca WorldDynamic por tipo de objeto); no bloquea ni solapa nada.
	ScanSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ScanSphere"));
	ScanSphere->SetupAttachment(SceneRoot);
	ScanSphere->InitSphereRadius(150.f);
	ScanSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ScanSphere->SetCollisionObjectType(ECC_WorldDynamic);
	ScanSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	ScanSphere->SetGenerateOverlapEvents(false);
	ScanSphere->SetCanEverAffectNavigation(false);
	ScanSphere->SetHiddenInGame(true);

	// El catálogo de siempre: los consumibles y lanzables de DT_Items, cada uno con su pickup.
	LootTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/Blueprints/Gameplay/Items/DT_Items.DT_Items")));
	// El tótem (revive a un compañero) vale mucho más que el resto: sale bastante menos.
	LootWeights.Add(TEXT("Totem"), 0.3f);
}

const UDataTable* ATN_ProcSearchSpot::GetLootTable() const
{
	if (PreloadedLootTable)
	{
		return PreloadedLootTable;
	}
	// Sin BeginPlay todavía (o en un cliente): lo que haya cargado; si no, se carga igual para no quedarse sin objeto.
	if (const UDataTable* Loaded = LootTable.Get())
	{
		return Loaded;
	}
	UE_LOG(LogTortunabo, Warning, TEXT("[Search] %s: catálogo sin precargar (%s), carga síncrona."), *GetName(), *LootTable.ToString());
	return LootTable.LoadSynchronous();
}

void ATN_ProcSearchSpot::BeginPlay()
{
	Super::BeginPlay();
	// El catálogo, ya ahora (el sitio nace al montar el mapa): la primera búsqueda o el primer cofre no cargan nada del
	// disco. El de serie, DT_Items, ya lo precarga UTN_GameplayPreloadSubsystem y aquí solo se resuelve.
	if (HasAuthority() && !LootTable.IsNull())
	{
		PreloadedLootTable = LootTable.LoadSynchronous();
	}
	ApplySpotShape();
	HintClock = FMath::FRandRange(0.f, 0.5f);
	// Cada anillo desfasado (no giran ni respiran todos a la vez), como los de los objetos del suelo, pero con el desfase
	// sacado de su sitio y no al azar: sale igual en todas las máquinas.
	const FVector Where = GetActorLocation();
	MarkerClock = FMath::Fmod(FMath::Abs(static_cast<float>(Where.X) * 0.0137f + static_cast<float>(Where.Y) * 0.0291f), 10.f);
}

void ATN_ProcSearchSpot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DormancyTimer);
	TNAmbientFX::RemoveOwner(this);
	// Al regenerarse el mapa, lo que salió de aquí y nadie recogió se va con él.
	if (HasAuthority() && EndPlayReason == EEndPlayReason::Destroyed)
	{
		for (const TWeakObjectPtr<AActor>& Loot : SpawnedLoot)
		{
			if (AActor* LootActor = Loot.Get())
			{
				LootActor->Destroy();
			}
		}
	}
	SpawnedLoot.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_ProcSearchSpot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATN_ProcSearchSpot, SpotShape, COND_InitialOnly);
	DOREPLIFETIME(ATN_ProcSearchSpot, SearchState);
}

void ATN_ProcSearchSpot::SetupSpot(float InRadius, float InHalfLength, float InHeight, const FLinearColor& InDustColor)
{
	SpotShape.Radius = FMath::Max(30.f, InRadius);
	SpotShape.HalfLength = FMath::Max(0.f, InHalfLength);
	SpotShape.Height = FMath::Max(50.f, InHeight);
	SpotShape.Dust = InDustColor.ToFColor(false);
	ApplySpotShape();
	FlushNetDormancy();
}

void ATN_ProcSearchSpot::OnRep_SpotShape()
{
	ApplySpotShape();
}

void ATN_ProcSearchSpot::ApplySpotShape()
{
	if (!ScanSphere)
	{
		return;
	}
	// La esfera envuelve la cápsula de la huella: si la tortuga está a su alcance del borde, la esfera también.
	ScanSphere->SetSphereRadius(SpotShape.HalfLength + SpotShape.Radius + 30.f);
	ScanSphere->SetRelativeLocation(FVector(0.f, 0.f, FMath::Min(SpotShape.Height * 0.5f, 150.f)));
	// Ya buscado: fuera del escaneo (tampoco tiene aviso). Los repetibles siguen en él: el respiro lo decide CanInteract.
	ScanSphere->SetCollisionEnabled(!bRepeatable && IsSearched() ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
}

bool ATN_ProcSearchSpot::IsSpent() const
{
	if (!IsSearched())
	{
		return false;
	}
	if (!bRepeatable)
	{
		return true;
	}
	// Repetible: agotado solo durante el respiro tras el último resultado (y nunca mientras alguien rebusca).
	return !SearchState.Searcher && ServerNow() - static_cast<double>(SearchState.OutcomeTime) < static_cast<double>(RepeatCooldown);
}

// ── Interacción ──────────────────────────────────────────────────────────────

bool ATN_ProcSearchSpot::CanInteract(APawn* Interactor) const
{
	if (!Super::CanInteract(Interactor) || IsSpentFor(Interactor))
	{
		return false;
	}
	// Uno a la vez: mientras otro rebusca, a los demás no les sale el aviso.
	const APawn* Current = SearchState.Searcher.Get();
	return Current == nullptr || Current == Interactor;
}

FVector ATN_ProcSearchSpot::GetInteractionPointFor(const APawn* Interactor) const
{
	const FVector Base = GetActorLocation();
	if (!Interactor)
	{
		return Base + FVector(0.f, 0.f, FMath::Min(SpotShape.Height * 0.5f, 150.f));
	}
	// El borde de la huella más cercano, a la altura de la tortuga dentro de la del decorado (desde el suelo junto a
	// él o desde encima, la distancia es la de verdad).
	const FVector PawnLocation = Interactor->GetActorLocation();
	FVector Point = RimPointToward(PawnLocation, 0.f);
	Point.Z = FMath::Clamp(PawnLocation.Z, Base.Z, Base.Z + FMath::Max(static_cast<double>(SpotShape.Height), 100.0));
	return Point;
}

float ATN_ProcSearchSpot::GetHoldDuration() const
{
	const float Forced = TNSearchSpotDetail::CVarSearchSeconds.GetValueOnGameThread();
	return Forced > 0.f ? FMath::Max(0.2f, Forced) : SearchSeconds;
}

float ATN_ProcSearchSpot::GetHoldProgress(const APawn* Interactor) const
{
	if (!Interactor || IsSpentFor(Interactor) || SearchState.Searcher.Get() != Interactor)
	{
		return -1.f;
	}
	const float Elapsed = static_cast<float>(ServerNow() - static_cast<double>(SearchState.SearchStart));
	return FMath::Clamp(Elapsed / GetHoldDuration(), 0.f, 1.f);
}

void ATN_ProcSearchSpot::BeginHoldInteract(APawn* Interactor)
{
	if (!HasAuthority() || !Interactor || SearchState.Searcher.Get() == Interactor)
	{
		return;
	}
	if (!CanInteract(Interactor) || !CanPawnSearch(Interactor) || !IsPawnInReach(Interactor, ReachSlack))
	{
		if (TNSearchSpotDetail::DebugInteraction())
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Search] %s no puede rebuscar %s (ya buscado, ocupado, lejos o sin poder)."), *Interactor->GetName(), *GetName());
		}
		return;
	}
	SetNetDormancy(DORM_Awake);
	const FTNSearchSpotState OldState = SearchState;
	SearchState.Searcher = Interactor;
	SearchState.SearchStart = static_cast<float>(ServerNow());
	CommitState(OldState);
	if (TNSearchSpotDetail::DebugInteraction())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Search] %s empieza a rebuscar %s (%.1f s)."), *Interactor->GetName(), *GetName(), GetHoldDuration());
	}
}

void ATN_ProcSearchSpot::EndHoldInteract(APawn* Interactor)
{
	if (HasAuthority() && Interactor && SearchState.Searcher.Get() == Interactor && !IsSpent())
	{
		CancelSearch(TEXT("ha soltado la tecla"));
	}
}

// ── Servidor ─────────────────────────────────────────────────────────────────

void ATN_ProcSearchSpot::ServerTickSearch()
{
	APawn* Pawn = SearchState.Searcher.Get();
	if (!CanPawnSearch(Pawn))
	{
		CancelSearch(TEXT("ya no puede (caparazón, tumbada, en brazos...)"));
		return;
	}
	if (!IsPawnInReach(Pawn, ReachSlack))
	{
		CancelSearch(TEXT("se ha alejado"));
		return;
	}
	if (ServerNow() - static_cast<double>(SearchState.SearchStart) >= GetHoldDuration())
	{
		FinishSearch();
	}
}

void ATN_ProcSearchSpot::FinishSearch()
{
	APawn* Pawn = SearchState.Searcher.Get();
	const FVector From = GetLootOrigin(Pawn);

	FTN_InventoryItem Item;
	AActor* Loot = nullptr;
	FVector Landing = From;
	if (FMath::FRand() < GetLuck() && PickLoot(Item, Pawn))
	{
		Landing = FindLanding(Pawn, From);
		Loot = SpawnLoot(Item, Landing);
	}

	SetNetDormancy(DORM_Awake);
	const FTNSearchSpotState OldState = SearchState;
	SearchState.Searcher = nullptr;
	SearchState.Outcome = Loot ? ETNSearchOutcome::Found : ETNSearchOutcome::Empty;
	SearchState.OutcomeTime = static_cast<float>(ServerNow());
	SearchState.LootFrom = From;
	SearchState.LootTo = Landing;
	SearchState.LootPickup = Loot;
	SearchState.SearchCount = static_cast<uint8>(SearchState.SearchCount + 1);
	CommitState(OldState);

	const FString Result = Loot ? FString::Printf(TEXT("¡puf! %s"), *Item.ItemId.ToString()) : FString(TEXT("¡pof! nada"));
	UE_LOG(LogTortunabo, Log, TEXT("[Search] %s ha rebuscado %s: %s."), *GetNameSafe(Pawn), *GetName(), *Result);
}

void ATN_ProcSearchSpot::CancelSearch(const TCHAR* Why)
{
	if (!SearchState.Searcher)
	{
		return;
	}
	if (TNSearchSpotDetail::DebugInteraction())
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Search] Se corta la búsqueda de %s en %s: %s."), *GetNameSafe(SearchState.Searcher.Get()), *GetName(), Why);
	}
	SetNetDormancy(DORM_Awake);
	const FTNSearchSpotState OldState = SearchState;
	SearchState.Searcher = nullptr;
	CommitState(OldState);
}

void ATN_ProcSearchSpot::CommitState(const FTNSearchSpotState& OldState)
{
	// Despierto (SetNetDormancy antes del cambio) y enviado ya; el anfitrión no recibe OnRep: lo aplica aquí.
	ForceNetUpdate();
	HandleStateChanged(OldState);
	ScheduleDormancy();
}

void ATN_ProcSearchSpot::ScheduleDormancy()
{
	GetWorldTimerManager().ClearTimer(DormancyTimer);
	if (SearchState.Searcher)
	{
		return;
	}
	// Unos segundos después del último cambio vuelve a dormir: casi siempre no cuesta nada de red.
	GetWorldTimerManager().SetTimer(DormancyTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!SearchState.Searcher)
		{
			SetNetDormancy(DORM_DormantAll);
		}
	}), 3.f, false);
}

bool ATN_ProcSearchSpot::CanPawnSearch(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || !Pawn->GetController())
	{
		return false;
	}
	if (const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Pawn))
	{
		if (Turtle->IsKnockedDown() || Turtle->IsDead() || Turtle->IsInShell())
		{
			return false;
		}
		if (const UTN_CarryComponent* Carry = Turtle->GetCarryComponent())
		{
			if (Carry->IsCarrying() || Carry->IsBeingCarried())
			{
				return false;
			}
		}
	}
	return true;
}

bool ATN_ProcSearchSpot::IsPawnInReach(const APawn* Pawn, float Slack) const
{
	if (!Pawn)
	{
		return false;
	}
	const double Allowed = static_cast<double>(FMath::Max(InteractionDistance, TNSearchSpotDetail::Reach) + Slack);
	return FVector::Dist(Pawn->GetActorLocation(), GetInteractionPointFor(Pawn)) <= Allowed;
}

float ATN_ProcSearchSpot::GetLuck() const
{
	const float Forced = TNSearchSpotDetail::CVarSearchLuck.GetValueOnGameThread();
	return Forced >= 0.f ? FMath::Clamp(Forced, 0.f, 1.f) : LootChance;
}

FVector ATN_ProcSearchSpot::GetLootOrigin(const APawn* Pawn) const
{
	// A ras del borde de la huella, hacia el que buscaba (sin él, hacia un lado cualquiera).
	const FVector PawnLocation = Pawn ? Pawn->GetActorLocation() : GetActorLocation() + GetActorRightVector() * 500.f;
	return RimPointToward(PawnLocation, 40.f);
}

FVector ATN_ProcSearchSpot::GetRummageOrigin(const APawn* Searcher) const
{
	return Searcher ? RimPointToward(Searcher->GetActorLocation(), 20.f) : GetActorLocation() + FVector(0.f, 0.f, 20.f);
}

float ATN_ProcSearchSpot::GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const
{
	if (const float* ByRow = LootWeights.Find(RowName))
	{
		return *ByRow;
	}
	if (const float* ById = LootWeights.Find(Row.ItemId))
	{
		return *ById;
	}
	return 1.f;
}

bool ATN_ProcSearchSpot::PickLoot(FTN_InventoryItem& OutItem, const APawn* /*Searcher*/) const
{
	const UDataTable* Table = GetLootTable();
	if (!Table)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Search] Sin catálogo de objetos (%s): no sale nada."), *LootTable.ToString());
		return false;
	}
	// La tabla del coop: las filas de DT_Items con su peso de aquí y los objetos del coop definidos en código (TN_CoopItems.h).
	return TNCoopItems::RollLoot(Table, [this](FName RowName, const FTN_InventoryItem& Row) { return GetLootWeight(RowName, Row); },
		FMath::FRand(), OutItem);
}

bool ATN_ProcSearchSpot::PickCatalogItem(const UDataTable* Table, TFunctionRef<float(FName, const FTN_InventoryItem&)> WeightOf,
	FTN_InventoryItem& OutItem)
{
	if (!Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FTN_InventoryItem::StaticStruct()))
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Search] El catálogo de objetos %s no es de FTN_InventoryItem: no sale nada."), *GetNameSafe(Table));
		return false;
	}

	// Solo lo que se puede recoger y usar (fuera filas a medio hacer o sin uso); cada uno con su peso.
	TArray<const FTN_InventoryItem*> Options;
	TArray<float> Weights;
	float Total = 0.f;
	Table->ForeachRow<FTN_InventoryItem>(TEXT("ATN_ProcSearchSpot::PickCatalogItem"), [&](const FName& RowName, const FTN_InventoryItem& Row)
	{
		if (!Row.IsValid() || !Row.PickupActorClass || Row.UseType == ETN_ItemUseType::None)
		{
			return;
		}
		const float Weight = WeightOf(RowName, Row);
		if (Weight <= 0.f)
		{
			return;
		}
		Options.Add(&Row);
		Weights.Add(Weight);
		Total += Weight;
	});
	if (Options.Num() == 0 || Total <= 0.f)
	{
		return false;
	}

	float Pick = FMath::FRand() * Total;
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		Pick -= Weights[i];
		if (Pick <= 0.f || i == Options.Num() - 1)
		{
			OutItem = *Options[i];
			return true;
		}
	}
	return false;
}

AActor* ATN_ProcSearchSpot::SpawnLoot(const FTN_InventoryItem& Item, const FVector& Where)
{
	UWorld* World = GetWorld();
	if (!World || !Item.PickupActorClass)
	{
		return nullptr;
	}
	// Con límite (los repetibles): lo recogido ya no cuenta (el pickup se destruye al cogerlo); si aún hay demasiados
	// objetos sin recoger, se va el más viejo.
	if (MaxLootLying > 0)
	{
		SpawnedLoot.RemoveAll([](const TWeakObjectPtr<AActor>& Lying) { return !Lying.IsValid(); });
		while (SpawnedLoot.Num() >= MaxLootLying)
		{
			if (AActor* Oldest = SpawnedLoot[0].Get())
			{
				Oldest->Destroy();
			}
			SpawnedLoot.RemoveAt(0);
		}
	}
	// Igual que las zonas de objetos y soltar lo equipado: el pickup de la fila, inicializado con ella.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATN_PickupInteractableBase* Pickup = World->SpawnActor<ATN_PickupInteractableBase>(Item.PickupActorClass, Where,
		FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), SpawnParams);
	if (!Pickup)
	{
		return nullptr;
	}
	Pickup->InitializeFromInventoryItem(Item);
	SpawnedLoot.Add(Pickup);
	return Pickup;
}

FVector ATN_ProcSearchSpot::FindLanding(const APawn* Pawn, const FVector& From) const
{
	// Hacia el que buscaba, a un metro largo del borde y algo de lado: cerquita de donde se ha rebuscado.
	FVector Out = Pawn ? (Pawn->GetActorLocation() - From).GetSafeNormal2D() : FVector::ZeroVector;
	if (Out.IsNearlyZero())
	{
		Out = (From - GetActorLocation()).GetSafeNormal2D();
	}
	if (Out.IsNearlyZero())
	{
		Out = GetActorRightVector();
	}
	const FVector Side(-Out.Y, Out.X, 0.0);
	const FVector Target = From + Out * FMath::FRandRange(95.f, 135.f) + Side * FMath::FRandRange(-55.f, 55.f);

	// El suelo: traza hacia abajo contra lo estático (terreno y decorados, no tortugas ni objetos); si no hay
	// colisión (aún cocinándose), la altura del terreno generado.
	const double Top = FMath::Max(From.Z, Pawn ? Pawn->GetActorLocation().Z : From.Z) + 150.0;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_SearchLanding), false, this);
	if (Pawn)
	{
		Query.AddIgnoredActor(Pawn);
	}
	if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(Target.X, Target.Y, Top), FVector(Target.X, Target.Y, Top - 900.0),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query))
	{
		return Hit.ImpactPoint + FVector(0.f, 0.f, 5.f);
	}
	if (const ATN_ProcMapGenerator* Generator = Cast<ATN_ProcMapGenerator>(GetOwner()))
	{
		return FVector(Target.X, Target.Y, Generator->GetTerrainHeightAt(Target) + 5.0);
	}
	return FVector(Target.X, Target.Y, GetActorLocation().Z + 5.0);
}

// ── Estado replicado ─────────────────────────────────────────────────────────

void ATN_ProcSearchSpot::OnRep_SearchState(const FTNSearchSpotState& OldState)
{
	HandleStateChanged(OldState);
}

void ATN_ProcSearchSpot::HandleStateChanged(const FTNSearchSpotState& OldState)
{
	ApplySpotShape();
	if (SearchState.Searcher)
	{
		// Rebuscando: el servidor cuenta el tiempo y el resto pone tierra y sonido a cada fotograma.
		SetActorTickInterval(0.f);
		RummageClock = 0.f;
	}

	const bool bScreen = GetNetMode() != NM_DedicatedServer;
	const double Since = ServerNow() - static_cast<double>(SearchState.OutcomeTime);
	// Resultado nuevo: cambia la cuenta de búsquedas (en los de una vez, al pasar de «sin buscar» a buscado).
	const bool bNewOutcome = SearchState.Outcome != ETNSearchOutcome::None && SearchState.SearchCount != OldState.SearchCount;
	if (bScreen && bNewOutcome && Since < TNSearchSpotDetail::FreshOutcomeSeconds)
	{
		EnsureFX();
		const FVector From = SearchState.LootFrom;
		if (SearchState.Outcome == ETNSearchOutcome::Found)
		{
			// ¡Puf!: nubecilla blanca, chispas doradas y unas piedrecitas; el objeto sale de ahí de un saltito.
			BurstFX(FxPoof, From, 14);
			BurstFX(FxSparkle, From + FVector(0.f, 0.f, 20.f), 12, FVector::UpVector, 5.f);
			BurstFX(FxBits, From, 5, FVector::UpVector);
			PlaySearchSound(ETNSearchSound::Puff, FMath::FRandRange(0.95f, 1.08f), 1.f, From);
		}
		else
		{
			// ¡Pof!: nube pequeña del color del suelo del bioma y unas piedrecitas; nada más.
			BurstFX(FxDust, From, 16, FVector::UpVector, 1.3f);
			BurstFX(FxBits, From, 7, FVector::UpVector);
			PlaySearchSound(ETNSearchSound::Pof, FMath::FRandRange(0.92f, 1.06f), 0.9f, From);
		}
	}

	// El objeto que ha salido: su saltito, una vez por objeto (si esta máquina lo recibe tarde, directamente en su sitio).
	if (bScreen && SearchState.Outcome == ETNSearchOutcome::Found)
	{
		AActor* Pickup = SearchState.LootPickup.Get();
		if (Pickup && HoppedActor.Get() != Pickup)
		{
			StartHop(Pickup, FMath::Max(0.0, Since));
		}
	}

	OnSearchStateChanged(OldState);
}

// ── Utilidades ───────────────────────────────────────────────────────────────

double ATN_ProcSearchSpot::ServerNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

FVector ATN_ProcSearchSpot::RimPointToward(const FVector& WorldPoint, float ZAbove) const
{
	const FVector Base = GetActorLocation();
	FVector Axis = GetActorForwardVector().GetSafeNormal2D();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::ForwardVector;
	}
	// Punto del eje de la cápsula más cercano en planta y, de ahí, al borde (dentro de la huella: el propio punto).
	const FVector Flat(WorldPoint.X - Base.X, WorldPoint.Y - Base.Y, 0.0);
	const double HalfLength = SpotShape.HalfLength;
	const double Along = FMath::Clamp(FVector::DotProduct(Flat, Axis), -HalfLength, HalfLength);
	const FVector OnAxis = Base + Axis * Along;
	const FVector Out(WorldPoint.X - OnAxis.X, WorldPoint.Y - OnAxis.Y, 0.0);
	const double Distance = Out.Size();
	const double Radius = SpotShape.Radius;
	FVector Point = Distance <= Radius ? FVector(WorldPoint.X, WorldPoint.Y, 0.0) : OnAxis + Out * (Radius / Distance);
	Point.Z = Base.Z + ZAbove;
	return Point;
}

FVector ATN_ProcSearchSpot::RandomRimPoint(float ZAbove) const
{
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Far = SpotShape.HalfLength + SpotShape.Radius + 200.f;
	return RimPointToward(GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Far, ZAbove);
}

// ── Efectos locales ──────────────────────────────────────────────────────────

void ATN_ProcSearchSpot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && SearchState.Searcher)
	{
		ServerTickSearch();
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		TickLocalFX(DeltaSeconds);
		if (TNSearchSpotDetail::CVarSearchShow.GetValueOnGameThread() != 0)
		{
			DrawDebugSpot(DeltaSeconds);
		}
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bBusy = SearchState.Searcher != nullptr || bHopActive || (bNearView && !IsSpentForLocalView()) || bMarkerAnimating
		|| Now - LastFxTime < TNSearchSpotDetail::FxTail || WantsFrameTick();
	const float WantedInterval = bBusy ? 0.f : TNSearchSpotDetail::IdleTickInterval;
	if (!FMath::IsNearlyEqual(GetActorTickInterval(), WantedInterval))
	{
		SetActorTickInterval(WantedInterval);
	}
}

void ATN_ProcSearchSpot::TickLocalFX(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Con la pantalla partida (#311), la cámara local más cercana.
	FVector View = GetActorLocation();
	TNLocalViews::ClosestCamera(World, GetActorLocation(), View);
	bNearView = FVector::Dist2D(View, RimPointToward(View, 0.f)) < static_cast<double>(HintDistance)
		&& FMath::Abs(View.Z - GetActorLocation().Z) < 3000.0;

	// Por buscar: alguna chispita dorada al pie, de vez en cuando («aquí se puede rebuscar»); en los decorados grandes,
	// más de una a la vez por el borde (una por cada 30 m de perímetro de más).
	if (bNearView && !IsSpentForLocalView() && !SearchState.Searcher)
	{
		HintClock -= DeltaSeconds;
		if (HintClock <= 0.f)
		{
			HintClock = FMath::FRandRange(0.28f, 0.55f);
			EnsureFX();
			const float Perimeter = 2.f * PI * SpotShape.Radius + 4.f * SpotShape.HalfLength;
			const int32 Sparkles = FMath::Clamp(1 + FMath::FloorToInt32(Perimeter / TNSearchSpotDetail::HintPerimeterPerSparkle), 1,
				TNSearchSpotDetail::MaxHintSparkles);
			for (int32 k = 0; k < Sparkles; ++k)
			{
				BurstFX(FxSparkle, RandomRimPoint(FMath::FRandRange(15.f, FMath::Min(SpotShape.Height * 0.6f, 140.f))), 1, FVector::UpVector);
			}
		}
	}

	// El anillo dorado fijo en el suelo, alrededor del decorado (sin seguir a nadie).
	TickMarker(DeltaSeconds);

	// Rebuscando: puñados de tierra y piedrecitas que saltan hacia el que busca, cada uno con su sonido.
	if (const APawn* Searcher = SearchState.Searcher.Get())
	{
		if (!IsSpent())
		{
			RummageClock -= DeltaSeconds;
			if (RummageClock <= 0.f)
			{
				RummageClock = FMath::FRandRange(0.12f, 0.2f);
				const FVector Rim = GetRummageOrigin(Searcher);
				const FVector Out = (Searcher->GetActorLocation() - Rim).GetSafeNormal2D();
				EnsureFX();
				BurstFX(FxBits, Rim, 2, (Out * 0.7f + FVector::UpVector).GetSafeNormal());
				if (FMath::FRand() < 0.6f)
				{
					BurstFX(FxDust, Rim, 1, FVector::UpVector, 0.6f);
				}
				PlaySearchSound(ETNSearchSound::Rummage, FMath::FRandRange(0.85f, 1.2f) * RummagePitch, FMath::FRandRange(0.65f, 1.f), Rim);
			}
		}
	}

	TickHop();
	if (FxSparkle == INDEX_NONE)
	{
		return;
	}
	const bool bFxQuiet = !bHopActive && !SearchState.Searcher && World->GetTimeSeconds() - LastFxTime >= TNSearchSpotDetail::FxTail;
	if (bFxQuiet && (!bNearView || IsSpentForLocalView()))
	{
		// Lejos de la cámara (o ya buscado) y sin nada vivo: fuera los emisores, para que no se acumulen instancias por
		// todo el mapa. Si vuelven a hacer falta se crean otra vez (las mallas de partícula van en caché).
		TNAmbientFX::RemoveOwner(this);
		FxSparkle = INDEX_NONE;
		FxDust = INDEX_NONE;
		FxBits = INDEX_NONE;
		FxPoof = INDEX_NONE;
		return;
	}
	TNAmbientFX::TickOwner(this, DeltaSeconds);
}

void ATN_ProcSearchSpot::EnsureFX()
{
	if (FxSparkle != INDEX_NONE || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const FLinearColor DustColor = SpotShape.Dust.ReinterpretAsLinear();
	const FVector Origin = GetActorLocation();

	// Chispitas doradas (pista de «buscable» y estallido del premio): las mismas que suben de los objetos del suelo.
	FxSparkle = TNAmbientFX::AddEmitter(this, TNLootGlow::SparkleDesc(18, 6000.f), Origin);

	// Nube de polvo del color del suelo del bioma (rebuscar y «¡pof!»).
	TNAmbientFX::FEmitterDesc Dust;
	Dust.Shape = TNAmbientFX::EShape::Puff;
	Dust.bSoft = true;
	Dust.bCloud = true;
	Dust.Color = DustColor;
	Dust.Alpha = 0.55f;
	Dust.MaxParticles = 40;
	Dust.Rate = 0.f;
	Dust.SpawnRadius = 20.f;
	Dust.SpawnHeight = 10.f;
	Dust.Speed = 130.f;
	Dust.SpeedJitter = 0.4f;
	Dust.Spread = 1.f;
	Dust.Gravity = -40.f;
	Dust.Drag = 2.2f;
	Dust.LifeMin = 0.55f;
	Dust.LifeMax = 1.f;
	Dust.SizeStart = 14.f;
	Dust.SizeEnd = 38.f;
	Dust.WakeDistance = 6000.f;
	FxDust = TNAmbientFX::AddEmitter(this, Dust, Origin);

	// Piedrecitas y terrones (opacos, algo más oscuros que el polvo) que saltan y caen.
	TNAmbientFX::FEmitterDesc Bits;
	Bits.Shape = TNAmbientFX::EShape::Ember;
	Bits.Color = DustColor * 0.55f;
	Bits.MaxParticles = 36;
	Bits.Rate = 0.f;
	Bits.SpawnRadius = 12.f;
	Bits.SpawnHeight = 5.f;
	Bits.Speed = 300.f;
	Bits.SpeedJitter = 0.35f;
	Bits.Spread = 0.7f;
	Bits.Gravity = -980.f;
	Bits.Drag = 0.4f;
	Bits.LifeMin = 0.35f;
	Bits.LifeMax = 0.6f;
	Bits.SizeStart = 7.f;
	Bits.SizeEnd = 5.f;
	Bits.WakeDistance = 5000.f;
	FxBits = TNAmbientFX::AddEmitter(this, Bits, Origin);

	// Nubecilla blanca del «¡puf!».
	TNAmbientFX::FEmitterDesc Poof;
	Poof.Shape = TNAmbientFX::EShape::Puff;
	Poof.bSoft = true;
	Poof.bCloud = true;
	Poof.Color = FLinearColor(1.f, 0.98f, 0.92f);
	Poof.Alpha = 0.75f;
	Poof.MaxParticles = 20;
	Poof.Rate = 0.f;
	Poof.SpawnRadius = 22.f;
	Poof.SpawnHeight = 25.f;
	Poof.Speed = 240.f;
	Poof.SpeedJitter = 0.4f;
	Poof.Spread = 1.f;
	Poof.Gravity = 0.f;
	Poof.Buoyancy = 45.f;
	Poof.Drag = 3.4f;
	Poof.LifeMin = 0.55f;
	Poof.LifeMax = 0.9f;
	Poof.SizeStart = 26.f;
	Poof.SizeEnd = 62.f;
	Poof.WakeDistance = 6000.f;
	FxPoof = TNAmbientFX::AddEmitter(this, Poof, Origin);
}

void ATN_ProcSearchSpot::BurstFX(int32 Emitter, const FVector& Where, int32 Count, const FVector& Direction, float SpeedScale)
{
	TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, Emitter);
	if (!E)
	{
		return;
	}
	E->Origin = Where;
	if (!Direction.IsNearlyZero())
	{
		E->Desc.Direction = Direction;
	}
	// La velocidad solo se lee al nacer: se escala para este estallido y se deja como estaba.
	const float BaseSpeed = E->Desc.Speed;
	E->Desc.Speed = BaseSpeed * SpeedScale;
	TNAmbientFX::Burst(*E, Count);
	E->Desc.Speed = BaseSpeed;
	if (const UWorld* World = GetWorld())
	{
		LastFxTime = World->GetTimeSeconds();
	}
}

void ATN_ProcSearchSpot::EmitSparkles(const FVector& Where, int32 Count, const FVector& Direction, float SpeedScale)
{
	if (GetNetMode() == NM_DedicatedServer || Count <= 0)
	{
		return;
	}
	EnsureFX();
	BurstFX(FxSparkle, Where, Count, Direction, SpeedScale);
}

void ATN_ProcSearchSpot::PlaySearchSound(ETNSearchSound Sound, float Pitch, float Volume, const FVector& Where)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!Synth)
	{
		Synth = UTN_SearchSynthComponent::AttachTo(this, Where);
	}
	if (Synth)
	{
		Synth->SetWorldLocation(Where);
		Synth->TriggerSound(Sound, Pitch, Volume);
	}
}

void ATN_ProcSearchSpot::StartHop(AActor* Pickup, double Elapsed)
{
	HoppedActor = Pickup;
	HopFrom = SearchState.LootFrom;
	HopTo = SearchState.LootTo;
	HopRotation = Pickup->GetActorRotation();
	if (Elapsed >= TNSearchSpotDetail::HopSeconds)
	{
		// Llega tarde (esta máquina lo ha visto después): ya está en el suelo.
		Pickup->SetActorLocation(HopTo);
		Pickup->SetActorScale3D(FVector::OneVector);
		bHopActive = false;
		return;
	}
	HopActor = Pickup;
	HopStart = GetWorld()->GetTimeSeconds() - Elapsed;
	bHopActive = true;
	SetActorTickInterval(0.f);
	TickHop();
}

void ATN_ProcSearchSpot::TickHop()
{
	if (!bHopActive)
	{
		return;
	}
	AActor* Pickup = HopActor.Get();
	const UWorld* World = GetWorld();
	if (!Pickup || !World)
	{
		// Recogido en el aire (o el mapa se va): nada que mover.
		bHopActive = false;
		return;
	}
	const float Alpha = FMath::Clamp(static_cast<float>((World->GetTimeSeconds() - HopStart) / TNSearchSpotDetail::HopSeconds), 0.f, 1.f);
	if (Alpha >= 1.f)
	{
		// Aterriza: en su sitio (el mismo que el del servidor), a tamaño real y con un poco de arena.
		Pickup->SetActorLocationAndRotation(HopTo, HopRotation);
		Pickup->SetActorScale3D(FVector::OneVector);
		bHopActive = false;
		EnsureFX();
		BurstFX(FxDust, HopTo, 4, FVector::UpVector, 0.7f);
		PlaySearchSound(ETNSearchSound::Rummage, 1.35f, 0.45f, HopTo);
		return;
	}
	// Parábola corta como la salida del probador, girando una vuelta y creciendo al asomar.
	const FVector Position = FMath::Lerp(HopFrom, HopTo, Alpha) + FVector(0.f, 0.f, 4.f * TNSearchSpotDetail::HopApex * Alpha * (1.f - Alpha));
	const float Grow = FMath::Min(1.f, Alpha / 0.35f);
	const float HopScale = FMath::Lerp(TNSearchSpotDetail::HopStartScale, 1.f, 1.f - FMath::Square(1.f - Grow));
	Pickup->SetActorLocationAndRotation(Position, HopRotation + FRotator(0.f, 360.f * (1.f - Alpha), 0.f));
	Pickup->SetActorScale3D(FVector(HopScale));
}

void ATN_ProcSearchSpot::DrawDebugSpot(float DeltaSeconds)
{
	DebugClock -= DeltaSeconds;
	if (DebugClock > 0.f)
	{
		return;
	}
	DebugClock = 0.3f;
	const UWorld* World = GetWorld();
	if (!World || TNLocalViews::ClosestCameraDistance(World, GetActorLocation()) > 30000.0)
	{
		return;
	}
	FColor Color(255, 200, 40);
	if (SearchState.Searcher)
	{
		Color = FColor(255, 120, 20);
	}
	else if (SearchState.Outcome == ETNSearchOutcome::Found)
	{
		Color = FColor(80, 220, 90);
	}
	else if (SearchState.Outcome == ETNSearchOutcome::Empty)
	{
		Color = FColor(140, 140, 140);
	}
	const FVector Base = GetActorLocation();
	FVector Axis = GetActorForwardVector().GetSafeNormal2D();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::ForwardVector;
	}
	constexpr float Life = 0.32f;
	DrawDebugLine(World, Base, Base + FVector(0.f, 0.f, 2500.f), Color, false, Life, SDPG_Foreground, 10.f);
	DrawDebugCapsule(World, Base + FVector(0.f, 0.f, 30.f), SpotShape.HalfLength + SpotShape.Radius, SpotShape.Radius,
		FRotationMatrix::MakeFromZ(Axis).ToQuat(), Color, false, Life, SDPG_Foreground, 3.f);
}

int32 TNSearchMarker::TraceRimGround(const UWorld* World, const FVector& Center, double RingRadius, const AActor* IgnoreActor, FVector (&OutPoints)[4])
{
	const double MaxStep = 120.0 + 0.4 * RingRadius;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_SearchMarker), false, IgnoreActor);
	int32 Hits = 0;
	for (int32 k = 0; k < 4; ++k)
	{
		const double Angle = UE_DOUBLE_HALF_PI * static_cast<double>(k);
		const double X = Center.X + FMath::Cos(Angle) * RingRadius;
		const double Y = Center.Y + FMath::Sin(Angle) * RingRadius;
		OutPoints[k] = FVector(X, Y, Center.Z);
		FHitResult Hit;
		if (World && World->LineTraceSingleByObjectType(Hit, FVector(X, Y, Center.Z + 150.0), FVector(X, Y, Center.Z - 400.0),
			FCollisionObjectQueryParams(ECC_WorldStatic), Query) && FMath::Abs(Hit.ImpactPoint.Z - Center.Z) <= MaxStep)
		{
			OutPoints[k].Z = Hit.ImpactPoint.Z;
			++Hits;
		}
	}
	return Hits;
}

float TNSearchMarker::RingRadiusForFoot(float FootRadius)
{
	// Los guiones del anillo de los objetos del suelo van en la corona de fuera: se escala para que empiecen MarkerMargin cm
	// más allá del borde de lo que rodea, y se vea alrededor en vez de quedar debajo.
	const float DashStart = TNLootGlow::RingDashInnerRadius / TNLootGlow::RingUnitRadius;
	return (FootRadius + TNSearchSpotDetail::MarkerMargin) / DashStart;
}

float ATN_ProcSearchSpot::GetMarkerRing(FVector& OutCenter, bool& bOutPending) const
{
	OutCenter = GetActorLocation();
	bOutPending = false;

	// Con punto propio (el montículo de arena de la playa): centrado en él y algo mayor que su base.
	FVector OwnGround = FVector::ZeroVector;
	float OwnFoot = 0.f;
	switch (GetMarkerAnchor(OwnGround, OwnFoot))
	{
		case ETNSearchMarkerAnchor::Point:
			OutCenter = OwnGround;
			return FMath::Max(MarkerRadius, TNSearchMarker::RingRadiusForFoot(OwnFoot));
		case ETNSearchMarkerAnchor::Pending:
			bOutPending = true;
			break;
		default:
			break;
	}

	// Sin él: abarca la huella entera. Es una cápsula que cabe en un círculo de radio Radius + HalfLength.
	const float Footprint = SpotShape.Radius + SpotShape.HalfLength;
	return FMath::Max(MarkerRadius, TNSearchMarker::RingRadiusForFoot(Footprint));
}

void ATN_ProcSearchSpot::FitMarkerToGround(const FVector& Center, float RingRadius)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	MarkerFitCenter = Center;
	MarkerFitRadius = RingRadius;
	MarkerTraceTime = World->GetTimeSeconds();
	++MarkerTraces;
	if (MarkerRing)
	{
		MarkerRing->SetCullDistance(MarkerDrawDistance + RingRadius);
	}

	// Lo que hay en el centro (el decorado, el montículo) taparía la traza: el suelo se mira en cuatro puntos de la propia
	// circunferencia del anillo, que queda fuera, y el anillo se apoya en el plano que forman. Un punto sin suelo a mano
	// (la colisión del terreno aún se está cocinando) o muy distinto del centro (un escalón, otro nivel) cuenta como el
	// suelo del centro, que es donde se puso el actor o el montículo.
	FVector Points[4];
	const int32 Hits = TNSearchMarker::TraceRimGround(World, Center, static_cast<double>(RingRadius), this, Points);
	bMarkerGrounded = Hits == 4;
	MarkerGround = FVector(Center.X, Center.Y, (Points[0].Z + Points[1].Z + Points[2].Z + Points[3].Z) * 0.25);
	// Normal del plano por las diagonales (+X/-X y +Y/-Y); con el suelo muy empinado, el anillo se queda plano. Es la misma
	// cuenta que inclina el montículo de arena de la playa (#744).
	MarkerTilt = TNSearchMarker::GroundTilt(Points[0], Points[1], Points[2], Points[3]);
}

void ATN_ProcSearchSpot::TickMarker(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || MarkerDrawDistance <= 0.f)
	{
		bMarkerAnimating = false;
		return;
	}
	FVector Center = GetActorLocation();
	bool bPending = false;
	const float RingRadius = GetMarkerRing(Center, bPending);

	// Se ve mientras quede por buscar y alguna cámara local (con la pantalla partida, la más cercana) esté a menos de
	// MarkerDrawDistance de su borde, igual en todas las máquinas con pantalla (no depende de dónde esté ninguna tortuga).
	const double CameraDistance = TNLocalViews::ClosestCameraDistance(World, Center);
	const double ViewDistance = CameraDistance < 1e8
		? FMath::Max(0.0, CameraDistance - static_cast<double>(RingRadius)) : 0.0;
	const bool bWant = !bPending && !IsSpentForLocalView() && ViewDistance < static_cast<double>(MarkerDrawDistance);
	MarkerAppear = bWant ? FMath::Min(1.f, MarkerAppear + TNSearchSpotDetail::MarkerAppearSpeed * DeltaSeconds)
		: FMath::Max(0.f, MarkerAppear - TNSearchSpotDetail::MarkerHideSpeed * DeltaSeconds);
	bMarkerAnimating = MarkerAppear > 0.f && ViewDistance < static_cast<double>(TNSearchSpotDetail::MarkerAnimDistance);
	if (MarkerAppear <= 0.f)
	{
		if (MarkerRing && MarkerRing->IsVisible())
		{
			MarkerRing->SetVisibility(false);
		}
		return;
	}
	if (!MarkerRing)
	{
		UStaticMesh* RingAsset = TNLootGlow::RingMesh();
		if (!RingAsset)
		{
			MarkerAppear = 0.f;
			bMarkerAnimating = false;
			return;
		}
		MarkerRing = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		MarkerRing->SetupAttachment(SceneRoot);
		MarkerRing->SetAbsolute(true, true, true);
		MarkerRing->SetStaticMesh(RingAsset);
		MarkerRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MarkerRing->SetGenerateOverlapEvents(false);
		MarkerRing->SetCanEverAffectNavigation(false);
		MarkerRing->SetCastShadow(false);
		MarkerRing->SetReceivesDecals(false);
		MarkerRing->SetCullDistance(MarkerDrawDistance + RingRadius);
		MarkerRing->RegisterComponent();
	}

	// El suelo bajo el anillo: al salir por primera vez, si cambia el centro o el tamaño y, mientras falte algún punto,
	// cada segundo unas pocas veces más. No se mueve con nadie: se busca una vez y queda.
	const bool bFitChanged = !FMath::IsNearlyEqual(RingRadius, MarkerFitRadius, 1.f) || !Center.Equals(MarkerFitCenter, 1.0);
	if (bFitChanged)
	{
		MarkerTraces = 0;
	}
	if (bFitChanged || (!bMarkerGrounded && MarkerTraces < TNSearchSpotDetail::MarkerMaxTraces
		&& World->GetTimeSeconds() - MarkerTraceTime > TNSearchSpotDetail::MarkerRetraceSeconds))
	{
		FitMarkerToGround(Center, RingRadius);
	}

	// Gira despacio y respira, como el de los objetos (la misma pose, TNLootGlow::RingPose); mientras alguien rebusca,
	// deprisa y latiendo.
	const bool bSearching = SearchState.Searcher != nullptr;
	MarkerClock += DeltaSeconds * (bSearching ? TNSearchSpotDetail::MarkerSearchSpinScale : 1.f);
	const float Grow = 1.f - FMath::Square(1.f - MarkerAppear);
	const float Breath = bSearching ? TNSearchSpotDetail::MarkerSearchPulse * FMath::Abs(FMath::Sin(MarkerClock * 1.6f))
		: TNLootGlow::RingBreathDepth * FMath::Sin(MarkerClock * TNLootGlow::RingBreathRate);
	if (!MarkerRing->IsVisible())
	{
		MarkerRing->SetVisibility(true);
	}
	MarkerRing->SetWorldTransform(TNLootGlow::RingPose(MarkerGround, MarkerTilt, RingRadius, MarkerClock, Grow, Breath));
}
