#pragma once

// C++ puro (sin tipos de Unreal): se compila y se mide fuera del motor con el arnés de Tools/RaceMusic (shell.cpp), como el
// resto de sintetizadores del juego. Lo usa UTN_ShellImpactSynthComponent (TN_ShellImpactSynth.cpp).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

/**
 * Golpes del caparazón contra el mundo, sintetizados. Siete timbres, según contra qué choca la bola: arena, roca, madera,
 * agua, otra tortuga, un enemigo y los trastos de los bañistas (plástico y lata). Todos son cortos (0,3 a 0,5 s) y se
 * disparan uno a uno desde una cola sin bloqueos; nada suena solo ni en bucle.
 *
 * Cada timbre se construye con las mismas piezas: un golpe grave de cuerpo (seno cuyo tono cae), un chasquido de ruido en el
 * ataque, resonadores modales que suenan tras el golpe (roca, madera, caparazón, plástico) y ruido filtrado que se cierra
 * (arena, agua). La fuerza (0..1, según la velocidad del impacto) cambia la energía, la duración y el brillo: un roce
 * flojo es un «fum» apagado y un golpazo, un «¡crac!» largo y brillante.
 */
namespace TNShellImpact
{
	constexpr float Pi = 3.14159265358979323846f;
	constexpr float TwoPi = 6.28318530717958647692f;
	constexpr int32_t MaxVoices = 8;
	constexpr int32_t Block = 16;
	/** Fundido de salida común (s): ningún golpe se corta con un chasquido. */
	constexpr float Tail = 0.03f;

	/** Timbres (el orden es el de ETNShellImpactSound). */
	enum EKind : uint8_t { Sand = 0, Rock, Wood, Water, Turtle, Enemy, Junk, NumKinds };

	/** Duración de cada timbre con fuerza 1 (s); con menos fuerza dura algo menos. */
	inline constexpr float Durations[NumKinds] = { 0.34f, 0.36f, 0.46f, 0.52f, 0.55f, 0.40f, 0.34f };

	/** Ganancia de cada timbre para que a igual fuerza suenen igual de fuerte (medida en el arnés: picos parejos con fuerza 1). */
	inline constexpr float KindTrims[NumKinds] = { 0.85f, 0.90f, 0.90f, 0.90f, 0.85f, 0.78f, 0.95f };

	/** Un disparo: timbre, fuerza (0..1), multiplicador de tono, ganancia y semilla del azar (que el mismo golpe suene igual en el arnés). */
	struct FEvent
	{
		uint8_t Kind = 0;
		float Strength = 0.5f;
		float Pitch = 1.f;
		float Gain = 1.f;
		uint32_t Seed = 1u;
	};

	/** Cola circular sin bloqueos de un productor (hilo de juego) y un consumidor (hilo de audio). */
	struct FQueue
	{
		static constexpr uint32_t Capacity = 16;

		FEvent Events[Capacity];
		std::atomic<uint32_t> WriteIndex{ 0 };
		std::atomic<uint32_t> ReadIndex{ 0 };
		std::atomic<float> Master{ 1.f };

		/** Hilo de juego. false si la cola está llena (el disparo se pierde). */
		bool Push(const FEvent& InEvent)
		{
			const uint32_t W = WriteIndex.load(std::memory_order_relaxed);
			const uint32_t R = ReadIndex.load(std::memory_order_acquire);
			if (W - R >= Capacity) { return false; }
			Events[W % Capacity] = InEvent;
			WriteIndex.store(W + 1, std::memory_order_release);
			return true;
		}

		/** Hilo de audio. */
		bool Pop(FEvent& OutEvent)
		{
			const uint32_t R = ReadIndex.load(std::memory_order_relaxed);
			const uint32_t W = WriteIndex.load(std::memory_order_acquire);
			if (R == W) { return false; }
			OutEvent = Events[R % Capacity];
			ReadIndex.store(R + 1, std::memory_order_release);
			return true;
		}
	};

	inline float Noise(uint32_t& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return static_cast<float>(State >> 8) * (1.f / 8388608.f) - 1.f;
	}

	inline float Unit(uint32_t& State) { return 0.5f + 0.5f * Noise(State); }

	inline float Smooth(float X)
	{
		const float T = std::clamp(X, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Paso bajo de un polo (y, restando, paso alto). */
	struct FOnePole
	{
		float Z = 0.f;
		float K = 1.f;
		void SetHz(float InHz, float InRate) { K = 1.f - std::exp(-TwoPi * std::clamp(InHz, 5.f, InRate * 0.4f) / InRate); }
		float Low(float InX) { Z += K * (InX - Z); return Z; }
		float High(float InX) { Z += K * (InX - Z); return InX - Z; }
	};

	/** Caída exponencial por muestra: se arma con su constante de tiempo y se lee con Next(). */
	struct FDecay
	{
		float V = 0.f;
		float K = 0.f;
		void Set(float InTauSeconds, float InRate, float InStart = 1.f)
		{
			V = InStart;
			K = std::exp(-1.f / (std::max(1e-4f, InTauSeconds) * InRate));
		}
		float Next() { const float Out = V; V *= K; if (V < 1e-6f) { V = 0.f; } return Out; }
	};

	/** Resonador modal (seno amortiguado por recurrencia de dos polos): «se golpea» con Strike y suena solo. */
	struct FResonator
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float B1 = 0.f;
		float B2 = 0.f;
		float SinW = 0.f;

		void Tune(float InHz, float InT60Seconds, float InRate)
		{
			const float W = TwoPi * std::min(InHz / InRate, 0.45f);
			const float R = std::exp(-6.9078f / (std::max(0.004f, InT60Seconds) * InRate));
			B1 = 2.f * R * std::cos(W);
			B2 = -R * R;
			SinW = std::sin(W);
		}

		void Strike(float InAmp) { Y1 += InAmp * SinW; }

		float Process()
		{
			const float Y = B1 * Y1 + B2 * Y2;
			Y2 = Y1;
			Y1 = Y;
			return Y;
		}
	};

	struct FVoice
	{
		bool bActive = false;
		uint8_t Kind = 0;
		float Age = 0.f;
		float Duration = 0.3f;
		float Strength = 0.5f;
		float Pitch = 1.f;
		float Gain = 1.f;
		uint32_t NoiseState = 0x2545F491u;
		bool bStruck = false;
		float PhaseA = 0.f;
		float PhaseB = 0.f;
		float PhaseC = 0.f;
		FDecay Body;
		FDecay Aux;
		FDecay Burst;
		FDecay Click;
		FDecay PitchFall;
		FResonator Res[3];
		float ResAmp[3] = {};
		FOnePole Lp;
		FOnePole Hp;
		/** Segundo momento (eco del crujido de la roca, segunda burbuja, gotas): instante en s y contador. */
		float NextEventAt = 1.0e9f;
		int32_t Events = 0;
		float Param0 = 0.f;
		float Param1 = 0.f;
	};

	class FCore
	{
	public:
		void Init(float InRate)
		{
			Rate = std::max(8000.f, InRate);
			InvRate = 1.f / Rate;
		}

		/** Mono repetido en todos los canales (el componente es mono y espacializado). */
		void Render(float* Out, int32_t Frames, int32_t Channels, FQueue& Queue)
		{
			FEvent Pending;
			while (Queue.Pop(Pending)) { StartVoice(Pending); }
			bool bAny = false;
			for (const FVoice& V : Voices) { bAny |= V.bActive; }
			if (!bAny)
			{
				for (int32_t i = 0; i < Frames * Channels; ++i) { Out[i] = 0.f; }
				return;
			}
			const float Master = Queue.Master.load(std::memory_order_relaxed);
			for (int32_t Frame = 0; Frame < Frames; Frame += Block)
			{
				const int32_t Count = std::min(Block, Frames - Frame);
				float Mix[Block] = {};
				for (FVoice& V : Voices)
				{
					if (V.bActive) { RenderVoice(V, Mix, Count); }
				}
				for (int32_t i = 0; i < Count; ++i)
				{
					// Saturación suave (aproximación racional de tanh): varios golpes a la vez no recortan.
					const float X = std::clamp(Mix[i] * Master, -3.f, 3.f);
					const float Soft = X * (27.f + X * X) / (27.f + 9.f * X * X);
					for (int32_t Ch = 0; Ch < Channels; ++Ch) { Out[(Frame + i) * Channels + Ch] = Soft; }
				}
			}
		}

		/** Diagnóstico (arnés): voces robadas mientras sonaban. */
		int32_t GetStealCount() const { return Steals; }

	private:
		float Rate = 48000.f;
		float InvRate = 1.f / 48000.f;
		FVoice Voices[MaxVoices];
		int32_t Steals = 0;

		void StartVoice(const FEvent& InEvent)
		{
			if (InEvent.Kind >= NumKinds) { return; }
			int32_t Best = 0;
			float Oldest = -1.f;
			bool bFree = false;
			for (int32_t i = 0; i < MaxVoices; ++i)
			{
				if (!Voices[i].bActive) { Best = i; bFree = true; break; }
				if (Voices[i].Age > Oldest) { Oldest = Voices[i].Age; Best = i; }
			}
			if (!bFree) { ++Steals; }
			FVoice& V = Voices[Best];
			V = FVoice();
			V.bActive = true;
			V.Kind = InEvent.Kind;
			V.Strength = std::clamp(InEvent.Strength, 0.f, 1.f);
			V.Pitch = std::clamp(InEvent.Pitch, 0.4f, 2.5f);
			V.Gain = std::clamp(InEvent.Gain, 0.f, 2.f);
			V.NoiseState = (InEvent.Seed * 2654435761u) | 1u;
			for (int32_t k = 0; k < 4; ++k) { Noise(V.NoiseState); }
			const float S = V.Strength;
			V.Duration = Durations[InEvent.Kind] * (0.7f + 0.3f * S);
			switch (InEvent.Kind)
			{
			case Sand: StartSand(V, S); break;
			case Rock: StartRock(V, S); break;
			case Wood: StartWood(V, S); break;
			case Water: StartWater(V, S); break;
			case Turtle: StartTurtle(V, S); break;
			case Enemy: StartEnemy(V, S); break;
			default: StartJunk(V, S); break;
			}
		}

		float Jit(FVoice& V, float InSpread) { return 1.f + InSpread * Noise(V.NoiseState); }

		// ── Arranque de cada timbre ─────────────────────────────────────────

		void StartSand(FVoice& V, float S)
		{
			// «Fum»: golpe grave que cae de tono y un soplo de ruido apagado que se cierra; casi sin resonancia.
			V.Body.Set(0.045f + 0.035f * S, Rate);
			V.PitchFall.Set(0.03f, Rate);
			V.Burst.Set(0.04f + 0.035f * S, Rate);
			V.Aux.Set(0.10f, Rate);
			V.Param0 = 58.f * V.Pitch;
			V.Hp.SetHz(2200.f, Rate);
			V.Lp.SetHz(900.f, Rate);
		}

		void StartRock(FVoice& V, float S)
		{
			// «¡Tac!»: chasquido seco y tres resonancias de piedra que se afinan con el azar; con mucha fuerza, un segundo crujido.
			const float T60 = 0.05f + 0.06f * S;
			V.Res[0].Tune(1250.f * V.Pitch * Jit(V, 0.06f), T60, Rate);
			V.Res[1].Tune(2380.f * V.Pitch * Jit(V, 0.06f), T60 * 0.7f, Rate);
			V.Res[2].Tune(3350.f * V.Pitch * Jit(V, 0.06f), T60 * 0.5f, Rate);
			V.ResAmp[0] = 0.62f; V.ResAmp[1] = 0.42f; V.ResAmp[2] = 0.26f;
			V.Click.Set(0.0018f, Rate);
			V.Body.Set(0.03f, Rate);
			V.PitchFall.Set(0.02f, Rate);
			V.Param0 = 140.f * V.Pitch;
			V.Hp.SetHz(2000.f, Rate);
			if (S > 0.55f) { V.NextEventAt = 0.009f + 0.006f * Unit(V.NoiseState); }
		}

		void StartWood(FVoice& V, float S)
		{
			// «¡Toc!» hueco: tablón libre (modos 1 : 2,76 : 5,4), un golpe grave de cuerpo y un chasquido blando.
			const float Base = 235.f * V.Pitch * Jit(V, 0.05f);
			V.Res[0].Tune(Base, 0.16f + 0.10f * S, Rate);
			V.Res[1].Tune(Base * 2.76f, 0.09f + 0.05f * S, Rate);
			V.Res[2].Tune(Base * 5.4f, 0.05f + 0.02f * S, Rate);
			V.ResAmp[0] = 0.95f; V.ResAmp[1] = 0.5f; V.ResAmp[2] = 0.26f;
			V.Click.Set(0.0028f, Rate);
			V.Body.Set(0.042f, Rate);
			V.PitchFall.Set(0.02f, Rate);
			V.Param0 = 105.f * V.Pitch;
			V.Hp.SetHz(900.f, Rate);
		}

		void StartWater(FVoice& V, float S)
		{
			// «¡Plof!»: chapoteo de ruido, una burbuja que sube de tono, un «gloop» grave y, con fuerza, gotas sueltas.
			V.Burst.Set(0.04f + 0.06f * S, Rate);
			V.Body.Set(0.085f, Rate);
			V.Aux.Set(0.10f, Rate);
			V.Param0 = (360.f + 90.f * Unit(V.NoiseState)) * V.Pitch;
			V.Param1 = 0.f;
			V.Hp.SetHz(1300.f, Rate);
			V.Lp.SetHz(4200.f, Rate);
			V.NextEventAt = 0.075f + 0.04f * Unit(V.NoiseState);
		}

		void StartTurtle(FVoice& V, float S)
		{
			// «¡Clonc!» de caparazón contra caparazón: hueco y algo largo.
			const float Base = 310.f * V.Pitch * Jit(V, 0.05f);
			V.Res[0].Tune(Base, 0.20f + 0.10f * S, Rate);
			V.Res[1].Tune(Base * 1.97f, 0.12f + 0.06f * S, Rate);
			V.Res[2].Tune(Base * 3.18f, 0.08f + 0.04f * S, Rate);
			V.ResAmp[0] = 0.9f; V.ResAmp[1] = 0.55f; V.ResAmp[2] = 0.32f;
			V.Click.Set(0.002f, Rate);
			V.Body.Set(0.05f, Rate);
			V.PitchFall.Set(0.022f, Rate);
			V.Param0 = 118.f * V.Pitch;
			V.Hp.SetHz(1200.f, Rate);
		}

		void StartEnemy(FVoice& V, float S)
		{
			// «¡Boing!» de dibujos animados: un tono que cae con vaivén y un fogonazo de ruido en el golpe.
			V.Body.Set(0.10f + 0.05f * S, Rate);
			V.PitchFall.Set(0.075f, Rate);
			V.Burst.Set(0.022f, Rate);
			V.Aux.Set(0.16f, Rate);
			V.Param0 = 175.f * V.Pitch;
			V.Lp.SetHz(2600.f, Rate);
		}

		void StartJunk(FVoice& V, float S)
		{
			// Plástico y lata: modos altos y secos («¡pok!», «¡tinc!»).
			const float Base = 640.f * V.Pitch * Jit(V, 0.10f);
			V.Res[0].Tune(Base, 0.08f + 0.05f * S, Rate);
			V.Res[1].Tune(Base * 2.35f, 0.05f + 0.03f * S, Rate);
			V.Res[2].Tune(Base * 4.1f, 0.03f + 0.02f * S, Rate);
			V.ResAmp[0] = 0.9f; V.ResAmp[1] = 0.6f; V.ResAmp[2] = 0.36f;
			V.Click.Set(0.0013f, Rate);
			V.Body.Set(0.024f, Rate);
			V.PitchFall.Set(0.015f, Rate);
			V.Param0 = 190.f * V.Pitch;
			V.Hp.SetHz(3000.f, Rate);
		}

		// ── Dibujo de cada timbre ───────────────────────────────────────────

		void RenderVoice(FVoice& V, float* Mix, int32_t Count)
		{
			float Buf[Block] = {};
			switch (V.Kind)
			{
			case Sand: RenderSand(V, Buf, Count); break;
			case Rock: RenderRock(V, Buf, Count); break;
			case Wood: RenderWood(V, Buf, Count); break;
			case Water: RenderWater(V, Buf, Count); break;
			case Turtle: RenderTurtle(V, Buf, Count); break;
			case Enemy: RenderEnemy(V, Buf, Count); break;
			default: RenderJunk(V, Buf, Count); break;
			}
			// Fundido de salida común, interpolado dentro del bloque.
			const float BlockLength = static_cast<float>(Count) * InvRate;
			const float FadeStart = Smooth((V.Duration - V.Age) / Tail);
			const float FadeEnd = Smooth((V.Duration - V.Age - BlockLength) / Tail);
			for (int32_t i = 0; i < Count; ++i)
			{
				const float A = static_cast<float>(i) / static_cast<float>(Count);
				Mix[i] += Buf[i] * V.Gain * KindTrims[V.Kind] * (FadeStart + (FadeEnd - FadeStart) * A);
			}
			V.Age += BlockLength;
			if (V.Age >= V.Duration) { V.bActive = false; }
		}

		/** Seno de cuerpo que cae de tono (Param0 = tono final): devuelve la muestra sin envolvente. */
		float BodySine(FVoice& V, float InFall)
		{
			const float Hz = V.Param0 * (1.f + InFall * V.PitchFall.Next());
			V.PhaseA += Hz * InvRate;
			V.PhaseA -= std::floor(V.PhaseA);
			return std::sin(TwoPi * V.PhaseA);
		}

		void RenderSand(FVoice& V, float* Buf, int32_t Count)
		{
			const float S = V.Strength;
			const float T = V.Age;
			// El ruido se apaga hacia lo grave: más fuerza, más brillo al principio.
			V.Lp.SetHz(320.f + (500.f + 1600.f * S) * std::exp(-T / 0.035f), Rate);
			for (int32_t i = 0; i < Count; ++i)
			{
				const float N = Noise(V.NoiseState);
				const float Attack = std::min(1.f, (T + static_cast<float>(i) * InvRate) / 0.002f);
				const float Thump = BodySine(V, 1.4f) * V.Body.Next();
				const float Puff = V.Lp.Low(N) * V.Burst.Next() * Attack;
				const float Grain = V.Hp.High(N) * V.Aux.Next() * Attack;
				Buf[i] += 0.62f * (0.45f + 0.55f * S) * Thump + 1.05f * Puff + 0.09f * (0.4f + 0.6f * S) * Grain;
			}
		}

		void RenderRock(FVoice& V, float* Buf, int32_t Count)
		{
			const float S = V.Strength;
			for (int32_t i = 0; i < Count; ++i)
			{
				const float Tl = V.Age + static_cast<float>(i) * InvRate;
				if (!V.bStruck)
				{
					V.bStruck = true;
					for (int32_t k = 0; k < 3; ++k) { V.Res[k].Strike(V.ResAmp[k] * (0.55f + 0.45f * S)); }
				}
				if (Tl >= V.NextEventAt)
				{
					// Segundo crujido: la piedra se rasca.
					V.NextEventAt = 1.0e9f;
					V.Click.Set(0.0022f, Rate, 0.55f);
					for (int32_t k = 0; k < 3; ++k) { V.Res[k].Strike(V.ResAmp[k] * 0.4f * (0.5f + 0.5f * S)); }
				}
				const float N = Noise(V.NoiseState);
				const float Snap = V.Hp.High(N) * V.Click.Next();
				const float Thump = BodySine(V, 1.0f) * V.Body.Next();
				const float Ring = V.Res[0].Process() + V.Res[1].Process() + V.Res[2].Process();
				Buf[i] += 0.85f * Ring + 0.55f * Snap * (0.5f + 0.5f * S) + 0.5f * (0.35f + 0.65f * S) * Thump;
			}
		}

		void RenderWood(FVoice& V, float* Buf, int32_t Count)
		{
			const float S = V.Strength;
			for (int32_t i = 0; i < Count; ++i)
			{
				if (!V.bStruck)
				{
					V.bStruck = true;
					for (int32_t k = 0; k < 3; ++k) { V.Res[k].Strike(V.ResAmp[k] * (0.5f + 0.5f * S)); }
				}
				const float N = Noise(V.NoiseState);
				const float Knock = V.Hp.High(N) * V.Click.Next();
				const float Thump = BodySine(V, 0.9f) * V.Body.Next();
				const float Ring = V.Res[0].Process() + V.Res[1].Process() + V.Res[2].Process();
				Buf[i] += 0.8f * Ring + 0.5f * Knock * (0.5f + 0.5f * S) + 0.45f * (0.4f + 0.6f * S) * Thump;
			}
		}

		void RenderWater(FVoice& V, float* Buf, int32_t Count)
		{
			const float S = V.Strength;
			for (int32_t i = 0; i < Count; ++i)
			{
				const float Tl = V.Age + static_cast<float>(i) * InvRate;
				const float Attack = std::min(1.f, Tl / 0.004f);
				const float N = Noise(V.NoiseState);
				const float Splash = V.Lp.Low(V.Hp.High(N)) * V.Burst.Next() * Attack;
				// Burbuja: seno que sube de tono mientras se apaga.
				const float BubbleHz = V.Param0 * (1.f + 1.5f * (1.f - std::exp(-Tl / 0.06f)));
				V.PhaseB += BubbleHz * InvRate;
				V.PhaseB -= std::floor(V.PhaseB);
				const float BubbleEnv = std::min(1.f, Tl / 0.003f) * std::exp(-Tl / 0.075f);
				const float Bubble = std::sin(TwoPi * V.PhaseB) * BubbleEnv;
				// «Gloop» grave.
				V.PhaseA += (62.f + 40.f * std::exp(-Tl / 0.05f)) * V.Pitch * InvRate;
				V.PhaseA -= std::floor(V.PhaseA);
				const float Gloop = std::sin(TwoPi * V.PhaseA) * V.Body.Next() * Attack;
				if (Tl >= V.NextEventAt && V.Events < 1 + static_cast<int32_t>(4.f * S))
				{
					// Gotas que caen: pequeños tintineos agudos.
					++V.Events;
					V.NextEventAt = Tl + 0.035f + 0.06f * Unit(V.NoiseState);
					V.Res[V.Events % 3].Tune((1900.f + 1800.f * Unit(V.NoiseState)) * V.Pitch, 0.03f, Rate);
					V.Res[V.Events % 3].Strike(0.16f + 0.14f * Unit(V.NoiseState));
				}
				else if (Tl >= V.NextEventAt)
				{
					V.NextEventAt = 1.0e9f;
				}
				const float Extra = V.Res[0].Process() + V.Res[1].Process() + V.Res[2].Process();
				Buf[i] += 0.62f * (0.45f + 0.55f * S) * Splash + 0.42f * Bubble + 0.45f * (0.3f + 0.7f * S) * Gloop + Extra;
			}
		}

		void RenderTurtle(FVoice& V, float* Buf, int32_t Count)
		{
			const float S = V.Strength;
			for (int32_t i = 0; i < Count; ++i)
			{
				if (!V.bStruck)
				{
					V.bStruck = true;
					for (int32_t k = 0; k < 3; ++k) { V.Res[k].Strike(V.ResAmp[k] * (0.5f + 0.5f * S)); }
				}
				const float N = Noise(V.NoiseState);
				const float Knock = V.Hp.High(N) * V.Click.Next();
				const float Thump = BodySine(V, 1.1f) * V.Body.Next();
				const float Ring = V.Res[0].Process() + V.Res[1].Process() + V.Res[2].Process();
				Buf[i] += 0.8f * Ring + 0.5f * Knock * (0.5f + 0.5f * S) + 0.5f * (0.4f + 0.6f * S) * Thump;
			}
		}

		void RenderEnemy(FVoice& V, float* Buf, int32_t Count)
		{
			const float S = V.Strength;
			for (int32_t i = 0; i < Count; ++i)
			{
				const float Tl = V.Age + static_cast<float>(i) * InvRate;
				const float Hz = V.Param0 * (1.f + 2.4f * V.PitchFall.Next());
				V.PhaseA += Hz * InvRate;
				V.PhaseA -= std::floor(V.PhaseA);
				// Vaivén de goma que se apaga.
				const float Wobble = 1.f + 0.32f * std::sin(TwoPi * 15.f * Tl) * std::exp(-Tl / 0.11f);
				const float Wave = std::sin(TwoPi * V.PhaseA) + 0.33f * std::sin(3.f * TwoPi * V.PhaseA);
				const float Env = std::min(1.f, Tl / 0.002f) * V.Body.Next();
				const float N = Noise(V.NoiseState);
				const float Thwack = V.Lp.Low(N) * V.Burst.Next();
				Buf[i] += 0.55f * (0.55f + 0.45f * S) * Wave * Env * Wobble + 0.55f * Thwack;
			}
		}

		void RenderJunk(FVoice& V, float* Buf, int32_t Count)
		{
			const float S = V.Strength;
			for (int32_t i = 0; i < Count; ++i)
			{
				if (!V.bStruck)
				{
					V.bStruck = true;
					for (int32_t k = 0; k < 3; ++k) { V.Res[k].Strike(V.ResAmp[k] * (0.5f + 0.5f * S)); }
				}
				const float N = Noise(V.NoiseState);
				const float Tick = V.Hp.High(N) * V.Click.Next();
				const float Thump = BodySine(V, 0.8f) * V.Body.Next();
				const float Ring = V.Res[0].Process() + V.Res[1].Process() + V.Res[2].Process();
				Buf[i] += 0.8f * Ring + 0.5f * Tick * (0.5f + 0.5f * S) + 0.3f * (0.4f + 0.6f * S) * Thump;
			}
		}
	};
}
