// ─────────────────────────────────────────────────────────────────────────────
// ATN_ProcFauna — fauna ambiental del mapa procedural: reparto junto al camino por
// bioma, máquina de estados por animal (reposo, paseo, alerta, huida, escondido y
// reaparición) y animación por piezas calculada en la CPU sobre ISM. Solo visual y
// local: cada máquina simula la suya y en un servidor dedicado no existe.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/ProcMap/TN_ProcFauna.h"
#include "World/ProcMap/TN_ProcMapGenerator.h"
#include "TN_ProcMapFaunaMeshes.h"
#include "TN_ProcMapRuntimeMesh.h"
#include "TN_ProcMapAmbientFX.h"
#include "TN_ProcMapKeepOut.h"
#include "Art/TN_Art.h"
#include "Core/TN_Log.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace TNFaunaSim
{
	/** TN.Fauna.Enable: 0 esconde toda la fauna (p. ej., para comparar el rendimiento). */
	static int32 FaunaEnabled = 1;
	static FAutoConsoleVariableRef CVarFaunaEnabled(TEXT("TN.Fauna.Enable"), FaunaEnabled,
		TEXT("1 = fauna ambiental del mapa procedural; 0 = escondida."), ECVF_Default);

	/** TN.Fauna.Stats: 1 escribe en el log cada 2 s los animales despiertos y el coste medio del Tick. */
	static int32 FaunaStats = 0;
	static FAutoConsoleVariableRef CVarFaunaStats(TEXT("TN.Fauna.Stats"), FaunaStats,
		TEXT("1 = métricas de la fauna ambiental en el log cada 2 s."), ECVF_Default);

	/** Histéresis del radio de simulación (cm): se duerme un poco más lejos de donde se despierta. */
	constexpr float SleepMargin = 800.f;
	/** El susto se contagia a los de su especie que estén a menos de esto (cm). */
	constexpr float GroupAlarmRadius = 900.f;
	/**
	 * Escala de los rangos de alarma de las especies: con los de la tabla huían a 6-9 m y, como la tortuga corre, casi
	 * nadie llegaba a verlos. Así dejan acercarse la mitad antes de salir corriendo (y se paran a mirar antes).
	 */
	constexpr float AlarmScale = 0.5f;
	/** Escala de la velocidad de huida: se les ve correr en vez de desaparecer de golpe. */
	constexpr float FleeScale = 0.75f;
	/** Nunca reaparece más cerca de una cámara (cm). */
	constexpr float RespawnNear = 3000.f;
	/** Los dormidos que quedan este trecho de camino (cm) por detrás de la cámara se reciclan por delante. */
	constexpr float BehindDistance = 8000.f;
	/** Tiempo escondido antes de volver (s). */
	constexpr float HideMin = 5.f;
	constexpr float HideMax = 12.f;
	/** Animales dormidos que revisa el reciclaje en cada fotograma. */
	constexpr int32 SweepPerFrame = 6;
	/** Gravedad de los saltos de los peces (cm/s²). */
	constexpr float FishGravity = 980.f;

	/** Gira From hacia To (grados) como mucho MaxStep. */
	inline float TurnToward(float From, float To, float MaxStep)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(From, To);
		return FMath::UnwindDegrees(From + FMath::Clamp(Delta, -MaxStep, MaxStep));
	}

	inline float YawOf(const FVector2D& D)
	{
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)));
	}

	inline FVector2D DirOfYaw(float YawDeg)
	{
		const float Rad = FMath::DegreesToRadians(YawDeg);
		return FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
	}

	/** Cabeceo (grados) de una velocidad con componente vertical Up y horizontal Flat. */
	inline float PitchOf(double Up, double Flat)
	{
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Up, FMath::Max(1.0, Flat))));
	}

	/** Peso de la marcha: 0 quieto, ~0,55 al paso y 1 a la carrera de huida. */
	inline float LocoOf(const TNFauna::FTNFaunaSpec& Sp, float Speed)
	{
		const float WalkPart = FMath::Clamp(Speed / FMath::Max(1.f, Sp.WalkSpeed), 0.f, 1.f);
		const float RunPart = FMath::Clamp((Speed - Sp.WalkSpeed) / FMath::Max(1.f, Sp.FleeSpeed - Sp.WalkSpeed), 0.f, 1.f);
		return WalkPart * 0.55f + RunPart * 0.45f;
	}

	/** Cangrejos: hacia qué lado andar (+1 su derecha, -1 su izquierda) para girar lo menos posible. */
	inline int8 CrabSide(float CurYaw, const FVector2D& MoveDir)
	{
		const float Heading = YawOf(MoveDir);
		const float TurnRight = FMath::Abs(FMath::FindDeltaAngleDegrees(CurYaw, Heading - 90.f));
		const float TurnLeft = FMath::Abs(FMath::FindDeltaAngleDegrees(CurYaw, Heading + 90.f));
		return static_cast<int8>(TurnRight <= TurnLeft ? 1 : -1);
	}

	/** Acción de reposo al azar entre las de la especie (mirar alrededor sale el doble de veces). */
	inline TNFauna::ETNFaunaAct PickAct(const TNFauna::FTNFaunaSpec& Sp, float Pick01)
	{
		using namespace TNFauna;
		constexpr int32 NumActs = static_cast<int32>(ETNFaunaAct::Count);
		ETNFaunaAct Options[NumActs + 1] = {};
		int32 NumOptions = 0;
		for (int32 i = 1; i < NumActs; ++i)
		{
			const ETNFaunaAct Each = static_cast<ETNFaunaAct>(i);
			if ((Sp.Acts & TNFaunaActBit(Each)) == 0) { continue; }
			Options[NumOptions++] = Each;
			if (Each == ETNFaunaAct::Look) { Options[NumOptions++] = Each; }
		}
		if (NumOptions == 0) { return ETNFaunaAct::None; }
		return Options[FMath::Clamp(static_cast<int32>(Pick01 * NumOptions), 0, NumOptions - 1)];
	}

	/** Duración de cada acción de reposo (s); Unit01 al azar. */
	inline float ActLength(TNFauna::ETNFaunaAct Act, float Unit01)
	{
		using TNFauna::ETNFaunaAct;
		float MinLen = 1.f;
		float MaxLen = 2.f;
		switch (Act)
		{
			case ETNFaunaAct::Look:     MinLen = 1.5f; MaxLen = 3.f; break;
			case ETNFaunaAct::Peck:     MinLen = 1.5f; MaxLen = 3.5f; break;
			case ETNFaunaAct::Graze:    MinLen = 3.f;  MaxLen = 6.f; break;
			case ETNFaunaAct::Scratch:  MinLen = 1.5f; MaxLen = 3.f; break;
			case ETNFaunaAct::Wag:      MinLen = 2.f;  MaxLen = 4.f; break;
			case ETNFaunaAct::Preen:    MinLen = 2.f;  MaxLen = 3.5f; break;
			case ETNFaunaAct::OneLeg:   MinLen = 5.f;  MaxLen = 10.f; break;
			case ETNFaunaAct::PushUp:   MinLen = 1.5f; MaxLen = 2.5f; break;
			case ETNFaunaAct::Sentinel: MinLen = 3.f;  MaxLen = 6.f; break;
			case ETNFaunaAct::Claws:    MinLen = 1.5f; MaxLen = 3.f; break;
			case ETNFaunaAct::Sit:      MinLen = 4.f;  MaxLen = 8.f; break;
			case ETNFaunaAct::Hop:      MinLen = 1.f;  MaxLen = 2.f; break;
			case ETNFaunaAct::Stretch:  MinLen = 1.5f; MaxLen = 2.5f; break;
			case ETNFaunaAct::Croak:    MinLen = 1.5f; MaxLen = 3.f; break;
			case ETNFaunaAct::Dig:      MinLen = 1.5f; MaxLen = 3.f; break;
			default: break;
		}
		return FMath::Lerp(MinLen, MaxLen, Unit01);
	}

	/** Pose de alerta de cada especie: pinzas en alto, vigilar de pie, flexiones, saltitos o mirar fijo. */
	inline TNFauna::ETNFaunaAct AlertAct(const TNFauna::FTNFaunaSpec& Sp)
	{
		using namespace TNFauna;
		if (Sp.Body == ETNFaunaBody::Crab) { return ETNFaunaAct::Claws; }
		if ((Sp.Acts & TNFaunaActBit(ETNFaunaAct::Sentinel)) != 0) { return ETNFaunaAct::Sentinel; }
		if ((Sp.Acts & TNFaunaActBit(ETNFaunaAct::PushUp)) != 0) { return ETNFaunaAct::PushUp; }
		if (Sp.Body == ETNFaunaBody::Quad && (Sp.Acts & TNFaunaActBit(ETNFaunaAct::Hop)) != 0) { return ETNFaunaAct::Hop; }
		return ETNFaunaAct::Look;
	}

	/** Peso (0..1) de la acción en curso: entra y sale en 0,3 s. */
	inline float ActWeight(float ActT, float ActDur)
	{
		return FMath::Clamp(FMath::Min(ActT, ActDur - ActT) / 0.3f, 0.f, 1.f);
	}

	/**
	 * Pieza de arte de cada pieza animada (Docs/Arte_Assets.md): una por especie y canal de animación (las manchas que
	 * brillan, aparte). Su malla de arte va en el ISM de esa pieza y el código la sigue moviendo (TNArt::UpdateInstances).
	 */
	inline FName TNFaunaArt(TNFauna::ETNFaunaSpecies Species, TNFauna::ETNFaunaBone Bone, bool bGlow)
	{
		using Sp = TNFauna::ETNFaunaSpecies;
		using B = TNFauna::ETNFaunaBone;
		switch (Species)
		{
			case Sp::Crab:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Crab.Body");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Crab.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Crab.LegFR");
					case B::ClawL: return TN_ART("ProcMap.Fauna.Crab.ClawL");
					case B::ClawR: return TN_ART("ProcMap.Fauna.Crab.ClawR");
					default:       return NAME_None;
				}
			case Sp::FiddlerCrab:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.FiddlerCrab.Body");
					case B::LegFL: return TN_ART("ProcMap.Fauna.FiddlerCrab.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.FiddlerCrab.LegFR");
					case B::ClawL: return TN_ART("ProcMap.Fauna.FiddlerCrab.ClawL");
					case B::ClawR: return TN_ART("ProcMap.Fauna.FiddlerCrab.ClawR");
					default:       return NAME_None;
				}
			case Sp::BabyTurtle:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.BabyTurtle.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.BabyTurtle.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.BabyTurtle.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.BabyTurtle.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.BabyTurtle.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.BabyTurtle.LegBR");
					default:       return NAME_None;
				}
			case Sp::SeaTurtle:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.SeaTurtle.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.SeaTurtle.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.SeaTurtle.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.SeaTurtle.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.SeaTurtle.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.SeaTurtle.LegBR");
					default:       return NAME_None;
				}
			case Sp::Gull:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Gull.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Gull.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Gull.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Gull.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Gull.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Gull.LegBR");
					default:       return NAME_None;
				}
			case Sp::Sandpiper:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Sandpiper.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Sandpiper.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Sandpiper.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Sandpiper.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Sandpiper.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Sandpiper.LegBR");
					default:       return NAME_None;
				}
			case Sp::Toucan:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Toucan.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Toucan.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Toucan.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Toucan.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Toucan.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Toucan.LegBR");
					default:       return NAME_None;
				}
			case Sp::Heron:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Heron.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Heron.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Heron.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Heron.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Heron.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Heron.LegBR");
					default:       return NAME_None;
				}
			case Sp::Flamingo:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Flamingo.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Flamingo.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Flamingo.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Flamingo.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Flamingo.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Flamingo.LegBR");
					default:       return NAME_None;
				}
			case Sp::Pelican:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Pelican.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Pelican.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Pelican.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Pelican.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Pelican.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Pelican.LegBR");
					default:       return NAME_None;
				}
			case Sp::Vulture:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Vulture.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Vulture.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Vulture.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Vulture.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Vulture.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Vulture.LegBR");
					default:       return NAME_None;
				}
			case Sp::Eagle:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Eagle.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Eagle.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Eagle.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Eagle.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Eagle.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Eagle.LegBR");
					default:       return NAME_None;
				}
			case Sp::Pigeon:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Pigeon.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Pigeon.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Pigeon.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Pigeon.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Pigeon.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Pigeon.LegBR");
					default:       return NAME_None;
				}
			case Sp::Hen:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Hen.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Hen.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Hen.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Hen.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Hen.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Hen.LegBR");
					default:       return NAME_None;
				}
			case Sp::Roadrunner:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Roadrunner.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Roadrunner.Head");
					case B::WingL: return TN_ART("ProcMap.Fauna.Roadrunner.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Roadrunner.WingR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Roadrunner.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Roadrunner.LegBR");
					default:       return NAME_None;
				}
			case Sp::Monkey:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Monkey.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Monkey.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Monkey.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Monkey.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Monkey.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Monkey.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Monkey.Tail");
					default:       return NAME_None;
				}
			case Sp::Capybara:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Capybara.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Capybara.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Capybara.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Capybara.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Capybara.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Capybara.LegBR");
					default:       return NAME_None;
				}
			case Sp::Meerkat:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Meerkat.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Meerkat.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Meerkat.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Meerkat.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Meerkat.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Meerkat.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Meerkat.Tail");
					default:       return NAME_None;
				}
			case Sp::Ibex:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Ibex.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Ibex.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Ibex.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Ibex.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Ibex.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Ibex.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Ibex.Tail");
					default:       return NAME_None;
				}
			case Sp::Marmot:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Marmot.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Marmot.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Marmot.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Marmot.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Marmot.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Marmot.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Marmot.Tail");
					default:       return NAME_None;
				}
			case Sp::Cat:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Cat.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Cat.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Cat.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Cat.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Cat.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Cat.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Cat.Tail");
					default:       return NAME_None;
				}
			case Sp::Rabbit:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Rabbit.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Rabbit.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Rabbit.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Rabbit.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Rabbit.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Rabbit.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Rabbit.Tail");
					default:       return NAME_None;
				}
			case Sp::Lizard:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Lizard.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Lizard.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Lizard.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Lizard.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Lizard.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Lizard.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Lizard.Tail");
					default:       return NAME_None;
				}
			case Sp::Salamander:
				switch (Bone)
				{
					case B::Body:  return bGlow ? TN_ART("ProcMap.Fauna.Salamander.BodyGlow") : TN_ART("ProcMap.Fauna.Salamander.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.Salamander.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Salamander.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Salamander.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.Salamander.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.Salamander.LegBR");
					case B::Tail:  return bGlow ? TN_ART("ProcMap.Fauna.Salamander.TailGlow") : TN_ART("ProcMap.Fauna.Salamander.Tail");
					default:       return NAME_None;
				}
			case Sp::MarineIguana:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.MarineIguana.Body");
					case B::Head:  return TN_ART("ProcMap.Fauna.MarineIguana.Head");
					case B::LegFL: return TN_ART("ProcMap.Fauna.MarineIguana.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.MarineIguana.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.MarineIguana.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.MarineIguana.LegBR");
					case B::Tail:  return TN_ART("ProcMap.Fauna.MarineIguana.Tail");
					default:       return NAME_None;
				}
			case Sp::DartFrog:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.DartFrog.Body");
					case B::LegFL: return TN_ART("ProcMap.Fauna.DartFrog.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.DartFrog.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.DartFrog.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.DartFrog.LegBR");
					default:       return NAME_None;
				}
			case Sp::TreeFrog:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.TreeFrog.Body");
					case B::LegFL: return TN_ART("ProcMap.Fauna.TreeFrog.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.TreeFrog.LegFR");
					case B::LegBL: return TN_ART("ProcMap.Fauna.TreeFrog.LegBL");
					case B::LegBR: return TN_ART("ProcMap.Fauna.TreeFrog.LegBR");
					default:       return NAME_None;
				}
			case Sp::Fish:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Fish.Body");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Fish.Tail");
					default:       return NAME_None;
				}
			case Sp::Mudskipper:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Mudskipper.Body");
					case B::Tail:  return TN_ART("ProcMap.Fauna.Mudskipper.Tail");
					case B::LegFL: return TN_ART("ProcMap.Fauna.Mudskipper.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.Mudskipper.LegFR");
					default:       return NAME_None;
				}
			case Sp::FireBeetle:
				switch (Bone)
				{
					case B::Body:  return bGlow ? TN_ART("ProcMap.Fauna.FireBeetle.BodyGlow") : TN_ART("ProcMap.Fauna.FireBeetle.Body");
					case B::LegFL: return TN_ART("ProcMap.Fauna.FireBeetle.LegFL");
					case B::LegFR: return TN_ART("ProcMap.Fauna.FireBeetle.LegFR");
					default:       return NAME_None;
				}
			case Sp::Bat:
				switch (Bone)
				{
					case B::Body:  return TN_ART("ProcMap.Fauna.Bat.Body");
					case B::WingL: return TN_ART("ProcMap.Fauna.Bat.WingL");
					case B::WingR: return TN_ART("ProcMap.Fauna.Bat.WingR");
					default:       return NAME_None;
				}
			default:
				return NAME_None;
		}
	}
}

ATN_ProcFauna::ATN_ProcFauna()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
}

void ATN_ProcFauna::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearFauna();
	Super::EndPlay(EndPlayReason);
}

void ATN_ProcFauna::Init(const ATN_ProcMapGenerator* InGenerator, uint32 InSeed)
{
	ClearFauna();
	GeneratorRef = InGenerator;
	bCustom = false;
	FaunaSeed = InSeed;
	const UWorld* World = GetWorld();
	if (!InGenerator || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	bPendingBuild = true;
	SetActorTickEnabled(true);
	if (InGenerator->IsMapReady())
	{
		BuildFauna();
	}
}

void ATN_ProcFauna::InitCustom(const FCustomTerrain& Terrain, uint32 InSeed)
{
	ClearFauna();
	GeneratorRef = nullptr;
	bCustom = true;
	Custom = Terrain;
	FaunaSeed = InSeed;
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || !Custom.HeightAt)
	{
		SetActorTickEnabled(false);
		return;
	}
	bPendingBuild = true;
	SetActorTickEnabled(true);
	BuildFauna();
}

void ATN_ProcFauna::ClearFauna()
{
	TNAmbientFX::RemoveOwner(this);
	SplashFX = INDEX_NONE;
	RingFX = INDEX_NONE;
	DustFX = INDEX_NONE;
	for (UInstancedStaticMeshComponent* ISM : PartISMs)
	{
		if (ISM) { ISM->DestroyComponent(); }
	}
	PartISMs.Reset();
	PartMeshes.Reset();
	PartBone.Reset();
	PartPivot.Reset();
	PartXf.Reset();
	Animals.Reset();
	Kinds.Reset();
	bPendingBuild = false;
	bBuilt = false;
	NumAwake = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reparto
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcFauna::BuildFauna()
{
	using namespace TNFauna;
	TRACE_CPUPROFILER_EVENT_SCOPE(TNFauna_Build);
	bPendingBuild = false;
	const ATN_ProcMapGenerator* Gen = GeneratorRef.Get();
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer || (!bCustom && (!Gen || !Gen->IsMapReady())))
	{
		return;
	}
	// Aunque no salga fauna, no se reintenta hasta que el generador regenere.
	bBuilt = true;
	BuiltForGeneration = bCustom ? 0 : Gen->GetBuiltGeneration();

	UMaterialInterface* SolidMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage"));
	if (!SolidMat)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Fauna] Falta M_ProcFoliage: no se crea la fauna."));
		return;
	}
	UMaterialInterface* GlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcGlow.M_ProcGlow"));
	if (!GlowMat) { GlowMat = SolidMat; }

	static const TNProcMap::FLayout EmptyLayout;
	const TNProcMap::FLayout& Layout = bCustom ? EmptyLayout : Gen->GetLayout();
	if (!bCustom && (!Layout.bValid || Layout.Main.Num() < 2 || Layout.Modules.Num() == 0))
	{
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	const FTransform GenXf = bCustom ? FTransform::Identity : Gen->GetActorTransform();
	WaterZ = bCustom ? Custom.WaterZ : static_cast<float>(GenXf.TransformPosition(FVector(0.0, 0.0, TNProcMap::SeaLevel + 2.0)).Z);
	TNProcMap::FRng Rng(static_cast<uint64>(FaunaSeed) * 0x9E3779B1ull + 0xFA17Aull);
	SimRng = static_cast<uint32>(Rng.Next() | 1ull);

	// ── Anclas: muestras del camino principal y de las ramas donde el suelo es terreno ──
	struct FTNFaunaAnchor
	{
		FVector2D P = FVector2D::ZeroVector;
		FVector2D Dir = FVector2D(0.0, 1.0);
		double Width = 800.0;
		float S = 0.f;
		int32 Module = INDEX_NONE;
		ETNProcBiome Biome = ETNProcBiome::Jungle;
	};
	constexpr uint32 SkipFlags = TNProcMap::PathFlags::Elevated | TNProcMap::PathFlags::Tunnel | TNProcMap::PathFlags::Slide
		| TNProcMap::PathFlags::Gap | TNProcMap::PathFlags::TowerTop | TNProcMap::PathFlags::UnderTower | TNProcMap::PathFlags::Start;
	TArray<FTNFaunaAnchor> Anchors;
	Anchors.Reserve(Layout.Main.Num() / 2 + 256);
	auto AddAnchor = [&Anchors](const TNProcMap::FPathSample& Sample, float Progress)
	{
		FTNFaunaAnchor& An = Anchors.AddDefaulted_GetRef();
		An.P = Sample.P;
		An.Dir = Sample.Dir;
		An.Width = Sample.Width;
		An.S = Progress;
		An.Module = Sample.Module;
		An.Biome = Sample.Biome;
	};
	if (bCustom)
	{
		// Mapa propio: las anclas ya vienen repartidas (sitios llanos y abiertos de la arena).
		for (const FCustomAnchor& Source : Custom.Anchors)
		{
			FTNFaunaAnchor& An = Anchors.AddDefaulted_GetRef();
			An.P = Source.P;
			An.Dir = FVector2D(0.0, 1.0);
			An.Width = Source.Width;
			An.S = Source.S;
			An.Module = Source.Module;
			An.Biome = Source.Biome;
		}
	}
	for (int32 i = 0; !bCustom && i < Layout.Main.Num(); i += 2)
	{
		const TNProcMap::FPathSample& Smp = Layout.Main[i];
		if ((Smp.Flags & SkipFlags) == 0 && Smp.Module >= 0) { AddAnchor(Smp, static_cast<float>(Smp.S)); }
	}
	for (const TNProcMap::FBranch& Br : Layout.Branches)
	{
		if (Br.Samples.Num() < 2 || !Layout.Main.IsValidIndex(Br.ForkSample) || !Layout.Main.IsValidIndex(Br.RejoinSample)) { continue; }
		// Progreso de las ramas: el del principal entre la salida y la vuelta (como el índice del generador).
		const double ForkS = Layout.Main[Br.ForkSample].S;
		const double RejoinS = Layout.Main[Br.RejoinSample].S;
		const double BranchLen = FMath::Max(1.0, Br.Samples.Last().S);
		for (int32 i = 0; i < Br.Samples.Num(); i += 3)
		{
			const TNProcMap::FPathSample& Smp = Br.Samples[i];
			if ((Smp.Flags & SkipFlags) == 0 && Smp.Module >= 0) { AddAnchor(Smp, static_cast<float>(FMath::Lerp(ForkS, RejoinS, Smp.S / BranchLen))); }
		}
	}
	if (Anchors.Num() == 0)
	{
		return;
	}

	// ── Sitios válidos por especie junto al camino (sirven para colocar y para reciclar) ──
	FTNProcKeepOut Keep;
	if (!bCustom) { Keep.AddLayout(Layout); }
	FTNFaunaBiomeTable Tables[TNProcMap::NumBiomes];
	for (int32 b = 0; b < TNProcMap::NumBiomes; ++b) { Tables[b] = TNFaunaBiomeTableOf(TNProcMap::BiomeFromIndex(b)); }
	TArray<FTNFaunaSpot> SpotsOf[TNFaunaNumSpecies];
	const float SteepPlace = FMath::Tan(FMath::DegreesToRadians(34.f));
	const float SteepClimber = FMath::Tan(FMath::DegreesToRadians(58.f));
	for (const FTNFaunaAnchor& An : Anchors)
	{
		const FTNFaunaBiomeTable& Table = Tables[TNProcMap::BiomeIndex(An.Biome)];
		const FVector2D Across(-An.Dir.Y, An.Dir.X);
		for (int32 e = 0; e < Table.Num; ++e)
		{
			const ETNFaunaSpecies SpId = Table.Entries[e].Species;
			const FTNFaunaSpec& Sp = TNFaunaSpec(SpId);
			const bool bWet = Sp.Habitat == ETNFaunaHabitat::Water || Sp.Habitat == ETNFaunaHabitat::Shallows;
			const bool bClimber = Sp.Flee == ETNFaunaFlee::Climb || Sp.Fallback == ETNFaunaFlee::Climb || Sp.bHighGround;
			const int32 Tries = Sp.bHighGround ? 4 : 2;
			bool bFound = false;
			FVector Best = FVector::ZeroVector;
			for (int32 t = 0; t < Tries; ++t)
			{
				// En el propio camino (bastantes) o a un lado: la mayoría a menos de 9 m de su borde, para que se crucen con
				// las tortugas; el resto, hasta 25 m (60 m las de agua).
				const bool bOnPath = !bWet && Rng.Chance(FMath::Min(0.6f, Sp.OnPath * 1.6f));
				const double Near = Rng.Chance(0.65) ? Rng.Range(60.0, 900.0) : Rng.Range(900.0, bWet ? 6000.0 : 2500.0);
				const double Lateral = bOnPath ? Rng.Range(-0.4, 0.4) * An.Width
					: (Rng.Chance(0.5) ? 1.0 : -1.0) * (An.Width * 0.5 + Near);
				const FVector2D MapP = An.P + An.Dir * Rng.Range(-250.0, 250.0) + Across * Lateral;
				if (bCustom ? (Custom.Blocked && Custom.Blocked(MapP)) : Keep.Blocked(MapP)) { continue; }
				const FVector WorldP = GenXf.TransformPosition(FVector(MapP.X, MapP.Y, 0.0));
				const float GroundH = GroundAt(WorldP);
				if (!HabitatOk(static_cast<uint8>(SpId), GroundH)) { continue; }
				if (Sp.Habitat != ETNFaunaHabitat::Water)
				{
					const float HX = GroundAt(WorldP + FVector(80.0, 0.0, 0.0));
					const float HY = GroundAt(WorldP + FVector(0.0, 80.0, 0.0));
					const float Grade = FMath::Max(FMath::Abs(HX - GroundH), FMath::Abs(HY - GroundH)) / 80.f;
					if (Grade > (bClimber ? SteepClimber : SteepPlace)) { continue; }
				}
				if (!bFound || (Sp.bHighGround && GroundH > Best.Z))
				{
					Best = FVector(WorldP.X, WorldP.Y, GroundH);
					bFound = true;
				}
				if (!Sp.bHighGround) { break; }
			}
			if (!bFound) { continue; }
			// La cota del sitio es la de su superficie: el agua para los que nadan, el aire para los que revolotean.
			if (Sp.Habitat == ETNFaunaHabitat::Water) { Best.Z = WaterZ; }
			else if (Sp.Gait == ETNFaunaGait::Hover) { Best.Z += Rng.Range(260.0, 480.0); }
			FTNFaunaSpot& NewSpot = SpotsOf[static_cast<int32>(SpId)].AddDefaulted_GetRef();
			NewSpot.Pos = Best;
			NewSpot.S = An.S;
			NewSpot.Module = An.Module;
		}
	}
	for (TArray<FTNFaunaSpot>& List : SpotsOf)
	{
		List.Sort([](const FTNFaunaSpot& SpotA, const FTNFaunaSpot& SpotB) { return SpotA.S < SpotB.S; });
	}

	// ── Colocación inicial: por cada módulo del camino, grupos de las especies de su bioma ──
	TArray<int32> RouteModules;
	for (const TNProcMap::FPathSample& Smp : Layout.Main)
	{
		if (Layout.Modules.IsValidIndex(Smp.Module)) { RouteModules.AddUnique(Smp.Module); }
	}
	if (bCustom)
	{
		for (const FTNFaunaAnchor& An : Anchors) { RouteModules.AddUnique(An.Module); }
	}
	// El bioma de cada módulo: el de su mapa generado o, en un mapa propio, el de sus anclas.
	auto ModuleBiome = [&](int32 Mod)
	{
		if (!bCustom) { return Layout.Modules[Mod].Biome; }
		for (const FTNFaunaAnchor& An : Anchors) { if (An.Module == Mod) { return An.Biome; } }
		return ETNProcBiome::Beach;
	};
	int32 Wanted = 0;
	for (const int32 Mod : RouteModules) { Wanted += Tables[TNProcMap::BiomeIndex(ModuleBiome(Mod))].PerModule; }
	const double WantScaled = FMath::Max(1.0, Wanted * static_cast<double>(Density));
	const double PerModuleScale = FMath::Min(1.0, static_cast<double>(MaxAnimals) / WantScaled) * Density;
	TArray<int32> Pool;
	for (const int32 Mod : RouteModules)
	{
		const FTNFaunaBiomeTable& Table = Tables[TNProcMap::BiomeIndex(ModuleBiome(Mod))];
		int32 Left = FMath::RoundToInt32(Table.PerModule * PerModuleScale);
		for (int32 Guard = 0; Left > 0 && Guard < 24 && Animals.Num() < MaxAnimals; ++Guard)
		{
			// Especie por peso entre las que tienen sitio en este módulo (las de agua, solo si hay agua).
			float Weights[FTNFaunaBiomeTable::MaxEntries] = {};
			float WeightSum = 0.f;
			for (int32 e = 0; e < Table.Num; ++e)
			{
				const TArray<FTNFaunaSpot>& Candidates = SpotsOf[static_cast<int32>(Table.Entries[e].Species)];
				const bool bHere = Candidates.ContainsByPredicate([Mod](const FTNFaunaSpot& Each) { return Each.Module == Mod; });
				Weights[e] = bHere ? Table.Entries[e].Weight : 0.f;
				WeightSum += Weights[e];
			}
			if (WeightSum <= 0.f) { break; }
			float Pick = static_cast<float>(Rng.Unit()) * WeightSum;
			int32 Chosen = INDEX_NONE;
			for (int32 e = 0; e < Table.Num; ++e)
			{
				if (Weights[e] <= 0.f) { continue; }
				Chosen = e;
				Pick -= Weights[e];
				if (Pick <= 0.f) { break; }
			}
			if (Chosen == INDEX_NONE) { break; }
			const ETNFaunaSpecies SpId = Table.Entries[Chosen].Species;
			const FTNFaunaSpec& Sp = TNFaunaSpec(SpId);
			const TArray<FTNFaunaSpot>& Candidates = SpotsOf[static_cast<int32>(SpId)];
			Pool.Reset();
			for (int32 k = 0; k < Candidates.Num(); ++k)
			{
				if (Candidates[k].Module == Mod) { Pool.Add(k); }
			}
			if (Pool.Num() == 0) { continue; }
			const FTNFaunaSpot& Center = Candidates[Pool[Rng.RangeInt(0, Pool.Num() - 1)]];
			const int32 GroupSize = FMath::Max(1, FMath::Min(Left, Rng.RangeInt(Sp.GroupMin, Sp.GroupMax)));
			for (int32 g = 0; g < GroupSize && Animals.Num() < MaxAnimals; ++g)
			{
				FVector Where = Center.Pos;
				if (g > 0)
				{
					// Alrededor del primero; si ahí no vale (agua, roca), pegado a él.
					const double Ang = Rng.Range(0.0, TNProcMap::TwoPi);
					const double Dist = Sp.GroupSpread * FMath::Sqrt(Rng.Unit());
					const FVector Probe(Center.Pos.X + FMath::Cos(Ang) * Dist, Center.Pos.Y + FMath::Sin(Ang) * Dist, Center.Pos.Z);
					const float GroundH = GroundAt(Probe);
					if (HabitatOk(static_cast<uint8>(SpId), GroundH))
					{
						const float SurfaceZ = Sp.Habitat == ETNFaunaHabitat::Water ? WaterZ
							: (Sp.Gait == ETNFaunaGait::Hover ? GroundH + static_cast<float>(Rng.Range(260.0, 480.0)) : GroundH);
						Where = FVector(Probe.X, Probe.Y, SurfaceZ);
					}
					else
					{
						Where = Center.Pos + FVector(FMath::Cos(Ang) * 45.0 * g, FMath::Sin(Ang) * 45.0 * g, 0.0);
					}
				}
				FTNFaunaAnimal& A = Animals.AddDefaulted_GetRef();
				A.Species = static_cast<uint8>(SpId);
				A.Pos = Where;
				A.Home = Where;
				A.HomeS = Center.S;
				A.Yaw = static_cast<float>(Rng.Range(-180.0, 180.0));
				A.Size = static_cast<float>(Rng.Range(Sp.ScaleMin, Sp.ScaleMax));
				A.Clock = static_cast<float>(Rng.Range(0.0, 20.0));
				A.SideSign = static_cast<int8>(Rng.Chance(0.5) ? 1 : -1);
				A.State = static_cast<uint8>(ETNFaunaState::Idle);
				A.Dur = static_cast<float>(Rng.Range(0.5, 4.0));
				A.Timer = static_cast<float>(Rng.Range(0.5, 6.0));
				A.Presence = 1.f;
			}
			Left -= GroupSize;
		}
	}
	if (Animals.Num() == 0)
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Fauna] Ningún sitio válido junto al camino: sin fauna."));
		return;
	}

	// ── Especies presentes: piezas, mallas e ISM ──
	int32 KindOf[TNFaunaNumSpecies];
	for (int32& KindIdx : KindOf) { KindIdx = INDEX_NONE; }
	for (int32 i = 0; i < Animals.Num(); ++i)
	{
		FTNFaunaAnimal& A = Animals[i];
		int32& KindIdx = KindOf[A.Species];
		if (KindIdx == INDEX_NONE)
		{
			KindIdx = Kinds.Num();
			Kinds.AddDefaulted_GetRef().Species = A.Species;
		}
		FTNFaunaKind& K = Kinds[KindIdx];
		A.Kind = KindIdx;
		A.Slot = K.Members.Num();
		K.Members.Add(i);
	}
	int32 NumTris = 0;
	for (FTNFaunaKind& K : Kinds)
	{
		TArray<FTNFaunaPart> Parts;
		FTNFaunaRig Rig;
		TNFaunaBuildSpecies(static_cast<ETNFaunaSpecies>(K.Species), Parts, Rig);
		K.BodyZ = static_cast<float>(Rig.BodyZ);
		K.HalfLen = static_cast<float>(Rig.HalfLen);
		K.Height = static_cast<float>(Rig.Height);
		K.Draft = static_cast<float>(Rig.Draft);
		K.Spots = MoveTemp(SpotsOf[K.Species]);
		K.FirstPart = PartISMs.Num();
		for (FTNFaunaPart& Part : Parts)
		{
			UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Part.Mesh, Part.bGlow ? GlowMat : SolidMat, false, 0.f, 1.f, 0.f);
			if (!Mesh) { continue; }
			NumTris += Part.Mesh.Tris.Num() / 3;
			TArray<FTransform>& Xf = PartXf.AddDefaulted_GetRef();
			Xf.Reserve(K.Members.Num());
			for (const int32 Member : K.Members)
			{
				Xf.Add(FTransform(FQuat::Identity, Animals[Member].Pos, FVector::ZeroVector));
			}
			UInstancedStaticMeshComponent* PartISM = MakePartISM(Mesh, Xf, Part.bShadow);
			// Malla de arte de la pieza, si la tiene (sin ella, nada cambia); el Tick la mueve con UpdateInstances.
			TNArt::ApplyToInstances(PartISM, TNFaunaSim::TNFaunaArt(static_cast<ETNFaunaSpecies>(K.Species), Part.Bone, Part.bGlow));
			PartISMs.Add(PartISM);
			PartMeshes.Add(Mesh);
			PartBone.Add(static_cast<uint8>(Part.Bone));
			PartPivot.Add(Part.Pivot);
		}
		K.NumParts = PartISMs.Num() - K.FirstPart;
	}

	// ── Efectos: gotas y ondas al saltar o sumergirse, polvo al enterrarse ──
	{
		TNAmbientFX::FEmitterDesc Drops;
		Drops.Shape = TNAmbientFX::EShape::Drop;
		Drops.Color = FLinearColor(0.85f, 0.95f, 1.f);
		Drops.MaxParticles = 48;
		Drops.Rate = 0.f;
		Drops.SpawnRadius = 18.f;
		Drops.Speed = 380.f;
		Drops.SpeedJitter = 0.4f;
		Drops.Spread = 0.55f;
		Drops.Gravity = -980.f;
		Drops.Drag = 0.4f;
		Drops.LifeMin = 0.35f;
		Drops.LifeMax = 0.7f;
		Drops.SizeStart = 7.f;
		Drops.SizeEnd = 3.f;
		Drops.WakeDistance = WakeRadius + 3000.f;
		SplashFX = TNAmbientFX::AddEmitter(this, Drops, GetActorLocation());

		TNAmbientFX::FEmitterDesc Ripples;
		Ripples.Shape = TNAmbientFX::EShape::Ring;
		Ripples.bSoft = true;
		Ripples.Color = FLinearColor(0.93f, 0.98f, 1.f);
		Ripples.Alpha = 0.5f;
		Ripples.MaxParticles = 16;
		Ripples.Rate = 0.f;
		Ripples.SpawnRadius = 5.f;
		Ripples.Direction = -FVector::UpVector;
		Ripples.Speed = 3.f;
		Ripples.SpeedJitter = 0.f;
		Ripples.Spread = 0.f;
		Ripples.Gravity = -3.f;
		Ripples.Drag = 0.f;
		Ripples.LifeMin = 1.2f;
		Ripples.LifeMax = 1.5f;
		Ripples.SizeStart = 25.f;
		Ripples.SizeEnd = 140.f;
		Ripples.WakeDistance = WakeRadius + 3000.f;
		RingFX = TNAmbientFX::AddEmitter(this, Ripples, GetActorLocation());

		TNAmbientFX::FEmitterDesc Dust;
		Dust.Shape = TNAmbientFX::EShape::Puff;
		Dust.bSoft = true;
		Dust.bCloud = true;
		Dust.Color = FLinearColor(0.8f, 0.72f, 0.58f);
		Dust.Alpha = 0.5f;
		Dust.MaxParticles = 36;
		Dust.Rate = 0.f;
		Dust.SpawnRadius = 14.f;
		Dust.SpawnHeight = 5.f;
		Dust.Speed = 110.f;
		Dust.Spread = 1.f;
		Dust.Gravity = 0.f;
		Dust.Buoyancy = 25.f;
		Dust.Drag = 1.6f;
		Dust.LifeMin = 0.5f;
		Dust.LifeMax = 0.9f;
		Dust.SizeStart = 10.f;
		Dust.SizeEnd = 26.f;
		Dust.WakeDistance = WakeRadius + 3000.f;
		DustFX = TNAmbientFX::AddEmitter(this, Dust, GetActorLocation());
	}

	UE_LOG(LogTortunabo, Log, TEXT("[Fauna] %d animales de %d especies en %d módulos · %d piezas (%d triángulos) · %.1f ms."),
		Animals.Num(), Kinds.Num(), RouteModules.Num(), PartISMs.Num(), NumTris, (FPlatformTime::Seconds() - T0) * 1000.0);
	SetActorTickEnabled(true);
}

UInstancedStaticMeshComponent* ATN_ProcFauna::MakePartISM(UStaticMesh* Mesh, const TArray<FTransform>& Initial, bool bCastShadow)
{
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	ISM->SetStaticMesh(Mesh);
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ISM->SetCanEverAffectNavigation(false);
	ISM->SetGenerateOverlapEvents(false);
	ISM->SetCastShadow(bCastShadow);
	ISM->bEvaluateWorldPositionOffset = false;
	ISM->bAffectDistanceFieldLighting = false;
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetupAttachment(SceneRoot);
	ISM->RegisterComponent();
	// Transformadas de las instancias en mundo: el componente se queda en el origen.
	ISM->SetAbsolute(true, true, true);
	ISM->SetWorldTransform(FTransform::Identity);
	const int32 CullEnd = FMath::RoundToInt(WakeRadius + 2000.f);
	ISM->SetCullDistances(CullEnd - 1000, CullEnd);
	ISM->AddInstances(Initial, false, false);
	ISM->SetVisibility(false);
	return ISM;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcFauna::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TRACE_CPUPROFILER_EVENT_SCOPE(TNFauna_Tick);
	const double TickStart = FPlatformTime::Seconds();
	const ATN_ProcMapGenerator* Gen = GeneratorRef.Get();
	if (!Gen && !bCustom)
	{
		ClearFauna();
		SetActorTickEnabled(false);
		return;
	}
	if (bPendingBuild)
	{
		if (bCustom || Gen->IsMapReady()) { BuildFauna(); }
		return;
	}
	if (!bBuilt)
	{
		return;
	}
	if (!bCustom && (!Gen->IsMapReady() || Gen->GetBuiltGeneration() != BuiltForGeneration))
	{
		// El generador ha regenerado sin destruir este actor: se reconstruye cuando el mapa nuevo esté listo.
		ClearFauna();
		bPendingBuild = true;
		return;
	}
	if (Animals.Num() == 0)
	{
		return;
	}

	const float Dt = FMath::Min(DeltaTime, 0.1f);
	GatherViewsAndThreats();
	if (bCustom && Custom.WaterZNow) { WaterZ = Custom.WaterZNow(); }
	ProgressTimer -= Dt;
	if (ProgressTimer <= 0.f && ViewLocs.Num() > 0)
	{
		ViewProgress = bCustom ? 0.f : Gen->GetPathProgress(ViewLocs[0]);
		ProgressTimer = 0.5f;
	}

	// Se simulan (y se ven) solo los que están cerca de alguna cámara local; el resto, a escala 0.
	const bool bEnabled = TNFaunaSim::FaunaEnabled != 0 && ViewLocs.Num() > 0;
	const float WakeSq = FMath::Square(WakeRadius);
	const float SleepSq = FMath::Square(WakeRadius + TNFaunaSim::SleepMargin);
	NumAwake = 0;
	for (int32 i = 0; i < Animals.Num(); ++i)
	{
		FTNFaunaAnimal& A = Animals[i];
		FTNFaunaKind& K = Kinds[A.Kind];
		const float ViewSq = bEnabled ? MinViewDistSq(A.Pos) : TNumericLimits<float>::Max();
		const bool bWake = bEnabled && ViewSq < (A.bAwake ? SleepSq : WakeSq);
		if (bWake != A.bAwake)
		{
			A.bAwake = bWake;
			const TNFauna::ETNFaunaState St = static_cast<TNFauna::ETNFaunaState>(A.State);
			if (bWake)
			{
				// Aparece poco a poco (los peces siguen bajo el agua hasta que saltan).
				if (St != TNFauna::ETNFaunaState::Hidden) { A.Presence = 0.f; }
			}
			else if (St == TNFauna::ETNFaunaState::Flee)
			{
				HideAnimal(A);
			}
			else if (St != TNFauna::ETNFaunaState::Hidden && St != TNFauna::ETNFaunaState::Idle && St != TNFauna::ETNFaunaState::Walk)
			{
				// Llegando, saliendo, en alerta o en pleno salto: se deja tranquilo en su sitio.
				A.Pos = A.Home;
				A.Vel = FVector::ZeroVector;
				A.Sink = 0.f;
				A.Air = 0.f;
				A.Pitch = 0.f;
				A.bAirborne = false;
				A.bPanic = false;
				StartIdle(A);
			}
		}
		if (!A.bAwake)
		{
			if (A.bShown) { WriteHidden(A, K); }
			if (A.State == static_cast<uint8>(TNFauna::ETNFaunaState::Hidden)) { SimHidden(A, K, Dt); }
			continue;
		}
		++NumAwake;
		// El agua que sube (TcT): los de tierra se esconden cuando les llega y reaparecen en otro sitio seco.
		if (bCustom && A.State != static_cast<uint8>(TNFauna::ETNFaunaState::Hidden) && IsFloodedLand(A)) { HideAnimal(A); }
		SimulateAnimal(A, K, Dt);
		WriteAnimal(A, K);
	}
	RecycleBehind();

	// Subida a las ISM: solo el tramo de instancias que ha cambiado; las especies sin nada a la vista, ocultas.
	for (FTNFaunaKind& K : Kinds)
	{
		if (K.DirtyMax >= K.DirtyMin)
		{
			const int32 First = K.DirtyMin;
			const int32 Count = K.DirtyMax - K.DirtyMin + 1;
			for (int32 p = 0; p < K.NumParts; ++p)
			{
				const int32 Part = K.FirstPart + p;
				UInstancedStaticMeshComponent* ISM = PartISMs[Part];
				if (!ISM) { continue; }
				const TArrayView<const FTransform> Range(PartXf[Part].GetData() + First, Count);
				TNArt::UpdateInstances(ISM, First, Range, false, false, false);
			}
			K.DirtyMin = MAX_int32;
			K.DirtyMax = -1;
		}
		const bool bVisible = K.Shown > 0;
		if (bVisible != K.bVisible)
		{
			K.bVisible = bVisible;
			for (int32 p = 0; p < K.NumParts; ++p)
			{
				if (UInstancedStaticMeshComponent* ISM = PartISMs[K.FirstPart + p]) { ISM->SetVisibility(bVisible); }
			}
		}
	}
	TNAmbientFX::TickOwner(this, Dt);

	if (TNFaunaSim::FaunaStats != 0)
	{
		StatsSeconds += FPlatformTime::Seconds() - TickStart;
		++StatsFrames;
		StatsTimer += DeltaTime;
		if (StatsTimer >= 2.f)
		{
			int32 NumHidden = 0;
			int32 NumShown = 0;
			for (const FTNFaunaAnimal& Each : Animals)
			{
				if (Each.State == static_cast<uint8>(TNFauna::ETNFaunaState::Hidden)) { ++NumHidden; }
				if (Each.bShown) { ++NumShown; }
			}
			int32 KindsShown = 0;
			for (const FTNFaunaKind& Each : Kinds) { KindsShown += Each.bVisible ? 1 : 0; }
			UE_LOG(LogTortunabo, Log, TEXT("[Fauna] %d animales · %d despiertos · %d a la vista · %d escondidos · %d/%d especies visibles · Tick medio %.3f ms"),
				Animals.Num(), NumAwake, NumShown, NumHidden, KindsShown, Kinds.Num(), StatsFrames > 0 ? StatsSeconds * 1000.0 / StatsFrames : 0.0);
			StatsTimer = 0.f;
			StatsSeconds = 0.0;
			StatsFrames = 0;
		}
	}
}

void ATN_ProcFauna::GatherViewsAndThreats()
{
	ViewLocs.Reset();
	ViewDirs.Reset();
	ThreatLocs.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Peligro: cualquier pawn de jugador (los remotos también: su PlayerState replica el pawn).
	TArray<const APawn*, TInlineAllocator<16>> Seen;
	auto AddThreat = [this, &Seen](const APawn* ThreatPawn)
	{
		if (!ThreatPawn || Seen.Contains(ThreatPawn) || ThreatPawn->IsA<ASpectatorPawn>() || ThreatPawn->IsHidden()) { return; }
		Seen.Add(ThreatPawn);
		ThreatLocs.Add(ThreatPawn->GetActorLocation());
	};
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController()) { continue; }
		if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
		{
			ViewLocs.Add(Cam->GetCameraLocation());
			ViewDirs.Add(Cam->GetCameraRotation().Vector());
		}
		AddThreat(PC->GetPawn());
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			AddThreat(PS ? PS->GetPawn() : nullptr);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Simulación
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcFauna::SimulateAnimal(FTNFaunaAnimal& A, FTNFaunaKind& K, float Dt)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	A.Clock += Dt;
	A.StateT += Dt;
	FVector ThreatLoc = FVector::ZeroVector;
	float ThreatSq = TNumericLimits<float>::Max();
	const bool bThreat = NearestThreat(A.Pos, ThreatLoc, ThreatSq);

	// Peces de agua honda: su propia rutina (bajo el agua y saltos en arco).
	if (Sp.Body == ETNFaunaBody::Fish && Sp.Habitat == ETNFaunaHabitat::Water)
	{
		SimFish(A, Dt, bThreat, ThreatSq);
		return;
	}
	switch (static_cast<ETNFaunaState>(A.State))
	{
		case ETNFaunaState::Flee:
			SimFlee(A, Dt, bThreat, ThreatLoc);
			break;
		case ETNFaunaState::Hidden:
			SimHidden(A, K, Dt);
			break;
		case ETNFaunaState::Emerge:
			SimEmerge(A, Dt);
			break;
		case ETNFaunaState::Arrive:
			SimArrive(A, Dt);
			break;
		default:
			SimCalm(A, K, Dt, bThreat, ThreatLoc, ThreatSq);
			break;
	}
	// Se desvanece a lo lejos al acabar la huida; si no, aparece poco a poco (al despertar o al reaparecer).
	if (A.bFading)
	{
		A.Presence = FMath::Max(0.f, A.Presence - Dt * 2.5f);
		if (A.Presence <= 0.f) { HideAnimal(A); }
	}
	else if (A.State != static_cast<uint8>(ETNFaunaState::Hidden))
	{
		A.Presence = FMath::Min(1.f, A.Presence + Dt * 2.5f);
	}
	A.Look = FMath::FInterpTo(A.Look, A.LookGoal, Dt, 7.f);
}

void ATN_ProcFauna::SimCalm(FTNFaunaAnimal& A, FTNFaunaKind& K, float Dt, bool bThreat, const FVector& ThreatLoc, float ThreatSq)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	// Un jugador dentro de su rango: huye.
	if (bThreat && ThreatSq < FMath::Square(Sp.AlarmRange * TNFaunaSim::AlarmScale))
	{
		StartFlee(A, K, ThreatLoc, true);
		return;
	}
	if (Sp.Gait == ETNFaunaGait::Hover)
	{
		SimHover(A, Dt);
		return;
	}
	if (A.bPanic)
	{
		// Su grupo ya huye: espera su turno (unas décimas) y sale detrás.
		A.Timer -= Dt;
		A.Speed = FMath::FInterpTo(A.Speed, 0.f, Dt, 8.f);
		if (A.Timer <= 0.f) { StartFlee(A, K, A.Threat, false); }
		return;
	}
	// Algo se acerca: se para, lo mira y adopta su pose de alerta.
	const bool bWary = bThreat && ThreatSq < FMath::Square(Sp.AlarmRange * TNFaunaSim::AlarmScale * 1.8f);
	if (bWary && A.State != static_cast<uint8>(ETNFaunaState::Alert))
	{
		A.State = static_cast<uint8>(ETNFaunaState::Alert);
		A.StateT = 0.f;
		A.Act = static_cast<uint8>(TNFaunaSim::AlertAct(Sp));
		A.ActT = 0.f;
		A.ActDur = 60.f;
	}
	switch (static_cast<ETNFaunaState>(A.State))
	{
		case ETNFaunaState::Alert:
		{
			A.Speed = FMath::FInterpTo(A.Speed, 0.f, Dt, 8.f);
			A.ActT += Dt;
			if (bThreat)
			{
				const FVector2D ToThreat(ThreatLoc.X - A.Pos.X, ThreatLoc.Y - A.Pos.Y);
				if (!ToThreat.IsNearlyZero())
				{
					A.Yaw = TNFaunaSim::TurnToward(A.Yaw, TNFaunaSim::YawOf(ToThreat), Sp.TurnRate * 0.5f * Dt);
				}
			}
			A.LookGoal = 0.f;
			if (!bWary && A.StateT > 1.2f) { StartIdle(A); }
			break;
		}
		case ETNFaunaState::Walk:
		{
			const FVector2D ToGoal(A.Goal.X - A.Pos.X, A.Goal.Y - A.Pos.Y);
			const float GoalDist = static_cast<float>(ToGoal.Size());
			if (GoalDist < 25.f || A.StateT > A.Dur)
			{
				StartIdle(A);
				break;
			}
			A.Speed = FMath::FInterpTo(A.Speed, Sp.WalkSpeed, Dt, 4.f);
			if (!StepGround(A, ToGoal / GoalDist, A.Speed, Dt, false, false))
			{
				StartIdle(A);
				break;
			}
			A.LookGoal = FMath::Sin(A.Clock * 0.9f) * 15.f;
			break;
		}
		default:
		{
			A.Speed = FMath::FInterpTo(A.Speed, 0.f, Dt, 6.f);
			A.ActT += Dt;
			A.Timer -= Dt;
			if (A.Timer <= 0.f)
			{
				// Mira alrededor a golpes de cabeza; en las demás acciones, apenas.
				const ETNFaunaAct Current = static_cast<ETNFaunaAct>(A.Act);
				const bool bScan = Current == ETNFaunaAct::Look || Current == ETNFaunaAct::Sentinel;
				A.Timer = RandIn(0.5f, 1.3f);
				A.LookGoal = bScan ? RandIn(-75.f, 75.f) : RandIn(-18.f, 18.f);
			}
			if (A.ActT >= A.ActDur)
			{
				A.Act = static_cast<uint8>(TNFaunaSim::PickAct(Sp, RandUnit()));
				A.ActT = 0.f;
				A.ActDur = TNFaunaSim::ActLength(static_cast<ETNFaunaAct>(A.Act), RandUnit());
			}
			if (A.StateT >= A.Dur)
			{
				if (RandUnit() < 0.55f) { StartWalk(A); }
				else { StartIdle(A); }
			}
			break;
		}
	}

	// Saltitos: al moverse los que brincan (ranas, conejo, tucán) o en el sitio (mono inquieto, pez del fango).
	const ETNFaunaState Now = static_cast<ETNFaunaState>(A.State);
	float ActAir = 0.f;
	if ((Now == ETNFaunaState::Idle || Now == ETNFaunaState::Alert) && A.Act == static_cast<uint8>(ETNFaunaAct::Hop))
	{
		const float HopH = Sp.HopHeight > 0.f ? Sp.HopHeight : 10.f;
		ActAir = HopH * TNFaunaSim::ActWeight(A.ActT, A.ActDur) * FMath::Abs(FMath::Sin(A.ActT * UE_TWO_PI * 1.3f));
	}
	if (Sp.Gait == ETNFaunaGait::Hop && A.Speed > 5.f)
	{
		A.HopPh += Dt * A.Speed / FMath::Max(1.f, Sp.Stride);
		A.Air = Sp.HopHeight * FMath::Abs(FMath::Sin(A.HopPh * UE_PI));
	}
	else if (ActAir > 0.f)
	{
		A.Air = ActAir;
	}
	else
	{
		A.Air = FMath::FInterpTo(A.Air, 0.f, Dt, 12.f);
	}
	A.bAirborne = Sp.Body == ETNFaunaBody::Frog && A.Air > 2.f;
	// Alas plegadas en reposo; abiertas al sol en Stretch.
	const bool bStretch = Now == ETNFaunaState::Idle && A.Act == static_cast<uint8>(ETNFaunaAct::Stretch);
	A.Open = FMath::FInterpTo(A.Open, bStretch ? 1.f : 0.f, Dt, 5.f);
}

void ATN_ProcFauna::SimHover(FTNFaunaAnimal& A, float Dt)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	// Revolotea en ochos alrededor de su punto, subiendo y bajando.
	A.bAirborne = true;
	A.Open = 1.f;
	A.FlapPh += Dt * Sp.FlapHz;
	const float Orbit = A.Clock * (1.2f + 0.4f * FMath::Sin(A.Clock * 0.37f));
	const FVector Target = A.Home + FVector(FMath::Cos(Orbit) * 220.f, FMath::Sin(Orbit * 1.3f) * 160.f, FMath::Sin(A.Clock * 2.1f) * 70.f);
	const FVector Want = ((Target - A.Pos) * 2.2f).GetClampedToMaxSize(Sp.WalkSpeed * 2.f);
	A.Vel = FMath::VInterpTo(A.Vel, Want, Dt, 4.f);
	A.Pos += A.Vel * Dt;
	const FVector2D Flat(A.Vel.X, A.Vel.Y);
	if (!Flat.IsNearlyZero(1.0))
	{
		A.Yaw = TNFaunaSim::TurnToward(A.Yaw, TNFaunaSim::YawOf(Flat), 540.f * Dt);
	}
	A.Pitch = FMath::Clamp(TNFaunaSim::PitchOf(A.Vel.Z, Flat.Size()), -20.f, 25.f);
	A.Speed = static_cast<float>(Flat.Size());
}

void ATN_ProcFauna::SimFlee(FTNFaunaAnimal& A, float Dt, bool bThreat, const FVector& ThreatLoc)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	if (bThreat) { A.Threat = ThreatLoc; }
	const ETNFaunaPhase Phase = static_cast<ETNFaunaPhase>(A.Phase);
	const ETNFaunaFlee UsedFlee = static_cast<ETNFaunaFlee>(A.FleeKind);
	switch (Phase)
	{
		case ETNFaunaPhase::Run:
		case ETNFaunaPhase::Dash:
		case ETNFaunaPhase::Climb:
		case ETNFaunaPhase::ToWater:
		{
			// Rumbo: lejos del peligro (hacia arriba si trepa, hacia el agua si se zambulle), revisado cada 0,3 s.
			A.Timer -= Dt;
			if (A.Timer <= 0.f)
			{
				A.Timer = 0.3f;
				if (Phase == ETNFaunaPhase::ToWater)
				{
					const FVector2D ToWater(A.Goal.X - A.Pos.X, A.Goal.Y - A.Pos.Y);
					if (!ToWater.IsNearlyZero()) { A.FleeDir = ToWater.GetSafeNormal(); }
				}
				else
				{
					FVector2D Away(A.Pos.X - A.Threat.X, A.Pos.Y - A.Threat.Y);
					Away = Away.IsNearlyZero() ? A.FleeDir : (Away.GetSafeNormal() + A.FleeDir * 0.6).GetSafeNormal();
					float Gain = 0.f;
					A.FleeDir = ChooseFleeDir(A, Away.IsNearlyZero() ? A.FleeDir : Away, Phase == ETNFaunaPhase::Climb, Gain);
				}
			}
			const bool bRunUp = Phase == ETNFaunaPhase::Dash && UsedFlee == ETNFaunaFlee::Fly;
			A.Speed = FMath::FInterpTo(A.Speed, Sp.FleeSpeed * TNFaunaSim::FleeScale * (bRunUp ? 0.45f : 1.f), Dt, 6.f);
			if (!StepGround(A, A.FleeDir, A.Speed, Dt, Phase == ETNFaunaPhase::Climb, Phase == ETNFaunaPhase::ToWater))
			{
				A.Timer = 0.f;
				A.Speed *= 0.5f;
			}
			// Saltitos: la cabra trepa a brincos y los que brincan huyen brincando.
			if (Sp.HopHeight > 0.f && (Phase == ETNFaunaPhase::Climb || Sp.Gait == ETNFaunaGait::Hop))
			{
				A.HopPh += Dt * A.Speed / FMath::Max(1.f, Sp.Stride * 1.5f);
				A.Air = Sp.HopHeight * FMath::Abs(FMath::Sin(A.HopPh * UE_PI));
				A.bAirborne = Sp.Body == ETNFaunaBody::Frog;
			}
			else
			{
				A.Air = FMath::FInterpTo(A.Air, 0.f, Dt, 12.f);
			}
			// La gallina corre aleteando; los que despegan, abren las alas en la carrera.
			const float WingGoal = (Sp.Body == ETNFaunaBody::Bird && UsedFlee == ETNFaunaFlee::Run) ? 0.7f : (bRunUp ? 0.5f : 0.f);
			A.Open = FMath::FInterpTo(A.Open, WingGoal, Dt, 8.f);
			if (A.Open > 0.05f) { A.FlapPh += Dt * Sp.FlapHz; }

			const float ThreatDistSq = static_cast<float>(FVector::DistSquared2D(A.Pos, A.Threat));
			if (Phase == ETNFaunaPhase::Dash && A.StateT >= Sp.Dash)
			{
				A.Timer = 0.f;
				if (UsedFlee == ETNFaunaFlee::Fly)
				{
					A.Phase = static_cast<uint8>(ETNFaunaPhase::TakeOff);
				}
				else
				{
					A.Phase = static_cast<uint8>(ETNFaunaPhase::Dig);
					BurstFX(DustFX, A.Pos + FVector(0.f, 0.f, 5.f), 7);
				}
			}
			else if (Phase == ETNFaunaPhase::ToWater)
			{
				if (GroundAt(A.Pos) < WaterZ - 15.f)
				{
					A.Timer = 0.f;
					A.Phase = static_cast<uint8>(ETNFaunaPhase::Sink);
					BurstFX(SplashFX, FVector(A.Pos.X, A.Pos.Y, WaterZ + 4.f), 8);
					BurstFX(RingFX, FVector(A.Pos.X, A.Pos.Y, WaterZ + 3.f), 1);
				}
				else if (A.StateT > 5.f)
				{
					A.Timer = 0.f;
					A.Phase = static_cast<uint8>(ETNFaunaPhase::Dig);
					BurstFX(DustFX, A.Pos + FVector(0.f, 0.f, 5.f), 7);
				}
			}
			else if (Phase == ETNFaunaPhase::Climb)
			{
				// A salvo en lo alto (o bien lejos y arriba): se queda mirando y el mono se burla dando saltitos.
				const float Climbed = static_cast<float>(A.Pos.Z) - A.StartZ;
				const bool bFar = ThreatDistSq > FMath::Square(Sp.AlarmRange * TNFaunaSim::AlarmScale * 1.6f);
				if ((Climbed > 450.f && FMath::Abs(A.Pitch) < 25.f) || (Climbed > 250.f && bFar && A.StateT > 1.2f))
				{
					A.Home = A.Pos;
					A.Speed = 0.f;
					A.Air = 0.f;
					A.bAirborne = false;
					StartIdle(A);
					ETNFaunaAct Taunt = ETNFaunaAct::Look;
					if ((Sp.Acts & TNFaunaActBit(ETNFaunaAct::Hop)) != 0) { Taunt = ETNFaunaAct::Hop; }
					else if ((Sp.Acts & TNFaunaActBit(ETNFaunaAct::PushUp)) != 0) { Taunt = ETNFaunaAct::PushUp; }
					else if ((Sp.Acts & TNFaunaActBit(ETNFaunaAct::Sit)) != 0) { Taunt = ETNFaunaAct::Sit; }
					A.Act = static_cast<uint8>(Taunt);
					A.ActDur = TNFaunaSim::ActLength(Taunt, RandUnit());
					return;
				}
				if (A.StateT > 7.f) { A.bFading = true; }
			}
			else if (Phase == ETNFaunaPhase::Run)
			{
				// Corre hasta perderse lejos y se desvanece.
				if (A.StateT > 1.2f && (ThreatDistSq > FMath::Square(3500.f) || A.StateT > 4.5f)) { A.bFading = true; }
			}
			break;
		}
		case ETNFaunaPhase::Dig:
		{
			// Se entierra sacudiéndose (la pose tiembla) y desaparece bajo el suelo.
			A.Speed = 0.f;
			A.Air = FMath::FInterpTo(A.Air, 0.f, Dt, 12.f);
			A.Timer += Dt;
			A.Sink = FMath::Min(1.f, A.Timer / 0.5f);
			if (A.Sink >= 1.f) { HideAnimal(A); }
			break;
		}
		case ETNFaunaPhase::Sink:
		{
			// Se hunde en el agua sin dejar de avanzar.
			A.Timer += Dt;
			A.Sink = FMath::Min(1.f, A.Timer / 0.7f);
			A.Pos += FVector(A.FleeDir * (Sp.FleeSpeed * 0.3f * Dt), 0.0);
			if (A.Sink >= 1.f) { HideAnimal(A); }
			break;
		}
		case ETNFaunaPhase::TakeOff:
		{
			// Abre las alas, se agacha y salta hacia arriba.
			A.Open = FMath::FInterpTo(A.Open, 1.f, Dt, 14.f);
			A.FlapPh += Dt * Sp.FlapHz * 1.5f;
			A.Timer += Dt;
			if (A.Timer >= 0.15f || Sp.Gait == ETNFaunaGait::Hover)
			{
				A.Phase = static_cast<uint8>(ETNFaunaPhase::Fly);
				A.Timer = 0.f;
				A.Vel = FVector(A.FleeDir * (Sp.FleeSpeed * 0.35f), Sp.FleeSpeed * 0.6f);
				A.Pos.Z += A.Air;
				A.Air = 0.f;
				A.bAirborne = true;
			}
			break;
		}
		case ETNFaunaPhase::Fly:
		{
			// Se aleja subiendo hasta ~22 m sobre donde estaba y sigue recto; lejos de la cámara se desvanece.
			A.Timer += Dt;
			const bool bClimbing = A.Pos.Z < A.StartZ + 2200.f;
			const FVector Desired(A.FleeDir * (Sp.FleeSpeed * TNFaunaSim::FleeScale), bClimbing ? Sp.FleeSpeed * TNFaunaSim::FleeScale * 0.45f : 0.f);
			A.Vel = FMath::VInterpTo(A.Vel, Desired, Dt, 2.5f);
			A.Pos += A.Vel * Dt;
			const float Floor = GroundAt(A.Pos) + 150.f;
			if (A.Pos.Z < Floor)
			{
				A.Pos.Z = Floor;
				A.Vel.Z = FMath::Max(A.Vel.Z, static_cast<double>(Sp.FleeSpeed) * 0.3);
			}
			const FVector2D Flat(A.Vel.X, A.Vel.Y);
			if (!Flat.IsNearlyZero(1.0))
			{
				A.Yaw = TNFaunaSim::TurnToward(A.Yaw, TNFaunaSim::YawOf(Flat), 360.f * Dt);
			}
			A.Pitch = FMath::FInterpTo(A.Pitch, FMath::Clamp(TNFaunaSim::PitchOf(A.Vel.Z, Flat.Size()), -25.f, 35.f), Dt, 6.f);
			A.Open = FMath::FInterpTo(A.Open, 1.f, Dt, 10.f);
			A.FlapPh += Dt * Sp.FlapHz * (bClimbing ? 1.f : 0.7f);
			A.bAirborne = true;
			if (A.Timer > 4.5f || MinViewDistSq(A.Pos) > FMath::Square(WakeRadius)) { A.bFading = true; }
			break;
		}
		default:
			HideAnimal(A);
			break;
	}
}

void ATN_ProcFauna::SimHidden(FTNFaunaAnimal& A, FTNFaunaKind& K, float Dt)
{
	using namespace TNFauna;
	A.Timer -= Dt;
	if (A.Timer > 0.f)
	{
		return;
	}
	// Si no encuentra sitio, reintenta al rato.
	A.Timer = 3.f;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	// Los que se metieron bajo tierra se asoman en su sitio si el peligro se ha ido (y hay alguien cerca para verlo).
	if (static_cast<ETNFaunaFlee>(A.FleeKind) == ETNFaunaFlee::Burrow && A.bAwake && RandUnit() < 0.6f)
	{
		FVector ThreatLoc = FVector::ZeroVector;
		float ThreatSq = TNumericLimits<float>::Max();
		if (!NearestThreat(A.Pos, ThreatLoc, ThreatSq) || ThreatSq > FMath::Square(Sp.AlarmRange * TNFaunaSim::AlarmScale * 1.6f))
		{
			A.Pos.Z = GroundAt(A.Pos);
			A.Home = A.Pos;
			A.State = static_cast<uint8>(ETNFaunaState::Emerge);
			A.Phase = static_cast<uint8>(ETNFaunaPhase::Rise);
			A.StateT = 0.f;
			A.Timer = 0.f;
			A.Sink = 1.f;
			A.Presence = 1.f;
			BurstFX(DustFX, A.Pos + FVector(0.f, 0.f, 5.f), 5);
			return;
		}
	}
	Respawn(A, K, false);
}

void ATN_ProcFauna::SimEmerge(FTNFaunaAnimal& A, float Dt)
{
	using namespace TNFauna;
	A.Timer += Dt;
	if (A.Phase == static_cast<uint8>(ETNFaunaPhase::Rise))
	{
		// Sale de la tierra o del agua sacudiéndose.
		A.Sink = FMath::Max(0.f, 1.f - A.Timer / 0.6f);
		if (A.Sink <= 0.f) { StartIdle(A); }
	}
	else if (A.Presence >= 1.f)
	{
		StartIdle(A);
	}
}

void ATN_ProcFauna::SimArrive(FTNFaunaAnimal& A, float Dt)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	// Llega volando desde lejos, frena aleteando y se posa en su sitio.
	const FVector ToHome = A.Home - A.Pos;
	const float HomeDist = static_cast<float>(ToHome.Size());
	A.bAirborne = true;
	A.Open = FMath::FInterpTo(A.Open, 1.f, Dt, 8.f);
	A.FlapPh += Dt * Sp.FlapHz * (HomeDist < 500.f ? 1.4f : 0.8f);
	if (HomeDist < 30.f || A.StateT > 8.f)
	{
		A.Pos = A.Home;
		A.Vel = FVector::ZeroVector;
		A.Pitch = 0.f;
		A.Air = 0.f;
		A.bAirborne = Sp.Gait == ETNFaunaGait::Hover;
		StartIdle(A);
		return;
	}
	const float Cruise = FMath::Min(Sp.FleeSpeed * 0.55f, HomeDist * 1.6f + 60.f);
	A.Vel = FMath::VInterpTo(A.Vel, ToHome / HomeDist * Cruise, Dt, 3.f);
	A.Pos += A.Vel * Dt;
	// Lejos de su sitio no atraviesa el terreno.
	if (HomeDist > 400.f)
	{
		const float Floor = GroundAt(A.Pos) + 200.f;
		if (A.Pos.Z < Floor) { A.Pos.Z = Floor; }
	}
	const FVector2D Flat(A.Vel.X, A.Vel.Y);
	if (!Flat.IsNearlyZero(1.0))
	{
		A.Yaw = TNFaunaSim::TurnToward(A.Yaw, TNFaunaSim::YawOf(Flat), 360.f * Dt);
	}
	A.Pitch = FMath::Clamp(TNFaunaSim::PitchOf(A.Vel.Z, Flat.Size()), -25.f, 20.f);
}

void ATN_ProcFauna::SimFish(FTNFaunaAnimal& A, float Dt, bool bThreat, float ThreatSq)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	if (A.State == static_cast<uint8>(ETNFaunaState::Jump))
	{
		// Arco fuera del agua: el morro sigue a la velocidad y la cola bate deprisa.
		A.Vel.Z -= TNFaunaSim::FishGravity * Dt;
		A.Pos += A.Vel * Dt;
		const FVector2D Flat(A.Vel.X, A.Vel.Y);
		A.Pitch = TNFaunaSim::PitchOf(A.Vel.Z, Flat.Size());
		A.bAirborne = true;
		A.Presence = 1.f;
		if (A.Vel.Z < 0.0 && A.Pos.Z < WaterZ - 25.f)
		{
			BurstFX(SplashFX, FVector(A.Pos.X, A.Pos.Y, WaterZ + 4.f), 10);
			BurstFX(RingFX, FVector(A.Pos.X, A.Pos.Y, WaterZ + 3.f), 1);
			A.State = static_cast<uint8>(ETNFaunaState::Idle);
			A.StateT = 0.f;
			A.bAirborne = false;
			A.Presence = 0.f;
			A.Pitch = 0.f;
			A.Timer = RandIn(2.5f, 7.f);
			A.Pos = FVector(A.Pos.X, A.Pos.Y, WaterZ);
		}
		return;
	}
	// Bajo el agua (invisible): salta cada pocos segundos si no hay nadie cerca.
	A.State = static_cast<uint8>(ETNFaunaState::Idle);
	A.Presence = 0.f;
	A.Timer -= Dt;
	if (bThreat && ThreatSq < FMath::Square(Sp.AlarmRange * TNFaunaSim::AlarmScale))
	{
		A.Timer = FMath::Max(A.Timer, 2.f);
		return;
	}
	if (A.Timer > 0.f)
	{
		return;
	}
	FVector Start(A.Home.X, A.Home.Y, WaterZ);
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		const FVector2D Off = TNFaunaSim::DirOfYaw(RandIn(0.f, 360.f)) * RandIn(0.f, Sp.WanderRadius);
		const FVector Probe(A.Home.X + Off.X, A.Home.Y + Off.Y, WaterZ);
		if (GroundAt(Probe) < WaterZ - 80.f)
		{
			Start = Probe;
			break;
		}
	}
	const FVector2D Dir = TNFaunaSim::DirOfYaw(RandIn(0.f, 360.f));
	A.Pos = FVector(Start.X, Start.Y, WaterZ - 20.f);
	A.Vel = FVector(Dir * RandIn(180.f, 330.f), RandIn(480.f, 650.f));
	A.Yaw = TNFaunaSim::YawOf(Dir);
	A.State = static_cast<uint8>(ETNFaunaState::Jump);
	A.StateT = 0.f;
	A.Presence = 1.f;
	BurstFX(SplashFX, FVector(Start.X, Start.Y, WaterZ + 4.f), 8);
}

void ATN_ProcFauna::StartIdle(FTNFaunaAnimal& A)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	A.State = static_cast<uint8>(ETNFaunaState::Idle);
	A.StateT = 0.f;
	A.Phase = 0;
	A.bPanic = false;
	A.bFading = false;
	A.Dur = RandIn(2.5f, 6.5f);
	const ETNFaunaAct Act = TNFaunaSim::PickAct(Sp, RandUnit());
	A.Act = static_cast<uint8>(Act);
	A.ActT = 0.f;
	A.ActDur = TNFaunaSim::ActLength(Act, RandUnit());
	// Las acciones largas (a una pata, sentado, vigilando) no se cortan a medias.
	if (Act == ETNFaunaAct::OneLeg || Act == ETNFaunaAct::Sit || Act == ETNFaunaAct::Sentinel || Act == ETNFaunaAct::Graze)
	{
		A.Dur = FMath::Max(A.Dur, A.ActDur);
	}
}

void ATN_ProcFauna::StartWalk(FTNFaunaAnimal& A)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		const FVector2D Off = TNFaunaSim::DirOfYaw(RandIn(0.f, 360.f)) * (RandIn(0.3f, 1.f) * Sp.WanderRadius);
		const FVector Target(A.Home.X + Off.X, A.Home.Y + Off.Y, A.Home.Z);
		if (!HabitatOk(A.Species, GroundAt(Target))) { continue; }
		A.Goal = Target;
		A.State = static_cast<uint8>(ETNFaunaState::Walk);
		A.StateT = 0.f;
		A.Act = static_cast<uint8>(ETNFaunaAct::None);
		A.ActT = 0.f;
		const float PathLen = static_cast<float>(FVector::Dist2D(Target, A.Pos));
		A.Dur = PathLen / FMath::Max(1.f, Sp.WalkSpeed) * 1.6f + 1.5f;
		if (Sp.Gait == ETNFaunaGait::Side)
		{
			A.SideSign = TNFaunaSim::CrabSide(A.Yaw, FVector2D(Target.X - A.Pos.X, Target.Y - A.Pos.Y));
		}
		return;
	}
	StartIdle(A);
}

void ATN_ProcFauna::StartFlee(FTNFaunaAnimal& A, FTNFaunaKind& K, const FVector& From, bool bSpread)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	A.Threat = From;
	A.State = static_cast<uint8>(ETNFaunaState::Flee);
	A.StateT = 0.f;
	A.Act = static_cast<uint8>(ETNFaunaAct::None);
	A.ActT = 0.f;
	A.bPanic = false;
	A.bFading = false;
	A.StartZ = static_cast<float>(A.Pos.Z);
	A.Timer = 0.3f;
	A.Sink = 0.f;
	FVector2D Away(A.Pos.X - From.X, A.Pos.Y - From.Y);
	Away = Away.IsNearlyZero() ? TNFaunaSim::DirOfYaw(A.Yaw) : Away.GetSafeNormal();
	Away = Away.GetRotated(RandIn(-25.f, 25.f));
	A.FleeDir = Away;

	// Modo de huida, con su plan B: sin agua cerca no se zambulle y sin pared a mano no trepa.
	ETNFaunaFlee Mode = Sp.Flee;
	float Gain = 0.f;
	if (Mode == ETNFaunaFlee::Dive)
	{
		if (Sp.Gait == ETNFaunaGait::Swim || GroundAt(A.Pos) < WaterZ - 30.f)
		{
			A.Phase = static_cast<uint8>(ETNFaunaPhase::Sink);
			A.Timer = 0.f;
			BurstFX(SplashFX, FVector(A.Pos.X, A.Pos.Y, WaterZ + 4.f), 8);
			BurstFX(RingFX, FVector(A.Pos.X, A.Pos.Y, WaterZ + 3.f), 1);
		}
		else if (FindWater(A, Away, A.Goal))
		{
			A.Phase = static_cast<uint8>(ETNFaunaPhase::ToWater);
			A.FleeDir = FVector2D(A.Goal.X - A.Pos.X, A.Goal.Y - A.Pos.Y).GetSafeNormal();
		}
		else
		{
			Mode = Sp.Fallback;
		}
	}
	if (Mode == ETNFaunaFlee::Climb)
	{
		const FVector2D ClimbDir = ChooseFleeDir(A, Away, true, Gain);
		if (Gain > 80.f)
		{
			A.Phase = static_cast<uint8>(ETNFaunaPhase::Climb);
			A.FleeDir = ClimbDir;
		}
		else
		{
			Mode = Sp.Fallback == ETNFaunaFlee::Climb ? ETNFaunaFlee::Run : Sp.Fallback;
		}
	}
	switch (Mode)
	{
		case ETNFaunaFlee::Burrow:
			if (Sp.Dash > 0.f)
			{
				A.Phase = static_cast<uint8>(ETNFaunaPhase::Dash);
				A.FleeDir = ChooseFleeDir(A, Away, false, Gain);
			}
			else
			{
				A.Phase = static_cast<uint8>(ETNFaunaPhase::Dig);
				A.Timer = 0.f;
				BurstFX(DustFX, A.Pos + FVector(0.f, 0.f, 5.f), 7);
			}
			break;
		case ETNFaunaFlee::Fly:
			A.Phase = static_cast<uint8>(Sp.Dash > 0.f && Sp.Gait != ETNFaunaGait::Hover ? ETNFaunaPhase::Dash : ETNFaunaPhase::TakeOff);
			A.Timer = A.Phase == static_cast<uint8>(ETNFaunaPhase::Dash) ? 0.3f : 0.f;
			break;
		case ETNFaunaFlee::Run:
			A.Phase = static_cast<uint8>(ETNFaunaPhase::Run);
			A.FleeDir = ChooseFleeDir(A, Away, false, Gain);
			break;
		default:
			// Dive y Climb ya tienen su fase.
			break;
	}
	A.FleeKind = static_cast<uint8>(Mode);
	if (Sp.Gait == ETNFaunaGait::Side)
	{
		A.SideSign = TNFaunaSim::CrabSide(A.Yaw, A.FleeDir);
	}

	// El susto se contagia: los de su especie que estén cerca huyen detrás, cada uno con su retraso.
	if (bSpread)
	{
		for (const int32 Member : K.Members)
		{
			FTNFaunaAnimal& Other = Animals[Member];
			if (&Other == &A || !Other.bAwake || Other.bPanic) { continue; }
			const ETNFaunaState OtherState = static_cast<ETNFaunaState>(Other.State);
			if (OtherState != ETNFaunaState::Idle && OtherState != ETNFaunaState::Walk && OtherState != ETNFaunaState::Alert) { continue; }
			if (FVector::DistSquared(Other.Pos, A.Pos) > FMath::Square(TNFaunaSim::GroupAlarmRadius)) { continue; }
			Other.State = static_cast<uint8>(ETNFaunaState::Alert);
			Other.StateT = 0.f;
			Other.bPanic = true;
			Other.Timer = RandIn(0.05f, 0.4f);
			Other.Threat = From;
		}
	}
}

void ATN_ProcFauna::HideAnimal(FTNFaunaAnimal& A)
{
	A.State = static_cast<uint8>(TNFauna::ETNFaunaState::Hidden);
	A.StateT = 0.f;
	A.Timer = RandIn(TNFaunaSim::HideMin, TNFaunaSim::HideMax);
	A.Presence = 0.f;
	A.Sink = 0.f;
	A.Air = 0.f;
	A.Speed = 0.f;
	A.Vel = FVector::ZeroVector;
	A.Open = 0.f;
	A.Pitch = 0.f;
	A.Roll = 0.f;
	A.bFading = false;
	A.bAirborne = false;
	A.bPanic = false;
}

bool ATN_ProcFauna::Respawn(FTNFaunaAnimal& A, FTNFaunaKind& K, bool bFarOnly)
{
	using namespace TNFauna;
	if (K.Spots.Num() == 0 || ViewLocs.Num() == 0)
	{
		return false;
	}
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	// Ventana de sitios alrededor de la cámara por el camino (sobre todo por delante); con bFarOnly, más allá
	// de donde se ve (se recicla dormido, sin que nadie lo vea aparecer).
	auto LowerBound = [&K](float Progress)
	{
		int32 Low = 0;
		int32 High = K.Spots.Num();
		while (Low < High)
		{
			const int32 Mid = (Low + High) / 2;
			if (K.Spots[Mid].S < Progress) { Low = Mid + 1; }
			else { High = Mid; }
		}
		return Low;
	};
	const int32 First = LowerBound(ViewProgress + (bFarOnly ? 6000.f : -4000.f));
	const int32 Last = LowerBound(ViewProgress + (bFarOnly ? 22000.f : 15000.f));
	if (Last <= First)
	{
		return false;
	}
	const ETNFaunaFlee UsedFlee = static_cast<ETNFaunaFlee>(A.FleeKind);
	const bool bFish = Sp.Body == ETNFaunaBody::Fish && Sp.Habitat == ETNFaunaHabitat::Water;
	const bool bFlier = Sp.Flee == ETNFaunaFlee::Fly;
	// Los que salen de la tierra o del agua, o llegan volando, pueden aparecer a la vista; los demás, no.
	const bool bShowsUp = bFish || bFlier || UsedFlee == ETNFaunaFlee::Burrow || Sp.Habitat == ETNFaunaHabitat::Water;
	const float InViewCos = FMath::Cos(FMath::DegreesToRadians(55.f));
	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		const int32 Idx = First + FMath::Min(Last - First - 1, static_cast<int32>(RandUnit() * (Last - First)));
		const FTNFaunaSpot& Spot = K.Spots[Idx];
		if (bCustom && !HabitatOk(A.Species, GroundAt(Spot.Pos))) { continue; }
		const float ViewSq = MinViewDistSq(Spot.Pos);
		if (bFarOnly ? ViewSq < FMath::Square(WakeRadius + 600.f) : ViewSq < FMath::Square(TNFaunaSim::RespawnNear)) { continue; }
		FVector ThreatLoc = FVector::ZeroVector;
		float ThreatSq = TNumericLimits<float>::Max();
		if (NearestThreat(Spot.Pos, ThreatLoc, ThreatSq) && ThreatSq < FMath::Square(Sp.AlarmRange * TNFaunaSim::AlarmScale * 2.f)) { continue; }
		if (!bFarOnly && !bShowsUp && ViewSq < FMath::Square(WakeRadius))
		{
			bool bInView = false;
			for (int32 v = 0; v < ViewLocs.Num() && v < ViewDirs.Num(); ++v)
			{
				const FVector ToSpot = (Spot.Pos - ViewLocs[v]).GetSafeNormal();
				if (FVector::DotProduct(ToSpot, ViewDirs[v]) > InViewCos) { bInView = true; break; }
			}
			if (bInView) { continue; }
		}

		// Sitio nuevo: su casa y su progreso por el camino.
		A.Pos = Spot.Pos;
		A.Home = Spot.Pos;
		A.HomeS = Spot.S;
		A.Yaw = RandIn(-180.f, 180.f);
		A.Pitch = 0.f;
		A.Roll = 0.f;
		A.Speed = 0.f;
		A.Vel = FVector::ZeroVector;
		A.Air = 0.f;
		A.Sink = 0.f;
		A.Open = 0.f;
		A.Look = 0.f;
		A.LookGoal = 0.f;
		A.bFading = false;
		A.bAirborne = false;
		A.bPanic = false;
		if (bFarOnly || bFish)
		{
			StartIdle(A);
			A.Presence = bFish ? 0.f : 1.f;
			A.Timer = RandIn(1.f, 4.f);
			return true;
		}
		A.StateT = 0.f;
		A.Timer = 0.f;
		if (bFlier)
		{
			// Llega volando desde lejos (por el lado contrario a la cámara) y se posa.
			FVector2D FromView(Spot.Pos.X - ViewLocs[0].X, Spot.Pos.Y - ViewLocs[0].Y);
			FromView = FromView.IsNearlyZero() ? TNFaunaSim::DirOfYaw(A.Yaw) : FromView.GetSafeNormal();
			A.Pos = Spot.Pos + FVector(FromView * 2600.0, 1400.0);
			A.Pos.Z = FMath::Max(A.Pos.Z, static_cast<double>(GroundAt(A.Pos)) + 600.0);
			A.Vel = (A.Home - A.Pos).GetSafeNormal() * (Sp.FleeSpeed * 0.5f);
			A.Yaw = TNFaunaSim::YawOf(-FromView);
			A.State = static_cast<uint8>(ETNFaunaState::Arrive);
			A.Open = 1.f;
			A.bAirborne = true;
			A.Presence = 0.f;
		}
		else if (UsedFlee == ETNFaunaFlee::Burrow || Sp.Flee == ETNFaunaFlee::Burrow || Sp.Habitat == ETNFaunaHabitat::Water)
		{
			// Sale de la tierra (o del agua) sacudiéndose.
			A.State = static_cast<uint8>(ETNFaunaState::Emerge);
			A.Phase = static_cast<uint8>(ETNFaunaPhase::Rise);
			A.Sink = 1.f;
			A.Presence = 1.f;
			if (Sp.Habitat == ETNFaunaHabitat::Water) { BurstFX(RingFX, FVector(A.Pos.X, A.Pos.Y, WaterZ + 3.f), 1); }
			else { BurstFX(DustFX, A.Pos + FVector(0.f, 0.f, 5.f), 5); }
		}
		else
		{
			// Aparece poco a poco fuera del encuadre.
			A.State = static_cast<uint8>(ETNFaunaState::Emerge);
			A.Phase = static_cast<uint8>(ETNFaunaPhase::Fade);
			A.Presence = 0.f;
		}
		return true;
	}
	return false;
}

void ATN_ProcFauna::RecycleBehind()
{
	using namespace TNFauna;
	if (ViewLocs.Num() == 0 || Animals.Num() == 0)
	{
		return;
	}
	// Unos pocos por fotograma: los dormidos que se han quedado atrás se mudan a un sitio por delante de la
	// cámara que aún no se ve. Así los animales acompañan a los jugadores por el camino.
	const float FarSq = FMath::Square(WakeRadius + 1500.f);
	for (int32 n = 0; n < TNFaunaSim::SweepPerFrame; ++n)
	{
		SweepCursor = (SweepCursor + 1) % Animals.Num();
		FTNFaunaAnimal& A = Animals[SweepCursor];
		if (A.bAwake) { continue; }
		const ETNFaunaState St = static_cast<ETNFaunaState>(A.State);
		if (St != ETNFaunaState::Idle && St != ETNFaunaState::Walk) { continue; }
		if (A.HomeS > ViewProgress - TNFaunaSim::BehindDistance) { continue; }
		if (MinViewDistSq(A.Pos) < FarSq) { continue; }
		Respawn(A, Kinds[A.Kind], true);
	}
}

bool ATN_ProcFauna::StepGround(FTNFaunaAnimal& A, const FVector2D& Dir, float MoveSpeed, float Dt, bool bClimb, bool bAnyGround)
{
	using namespace TNFauna;
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	const bool bSide = Sp.Gait == ETNFaunaGait::Side;
	// Gira hacia el rumbo (los cangrejos, de lado) y avanza hacia donde mira: los giros hacen curva.
	const float Heading = TNFaunaSim::YawOf(Dir);
	const float FaceYaw = bSide ? Heading - 90.f * A.SideSign : Heading;
	const float TurnBoost = MoveSpeed > Sp.WalkSpeed * 1.5f ? 2.5f : 1.f;
	A.Yaw = TNFaunaSim::TurnToward(A.Yaw, FaceYaw, Sp.TurnRate * TurnBoost * Dt);
	const float MoveYaw = bSide ? A.Yaw + 90.f * A.SideSign : A.Yaw;
	const FVector2D Fwd = TNFaunaSim::DirOfYaw(MoveYaw);
	const float Misalign = FMath::Abs(FMath::FindDeltaAngleDegrees(MoveYaw, Heading));
	const float Step = MoveSpeed * Dt * (Misalign > 70.f ? 0.35f : 1.f);
	if (Step <= UE_KINDA_SMALL_NUMBER)
	{
		return true;
	}
	FVector Next(A.Pos.X + Fwd.X * Step, A.Pos.Y + Fwd.Y * Step, A.Pos.Z);
	float GroundH = GroundAt(Next);
	if (Sp.Gait == ETNFaunaGait::Swim)
	{
		// Nada por la superficie: solo donde el agua es honda.
		if (GroundH > WaterZ - 60.f) { return false; }
		A.Pos = FVector(Next.X, Next.Y, WaterZ);
		A.Gait += Step / FMath::Max(1.f, Sp.Stride);
		return true;
	}
	float Rise = GroundH - static_cast<float>(A.Pos.Z);
	// Sin trepar no sube paredes ni se tira por los cortados.
	if (!bClimb && (Rise > Step * 1.0f + 0.5f || Rise < -Step * 1.5f - 0.5f)) { return false; }
	if (!bAnyGround && !HabitatOk(A.Species, GroundH)) { return false; }
	if (bClimb && FMath::Abs(Rise) > Step * 0.5f)
	{
		// Trepa a velocidad constante por la superficie: en la pared avanza poco en planta y mucho en altura.
		const float Along = Step * Step / FMath::Sqrt(Step * Step + Rise * Rise);
		Next = FVector(A.Pos.X + Fwd.X * Along, A.Pos.Y + Fwd.Y * Along, A.Pos.Z);
		GroundH = GroundAt(Next);
		Rise = GroundH - static_cast<float>(A.Pos.Z);
	}
	const float Horiz = static_cast<float>(FVector::Dist2D(Next, A.Pos));
	const float Slope = FMath::RadiansToDegrees(FMath::Atan2(Rise, FMath::Max(0.1f, Horiz)));
	const float MaxTilt = bClimb ? 80.f : (Sp.Body == ETNFaunaBody::Bird ? 12.f : 32.f);
	if (bSide)
	{
		// Con Roll positivo baja su derecha: el lado hacia el que anda cuesta arriba queda alto.
		A.Roll = FMath::FInterpTo(A.Roll, FMath::Clamp(-Slope * A.SideSign, -MaxTilt, MaxTilt), Dt, 8.f);
		A.Pitch = FMath::FInterpTo(A.Pitch, 0.f, Dt, 8.f);
	}
	else
	{
		A.Pitch = FMath::FInterpTo(A.Pitch, FMath::Clamp(Slope, -MaxTilt, MaxTilt), Dt, 10.f);
		A.Roll = FMath::FInterpTo(A.Roll, 0.f, Dt, 8.f);
	}
	A.Gait += FMath::Sqrt(Horiz * Horiz + Rise * Rise) / FMath::Max(1.f, Sp.Stride);
	A.Pos = FVector(Next.X, Next.Y, GroundH);
	return true;
}

FVector2D ATN_ProcFauna::ChooseFleeDir(const FTNFaunaAnimal& A, const FVector2D& Away, bool bClimb, float& OutGain) const
{
	using namespace TNFauna;
	// Tantea el suelo a 4,5 m en abanico: al huir corriendo busca lo más llano y seco; al trepar, lo más empinado hacia arriba.
	static const float Offsets[] = { 0.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 125.f, -125.f };
	constexpr float Probe = 450.f;
	FVector2D Best = Away;
	float BestScore = -1.0e9f;
	OutGain = 0.f;
	for (const float Offset : Offsets)
	{
		const FVector2D Cand = Away.GetRotated(Offset);
		const FVector P(A.Pos.X + Cand.X * Probe, A.Pos.Y + Cand.Y * Probe, A.Pos.Z);
		const float GroundH = GroundAt(P);
		const float Rise = GroundH - static_cast<float>(A.Pos.Z);
		float Score = -FMath::Abs(Offset) / 90.f;
		if (!HabitatOk(A.Species, GroundH)) { Score -= 10.f; }
		if (bClimb)
		{
			Score += Rise / Probe * 2.5f;
		}
		else
		{
			Score -= FMath::Abs(Rise) / Probe * 3.f;
			if (Rise > Probe || Rise < -Probe * 1.5f) { Score -= 5.f; }
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Cand;
			OutGain = Rise;
		}
	}
	return Best;
}

bool ATN_ProcFauna::FindWater(const FTNFaunaAnimal& A, const FVector2D& Away, FVector& OutGoal) const
{
	// Busca agua honda en anillos cada vez más anchos; entre los rumbos con agua, el que más se aleja del peligro.
	static const float Radii[] = { 500.f, 1100.f, 1800.f, 2600.f };
	for (const float Radius : Radii)
	{
		float BestDot = -2.f;
		bool bAny = false;
		for (int32 k = 0; k < 12; ++k)
		{
			const FVector2D Cand = TNFaunaSim::DirOfYaw(k * 30.f);
			const FVector P(A.Pos.X + Cand.X * Radius, A.Pos.Y + Cand.Y * Radius, A.Pos.Z);
			if (GroundAt(P) >= WaterZ - 30.f) { continue; }
			const float Dot = static_cast<float>(FVector2D::DotProduct(Cand, Away));
			if (Dot > BestDot)
			{
				BestDot = Dot;
				OutGoal = FVector(P.X, P.Y, WaterZ);
				bAny = true;
			}
		}
		if (bAny) { return true; }
	}
	return false;
}

bool ATN_ProcFauna::IsFloodedLand(const FTNFaunaAnimal& A) const
{
	using namespace TNFauna;
	const ETNFaunaHabitat Habitat = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species)).Habitat;
	return (Habitat == ETNFaunaHabitat::Land || Habitat == ETNFaunaHabitat::Shore) && GroundAt(A.Pos) < WaterZ - 20.f;
}

bool ATN_ProcFauna::HabitatOk(uint8 InSpecies, float GroundH) const
{
	using namespace TNFauna;
	switch (TNFaunaSpec(static_cast<ETNFaunaSpecies>(InSpecies)).Habitat)
	{
		case ETNFaunaHabitat::Shore:    return GroundH >= WaterZ - 20.f && GroundH <= WaterZ + 220.f;
		case ETNFaunaHabitat::Shallows: return GroundH >= WaterZ - 70.f && GroundH <= WaterZ + 30.f;
		case ETNFaunaHabitat::Water:    return GroundH <= WaterZ - 90.f;
		case ETNFaunaHabitat::Land:
		default:                        return GroundH >= WaterZ + 25.f;
	}
}

float ATN_ProcFauna::GroundAt(const FVector& P) const
{
	const ATN_ProcMapGenerator* Gen = GeneratorRef.Get();
	if (bCustom && Custom.HeightAt) { return Custom.HeightAt(P); }
	return Gen ? Gen->GetTerrainHeightAt(P) : static_cast<float>(P.Z);
}

float ATN_ProcFauna::MinViewDistSq(const FVector& P) const
{
	float Best = TNumericLimits<float>::Max();
	for (const FVector& View : ViewLocs)
	{
		Best = FMath::Min(Best, static_cast<float>(FVector::DistSquared(P, View)));
	}
	return Best;
}

bool ATN_ProcFauna::NearestThreat(const FVector& P, FVector& OutLoc, float& OutDistSq) const
{
	bool bAny = false;
	OutDistSq = TNumericLimits<float>::Max();
	for (const FVector& Loc : ThreatLocs)
	{
		const float DistSq = static_cast<float>(FVector::DistSquared(P, Loc));
		if (DistSq < OutDistSq)
		{
			OutDistSq = DistSq;
			OutLoc = Loc;
			bAny = true;
		}
	}
	return bAny;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dibujo
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcFauna::WriteHidden(FTNFaunaAnimal& A, FTNFaunaKind& K)
{
	const FTransform Gone(FQuat::Identity, A.Pos, FVector::ZeroVector);
	for (int32 p = 0; p < K.NumParts; ++p)
	{
		PartXf[K.FirstPart + p][A.Slot] = Gone;
	}
	if (A.bShown)
	{
		A.bShown = false;
		--K.Shown;
	}
	K.DirtyMin = FMath::Min(K.DirtyMin, A.Slot);
	K.DirtyMax = FMath::Max(K.DirtyMax, A.Slot);
}

void ATN_ProcFauna::WriteAnimal(FTNFaunaAnimal& A, FTNFaunaKind& K)
{
	using namespace TNFauna;
	if (A.State == static_cast<uint8>(ETNFaunaState::Hidden) || A.Presence <= 0.01f)
	{
		if (A.bShown) { WriteHidden(A, K); }
		return;
	}
	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	FTransform Bones[FaunaBoneCount];
	PoseBones(K, A, Bones);
	// Raíz: posición, orientación (con la pendiente) y escala de aparición; se hunde al enterrarse y al flotar.
	const float Grow = A.Size * FMath::SmoothStep(0.f, 1.f, A.Presence);
	const bool bFloat = Sp.Gait == ETNFaunaGait::Swim && Sp.Body != ETNFaunaBody::Fish && !A.bAirborne;
	const float Lift = (A.Air - A.Sink * K.Height * 1.15f - (bFloat ? K.Draft : 0.f)) * A.Size;
	const FTransform Root(FRotator(A.Pitch, A.Yaw, A.Roll), A.Pos + FVector(0.0, 0.0, Lift), FVector(Grow));
	const FTransform& BodyBone = Bones[static_cast<int32>(ETNFaunaBone::Body)];
	const FTransform BodyFrame = FTransform(BodyBone.GetRotation(), FVector(0.0, 0.0, K.BodyZ) + BodyBone.GetTranslation()) * Root;
	for (int32 p = 0; p < K.NumParts; ++p)
	{
		const int32 Part = K.FirstPart + p;
		const int32 BoneIdx = FMath::Clamp(static_cast<int32>(PartBone[Part]), 0, FaunaBoneCount - 1);
		FTransform& Dst = PartXf[Part][A.Slot];
		if (BoneIdx == static_cast<int32>(ETNFaunaBone::Body))
		{
			// El cuerpo lleva su propia escala (la garganta de la rana); sus hijos cuelgan del marco sin ella.
			Dst = FTransform(FQuat::Identity, FVector::ZeroVector, BodyBone.GetScale3D()) * BodyFrame;
		}
		else
		{
			const FTransform& Local = Bones[BoneIdx];
			Dst = FTransform(Local.GetRotation(), PartPivot[Part] + Local.GetTranslation(), Local.GetScale3D()) * BodyFrame;
		}
	}
	if (!A.bShown)
	{
		A.bShown = true;
		++K.Shown;
	}
	K.DirtyMin = FMath::Min(K.DirtyMin, A.Slot);
	K.DirtyMax = FMath::Max(K.DirtyMax, A.Slot);
}

void ATN_ProcFauna::PoseBones(const FTNFaunaKind& K, const FTNFaunaAnimal& A, FTransform* OutBones)
{
	using namespace TNFauna;
	static_assert(FaunaBoneCount == static_cast<int32>(ETNFaunaBone::Count), "FaunaBoneCount debe coincidir con ETNFaunaBone::Count");
	for (int32 b = 0; b < FaunaBoneCount; ++b) { OutBones[b] = FTransform::Identity; }
	auto PoseRot = [OutBones](ETNFaunaBone WhichBone, const FRotator& BoneRot, const FVector& BoneOff, const FVector& BoneScale)
	{
		OutBones[static_cast<int32>(WhichBone)] = FTransform(BoneRot, BoneOff, BoneScale);
	};
	auto PoseQuat = [OutBones](ETNFaunaBone WhichBone, const FQuat& BoneQuat, const FVector& BoneScale)
	{
		OutBones[static_cast<int32>(WhichBone)] = FTransform(BoneQuat, FVector::ZeroVector, BoneScale);
	};
	const FVector NoOff = FVector::ZeroVector;
	const FVector Unit = FVector::OneVector;

	const FTNFaunaSpec& Sp = TNFaunaSpec(static_cast<ETNFaunaSpecies>(A.Species));
	const ETNFaunaState St = static_cast<ETNFaunaState>(A.State);
	const ETNFaunaAct Act = static_cast<ETNFaunaAct>(A.Act);
	const bool bAlert = St == ETNFaunaState::Alert;
	const bool bCalm = St == ETNFaunaState::Idle || bAlert;
	const float ActW = bCalm ? TNFaunaSim::ActWeight(A.ActT, A.ActDur) : 0.f;
	const float Tm = A.Clock;
	const float Ph = A.Gait * UE_TWO_PI;
	const float Loco = TNFaunaSim::LocoOf(Sp, A.Speed);
	const float Move = FMath::Min(1.f, Loco * 2.f);
	const bool bDig = St == ETNFaunaState::Flee && A.Phase == static_cast<uint8>(ETNFaunaPhase::Dig);
	const bool bRise = St == ETNFaunaState::Emerge && A.Phase == static_cast<uint8>(ETNFaunaPhase::Rise);

	FRotator BodyRot = FRotator::ZeroRotator;
	FVector BodyOff = FVector::ZeroVector;
	FVector BodyScale = FVector::OneVector;
	// Al enterrarse (o al salir) tiembla entero.
	if (bDig || bRise)
	{
		BodyRot.Roll += FMath::Sin(Tm * 44.f) * (bDig ? 12.f : 6.f);
		BodyRot.Yaw += FMath::Sin(Tm * 37.f) * (bDig ? 8.f : 4.f);
	}

	switch (Sp.Body)
	{
		case ETNFaunaBody::Bird:
		{
			const bool bFloat = Sp.Gait == ETNFaunaGait::Swim && !A.bAirborne;
			const float Swing = FMath::Sin(Ph);
			if (!A.bAirborne && !bFloat)
			{
				// Contoneo al andar.
				BodyOff.Z += FMath::Abs(Swing) * 1.2f * Loco;
				BodyRot.Roll += Swing * 5.f * Loco;
			}
			if (bFloat)
			{
				// Mecido por el agua.
				BodyOff.Z += FMath::Sin(Tm * 1.7f) * 1.2f;
				BodyRot.Roll += FMath::Sin(Tm * 1.1f) * 3.f;
			}
			// Cabeza: cabeceo al andar, picotazos (el flamenco la mete en el agua), acicalarse, alerta erguida.
			float HeadPitch = A.bAirborne ? 12.f : FMath::Sin(Ph * 2.f) * 6.f * Loco;
			float HeadYaw = A.Look;
			float HeadRoll = 0.f;
			const float HeadFwd = A.bAirborne ? 0.f : FMath::Sin(Ph * 2.f) * 1.5f * Loco;
			if (Act == ETNFaunaAct::Peck)
			{
				const float Dip = Sp.bLongNeck ? FMath::Clamp(FMath::Sin(A.ActT * 1.6f) * 1.6f, 0.f, 1.f) : FMath::Max(0.f, FMath::Sin(A.ActT * 9.f));
				HeadPitch -= ActW * (Sp.bLongNeck ? 115.f : 70.f) * Dip;
			}
			else if (Act == ETNFaunaAct::Preen)
			{
				HeadYaw += ActW * 125.f * (FMath::Sin(A.ActT * 0.8f) >= 0.f ? 1.f : -1.f);
				HeadPitch -= ActW * 25.f;
				HeadRoll = ActW * 20.f;
			}
			else if (Act == ETNFaunaAct::Scratch)
			{
				HeadPitch -= ActW * 30.f;
			}
			if (bAlert) { HeadPitch += 8.f; }
			PoseRot(ETNFaunaBone::Head, FRotator(HeadPitch, HeadYaw, HeadRoll), FVector(HeadFwd, 0.f, 0.f), Unit);
			// Patas: pasos alternos (juntas al brincar), recogidas en vuelo, bajo el agua al flotar, una
			// recogida en OneLeg y escarbando en Scratch.
			for (const float Side : { -1.f, 1.f })
			{
				float LegPitch = 0.f;
				FVector LegScale = FVector::OneVector;
				if (A.bAirborne)
				{
					LegPitch = -70.f;
					LegScale.Z = 0.6f;
				}
				else if (bFloat)
				{
					LegScale = FVector(1.f, 1.f, 0.05f);
				}
				else
				{
					if (Sp.Gait == ETNFaunaGait::Hop)
					{
						LegPitch = -25.f * A.Air / FMath::Max(1.f, Sp.HopHeight);
					}
					else
					{
						LegPitch = FMath::Sin(Ph + (Side > 0.f ? UE_PI : 0.f)) * (25.f + 20.f * Loco) * Move;
					}
					if (Act == ETNFaunaAct::OneLeg && Side > 0.f)
					{
						LegPitch = FMath::Lerp(LegPitch, -15.f, ActW);
						LegScale.Z = FMath::Lerp(1.f, 0.35f, ActW);
					}
					if (Act == ETNFaunaAct::Scratch && Side > 0.f)
					{
						LegPitch -= ActW * 45.f * FMath::Max(0.f, FMath::Sin(A.ActT * 10.f));
					}
				}
				PoseRot(Side < 0.f ? ETNFaunaBone::LegBL : ETNFaunaBone::LegBR, FRotator(LegPitch, 0.f, 0.f), NoOff, LegScale);
			}
			// Alas: plegadas hacia atrás contra el costado (y más cortas) o abiertas aleteando.
			float WingOpen = A.Open;
			float Flap = FMath::Sin(A.FlapPh * UE_TWO_PI) * 42.f + 8.f;
			if (Act == ETNFaunaAct::Stretch && St == ETNFaunaState::Idle)
			{
				WingOpen = FMath::Max(WingOpen, ActW);
				Flap = 22.f + FMath::Sin(A.ActT * 3.f) * 14.f;
			}
			const FVector FoldScale(0.8f, 0.5f, 1.f);
			for (const float Side : { -1.f, 1.f })
			{
				// Plegada: la envergadura gira hacia atrás (Z) y el borde de salida cae por el costado (X).
				const FQuat Fold = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(100.f * Side)) * FQuat(FVector::UpVector, FMath::DegreesToRadians(90.f * Side));
				const FQuat Spread(FVector::ForwardVector, FMath::DegreesToRadians(Flap * Side));
				PoseQuat(Side < 0.f ? ETNFaunaBone::WingL : ETNFaunaBone::WingR, FQuat::Slerp(Fold, Spread, WingOpen), FMath::Lerp(FoldScale, Unit, WingOpen));
			}
			break;
		}
		case ETNFaunaBody::Quad:
		case ETNFaunaBody::Lizard:
		{
			const bool bSprawl = Sp.Body == ETNFaunaBody::Lizard;
			const bool bGallop = !bSprawl && Loco > 0.62f;
			const bool bCat = A.Species == static_cast<uint8>(ETNFaunaSpecies::Cat);
			const float Amp = (bSprawl ? 28.f : 22.f) + 22.f * Loco;
			// Sentado o vigilando de pie: el cuerpo gira por la cadera.
			float SitAng = 0.f;
			if (Act == ETNFaunaAct::Sit || (Act == ETNFaunaAct::Scratch && !bSprawl)) { SitAng = 42.f * ActW; }
			if (Act == ETNFaunaAct::Sentinel) { SitAng = 72.f * ActW; }
			float BodyPitch = SitAng;
			if (bGallop) { BodyPitch += FMath::Sin(Ph) * 6.f; }
			if (Act == ETNFaunaAct::PushUp)
			{
				// Flexiones: el pecho sube y baja a golpes.
				BodyPitch += ActW * (0.5f + 0.5f * FMath::Sin(A.ActT * 11.f)) * 14.f;
			}
			BodyOff.Z += FMath::Abs(FMath::Sin(Ph)) * (bGallop ? 3.f : 1.f) * Loco;
			if (bSprawl) { BodyRot.Yaw += FMath::Sin(Ph) * 9.f * Move; }
			BodyRot.Pitch += BodyPitch;
			if (!FMath::IsNearlyZero(BodyPitch))
			{
				const FVector Hip(-K.HalfLen * 0.7f, 0.f, -K.BodyZ * 0.35f);
				BodyOff += Hip - FRotator(BodyPitch, 0.f, 0.f).RotateVector(Hip);
			}
			// Cabeza: mira al frente aunque el cuerpo se levante; pasta, escarba, se lame la mano.
			float HeadPitch = -BodyPitch * 0.85f + FMath::Sin(Ph * 2.f) * 3.f * Loco;
			const float HeadYaw = A.Look - static_cast<float>(BodyRot.Yaw) * 0.6f;
			float HeadRoll = 0.f;
			if (Act == ETNFaunaAct::Graze) { HeadPitch -= ActW * (42.f + FMath::Sin(A.ActT * 9.f) * 4.f); }
			if (Act == ETNFaunaAct::Dig) { HeadPitch -= ActW * 25.f; }
			if (Act == ETNFaunaAct::Scratch)
			{
				HeadRoll = ActW * 18.f;
				if (bCat) { HeadPitch -= ActW * 25.f; }
			}
			PoseRot(ETNFaunaBone::Head, FRotator(HeadPitch, HeadYaw, HeadRoll), NoOff, Unit);
			// Patas: diagonales al paso, por pares al galope; abiertas en abanico los lagartos.
			for (int32 LegIdx = 0; LegIdx < 4; ++LegIdx)
			{
				const bool bFront = LegIdx < 2;
				const float Side = (LegIdx % 2 == 0) ? -1.f : 1.f;
				float LegPhase = (bFront == (Side < 0.f)) ? 0.f : UE_PI;
				if (bGallop) { LegPhase = (bFront ? 0.f : UE_PI) + (Side > 0.f ? 0.35f : 0.f); }
				const float Swing = FMath::Sin(Ph + LegPhase) * Amp * Move;
				float LegPitch = 0.f;
				float LegYaw = 0.f;
				float LegRoll = 0.f;
				if (bSprawl)
				{
					LegYaw = -Side * Swing;
					// Levanta el pie al adelantarlo (Roll positivo baja el lado +Y).
					LegRoll = -Side * FMath::Max(0.f, FMath::Sin(Ph + LegPhase + UE_HALF_PI)) * 18.f * Move;
				}
				else
				{
					LegPitch = Swing;
				}
				// Sentado: las traseras vuelven a la vertical y las delanteras cuelgan delante del pecho.
				LegPitch -= SitAng * (bFront ? 0.55f : 1.f);
				if (!bFront) { LegPitch += 10.f * SitAng / 72.f; }
				if (!bSprawl && bFront && Side < 0.f && Act == ETNFaunaAct::Scratch)
				{
					// El mono se rasca la cabeza; el gato se lame la mano.
					const float Raise = bCat ? 75.f : 150.f + FMath::Sin(A.ActT * 14.f) * 12.f;
					LegPitch = FMath::Lerp(LegPitch, Raise, ActW);
				}
				if (!bSprawl && bFront && Act == ETNFaunaAct::Hop) { LegPitch = FMath::Lerp(LegPitch, 120.f, ActW); }
				if (bFront && Act == ETNFaunaAct::Dig) { LegPitch += ActW * FMath::Sin(A.ActT * 16.f + Side) * 35.f; }
				if (bDig) { LegPitch += FMath::Sin(Tm * 30.f + LegIdx) * 30.f; }
				const ETNFaunaBone LegBone = bFront ? (Side < 0.f ? ETNFaunaBone::LegFL : ETNFaunaBone::LegFR) : (Side < 0.f ? ETNFaunaBone::LegBL : ETNFaunaBone::LegBR);
				PoseRot(LegBone, FRotator(LegPitch, LegYaw, LegRoll), NoOff, Unit);
			}
			// Cola: se mece (y se menea en Wag), serpentea con los lagartos y se queda en el suelo al sentarse.
			const bool bWag = Act == ETNFaunaAct::Wag;
			float TailYaw = FMath::Sin(Tm * (bWag ? 5.f : 1.3f)) * (bWag ? 28.f * FMath::Max(ActW, 0.3f) : 8.f);
			if (bSprawl) { TailYaw -= FMath::Sin(Ph) * 22.f * Move; }
			const float TailPitch = -SitAng * 0.8f + (bSprawl ? 0.f : 12.f * Loco);
			PoseRot(ETNFaunaBone::Tail, FRotator(TailPitch, TailYaw, 0.f), NoOff, Unit);
			break;
		}
		case ETNFaunaBody::Crab:
		{
			// Anda de lado: los dos juegos de patas se alternan (uno sube mientras el otro baja).
			const float Busy = Move + (bDig ? 1.f : 0.f);
			const float Swing = FMath::Sin(Ph);
			BodyOff.Z += FMath::Abs(FMath::Sin(Ph * 2.f)) * 0.8f * Busy;
			BodyRot.Roll += Swing * 3.f * Busy;
			if (Act == ETNFaunaAct::Dig) { BodyRot.Roll += FMath::Sin(A.ActT * 30.f) * 5.f * ActW; }
			const float Twitch = FMath::Sin(Tm * 3.f) * 2.f;
			PoseRot(ETNFaunaBone::LegFL, FRotator(0.f, Swing * 10.f * Busy, Swing * 16.f * Busy + Twitch), NoOff, Unit);
			PoseRot(ETNFaunaBone::LegFR, FRotator(0.f, -Swing * 10.f * Busy, Swing * 16.f * Busy - Twitch), NoOff, Unit);
			// Pinzas: abren y cierran solas; en alto al agitarlas o en alerta (el violinista saluda con la grande).
			const bool bFiddler = A.Species == static_cast<uint8>(ETNFaunaSpecies::FiddlerCrab);
			const float ClawT = bAlert ? A.StateT : A.ActT;
			for (const float Side : { -1.f, 1.f })
			{
				float ClawPitch = FMath::Sin(Tm * 4.f + Side) * 6.f;
				float ClawYaw = 0.f;
				if (Act == ETNFaunaAct::Claws)
				{
					if (bFiddler && Side > 0.f)
					{
						ClawPitch += ActW * (20.f + 45.f * FMath::Max(0.f, FMath::Sin(ClawT * 7.f)));
					}
					else
					{
						ClawPitch += ActW * 40.f;
						ClawYaw = ActW * FMath::Sin(ClawT * 6.f + Side) * 15.f;
					}
				}
				if (Act == ETNFaunaAct::Dig) { ClawPitch -= ActW * 20.f; }
				PoseRot(Side < 0.f ? ETNFaunaBone::ClawL : ETNFaunaBone::ClawR, FRotator(ClawPitch, ClawYaw * Side, 0.f), NoOff, Unit);
			}
			break;
		}
		case ETNFaunaBody::Frog:
		{
			// Sentada un poco erguida; en el salto se estira: patas traseras atrás y delanteras adelante.
			const float HopT = FMath::Frac(A.HopPh);
			const float Kick = A.bAirborne ? FMath::Sin(HopT * UE_PI) : 0.f;
			BodyRot.Pitch += 14.f + (A.bAirborne ? FMath::Lerp(22.f, -15.f, HopT) : 0.f);
			if (Act == ETNFaunaAct::Croak)
			{
				// Infla la garganta.
				const float Puff = ActW * FMath::Max(0.f, FMath::Sin(A.ActT * 14.f));
				BodyScale = FVector(1.f, 1.f + 0.07f * Puff, 1.f + 0.1f * Puff);
			}
			const float Shake = bDig ? FMath::Sin(Tm * 30.f) * 25.f : 0.f;
			PoseRot(ETNFaunaBone::LegBL, FRotator(-70.f * Kick + Shake, 0.f, 0.f), NoOff, Unit);
			PoseRot(ETNFaunaBone::LegBR, FRotator(-70.f * Kick - Shake, 0.f, 0.f), NoOff, Unit);
			PoseRot(ETNFaunaBone::LegFL, FRotator(35.f * Kick - 14.f, 0.f, 0.f), NoOff, Unit);
			PoseRot(ETNFaunaBone::LegFR, FRotator(35.f * Kick - 14.f, 0.f, 0.f), NoOff, Unit);
			break;
		}
		case ETNFaunaBody::Fish:
		{
			// La cola bate (deprisa en el salto); el del fango rema con las aletas y da brincos.
			const bool bSkipper = Sp.Habitat != ETNFaunaHabitat::Water;
			const float TailHz = (A.bAirborne && !bSkipper) ? 22.f : 8.f;
			PoseRot(ETNFaunaBone::Tail, FRotator(0.f, FMath::Sin(Tm * TailHz) * 26.f, 0.f), NoOff, Unit);
			BodyRot.Roll += FMath::Sin(Tm * 3.f) * (bSkipper ? 2.f : 6.f);
			if (bSkipper)
			{
				float Fin = FMath::Sin(Ph) * 25.f * Move;
				if (Act == ETNFaunaAct::Hop) { Fin += ActW * 20.f * FMath::Sin(A.ActT * 9.f); }
				if (bDig) { Fin += FMath::Sin(Tm * 30.f) * 25.f; }
				PoseRot(ETNFaunaBone::LegFL, FRotator(Fin, 0.f, 0.f), NoOff, Unit);
				PoseRot(ETNFaunaBone::LegFR, FRotator(-Fin, 0.f, 0.f), NoOff, Unit);
			}
			break;
		}
		case ETNFaunaBody::Turtle:
		{
			if (Sp.Gait == ETNFaunaGait::Swim)
			{
				// Vuela bajo el agua: aletas delanteras a la vez, traseras de timón, cabeza fuera.
				const float Stroke = FMath::Sin(Tm * 2.6f);
				PoseRot(ETNFaunaBone::LegFL, FRotator(0.f, Stroke * 14.f, -Stroke * 32.f), NoOff, Unit);
				PoseRot(ETNFaunaBone::LegFR, FRotator(0.f, -Stroke * 14.f, Stroke * 32.f), NoOff, Unit);
				const float Rudder = FMath::Sin(Tm * 1.3f) * 14.f;
				PoseRot(ETNFaunaBone::LegBL, FRotator(0.f, Rudder, 0.f), NoOff, Unit);
				PoseRot(ETNFaunaBone::LegBR, FRotator(0.f, -Rudder, 0.f), NoOff, Unit);
				BodyOff.Z += FMath::Sin(Tm * 1.2f) * 1.5f;
				BodyRot.Pitch += FMath::Sin(Tm * 0.9f) * 3.f;
				PoseRot(ETNFaunaBone::Head, FRotator(10.f + FMath::Sin(Tm * 0.7f) * 6.f, A.Look, 0.f), NoOff, Unit);
			}
			else
			{
				// Rema con las aletas por la arena, contoneándose.
				const float Busy = Move + (bDig ? 1.f : 0.f);
				const float Swing = FMath::Sin(Ph);
				const float Lift = FMath::Max(0.f, FMath::Cos(Ph)) * 20.f * Busy;
				PoseRot(ETNFaunaBone::LegFL, FRotator(0.f, Swing * 35.f * Busy, Lift), NoOff, Unit);
				PoseRot(ETNFaunaBone::LegFR, FRotator(0.f, Swing * 35.f * Busy, -Lift), NoOff, Unit);
				PoseRot(ETNFaunaBone::LegBL, FRotator(0.f, -Swing * 20.f * Busy, 0.f), NoOff, Unit);
				PoseRot(ETNFaunaBone::LegBR, FRotator(0.f, -Swing * 20.f * Busy, 0.f), NoOff, Unit);
				BodyRot.Roll += Swing * 6.f * Busy;
				PoseRot(ETNFaunaBone::Head, FRotator(bAlert ? 12.f : 0.f, A.Look, 0.f), NoOff, Unit);
			}
			break;
		}
		case ETNFaunaBody::Beetle:
		{
			const float Busy = Move + (bDig ? 1.f : 0.f);
			const float Swing = FMath::Sin(Ph);
			PoseRot(ETNFaunaBone::LegFL, FRotator(0.f, Swing * 18.f * Busy, Swing * 10.f * Busy), NoOff, Unit);
			PoseRot(ETNFaunaBone::LegFR, FRotator(0.f, Swing * 18.f * Busy, Swing * 10.f * Busy), NoOff, Unit);
			BodyOff.Z += FMath::Abs(FMath::Sin(Ph * 2.f)) * 0.4f * Busy;
			break;
		}
		case ETNFaunaBody::Bat:
		{
			// Aleteo rápido y amplio; el cuerpo sube y baja con cada golpe.
			const float Flap = FMath::Sin(A.FlapPh * UE_TWO_PI);
			const float Beat = FMath::DegreesToRadians(Flap * 55.f + 5.f);
			PoseQuat(ETNFaunaBone::WingL, FQuat(FVector::ForwardVector, -Beat), Unit);
			PoseQuat(ETNFaunaBone::WingR, FQuat(FVector::ForwardVector, Beat), Unit);
			BodyOff.Z -= Flap * 2.f;
			BodyRot.Pitch += 10.f;
			break;
		}
		default:
			break;
	}
	OutBones[static_cast<int32>(ETNFaunaBone::Body)] = FTransform(BodyRot, BodyOff, BodyScale);
}

// ─────────────────────────────────────────────────────────────────────────────
// Utilidades
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcFauna::BurstFX(int32 Emitter, const FVector& Where, int32 Count)
{
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, Emitter))
	{
		E->Origin = Where;
		TNAmbientFX::Burst(*E, Count);
	}
}

float ATN_ProcFauna::RandUnit()
{
	SimRng ^= SimRng << 13;
	SimRng ^= SimRng >> 17;
	SimRng ^= SimRng << 5;
	return static_cast<float>(SimRng & 0xFFFFFFu) / 16777215.f;
}

float ATN_ProcFauna::RandIn(float Lo, float Hi)
{
	return Lo + (Hi - Lo) * RandUnit();
}
