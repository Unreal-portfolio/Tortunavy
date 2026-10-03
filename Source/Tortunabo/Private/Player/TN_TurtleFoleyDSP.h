#pragma once

#include "CoreMinimal.h"
#include <atomic>

/**
 * Motor DSP de los ruidos del cuerpo de la tortuga (UTN_TurtleFoleyComponent): pasos según la superficie, aterrizajes y
 * jadeo. C++ puro (solo CoreMinimal y <atomic>) para poder probarlo y calibrarlo fuera del motor.
 *
 * Pasos: cada pisada es la fuerza de apoyo de una pata blanda, en dos contactos muy seguidos (almohadilla y dedos), que
 * excita a la vez el golpe sordo de la propia pata y la textura de cada superficie mezclada por su peso: arena (ruido
 * granulado que se hunde), tierra y hojas (golpe apagado y crujidos sueltos), roca (toque corto con un chasquido suave y
 * arenilla), madera (tablón hueco: tres modos resonantes) y agua poco profunda (chapoteo que baja de tono y burbujas).
 * Patas blandas de dibujos animados: nada de tacones ni botas.
 *
 * Jadeo: respiraciones por la boca con el modelo fuente-filtro de la tos de la tormenta (aire y algo de voz por tres
 * formantes), con el ritmo y la fuerza que manda el juego (Pant, 0..1) y un suspiro al calmarse tras un jadeo fuerte.
 * La voz sale de la semilla de la tortuga con la misma cuenta que UTN_StormCoughComponent: jadeo y tos son la misma voz.
 *
 * Panzazo: el «plaf» de la tripa al caer (golpe grave ancho y chasquido de carne blanda, más la capa de la superficie),
 * el «tonc» hueco del caparazón al chocar arrastrándose y el arrastre continuo (FDragVoice): el roce de cada superficie
 * (arena que sisea, tierra y hojas, roca a trompicones, tablón que zumba, estela en el agua) con la fuerza y la viveza
 * que manda el juego (Drag y DragPace, que siguen a la velocidad del deslizamiento).
 *
 * Nado y caparazón: la brazada (StepKind::Stroke) es solo el agua que empujan las aletas, con ataque lento, cola larga y
 * burbujas detrás; meterse y salir del caparazón (StepKind::Shell) es un roce de banda que barre (hacia grave al entrar,
 * hacia agudo al salir) y el «tonc» hueco de la concha. Las dos llevan un fundido de salida para que la cola no corte.
 *
 * Hilos: FSharedParams lo escribe el hilo de juego (atómicos relajados y un anillo de pasos con un solo escritor) y lo
 * lee el de audio; Busy va al revés. Cada generador lleva su propio cursor del anillo y solo lee pasos si su arranque
 * es el vigente (RunId): el de un arranque anterior que aún suene no roba ni repite pasos. Todo lo demás vive solo en
 * el hilo de audio: sin asignaciones, sin UObjects, sin logs ni bloqueos; el azar sale de un xorshift propio.
 */
namespace TNTurtleFoley
{
	constexpr float Pi = 3.14159265f;

	/** Muestras por bloque de control: envolventes, filtros que se mueven y planificación se recalculan a este ritmo. */
	constexpr int32 BlockFrames = 32;

	/** Pisadas sonando a la vez (dos patas solapadas, un aterrizaje y margen). */
	constexpr int32 MaxSteps = 6;

	/** Huecos del anillo de pasos (el lector nunca se queda más de la mitad por detrás). */
	constexpr uint32 RingSize = 16;

	/** Burbujas por pisada en el agua. */
	constexpr int32 MaxBubbles = 3;

	/** Envolventes de capa de cada pisada (FStepVoice::Env). */
	constexpr int32 NumStepEnv = 10;

	/** Superficies (índices de FStepEvent::Surf). */
	namespace Surface
	{
		constexpr int32 Sand = 0;   ///< Arena seca o húmeda.
		constexpr int32 Soil = 1;   ///< Tierra, hierba y hojarasca.
		constexpr int32 Rock = 2;   ///< Roca, piedra y sillares.
		constexpr int32 Wood = 3;   ///< Tablones y madera hueca.
		constexpr int32 Water = 4;  ///< Agua poco profunda.
		constexpr int32 Num = 5;
	}

	/** Tipos de pisada. */
	namespace StepKind
	{
		constexpr uint8 Step = 0;   ///< Paso normal (una pata).
		constexpr uint8 Land = 1;   ///< Aterrizaje (las dos patas casi a la vez, más fuerte).
		constexpr uint8 Scuff = 2;  ///< Impulso del salto: roce de despegue, más textura que golpe.
		constexpr uint8 Belly = 3;  ///< Panzazo contra el suelo: «plaf» de tripa (golpe grave ancho y chasquido blando).
		constexpr uint8 Bump = 4;   ///< Choque del caparazón contra un obstáculo arrastrándose: «tonc» hueco.
		constexpr uint8 Stash = 5;  ///< Guardar (Foot 0) o sacar (Foot 1, más agudo) algo del caparazón: «toc» hueco corto.
		constexpr uint8 Stroke = 6; ///< Brazada al nadar: el agua que empujan las aletas (entra despacio, cola larga y burbujas).
		constexpr uint8 Shell = 7;  ///< Meterse (Foot 0) o salir (Foot 1) del caparazón: roce de la piel y «tonc» hueco.
	}

	/** Fase del ciclo de la brazada (0..1, la de UTN_TurtleAnimInstance::GetSwimStrokePhase) en la que suena: aleta estirada. */
	constexpr float SwimStrokeAt = 0.25f;

	/** Si la fase de un ciclo (0..1) ha pasado por At al ir de Prev a Cur (avanzando, con la vuelta de 1 a 0). */
	inline bool CrossedPhase(float Prev, float Cur, float At)
	{
		if (Prev <= Cur) { return Prev < At && At <= Cur; }
		return Prev < At || At <= Cur;
	}

	/** Una pisada que manda el hilo de juego. */
	struct FStepEvent
	{
		uint8 Kind = StepKind::Step;
		/** 0 = izquierda, 1 = derecha (cambia un poco el tono). */
		uint8 Foot = 0;
		/** Fuerza: ~0,4 andando despacio, ~0,65 andando, ~0,95 corriendo, hasta 1,4 al caer de alto. */
		float Force = 0.6f;
		/** 0 = andar .. 1 = carrera: apoyo más corto y timbre más vivo. */
		float Pace = 0.f;
		/** 1 = normal; más si carga con otra tortuga (más grave y pesado). */
		float Heavy = 1.f;
		/** Pesos de cada superficie (se normalizan al disparar). */
		float Surf[Surface::Num] = { 0.f, 0.f, 1.f, 0.f, 0.f };
	};

	/** Parámetros compartidos entre hilos. */
	struct FSharedParams
	{
		/** Anillo de pasos: solo lo escribe el hilo de juego; cada generador lo lee con su propio cursor. */
		FStepEvent Events[RingSize];
		std::atomic<uint32> WriteSeq{ 0u };
		/** Arranque vigente del sintetizador y dónde estaba el anillo al arrancar (los fija Init en el hilo de juego). */
		std::atomic<uint32> RunId{ 0u };
		std::atomic<uint32> RunStartSeq{ 0u };
		/** Ganancia final (volumen, consola y refuerzo de la tortuga local), la de los pasos y la del jadeo. */
		std::atomic<float> Master{ 1.f };
		std::atomic<float> StepGain{ 1.f };
		std::atomic<float> BreathGain{ 1.f };
		/** Jadeo objetivo: 0 = respira normal (callada); 1 = agotada. */
		std::atomic<float> Pant{ 0.f };
		/** 1 = cortar el jadeo ya (manda la tos o ha caído): no empieza más respiraciones ni suspira. */
		std::atomic<int32> PantHush{ 0 };
		/** Lo escribe el hilo de audio: 1 mientras suena algo o sigue jadeando. */
		std::atomic<int32> Busy{ 0 };
		/** Se fijan antes de arrancar: la voz (una por tortuga) y el azar de cada arranque. */
		std::atomic<uint32> VoiceSeed{ 1u };
		std::atomic<uint32> RunSeed{ 1u };
		/** Arrastre sobre la tripa: fuerza 0..1 (0 = callado), viveza 0..1 (velocidad), ganancia y pesos de superficie. */
		std::atomic<float> Drag{ 0.f };
		std::atomic<float> DragPace{ 0.f };
		std::atomic<float> DragGain{ 1.f };
		std::atomic<float> DragSurf[Surface::Num]{};

		static float Get(const std::atomic<float>& Value) { return Value.load(std::memory_order_relaxed); }
		static void Set(std::atomic<float>& Value, float NewValue) { Value.store(NewValue, std::memory_order_relaxed); }

		/** Hilo de juego: deja una pisada en el anillo. */
		void PushStep(const FStepEvent& InEvent)
		{
			const uint32 W = WriteSeq.load(std::memory_order_relaxed);
			Events[W % RingSize] = InEvent;
			WriteSeq.store(W + 1u, std::memory_order_release);
		}
	};

	// ─────────────────────────────────────────────────────────────────────────
	// Utilidades
	// ─────────────────────────────────────────────────────────────────────────

	/** xorshift32 propio (el mismo que la tos): barato, determinista con la semilla y sin estado global. */
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

	/** Semilla derivada (la misma cuenta que la tos: la voz de cada tortuga coincide). */
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

	/** Multiplicador por muestra de un decaimiento exponencial con constante de tiempo Seconds. */
	inline float DecayPerSample(float Seconds, float InvRate)
	{
		return FMath::Exp(-InvRate / FMath::Max(1e-5f, Seconds));
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

	/** Resonador de dos polos (un modo de vibración): con un impulso unidad, un seno que se apaga desde 1. */
	struct FResonator
	{
		float Y1 = 0.f;
		float Y2 = 0.f;
		float C = 0.f;
		float R2 = 0.f;
		float Norm = 1.f;

		void Set(float Hz, float DecaySeconds, float InvRate)
		{
			const float W = 2.f * Pi * FMath::Clamp(Hz * InvRate, 1e-4f, 0.45f);
			const float R = DecayPerSample(DecaySeconds, InvRate);
			C = 2.f * R * FMath::Cos(W);
			R2 = R * R;
			Norm = FMath::Sin(W);
		}

		void Reset() { Y1 = Y2 = 0.f; }

		float Process(float X)
		{
			const float Y = X + C * Y1 - R2 * Y2;
			Y2 = Y1;
			Y1 = Y;
			return Y * Norm;
		}

		void Flush()
		{
			if (FMath::Abs(Y1) < 1e-15f) { Y1 = 0.f; }
			if (FMath::Abs(Y2) < 1e-15f) { Y2 = 0.f; }
		}
	};

	/**
	 * Fuente glotal de la voz del jadeo: derivada de un pulso de Rosenberg (la glotis se abre en el 45 % del ciclo y se
	 * cierra de golpe en el 17 % siguiente) con jitter y shimmer por ciclo, como la de la tos.
	 */
	struct FGlottis
	{
		float Phase = 0.f;
		float CycleInc = 0.f;
		float CycleAmp = 1.f;
		float Prev = 0.f;

		void Reset(float Inc)
		{
			Phase = 0.f;
			CycleInc = Inc;
			CycleAmp = 1.f;
			Prev = 0.f;
		}

		float Next(float Inc, float Jitter, float Shimmer, FRandom& Rng)
		{
			Phase += CycleInc;
			if (Phase >= 1.f)
			{
				Phase -= FMath::FloorToFloat(Phase);
				CycleInc = Inc * (1.f + Jitter * Rng.Bipolar());
				CycleAmp = 1.f - Shimmer * Rng.Unit();
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

	// ─────────────────────────────────────────────────────────────────────────
	// Calibración
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Niveles de cada capa con su peso a 1, calibrados en un arnés fuera del motor con Master 1: un paso andando
	 * (fuerza 0,65) da picos de unos -17 dBFS; corriendo (0,95), unos -12; un aterrizaje fuerte, unos -8. El jadeo más
	 * fuerte, picos de unos -13 dBFS; el flojo del principio, unos -22.
	 */
	namespace Trim
	{
		constexpr float Thump = 0.115f;  ///< Golpe sordo de la pata (seno grave que cae).
		constexpr float Pat = 0.4f;       ///< «Pat» de la almohadilla (ruido apagado).
		constexpr float Sand = 0.27f;    ///< Arena: granulado agudo y «shhh».
		constexpr float SoilThud = 1.26f; ///< Tierra: golpe apagado.
		constexpr float Leaf = 0.2f;     ///< Tierra: crujidos de hojas.
		constexpr float Click = 0.1f;    ///< Roca: chasquido del contacto.
		constexpr float Tock = 1.9f;     ///< Roca: el «toc» corto de la piedra.
		constexpr float Grit = 0.18f;    ///< Roca: arenilla.
		constexpr float Wood = 0.006f;   ///< Madera: modos del tablón.
		constexpr float Splash = 0.4f;   ///< Agua: chapoteo.
		constexpr float Slosh = 0.55f;   ///< Agua: masa de agua grave.
		constexpr float Bubble = 0.095f;  ///< Agua: burbujas.
		constexpr float Slap = 0.3f;     ///< Panzazo: chasquido de la tripa blanda contra el suelo.
		constexpr float Shell = 0.009f;  ///< Choque arrastrándose: modos huecos del caparazón.
		constexpr float Swish = 0.3f;    ///< Caparazón: roce de la piel al meterse o salir (ruido de banda que barre).
		constexpr float Air = 5.f;       ///< Jadeo: aire por los formantes.
		constexpr float Voice = 0.7f;    ///< Jadeo: voz por los formantes.
		constexpr float Breath = 0.1f;   ///< Jadeo: nivel general.
	}

	/** Umbral del limitador de salida (0,8 ≈ -2 dBFS): solo actúa con el refuerzo de la tortuga local y caídas fuertes. */
	constexpr float LimThreshold = 0.8f;

	// ─────────────────────────────────────────────────────────────────────────
	// Pisadas
	// ─────────────────────────────────────────────────────────────────────────

	/** Una burbuja del chapoteo: seno que sube de tono mientras se apaga. */
	struct FBubble
	{
		float At = 0.f;
		float F0 = 900.f;
		float Rise = 30.f;
		float Amp = 0.f;
		float Phase = 0.f;
		float Env = 0.f;
		float K = 0.999f;
		bool bStarted = false;
	};

	/** Una pisada sonando. */
	struct FStepVoice
	{
		bool bActive = false;
		/** Segundos desde que empezó y duración total (con las colas). */
		float Time = 0.f;
		float Dur = 0.3f;
		float Gain = 1.f;
		/** Pesos de superficie normalizados. */
		float W[Surface::Num] = {};
		/** Dos contactos: almohadilla y dedos (en el aterrizaje, una pata y la otra). */
		float HitAt[2] = { 0.f, 0.03f };
		float HitAmp[2] = { 1.f, 0.6f };
		float HitAtk[2] = { 0.004f, 0.005f };
		float HitDec[2] = { 0.025f, 0.035f };
		bool bHitDone[2] = { false, false };
		/** 0..1: agudos y granos (más con la carrera y la fuerza). */
		float Bright = 0.5f;
		/** Golpe sordo: tono al empezar y al acabar el deslizamiento. */
		float PadF0 = 150.f;
		float PadF1 = 95.f;
		float PadPhase = 0.f;
		/** Panzazo: cuánto chasquido de tripa lleva (0 en las pisadas). */
		float Slap = 0.f;
		FOnePole SlapHP;
		/** Choque arrastrándose: cuánto suena el caparazón hueco (los modos del tablón, afinados al caparazón). */
		float Shell = 0.f;
		/** Golpe sordo y «pat» de la pata (0 en la brazada: ahí solo suena el agua). */
		float Pad = 1.f;
		/** Agua: ataque del chapoteo (la masa de agua tarda el triple), cola (0 = la de la pisada), caída del tono y su suelo. */
		float WaterAtk = 0.004f;
		float WaterTail = 0.f;
		float SplashSweep = 0.05f;
		float SplashLow = 1000.f;
		/** Roce del caparazón: nivel (0 = nada), arranque y duración (s) y centro del filtro al empezar y al acabar (Hz). */
		float Swish = 0.f;
		float SwishAt = 0.f;
		float SwishDur = 0.1f;
		float SwishF0 = 2000.f;
		float SwishF1 = 800.f;
		FBandPass SwishBP;
		/** Fundido de salida al final (s; 0 = corte, como las pisadas): las colas largas no acaban en seco. */
		float Release = 0.f;
		/** Envolventes al final del bloque anterior: golpe, «pat», arena, tierra, hojas, arenilla, chapoteo, masa de agua, chasquido de tripa y roce. */
		float Env[NumStepEnv] = {};
		/** Chasquido de cada contacto (decae muestra a muestra). */
		float HitEnv = 0.f;
		float HitK = 0.99f;
		/** Granos (arena, hojas, arenilla): envolvente de cada grano, su caída y su ritmo máximo por muestra. */
		float SandGrain = 0.f;
		float LeafGrain = 0.f;
		float GritGrain = 0.f;
		float SandGrainK = 0.99f;
		float LeafGrainK = 0.99f;
		float GritGrainK = 0.99f;
		float SandRate = 0.f;
		float LeafRate = 0.f;
		float GritRate = 0.f;
		FOnePole PatLP;
		FBandPass SandBP;
		FOnePole SandLP;
		FOnePole SoilLPA;
		FOnePole SoilLPB;
		FBandPass LeafBP;
		FOnePole ClickHPA;
		FOnePole ClickHPB;
		FBandPass TockBP;
		FBandPass GritBP;
		FOnePole WoodLP;
		FResonator Modes[3];
		float ModeGain[3] = { 1.f, 0.55f, 0.3f };
		FBandPass SplashBP;
		FOnePole SloshLP;
		float SplashHz = 2800.f;
		float Depth = 0.5f;
		FBubble Bubbles[MaxBubbles];
		FRandom Rng;
	};

	/** Envolvente de fuerza de apoyo de los dos contactos T segundos después de empezar, con las caídas por DecayScale. */
	inline float ContactEnvelope(const FStepVoice& V, float T, float DecayScale, float AttackScale)
	{
		float Sum = 0.f;
		for (int32 k = 0; k < 2; ++k)
		{
			const float X = T - V.HitAt[k];
			if (X <= 0.f) { continue; }
			const float Atk = V.HitAtk[k] * AttackScale;
			Sum += V.HitAmp[k] * SmoothStep01(X / Atk) * FMath::Exp(-FMath::Max(0.f, X - Atk) / (V.HitDec[k] * DecayScale));
		}
		return Sum;
	}

	/** Roce del caparazón T segundos después de empezar: una joroba (sen²) entre SwishAt y SwishAt + SwishDur. */
	inline float SwishEnvelope(const FStepVoice& V, float T)
	{
		const float X = (T - V.SwishAt) / FMath::Max(1e-3f, V.SwishDur);
		if (X <= 0.f || X >= 1.f) { return 0.f; }
		const float S = FastSin01(0.5f * X);
		return S * S;
	}

	/** Voz propia de una tortuga (sale de su semilla; la misma cuenta que la tos). */
	struct FTraits
	{
		/** Tono base de la voz (Hz): de ~140 a ~245 según la tortuga. */
		float F0 = 185.f;
		/** Escala de formantes (tracto algo más corto que el de un adulto: tortuga de dibujos). */
		float Tract = 1.12f;
		/** Tono del golpe de sus patas (Hz): las de voz grave pisan algo más grave. */
		float FootHz = 150.f;
	};

	// ─────────────────────────────────────────────────────────────────────────
	// Respiraciones
	// ─────────────────────────────────────────────────────────────────────────

	/** Una respiración: espiración «hah», inspiración «hhh» o suspiro al calmarse. */
	struct FBreathEvent
	{
		bool bExhale = true;
		float Dur = 0.3f;
		float Amp = 1.f;
		float FormStart[3] = { 800.f, 1250.f, 2650.f };
		float FormEnd[3] = { 720.f, 1200.f, 2650.f };
		float Q[3] = { 3.2f, 4.5f, 5.5f };
		float Gain[3] = { 1.f, 0.7f, 0.35f };
		/** Cuánta voz lleva (0 = solo aire). */
		float Voice = 0.f;
		float F0Start = 180.f;
		float F0End = 150.f;
	};

	struct FBreathVoice
	{
		FBreathEvent Ev;
		bool bActive = false;
		float Time = 0.f;
		/** Envolventes al final del bloque anterior: aire y voz. */
		float Env[2] = {};
		FGlottis Glottis;
		FBandPass Form[3];
		FOnePole Lips;
		FOnePole AirCut;
		FOnePole HissA;
	};

	/** Envolventes de una respiración T segundos después de empezar: [0] aire y [1] voz (0 al empezar y al acabar). */
	inline void BreathEnvelopes(const FBreathEvent& Ev, float T, float* OutEnv)
	{
		OutEnv[0] = 0.f;
		OutEnv[1] = 0.f;
		if (T <= 0.f || T >= Ev.Dur) { return; }
		const float Left = Ev.Dur - T;
		if (Ev.bExhale)
		{
			// «Hah»: entra en ~25 ms, se va apagando y se cierra suave.
			const float Shape = SmoothStep01(T / 0.025f) * (0.55f + 0.45f * FMath::Exp(-T / (0.3f * Ev.Dur)))
				* SmoothStep01(Left / FMath::Min(0.08f, 0.4f * Ev.Dur));
			OutEnv[0] = Shape;
			OutEnv[1] = Ev.Voice * SmoothStep01((T - 0.01f) / 0.03f) * Shape;
		}
		else
		{
			// «Hhh» hacia dentro: crece mientras entra el aire y se corta casi de golpe.
			const float U = T / Ev.Dur;
			OutEnv[0] = SmoothStep01(T / 0.05f) * (0.45f + 0.55f * U) * SmoothStep01(Left / 0.03f);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Arrastre sobre la tripa
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Niveles de cada capa del arrastre con su peso a 1 y la fuerza a 1 (deslizamiento rápido). Estimados sobre el ancho
	 * de banda de cada filtro (unos -28 dBFS eficaces con Master 1, por debajo de los pasos corriendo, que son golpes);
	 * sin pasar aún por el arnés: se afinan de oído con DragLoudness y TN.Voice.Drag.
	 */
	namespace DragTrim
	{
		constexpr float Body = 0.5f;      ///< Roce grave del cuerpo contra el suelo (todas las superficies).
		constexpr float Sand = 0.25f;     ///< Arena: «shhh» granulado.
		constexpr float SandHiss = 0.25f; ///< Arena: siseo de fondo.
		constexpr float Soil = 0.3f;      ///< Tierra: roce sordo.
		constexpr float Leaf = 0.22f;     ///< Tierra: crujidos de hojas sueltas.
		constexpr float Rock = 0.45f;     ///< Roca: raspado a trompicones (se pega y se suelta).
		constexpr float Grit = 0.2f;      ///< Roca: arenilla.
		constexpr float Wood = 0.25f;     ///< Madera: roce.
		constexpr float WoodHum = 0.35f;  ///< Madera: zumbido del tablón.
		constexpr float Water = 0.25f;    ///< Agua: siseo de la estela.
		constexpr float WaterLow = 0.3f;  ///< Agua: masa de agua que empuja la tripa.
		constexpr float Bubble = 0.06f;   ///< Agua: burbujas sueltas.
	}

	/**
	 * Arrastre continuo: ruido por la textura de cada superficie, mezclada por sus pesos, con la fuerza y la viveza que
	 * manda el juego. Más rápido: más fuerte, más agudo y con los granos más seguidos. Una fluctuación lenta (los baches)
	 * hace que no suene a ruido plano.
	 */
	struct FDragVoice
	{
		/** Fuerza, viveza y pesos ya suavizados. */
		float Level = 0.f;
		float Pace = 0.f;
		float W[Surface::Num] = { 0.f, 0.f, 1.f, 0.f, 0.f };
		/** Baches: nivel que va y viene (-1..1) hacia un objetivo que cambia cada pocas centésimas. */
		float Wobble = 0.f;
		float WobbleTarget = 0.f;
		float WobbleTimer = 0.f;
		FOnePole BodyA;
		FOnePole BodyB;
		FBandPass SandBP;
		FOnePole SandLP;
		float SandGrain = 0.f;
		float SandGrainK = 0.99f;
		FBandPass SoilBP;
		FBandPass LeafBP;
		float LeafGrain = 0.f;
		float LeafGrainK = 0.99f;
		FBandPass RockBP;
		FBandPass GritBP;
		float Stick = 0.f;
		float StickK = 0.99f;
		float GritGrain = 0.f;
		float GritGrainK = 0.99f;
		FBandPass WoodBP;
		FResonator WoodModes[2];
		FBandPass WaterBP;
		FOnePole WaterLP;
		/** Una burbuja de la estela: seno que sube de tono mientras se apaga. */
		float BubblePhase = 0.f;
		float BubbleEnv = 0.f;
		float BubbleK = 0.999f;
		float BubbleF0 = 700.f;
		float BubbleRise = 20.f;
		float BubbleAge = 0.f;
		FRandom Rng;
	};

	// ─────────────────────────────────────────────────────────────────────────
	// Motor
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Motor de una tortuga: dispara las pisadas que llegan por el anillo, decide las respiraciones con el jadeo que manda
	 * el juego, arrastra la tripa con la fuerza que le llega y deja una salida mono limitada.
	 */
	class FEngine
	{
	public:
		void Init(float InRate, uint32 InVoiceSeed, uint32 InRunSeed, uint32 InRunId, uint32 InCursor)
		{
			Rate = FMath::Max(8000.f, InRate);
			InvRate = 1.f / Rate;
			RunId = InRunId;
			Cursor = InCursor;
			Rng.SetSeed(MixSeed(InRunSeed, 11u));
			NoiseRng.SetSeed(MixSeed(InRunSeed, 12u));
			BuildTraits(InVoiceSeed);
			for (FStepVoice& Slot : Steps) { Slot.bActive = false; }
			Breath[0].bActive = false;
			Breath[1].bActive = false;
			Clock = 0.f;
			PantLevel = 0.f;
			BoutPeak = 0.f;
			NextBreathAt = 0.f;
			bBreathing = false;
			bNextExhale = true;
			bFirstBlock = true;
			Master = 0.f;
			PrevGain = 0.f;
			LimEnv = 0.f;
			LimAttack = 1.f - FMath::Exp(-1.f / (0.001f * Rate));
			LimRelease = 1.f - FMath::Exp(-1.f / (0.15f * Rate));
			DcCut.SetHz(25.f, InvRate);
			DcCut.Reset();
			InitDrag(InRunSeed);
		}

		/** Rellena NumFrames tramas intercaladas de OutChannels canales (mono; si llegan más, el mismo valor en todos). */
		void Render(float* Out, int32 NumFrames, int32 OutChannels, FSharedParams& P)
		{
			const bool bCurrent = P.RunId.load(std::memory_order_acquire) == RunId;
			if (bCurrent) { PullSteps(P); }
			const int32 Channels = FMath::Max(1, OutChannels);
			int32 Done = 0;
			while (Done < NumFrames)
			{
				const int32 N = FMath::Min(BlockFrames, NumFrames - Done);
				RenderBlock(N, P, bCurrent);
				float* Dst = Out + Done * Channels;
				for (int32 i = 0; i < N; ++i)
				{
					for (int32 c = 0; c < Channels; ++c) { Dst[i * Channels + c] = Mix[i]; }
				}
				Done += N;
			}
			// Un generador viejo que aún suene no pisa el estado del vigente.
			if (bCurrent) { P.Busy.store(IsBusy() ? 1 : 0, std::memory_order_relaxed); }
		}

		/** Arnés fuera del motor: dispara una pisada ya mismo (sin pasar por el anillo). */
		void TriggerStepNow(const FStepEvent& InEvent) { StartStep(InEvent); }

		/** Depuración y arnés: nivel de jadeo suavizado, respiraciones dichas y la voz. */
		float GetPantLevel() const { return PantLevel; }
		int32 GetBreathCount() const { return BreathCount; }
		const FTraits& GetTraits() const { return Traits; }

	private:
		void BuildTraits(uint32 InVoiceSeed)
		{
			// La misma cuenta que BuildTraits de la tos (TN_StormCough.cpp): mismo tono y mismo tracto.
			FRandom VoiceRng;
			VoiceRng.SetSeed(MixSeed(InVoiceSeed, 0xC0u));
			const float Pitch = FMath::Pow(2.f, VoiceRng.Range(-0.42f, 0.42f));
			Traits.F0 = 185.f * Pitch;
			Traits.Tract = 1.12f * FMath::Pow(Pitch, 0.45f) * VoiceRng.Range(0.97f, 1.03f);
			// Las patas: algo más graves en las tortugas de voz grave, con su propia variación.
			FRandom FootRng;
			FootRng.SetSeed(MixSeed(InVoiceSeed, 0xF0u));
			Traits.FootHz = 150.f * FMath::Pow(Pitch, 0.3f) * FMath::Pow(2.f, FootRng.Range(-0.15f, 0.15f));
		}

		bool IsBusy() const
		{
			if (bBreathing || Breath[0].bActive || Breath[1].bActive || Drag.Level > 1e-4f) { return true; }
			for (const FStepVoice& Slot : Steps)
			{
				if (Slot.bActive) { return true; }
			}
			return false;
		}

		/** Lee los pasos nuevos del anillo (si se ha quedado muy atrás, se salta los viejos). */
		void PullSteps(FSharedParams& P)
		{
			const uint32 W = P.WriteSeq.load(std::memory_order_acquire);
			if (W - Cursor > RingSize / 2u) { Cursor = W - RingSize / 2u; }
			while (Cursor != W)
			{
				StartStep(P.Events[Cursor % RingSize]);
				++Cursor;
			}
		}

		// ── Pisadas ──────────────────────────────────────────────────────────

		void StartStep(const FStepEvent& InEvent)
		{
			// Una voz libre o, si no queda, la más vieja.
			FStepVoice* Chosen = &Steps[0];
			float Oldest = -1.f;
			for (FStepVoice& Slot : Steps)
			{
				if (!Slot.bActive)
				{
					Chosen = &Slot;
					break;
				}
				if (Slot.Time > Oldest)
				{
					Oldest = Slot.Time;
					Chosen = &Slot;
				}
			}
			FStepVoice& V = *Chosen;
			V.bActive = true;
			V.Time = 0.f;
			V.Rng.SetSeed(Rng.NextU());
			FRandom& R = V.Rng;

			// Pesos normalizados (sin datos: roca).
			float Sum = 0.f;
			for (int32 s = 0; s < Surface::Num; ++s)
			{
				V.W[s] = FMath::Max(0.f, InEvent.Surf[s]);
				Sum += V.W[s];
			}
			if (Sum <= 1e-4f)
			{
				for (int32 s = 0; s < Surface::Num; ++s) { V.W[s] = 0.f; }
				V.W[Surface::Rock] = 1.f;
				Sum = 1.f;
			}
			for (int32 s = 0; s < Surface::Num; ++s) { V.W[s] = V.W[s] < 0.03f * Sum ? 0.f : V.W[s] / Sum; }

			// Panzazo: toda la tripa a la vez, más grave y ancho, con chasquido de carne blanda. Choque arrastrándose: el
			// caparazón hueco contra el obstáculo, sin la textura del suelo.
			const bool bBelly = InEvent.Kind == StepKind::Belly;
			const bool bBump = InEvent.Kind == StepKind::Bump;
			// Guardar o sacar algo del caparazón: el mismo hueco que el choque, más corto, agudo y flojo, sin suelo.
			const bool bStash = InEvent.Kind == StepKind::Stash;
			V.Slap = bBelly ? 1.f : 0.f;
			V.Shell = (bBump || bStash) ? 1.f : 0.f;
			if (bBump || bStash)
			{
				for (int32 s = 0; s < Surface::Num; ++s) { V.W[s] = 0.f; }
			}

			const float Force = FMath::Clamp(InEvent.Force, 0.05f, 1.5f);
			const float Pace = FMath::Clamp(InEvent.Pace, 0.f, 1.f);
			const float Heavy = FMath::Clamp(InEvent.Heavy, 1.f, 2.f);
			V.Gain = FMath::Pow(Force, 1.3f) * R.Range(0.84f, 1.08f) * FMath::Sqrt(Heavy);
			V.Bright = FMath::Clamp((0.3f + 0.45f * Pace + 0.3f * (Force - 0.6f)) * R.Range(0.85f, 1.15f), 0.f, 1.f);

			// Contactos: almohadilla y dedos (más juntos corriendo); al aterrizar, una pata y la otra; al saltar, un roce.
			const bool bLand = InEvent.Kind == StepKind::Land || bBelly;
			const bool bScuff = InEvent.Kind == StepKind::Scuff;
			const float LandDecay = bBelly ? 1.8f : (bLand ? 1.3f : 1.f);
			V.HitAt[0] = 0.f;
			V.HitAmp[0] = 1.f;
			V.HitAtk[0] = bScuff ? 0.012f : FMath::Lerp(0.004f, 0.0025f, V.Bright);
			V.HitDec[0] = (bScuff ? 0.05f : FMath::Lerp(0.028f, 0.018f, Pace)) * LandDecay * (bStash ? 0.7f : 1.f);
			if (bScuff || bBump || bStash)
			{
				V.HitAt[1] = 0.f;
				V.HitAmp[1] = 0.f;
			}
			else if (bBelly)
			{
				V.HitAt[1] = R.Range(0.004f, 0.012f);
				V.HitAmp[1] = R.Range(0.8f, 1.f);
			}
			else if (bLand)
			{
				V.HitAt[1] = R.Range(0.012f, 0.03f);
				V.HitAmp[1] = R.Range(0.7f, 0.95f);
			}
			else
			{
				V.HitAt[1] = FMath::Lerp(0.045f, 0.018f, Pace) * R.Range(0.8f, 1.25f);
				V.HitAmp[1] = R.Range(0.4f, 0.7f);
			}
			V.HitAtk[1] = 0.005f;
			V.HitDec[1] = FMath::Lerp(0.04f, 0.025f, Pace) * LandDecay;
			V.bHitDone[0] = false;
			V.bHitDone[1] = V.HitAmp[1] <= 0.f;
			V.HitEnv = 0.f;
			V.HitK = DecayPerSample(FMath::Lerp(0.0018f, 0.0011f, V.Bright), InvRate);
			for (float& E : V.Env) { E = 0.f; }

			// Golpe sordo de la pata: cada tortuga el suyo, la derecha un pelín más aguda y más grave si carga peso. La
			// tripa entera, bastante más grave.
			const float FootTilt = InEvent.Foot == 0 ? 0.97f : 1.03f;
			const float PadScale = bBelly ? 0.62f : (bBump ? 0.75f : (bStash ? 1.4f : (bLand ? 0.85f : 1.f)));
			V.PadF0 = Traits.FootHz * FootTilt * R.Range(0.92f, 1.08f) / FMath::Sqrt(Heavy) * PadScale;
			V.PadF1 = V.PadF0 * R.Range(0.58f, 0.68f);
			V.PadPhase = 0.f;
			V.PatLP.SetHz(FMath::Lerp(480.f, 900.f, V.Bright), InvRate);
			V.PatLP.Reset();
			V.SlapHP.SetHz(R.Range(800.f, 1200.f), InvRate);
			V.SlapHP.Reset();

			// Arena: granos densos y agudos que se hunden, más con la carrera.
			V.SandBP.Set(R.Range(2400.f, 3400.f) * FMath::Lerp(0.85f, 1.2f, V.Bright), 0.9f, InvRate);
			V.SandBP.Reset();
			V.SandLP.SetHz(1500.f, InvRate);
			V.SandLP.Reset();
			V.SandGrain = 0.f;
			V.SandGrainK = DecayPerSample(0.0009f, InvRate);
			V.SandRate = FMath::Lerp(900.f, 2200.f, V.Bright) * InvRate;

			// Tierra y hojas: golpe muy apagado y crujidos sueltos.
			V.SoilLPA.SetHz(R.Range(260.f, 340.f), InvRate);
			V.SoilLPB.SetHz(R.Range(260.f, 340.f), InvRate);
			V.SoilLPA.Reset();
			V.SoilLPB.Reset();
			V.LeafBP.Set(R.Range(3000.f, 4600.f), 1.4f, InvRate);
			V.LeafBP.Reset();
			V.LeafGrain = 0.f;
			V.LeafGrainK = DecayPerSample(0.0022f, InvRate);
			V.LeafRate = FMath::Lerp(70.f, 150.f, V.Bright) * InvRate;

			// Roca: chasquido suave (pata blanda), un «toc» corto y algo de arenilla.
			V.ClickHPA.SetHz(1800.f, InvRate);
			V.ClickHPB.SetHz(1800.f, InvRate);
			V.ClickHPA.Reset();
			V.ClickHPB.Reset();
			V.TockBP.Set(R.Range(1100.f, 1700.f) * FMath::Lerp(0.9f, 1.15f, V.Bright), 14.f, InvRate);
			V.TockBP.Reset();
			V.GritBP.Set(R.Range(4200.f, 6200.f), 1.2f, InvRate);
			V.GritBP.Reset();
			V.GritGrain = 0.f;
			V.GritGrainK = DecayPerSample(0.0006f, InvRate);
			V.GritRate = FMath::Lerp(120.f, 320.f, V.Bright) * InvRate;

			// Madera: tablón libre (modos 1 : 2,76 : 5,40) golpeado con algo blando. En un choque arrastrándose, los mismos
			// modos afinados al caparazón (más agudo y corto que un tablón).
			// Guardar en el caparazón suena más grave (entra) y sacar, más agudo (sale).
			const float Plank = bStash ? (InEvent.Foot == 0 ? R.Range(330.f, 380.f) : R.Range(440.f, 520.f))
				: (bBump ? R.Range(270.f, 340.f) : R.Range(170.f, 250.f) / FMath::Sqrt(Heavy));
			V.WoodLP.SetHz(FMath::Lerp(1200.f, 2200.f, V.Bright), InvRate);
			V.WoodLP.Reset();
			V.Modes[0].Set(Plank, R.Range(0.06f, 0.09f), InvRate);
			V.Modes[1].Set(Plank * R.Range(2.65f, 2.85f), R.Range(0.035f, 0.05f), InvRate);
			V.Modes[2].Set(Plank * R.Range(5.2f, 5.6f), R.Range(0.018f, 0.026f), InvRate);
			for (FResonator& Mode : V.Modes) { Mode.Reset(); }

			// Agua: chapoteo que baja de tono, masa de agua y burbujas (más con la profundidad y la fuerza).
			V.Depth = FMath::Clamp(0.4f + 0.4f * Force + 0.2f * R.Unit(), 0.f, 1.f);
			V.SplashHz = R.Range(2600.f, 3600.f);
			V.SplashBP.Set(V.SplashHz, 1.3f, InvRate);
			V.SplashBP.Reset();
			V.SloshLP.SetHz(R.Range(380.f, 520.f), InvRate);
			V.SloshLP.Reset();
			for (int32 b = 0; b < MaxBubbles; ++b)
			{
				FBubble& Bub = V.Bubbles[b];
				Bub.bStarted = false;
				Bub.Phase = 0.f;
				Bub.Env = 0.f;
				const bool bUse = V.W[Surface::Water] > 0.f && (b == 0 || R.Chance(0.35f + 0.4f * V.Depth));
				Bub.Amp = bUse ? R.Range(0.5f, 1.f) : 0.f;
				Bub.At = R.Range(0.012f, 0.09f) + 0.02f * static_cast<float>(b);
				// La primera, más grande y grave («gloc»); las demás, pequeñas y agudas.
				Bub.F0 = b == 0 ? R.Range(380.f, 620.f) : R.Range(800.f, 1500.f);
				const float Life = b == 0 ? R.Range(0.03f, 0.05f) : R.Range(0.012f, 0.025f);
				Bub.K = DecayPerSample(Life, InvRate);
				Bub.Rise = R.Range(0.5f, 1.2f) / Life;
			}

			V.Dur = FMath::Max(V.HitAt[0], V.HitAt[1]) + (bBelly ? 0.42f : (bLand ? 0.32f : 0.24f)) + (V.W[Surface::Water] > 0.f ? 0.25f : 0.f)
				+ ((V.W[Surface::Wood] > 0.f || bBump || bStash) ? 0.12f : 0.f);

			// Lo que no es una pisada, por encima de lo de serie.
			V.Pad = 1.f;
			V.WaterAtk = 0.004f;
			V.WaterTail = 0.f;
			V.SplashSweep = 0.05f;
			V.SplashLow = 1000.f;
			V.Swish = 0.f;
			V.Release = 0.f;
			if (InEvent.Kind == StepKind::Stroke)
			{
				ShapeStroke(V, Force);
			}
			else if (InEvent.Kind == StepKind::Shell)
			{
				ShapeShell(V, InEvent.Foot == 0);
			}
		}

		/** Brazada: solo el agua que empujan las dos aletas (sin pata ni suelo), que entra despacio y se va con burbujas. */
		void ShapeStroke(FStepVoice& V, float Force)
		{
			FRandom& R = V.Rng;
			for (int32 s = 0; s < Surface::Num; ++s) { V.W[s] = 0.f; }
			V.W[Surface::Water] = 1.f;
			V.Pad = 0.f;
			V.Slap = 0.f;
			V.Shell = 0.f;
			V.HitAt[1] = 0.f;
			V.HitAmp[1] = 0.f;
			V.bHitDone[1] = true;

			// Más agua y más grave cuanto más fuerte bracea.
			V.Depth = FMath::Clamp(0.45f + 0.35f * Force + 0.2f * R.Unit(), 0.f, 1.f);
			V.WaterAtk = R.Range(0.045f, 0.07f);
			V.WaterTail = FMath::Lerp(0.11f, 0.17f, V.Depth);
			V.SplashHz = R.Range(1500.f, 2200.f) * FMath::Lerp(0.9f, 1.15f, V.Bright);
			V.SplashSweep = 0.16f;
			V.SplashLow = R.Range(550.f, 700.f);
			V.SplashBP.Set(V.SplashHz, 1.3f, InvRate);
			V.SplashBP.Reset();
			V.SloshLP.SetHz(R.Range(260.f, 380.f), InvRate);
			V.SloshLP.Reset();
			for (int32 b = 0; b < MaxBubbles; ++b)
			{
				FBubble& Bub = V.Bubbles[b];
				Bub.bStarted = false;
				Bub.Phase = 0.f;
				Bub.Env = 0.f;
				Bub.Amp = (b == 0 || R.Chance(0.5f + 0.3f * V.Depth)) ? R.Range(0.4f, 0.9f) : 0.f;
				// Detrás de la aleta, cuando el agua ya se ha movido.
				Bub.At = V.WaterAtk + R.Range(0.02f, 0.2f) + 0.03f * static_cast<float>(b);
				Bub.F0 = b == 0 ? R.Range(320.f, 520.f) : R.Range(650.f, 1200.f);
				const float Life = b == 0 ? R.Range(0.035f, 0.06f) : R.Range(0.015f, 0.03f);
				Bub.K = DecayPerSample(Life, InvRate);
				Bub.Rise = R.Range(0.5f, 1.2f) / Life;
			}
			V.Release = 0.06f;
			// La masa de agua cae con 1,6 veces la cola: a 4,5 constantes queda a unos -39 dB y el fundido la cierra.
			V.Dur = V.WaterAtk + 4.5f * 1.6f * V.WaterTail;
		}

		/**
		 * Caparazón: al meterse, la piel roza hacia dentro (barrido de agudo a grave) y acaba en un «tonc» grave del cuerpo
		 * contra la concha; al salir, un «pop» que sube de tono y el roce hacia fuera (de grave a agudo), más agudo y flojo.
		 */
		void ShapeShell(FStepVoice& V, bool bEntering)
		{
			FRandom& R = V.Rng;
			for (int32 s = 0; s < Surface::Num; ++s) { V.W[s] = 0.f; }
			V.Slap = 0.f;
			V.Shell = 1.f;
			V.HitAt[1] = 0.f;
			V.HitAmp[1] = 0.f;
			V.bHitDone[1] = true;
			V.Swish = bEntering ? 1.f : 0.8f;
			float Plank = 0.f;
			if (bEntering)
			{
				V.SwishAt = 0.f;
				V.SwishDur = R.Range(0.08f, 0.1f);
				V.SwishF0 = R.Range(2200.f, 2800.f);
				V.SwishF1 = R.Range(600.f, 800.f);
				V.HitAt[0] = 0.85f * V.SwishDur;
				V.HitDec[0] *= 1.2f;
				V.PadF0 = Traits.FootHz * R.Range(0.62f, 0.72f);
				V.PadF1 = V.PadF0 * R.Range(0.58f, 0.66f);
				Plank = R.Range(230.f, 280.f);
			}
			else
			{
				V.HitAt[0] = 0.f;
				V.SwishAt = R.Range(0.015f, 0.025f);
				V.SwishDur = R.Range(0.06f, 0.08f);
				V.SwishF0 = R.Range(700.f, 900.f);
				V.SwishF1 = R.Range(2200.f, 2800.f);
				V.PadF0 = Traits.FootHz * R.Range(0.9f, 1.f);
				V.PadF1 = V.PadF0 * R.Range(1.5f, 1.7f);
				Plank = R.Range(340.f, 400.f);
			}
			V.SwishBP.Set(V.SwishF0, 1.6f, InvRate);
			V.SwishBP.Reset();
			V.Modes[0].Set(Plank, R.Range(0.06f, 0.09f), InvRate);
			V.Modes[1].Set(Plank * R.Range(2.65f, 2.85f), R.Range(0.035f, 0.05f), InvRate);
			V.Modes[2].Set(Plank * R.Range(5.2f, 5.6f), R.Range(0.018f, 0.026f), InvRate);
			for (FResonator& Mode : V.Modes) { Mode.Reset(); }
			V.Release = 0.03f;
			V.Dur = FMath::Max(V.HitAt[0], V.SwishAt + V.SwishDur) + 0.3f;
		}

		void RenderStep(FStepVoice& V, int32 N)
		{
			const float BlockDt = static_cast<float>(N) * InvRate;
			const float TEnd = V.Time + BlockDt;

			// Envolventes al final del bloque (se interpolan dentro): cada capa con sus tiempos.
			float EnvEnd[NumStepEnv];
			EnvEnd[0] = ContactEnvelope(V, TEnd, 1.3f, 1.f);                   // Golpe sordo.
			EnvEnd[1] = ContactEnvelope(V, TEnd, 0.55f, 1.f);                  // «Pat».
			EnvEnd[2] = ContactEnvelope(V, TEnd, FMath::Lerp(3.2f, 2.2f, V.Bright), 2.2f); // Arena (se hunde).
			EnvEnd[3] = ContactEnvelope(V, TEnd, 1.4f, 1.f);                   // Tierra.
			EnvEnd[4] = ContactEnvelope(V, TEnd, 2.4f, 1.5f);                  // Hojas.
			EnvEnd[5] = ContactEnvelope(V, TEnd, 1.2f, 1.f);                   // Arenilla.
			{
				const float S = SmoothStep01(TEnd / V.WaterAtk);
				const float Tail = V.WaterTail > 0.f ? V.WaterTail : FMath::Lerp(0.07f, 0.12f, V.Depth);
				EnvEnd[6] = S * FMath::Exp(-TEnd / Tail);                           // Chapoteo.
				EnvEnd[7] = SmoothStep01(TEnd / (3.f * V.WaterAtk)) * FMath::Exp(-TEnd / (1.6f * Tail)); // Masa de agua.
			}
			EnvEnd[8] = V.Slap > 0.f ? ContactEnvelope(V, TEnd, 0.8f, 0.4f) : 0.f;    // Chasquido de tripa.
			EnvEnd[9] = V.Swish > 0.f ? V.Swish * SwishEnvelope(V, TEnd) : 0.f;    // Roce del caparazón.

			// Chasquido en cada contacto (con precisión de bloque: 0,7 ms).
			for (int32 k = 0; k < 2; ++k)
			{
				if (!V.bHitDone[k] && TEnd >= V.HitAt[k])
				{
					V.bHitDone[k] = true;
					V.HitEnv += V.HitAmp[k];
				}
			}

			// Tono del golpe sordo (cae en ~20 ms desde el primer contacto) y del chapoteo (baja de agudo a medio).
			const float TMid = V.Time + 0.5f * BlockDt;
			const float PadHz = V.PadF1 + (V.PadF0 - V.PadF1) * FMath::Exp(-FMath::Max(0.f, TMid - V.HitAt[0]) / 0.02f);
			const float PadInc = FMath::Min(PadHz * InvRate, 0.45f);
			const float WSand = V.W[Surface::Sand];
			const float WSoil = V.W[Surface::Soil];
			const float WRock = V.W[Surface::Rock];
			const float WWood = V.W[Surface::Wood];
			const float WWater = V.W[Surface::Water];
			if (WWater > 0.f)
			{
				V.SplashBP.Set(V.SplashLow + (V.SplashHz - V.SplashLow) * FMath::Exp(-TMid / V.SplashSweep), 1.3f, InvRate);
			}
			if (V.Swish > 0.f)
			{
				// El roce barre de SwishF0 a SwishF1 mientras dura.
				const float X = FMath::Clamp((TMid - V.SwishAt) / V.SwishDur, 0.f, 1.f);
				V.SwishBP.Set(V.SwishF0 * FMath::Pow(V.SwishF1 / V.SwishF0, X), 1.6f, InvRate);
			}
			// Fundido de salida (solo las voces con Release: brazadas y caparazón).
			const float FadeStart = V.Release > 0.f ? SmoothStep01((V.Dur - V.Time) / V.Release) : 1.f;
			const float FadeEnd = V.Release > 0.f ? SmoothStep01((V.Dur - TEnd) / V.Release) : 1.f;

			const float InvN = 1.f / static_cast<float>(N);
			float E[NumStepEnv];
			float DE[NumStepEnv];
			for (int32 k = 0; k < NumStepEnv; ++k)
			{
				E[k] = V.Env[k];
				DE[k] = (EnvEnd[k] - V.Env[k]) * InvN;
			}
			FRandom& R = V.Rng;
			const float Amp = V.Gain;
			const float WoodMix = WWood + V.Shell;
			const float WoodTrim = V.Shell > 0.f ? Trim::Shell : Trim::Wood;
			float Fade = FadeStart;
			const float DFade = (FadeEnd - FadeStart) * InvN;
			for (int32 i = 0; i < N; ++i)
			{
				for (int32 k = 0; k < NumStepEnv; ++k) { E[k] += DE[k]; }
				Fade += DFade;
				const float Noise = R.Bipolar();
				V.HitEnv *= V.HitK;

				// La pata: golpe grave y «pat» apagado (en todas las superficies; no en la brazada).
				V.PadPhase += PadInc;
				V.PadPhase -= FMath::FloorToFloat(V.PadPhase);
				float Y = V.Pad * (Trim::Thump * E[0] * FastSin01(V.PadPhase) + Trim::Pat * E[1] * V.PatLP.Low(Noise));
				if (V.Swish > 0.f)
				{
					Y += Trim::Swish * E[9] * V.SwishBP.Process(Noise);
				}
				if (V.Slap > 0.f)
				{
					// La tripa blanda contra el suelo: un chasquido de ruido brillante que dura lo que el golpe.
					Y += V.Slap * Trim::Slap * E[8] * V.SlapHP.High(Noise);
				}

				if (WSand > 0.f)
				{
					// Granos densos que se apagan en ~1 ms sobre un «shhh» de fondo.
					if (R.Unit() < V.SandRate * E[2]) { V.SandGrain = FMath::Max(V.SandGrain, R.Range(0.4f, 1.f)); }
					V.SandGrain *= V.SandGrainK;
					const float Grainy = V.SandBP.Process(Noise * (0.3f + 0.7f * V.SandGrain));
					Y += WSand * Trim::Sand * E[2] * (Grainy + 0.3f * V.SandLP.Low(Noise));
				}
				if (WSoil > 0.f)
				{
					if (R.Unit() < V.LeafRate * E[4]) { V.LeafGrain = FMath::Max(V.LeafGrain, R.Range(0.5f, 1.f)); }
					V.LeafGrain *= V.LeafGrainK;
					const float Thud = V.SoilLPB.Low(V.SoilLPA.Low(Noise));
					Y += WSoil * (Trim::SoilThud * E[3] * Thud + Trim::Leaf * V.LeafBP.Process(Noise * V.LeafGrain));
				}
				if (WRock > 0.f)
				{
					const float Click = V.ClickHPB.High(V.ClickHPA.High(Noise)) * V.HitEnv;
					if (R.Unit() < V.GritRate * E[5]) { V.GritGrain = FMath::Max(V.GritGrain, R.Range(0.3f, 1.f)); }
					V.GritGrain *= V.GritGrainK;
					Y += WRock * (Trim::Click * Click + Trim::Tock * V.TockBP.Process(Noise * V.HitEnv)
						+ Trim::Grit * V.GritBP.Process(Noise * V.GritGrain));
				}
				if (WoodMix > 0.f)
				{
					// El golpe blando excita los modos del tablón (o del caparazón, en un choque).
					const float Exc = V.WoodLP.Low(V.HitEnv * (0.75f + 0.25f * Noise));
					const float Modes = V.ModeGain[0] * V.Modes[0].Process(Exc) + V.ModeGain[1] * V.Modes[1].Process(Exc)
						+ V.ModeGain[2] * V.Modes[2].Process(Exc);
					Y += WoodMix * WoodTrim * Modes;
				}
				if (WWater > 0.f)
				{
					float Water = Trim::Splash * E[6] * V.SplashBP.Process(Noise) + Trim::Slosh * E[7] * V.SloshLP.Low(Noise);
					for (FBubble& Bub : V.Bubbles)
					{
						if (Bub.Amp <= 0.f) { continue; }
						if (!Bub.bStarted)
						{
							if (V.Time + static_cast<float>(i) * InvRate < Bub.At) { continue; }
							Bub.bStarted = true;
							Bub.Env = 1.f;
						}
						if (Bub.Env < 1e-4f) { continue; }
						const float Age = V.Time + static_cast<float>(i) * InvRate - Bub.At;
						Bub.Phase += FMath::Min(Bub.F0 * (1.f + Bub.Rise * Age) * InvRate, 0.45f);
						Bub.Phase -= FMath::FloorToFloat(Bub.Phase);
						Water += Trim::Bubble * Bub.Amp * Bub.Env * FastSin01(Bub.Phase);
						Bub.Env *= Bub.K;
					}
					Y += WWater * V.Depth * Water;
				}
				Mix[i] += Y * Amp * Fade;
			}
			for (int32 k = 0; k < NumStepEnv; ++k) { V.Env[k] = EnvEnd[k]; }
			V.Time = TEnd;
			V.SlapHP.Flush();
			V.SwishBP.Flush();
			V.SandBP.Flush();
			V.LeafBP.Flush();
			V.TockBP.Flush();
			V.GritBP.Flush();
			V.SplashBP.Flush();
			V.PatLP.Flush();
			V.SandLP.Flush();
			V.SoilLPA.Flush();
			V.SoilLPB.Flush();
			V.ClickHPA.Flush();
			V.ClickHPB.Flush();
			V.WoodLP.Flush();
			V.SloshLP.Flush();
			for (FResonator& Mode : V.Modes) { Mode.Flush(); }
			if (V.Time >= V.Dur) { V.bActive = false; }
		}

		// ── Respiraciones ────────────────────────────────────────────────────

		/** Pausa entre respiraciones y fuerza: jadeo suave con Pant bajo; «hah-hah» rápido y con voz al agotarse. */
		void ScheduleBreath(float Dt, float Target, bool bHush)
		{
			// Sube deprisa y se calma despacio (salvo si hay que callar: la tos manda o ha caído).
			const float Tau = bHush ? 0.15f : (Target > PantLevel ? 0.35f : 1.2f);
			PantLevel += (Target - PantLevel) * TimeCoef(Tau, Dt);
			if (!bBreathing)
			{
				if (bHush || PantLevel < 0.05f) { return; }
				bBreathing = true;
				bNextExhale = true;
				BoutPeak = 0.f;
				NextBreathAt = Clock + Rng.Range(0.05f, 0.2f);
			}
			BoutPeak = FMath::Max(BoutPeak, PantLevel);
			if (Clock < NextBreathAt) { return; }
			if (bHush)
			{
				bBreathing = false;
				return;
			}
			// Se calma: tras una inspiración, si ya casi no hace falta, acaba (con un suspiro si ha jadeado fuerte).
			if (bNextExhale && PantLevel < 0.05f && Target < 0.05f)
			{
				bBreathing = false;
				if (BoutPeak > 0.45f) { TriggerBreath(BuildSigh()); }
				return;
			}
			const float Strain = FMath::Clamp(PantLevel, 0.05f, 1.f);
			const float Period = FMath::Lerp(1.1f, 0.4f, FMath::Pow(Strain, 0.75f)) * Rng.Range(0.9f, 1.12f);
			if (bNextExhale)
			{
				const FBreathEvent Ev = BuildExhale(Strain, Period);
				TriggerBreath(Ev);
				NextBreathAt = Clock + Ev.Dur + Period * Rng.Range(0.04f, 0.08f);
			}
			else
			{
				const FBreathEvent Ev = BuildInhale(Strain, Period);
				TriggerBreath(Ev);
				NextBreathAt = Clock + Ev.Dur + Period * Rng.Range(0.08f, 0.14f);
			}
			bNextExhale = !bNextExhale;
		}

		/** «Hah» por la boca abierta (la lengua fuera): más fuerte y con más voz cuanto más cansada. */
		FBreathEvent BuildExhale(float Strain, float Period)
		{
			FBreathEvent Ev;
			Ev.bExhale = true;
			Ev.Dur = Period * Rng.Range(0.4f, 0.46f);
			Ev.Amp = FMath::Lerp(0.3f, 1.f, Strain) * Rng.Range(0.85f, 1.08f);
			static constexpr float Open[3] = { 780.f, 1230.f, 2600.f };
			const float Shift = Traits.Tract * Rng.Range(0.96f, 1.04f);
			for (int32 f = 0; f < 3; ++f) { Ev.FormStart[f] = Open[f] * Shift; }
			Ev.FormEnd[0] = Ev.FormStart[0] * Rng.Range(0.85f, 0.92f);
			Ev.FormEnd[1] = Ev.FormStart[1] * Rng.Range(0.93f, 0.98f);
			Ev.FormEnd[2] = Ev.FormStart[2];
			Ev.Q[0] = 3.2f;
			Ev.Q[1] = 4.5f;
			Ev.Q[2] = 5.5f;
			Ev.Gain[0] = 1.f;
			Ev.Gain[1] = 0.7f;
			Ev.Gain[2] = 0.35f;
			// La voz («huh») solo aparece ya cansada; de vez en cuando, un «heh» más marcado.
			float Voice = SmoothStep01((Strain - 0.35f) / 0.5f) * Rng.Range(0.25f, 0.55f);
			if (Strain > 0.6f && Rng.Chance(0.2f)) { Voice *= 1.6f; }
			Ev.Voice = FMath::Clamp(Voice, 0.f, 1.f);
			Ev.F0Start = Traits.F0 * Rng.Range(0.9f, 1.05f);
			Ev.F0End = Ev.F0Start * Rng.Range(0.78f, 0.86f);
			return Ev;
		}

		/** «Hhh» hacia dentro: más agudo, sin voz y algo más flojo. */
		FBreathEvent BuildInhale(float Strain, float Period)
		{
			FBreathEvent Ev;
			Ev.bExhale = false;
			Ev.Dur = Period * Rng.Range(0.3f, 0.38f);
			Ev.Amp = FMath::Lerp(0.25f, 0.7f, Strain) * Rng.Range(0.85f, 1.08f);
			static constexpr float Close[3] = { 430.f, 1800.f, 2700.f };
			const float Shift = Traits.Tract * Rng.Range(0.95f, 1.05f);
			for (int32 f = 0; f < 3; ++f)
			{
				Ev.FormStart[f] = Close[f] * Shift;
				Ev.FormEnd[f] = Ev.FormStart[f] * Rng.Range(1.f, 1.08f);
			}
			Ev.Q[0] = 2.6f;
			Ev.Q[1] = 3.5f;
			Ev.Q[2] = 4.5f;
			Ev.Gain[0] = 0.6f;
			Ev.Gain[1] = 1.f;
			Ev.Gain[2] = 0.6f;
			Ev.Voice = 0.f;
			return Ev;
		}

		/** Suspiro de alivio al calmarse tras un jadeo fuerte: «haaah» largo, flojo y con algo de voz. */
		FBreathEvent BuildSigh()
		{
			FBreathEvent Ev;
			Ev.bExhale = true;
			Ev.Dur = Rng.Range(0.55f, 0.8f);
			Ev.Amp = Rng.Range(0.4f, 0.5f);
			static constexpr float Schwa[3] = { 560.f, 1400.f, 2500.f };
			for (int32 f = 0; f < 3; ++f)
			{
				Ev.FormStart[f] = Schwa[f] * Traits.Tract;
				Ev.FormEnd[f] = Ev.FormStart[f] * (f == 0 ? 0.8f : 0.95f);
			}
			Ev.Q[0] = 3.5f;
			Ev.Q[1] = 4.5f;
			Ev.Q[2] = 5.f;
			Ev.Voice = Rng.Range(0.2f, 0.35f);
			Ev.F0Start = Traits.F0 * Rng.Range(0.85f, 0.95f);
			Ev.F0End = Ev.F0Start * 0.78f;
			return Ev;
		}

		void TriggerBreath(const FBreathEvent& Ev)
		{
			FBreathVoice& V = Breath[0].bActive ? Breath[1] : Breath[0];
			V.Ev = Ev;
			V.bActive = Ev.Dur > 0.f;
			V.Time = 0.f;
			V.Env[0] = 0.f;
			V.Env[1] = 0.f;
			V.Glottis.Reset(FMath::Min(Ev.F0Start * InvRate, 0.45f));
			for (int32 f = 0; f < 3; ++f)
			{
				V.Form[f].Reset();
				V.Form[f].Set(Ev.FormStart[f], Ev.Q[f], InvRate);
			}
			V.Lips.SetHz(Ev.bExhale ? 4500.f : 6000.f, InvRate);
			V.Lips.Reset();
			V.AirCut.SetHz(Ev.bExhale ? 180.f : 550.f, InvRate);
			V.AirCut.Reset();
			V.HissA.SetHz(3500.f, InvRate);
			V.HissA.Reset();
			++BreathCount;
		}

		void RenderBreath(FBreathVoice& V, int32 N, float Gain)
		{
			const FBreathEvent& Ev = V.Ev;
			const float BlockDt = static_cast<float>(N) * InvRate;
			const float TEnd = V.Time + BlockDt;
			float EnvEnd[2];
			BreathEnvelopes(Ev, TEnd, EnvEnd);

			// Formantes y tono al centro del bloque: se deslizan del principio al final.
			const float U = FMath::Clamp((V.Time + 0.5f * BlockDt) / Ev.Dur, 0.f, 1.f);
			const float Glide = SmoothStep01(U);
			for (int32 f = 0; f < 3; ++f)
			{
				V.Form[f].Set(FMath::Lerp(Ev.FormStart[f], Ev.FormEnd[f], Glide), Ev.Q[f], InvRate);
			}
			const float Tone = Ev.F0Start * FMath::Pow(Ev.F0End / FMath::Max(1.f, Ev.F0Start), U);
			const float ToneInc = FMath::Min(Tone * InvRate, 0.45f);
			const float Amp = Ev.Amp * Trim::Breath * Gain;
			// Espirando se oye además el soplo agudo de la boca; inspirando, algo más.
			const float HissMix = Ev.bExhale ? 0.12f : 0.2f;

			const float InvN = 1.f / static_cast<float>(N);
			float E0 = V.Env[0];
			float E1 = V.Env[1];
			const float D0 = (EnvEnd[0] - V.Env[0]) * InvN;
			const float D1 = (EnvEnd[1] - V.Env[1]) * InvN;
			for (int32 i = 0; i < N; ++i)
			{
				E0 += D0;
				E1 += D1;
				const float Noise = NoiseRng.Bipolar();
				float Exc = E0 * Trim::Air * Noise;
				if (E1 > 1e-5f)
				{
					Exc += E1 * Trim::Voice * V.Glottis.Next(ToneInc, 0.02f, 0.12f, NoiseRng);
				}
				float Y = Ev.Gain[0] * V.Form[0].Process(Exc) + Ev.Gain[1] * V.Form[1].Process(Exc) + Ev.Gain[2] * V.Form[2].Process(Exc);
				Y = V.Lips.Low(Y);
				Y += HissMix * Trim::Air * E0 * V.HissA.High(Noise);
				Y = V.AirCut.High(Y);
				Mix[i] += Y * Amp;
			}
			V.Env[0] = EnvEnd[0];
			V.Env[1] = EnvEnd[1];
			V.Time = TEnd;
			for (FBandPass& Band : V.Form) { Band.Flush(); }
			V.Lips.Flush();
			V.AirCut.Flush();
			V.HissA.Flush();
			if (V.Time >= Ev.Dur) { V.bActive = false; }
		}

		// ── Arrastre ─────────────────────────────────────────────────────────

		void InitDrag(uint32 InRunSeed)
		{
			FDragVoice& D = Drag;
			D.Rng.SetSeed(MixSeed(InRunSeed, 13u));
			D.Level = 0.f;
			D.Pace = 0.f;
			for (int32 s = 0; s < Surface::Num; ++s) { D.W[s] = s == Surface::Rock ? 1.f : 0.f; }
			D.Wobble = 0.f;
			D.WobbleTarget = 0.f;
			D.WobbleTimer = 0.f;
			D.BodyA.SetHz(160.f, InvRate);
			D.BodyB.SetHz(160.f, InvRate);
			D.SandLP.SetHz(1400.f, InvRate);
			D.LeafBP.Set(3800.f, 1.4f, InvRate);
			D.GritBP.Set(5200.f, 1.2f, InvRate);
			D.WaterLP.SetHz(380.f, InvRate);
			D.WoodModes[0].Set(190.f, 0.08f, InvRate);
			D.WoodModes[1].Set(520.f, 0.05f, InvRate);
			D.SandGrainK = DecayPerSample(0.0009f, InvRate);
			D.LeafGrainK = DecayPerSample(0.0022f, InvRate);
			D.StickK = DecayPerSample(0.018f, InvRate);
			D.GritGrainK = DecayPerSample(0.0006f, InvRate);
			D.SandGrain = D.LeafGrain = D.Stick = D.GritGrain = 0.f;
			D.BubbleEnv = 0.f;
			D.BodyA.Reset();
			D.BodyB.Reset();
			D.SandBP.Reset();
			D.SandLP.Reset();
			D.SoilBP.Reset();
			D.LeafBP.Reset();
			D.RockBP.Reset();
			D.GritBP.Reset();
			D.WoodBP.Reset();
			D.WaterBP.Reset();
			D.WaterLP.Reset();
			for (FResonator& Mode : D.WoodModes) { Mode.Reset(); }
		}

		/** Arrastre de un bloque sumado a Mix. Devuelve si ha sonado algo. Un generador viejo solo lo apaga. */
		bool RenderDrag(int32 N, const FSharedParams& P, bool bCurrent)
		{
			FDragVoice& D = Drag;
			const float Dt = static_cast<float>(N) * InvRate;
			const float Target = bCurrent ? FMath::Clamp(FSharedParams::Get(P.Drag), 0.f, 1.5f) : 0.f;
			const float StartLevel = D.Level;
			// Entra en unas centésimas y se apaga algo más despacio (el cuerpo aún roza al pararse).
			D.Level += (Target - D.Level) * TimeCoef(Target > D.Level ? 0.03f : 0.1f, Dt);
			if (D.Level < 1e-4f && StartLevel < 1e-4f)
			{
				D.Level = 0.f;
				return false;
			}
			const float PaceGoal = bCurrent ? FMath::Clamp(FSharedParams::Get(P.DragPace), 0.f, 1.f) : D.Pace;
			D.Pace += (PaceGoal - D.Pace) * TimeCoef(0.08f, Dt);

			// Pesos de superficie hacia los que manda el juego (normalizados; sin datos, roca).
			float Goal[Surface::Num];
			float Sum = 0.f;
			for (int32 s = 0; s < Surface::Num; ++s)
			{
				Goal[s] = bCurrent ? FMath::Max(0.f, FSharedParams::Get(P.DragSurf[s])) : D.W[s];
				Sum += Goal[s];
			}
			if (Sum <= 1e-4f)
			{
				for (int32 s = 0; s < Surface::Num; ++s) { Goal[s] = s == Surface::Rock ? 1.f : 0.f; }
				Sum = 1.f;
			}
			const float WeightK = TimeCoef(0.06f, Dt);
			for (int32 s = 0; s < Surface::Num; ++s) { D.W[s] += (Goal[s] / Sum - D.W[s]) * WeightK; }

			// Baches: el nivel sube y baja un poco hacia un objetivo nuevo cada 40-110 ms.
			D.WobbleTimer -= Dt;
			if (D.WobbleTimer <= 0.f)
			{
				D.WobbleTimer = D.Rng.Range(0.04f, 0.11f);
				D.WobbleTarget = D.Rng.Bipolar();
			}
			D.Wobble += (D.WobbleTarget - D.Wobble) * TimeCoef(0.03f, Dt);

			// Más deprisa: filtros más agudos y granos más seguidos.
			const float Pace = D.Pace;
			D.SandBP.Set(FMath::Lerp(1900.f, 3600.f, Pace), 0.8f, InvRate);
			D.SoilBP.Set(FMath::Lerp(480.f, 1000.f, Pace), 0.9f, InvRate);
			D.RockBP.Set(FMath::Lerp(1400.f, 2600.f, Pace), 1.6f, InvRate);
			D.WoodBP.Set(FMath::Lerp(650.f, 1400.f, Pace), 1.2f, InvRate);
			D.WaterBP.Set(FMath::Lerp(900.f, 2400.f, Pace), 1.f, InvRate);
			const float SandRate = FMath::Lerp(700.f, 2600.f, Pace) * InvRate;
			const float LeafRate = FMath::Lerp(10.f, 45.f, Pace) * InvRate;
			const float StickRate = FMath::Lerp(9.f, 40.f, Pace) * InvRate;
			const float GritRate = FMath::Lerp(150.f, 650.f, Pace) * InvRate;
			const float BubbleRate = FMath::Lerp(3.f, 12.f, Pace) * InvRate;
			const float WSand = D.W[Surface::Sand];
			const float WSoil = D.W[Surface::Soil];
			const float WRock = D.W[Surface::Rock];
			const float WWood = D.W[Surface::Wood];
			const float WWater = D.W[Surface::Water];

			const float Gain = FMath::Max(0.f, FSharedParams::Get(P.DragGain)) * (1.f + 0.22f * D.Wobble);
			const float InvN = 1.f / static_cast<float>(N);
			float L = StartLevel * Gain;
			const float DL = (D.Level * Gain - L) * InvN;
			FRandom& R = D.Rng;
			for (int32 i = 0; i < N; ++i)
			{
				L += DL;
				const float Noise = R.Bipolar();
				// El cuerpo que roza: un fondo grave en todas las superficies.
				float Y = DragTrim::Body * D.BodyB.Low(D.BodyA.Low(Noise));
				if (WSand > 0.02f)
				{
					if (R.Unit() < SandRate) { D.SandGrain = FMath::Max(D.SandGrain, R.Range(0.4f, 1.f)); }
					D.SandGrain *= D.SandGrainK;
					Y += WSand * (DragTrim::Sand * D.SandBP.Process(Noise * (0.35f + 0.65f * D.SandGrain))
						+ DragTrim::SandHiss * D.SandLP.Low(Noise));
				}
				if (WSoil > 0.02f)
				{
					if (R.Unit() < LeafRate) { D.LeafGrain = FMath::Max(D.LeafGrain, R.Range(0.5f, 1.f)); }
					D.LeafGrain *= D.LeafGrainK;
					Y += WSoil * (DragTrim::Soil * D.SoilBP.Process(Noise) + DragTrim::Leaf * D.LeafBP.Process(Noise * D.LeafGrain));
				}
				if (WRock > 0.02f)
				{
					// Se pega y se suelta: el raspado va a trompicones, con arenilla encima.
					if (R.Unit() < StickRate) { D.Stick = FMath::Max(D.Stick, R.Range(0.5f, 1.f)); }
					D.Stick *= D.StickK;
					if (R.Unit() < GritRate) { D.GritGrain = FMath::Max(D.GritGrain, R.Range(0.3f, 1.f)); }
					D.GritGrain *= D.GritGrainK;
					Y += WRock * (DragTrim::Rock * D.RockBP.Process(Noise * (0.25f + 0.75f * D.Stick))
						+ DragTrim::Grit * D.GritBP.Process(Noise * D.GritGrain));
				}
				if (WWood > 0.02f)
				{
					const float Scrape = D.WoodBP.Process(Noise);
					const float Hum = D.WoodModes[0].Process(Noise * 0.05f) + 0.6f * D.WoodModes[1].Process(Noise * 0.05f);
					Y += WWood * (DragTrim::Wood * Scrape + DragTrim::WoodHum * Hum);
				}
				if (WWater > 0.02f)
				{
					float Water = DragTrim::Water * D.WaterBP.Process(Noise) + DragTrim::WaterLow * D.WaterLP.Low(Noise);
					if (D.BubbleEnv < 0.02f && R.Unit() < BubbleRate)
					{
						D.BubbleEnv = R.Range(0.5f, 1.f);
						D.BubbleF0 = R.Range(500.f, 1300.f);
						const float Life = R.Range(0.012f, 0.03f);
						D.BubbleK = DecayPerSample(Life, InvRate);
						D.BubbleRise = R.Range(0.5f, 1.2f) / Life;
						D.BubbleAge = 0.f;
						D.BubblePhase = 0.f;
					}
					if (D.BubbleEnv > 1e-4f)
					{
						D.BubbleAge += InvRate;
						D.BubblePhase += FMath::Min(D.BubbleF0 * (1.f + D.BubbleRise * D.BubbleAge) * InvRate, 0.45f);
						D.BubblePhase -= FMath::FloorToFloat(D.BubblePhase);
						Water += DragTrim::Bubble * D.BubbleEnv * FastSin01(D.BubblePhase);
						D.BubbleEnv *= D.BubbleK;
					}
					Y += WWater * Water;
				}
				Mix[i] += Y * L;
			}
			D.BodyA.Flush();
			D.BodyB.Flush();
			D.SandBP.Flush();
			D.SandLP.Flush();
			D.SoilBP.Flush();
			D.LeafBP.Flush();
			D.RockBP.Flush();
			D.GritBP.Flush();
			D.WoodBP.Flush();
			D.WaterBP.Flush();
			D.WaterLP.Flush();
			for (FResonator& Mode : D.WoodModes) { Mode.Flush(); }
			return true;
		}

		// ── Bloque ───────────────────────────────────────────────────────────

		void RenderBlock(int32 N, FSharedParams& P, bool bCurrent)
		{
			const float Dt = static_cast<float>(N) * InvRate;
			Clock += Dt;
			const float MasterGoal = FMath::Max(0.f, FSharedParams::Get(P.Master));
			Master = bFirstBlock ? MasterGoal : Master + (MasterGoal - Master) * TimeCoef(0.1f, Dt);
			bFirstBlock = false;

			// Un generador viejo no empieza respiraciones: solo acaba lo que suena.
			const float PantTarget = bCurrent ? FMath::Clamp(FSharedParams::Get(P.Pant), 0.f, 1.f) : 0.f;
			const bool bHush = !bCurrent || P.PantHush.load(std::memory_order_relaxed) != 0;
			ScheduleBreath(Dt, PantTarget, bHush);

			for (int32 i = 0; i < N; ++i) { Mix[i] = 0.f; }
			bool bAny = false;
			const float StepGain = FMath::Max(0.f, FSharedParams::Get(P.StepGain));
			for (FStepVoice& Slot : Steps)
			{
				if (Slot.bActive)
				{
					RenderStep(Slot, N);
					bAny = true;
				}
			}
			if (bAny && StepGain != 1.f)
			{
				for (int32 i = 0; i < N; ++i) { Mix[i] *= StepGain; }
			}
			const float BreathGain = FMath::Max(0.f, FSharedParams::Get(P.BreathGain));
			for (FBreathVoice& Slot : Breath)
			{
				if (Slot.bActive)
				{
					RenderBreath(Slot, N, BreathGain);
					bAny = true;
				}
			}
			if (RenderDrag(N, P, bCurrent))
			{
				bAny = true;
			}

			// Salida: volumen, sin continua y limitador suave.
			const float GainEnd = Master;
			if (!bAny && FMath::Abs(DcCut.Z) < 1e-6f)
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

		float Rate = 48000.f;
		float InvRate = 1.f / 48000.f;
		/** Arranque de este generador y su cursor del anillo de pasos. */
		uint32 RunId = 0u;
		uint32 Cursor = 0u;
		FRandom Rng;
		FRandom NoiseRng;
		FTraits Traits;
		FStepVoice Steps[MaxSteps];
		FBreathVoice Breath[2];
		FDragVoice Drag;
		/** Segundos desde que arrancó el generador. */
		float Clock = 0.f;
		/** Jadeo suavizado (0..1), el más fuerte de esta tanda, cuándo toca la siguiente respiración y cuál. */
		float PantLevel = 0.f;
		float BoutPeak = 0.f;
		float NextBreathAt = 0.f;
		bool bBreathing = false;
		bool bNextExhale = true;
		bool bFirstBlock = true;
		float Master = 0.f;
		float PrevGain = 0.f;
		float LimEnv = 0.f;
		float LimAttack = 0.f;
		float LimRelease = 0.f;
		FOnePole DcCut;
		int32 BreathCount = 0;
		float Mix[BlockFrames] = {};
	};
}
