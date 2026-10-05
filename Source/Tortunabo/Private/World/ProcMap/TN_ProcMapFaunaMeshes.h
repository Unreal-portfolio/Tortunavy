#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "TN_ProcMapMeshKit.h"

/**
 * Fauna ambiental de caras planas (solo visual, la anima ATN_ProcFauna): especies por bioma, su ficha
 * (tamaño, velocidades, rango de alarma, modo de huida, acciones de reposo) y sus mallas por PIEZAS.
 * Cada pieza es una malla propia con el pivote en su origen (la cadera de una pata, el hombro de un ala,
 * la base del cuello...), para que el actor la gire en la CPU. Espacio local: X adelante, Y a la
 * derecha, Z arriba, cm a escala 1. La raíz del animal está en el suelo bajo el cuerpo; el pivote del
 * cuerpo está a Rig.BodyZ sobre ella y el de las demás piezas va en el espacio del cuerpo. Sin
 * dependencias del motor más allá de CoreMinimal, como las mallas de la vegetación.
 *
 * Tamaños respecto a la tortuga jugadora (~1,4 m): cangrejo 25-40 cm, mono ~70 cm con la cola, cabra
 * ~1 m a la cruz, flamenco ~1,3 m. Velocidades de huida de 2 a 3 veces la carrera de la tortuga (800 cm/s).
 * Paleta alegre y saturada, legible de lejos; se escribe como sRGB (MakeStaticMesh la decodifica).
 */
namespace TNFauna
{
	using namespace TNProcMesh;

	// ─────────────────────────────────────────────────────────────────────────
	// Tipos
	// ─────────────────────────────────────────────────────────────────────────

	/** Especies de la fauna ambiental. */
	enum class ETNFaunaSpecies : uint8
	{
		Crab,          ///< Cangrejo rojo de playa: anda de lado y se entierra.
		FiddlerCrab,   ///< Cangrejo violinista del manglar: una pinza enorme que agita.
		BabyTurtle,    ///< Cría de tortuga marina: corre al mar.
		SeaTurtle,     ///< Tortuga marina nadando en la superficie: se sumerge.
		Gull,          ///< Gaviota en el suelo.
		Sandpiper,     ///< Correlimos: corretea y echa a volar.
		Toucan,        ///< Tucán posado que da saltitos.
		Heron,         ///< Garza blanca en el agua somera.
		Flamingo,      ///< Flamenco a una pata en el agua somera.
		Pelican,       ///< Pelícano flotando.
		Vulture,       ///< Buitre en el suelo, alas al sol.
		Eagle,         ///< Águila posada en lo alto.
		Pigeon,        ///< Paloma.
		Hen,           ///< Gallina: corre aleteando.
		Roadrunner,    ///< Correcaminos: el más rápido.
		Monkey,        ///< Mono: se rasca, salta y trepa por las paredes.
		Capybara,      ///< Capibara: pasta tranquila.
		Meerkat,       ///< Suricato: vigila de pie y se mete en su madriguera.
		Ibex,          ///< Cabra montés: trepa a saltitos por los acantilados.
		Marmot,        ///< Marmota: vigila de pie y se entierra.
		Cat,           ///< Gato: se sienta, se lame y trepa a los muros.
		Rabbit,        ///< Conejo: salta y corre a su madriguera.
		Lizard,        ///< Lagartija turquesa: flexiones y trepa por las paredes.
		Salamander,    ///< Salamandra de fuego: manchas que brillan, se mete en la roca.
		MarineIguana,  ///< Iguana marina de la roca volcánica: cresta de púas.
		DartFrog,      ///< Rana dardo azul.
		TreeFrog,      ///< Rana verde de ojos rojos.
		Fish,          ///< Pez que salta del agua en arco.
		Mudskipper,    ///< Pez del fango que brinca por el barro.
		FireBeetle,    ///< Escarabajo de fuego con rayas que brillan.
		Bat,           ///< Murciélago que revolotea.
		Count
	};

	constexpr int32 TNFaunaNumSpecies = static_cast<int32>(ETNFaunaSpecies::Count);

	/** Tipo de cuerpo: decide las piezas y la animación. */
	enum class ETNFaunaBody : uint8 { Bird, Quad, Lizard, Crab, Frog, Fish, Turtle, Beetle, Bat };

	/** Cómo huye. */
	enum class ETNFaunaFlee : uint8
	{
		Burrow,  ///< Se entierra (o se mete en la roca) y desaparece.
		Climb,   ///< Trepa por el terreno sin importar la pendiente: las paredes que la tortuga no sube.
		Fly,     ///< Echa a volar aleteando y se aleja subiendo.
		Dive,    ///< Se mete en el agua y desaparece.
		Run      ///< Corre muy rápido y se pierde lejos.
	};

	/** Dónde vive (restricción dura al colocarlo y al moverse). */
	enum class ETNFaunaHabitat : uint8
	{
		Land,      ///< Suelo seco.
		Shore,     ///< Orilla: barro y arena junto al agua.
		Shallows,  ///< Agua somera (patas mojadas).
		Water      ///< Agua honda (nada, flota o salta).
	};

	/** Cómo se desplaza. */
	enum class ETNFaunaGait : uint8 { Walk, Hop, Side, Swim, Hover };

	/** Canales de animación: cada pieza sigue uno (varias piezas pueden compartirlo). */
	enum class ETNFaunaBone : uint8 { Body, Head, LegFL, LegFR, LegBL, LegBR, WingL, WingR, Tail, ClawL, ClawR, Count };

	/** Acciones de reposo (Acts de la ficha es una máscara de bits de estas). */
	enum class ETNFaunaAct : uint8
	{
		None,
		Look,      ///< Mira alrededor a golpes de cabeza.
		Peck,      ///< Picotea o come del suelo (el flamenco, con la cabeza en el agua).
		Graze,     ///< Pasta con la cabeza baja y mastica.
		Scratch,   ///< Se rasca la cabeza (mono), se lame la mano (gato) o escarba (gallina).
		Wag,       ///< Menea la cola.
		Preen,     ///< Se acicala el ala.
		OneLeg,    ///< Descansa a una pata (flamenco, garza).
		PushUp,    ///< Flexiones (lagartos).
		Sentinel,  ///< De pie, vigilando (suricato, marmota, conejo).
		Claws,     ///< Agita las pinzas (cangrejos).
		Sit,       ///< Sentado (mono, gato).
		Hop,       ///< Saltitos en el sitio (mono, pez del fango).
		Stretch,   ///< Abre las alas al sol.
		Croak,     ///< Infla la garganta (ranas).
		Dig,       ///< Escarba con las patas delanteras.
		Count
	};

	constexpr uint32 TNFaunaActBit(ETNFaunaAct Act) { return 1u << static_cast<uint32>(Act); }

	/** Estados de la máquina de estados de cada animal. */
	enum class ETNFaunaState : uint8
	{
		Idle,    ///< Quieto con una acción de reposo.
		Walk,    ///< Paseando hacia un punto de su zona.
		Alert,   ///< Algo se acerca: se para y lo mira (o espera su turno para huir con el grupo).
		Flee,    ///< Huyendo (la fase va en ETNFaunaPhase).
		Hidden,  ///< Escondido o lejos: invisible hasta reaparecer.
		Emerge,  ///< Reapareciendo: sale de la tierra o del agua, o aparece poco a poco.
		Arrive,  ///< Llega volando y se posa (voladores reciclados).
		Jump     ///< Salto de un pez fuera del agua.
	};

	/** Fases de la huida (y de la reaparición). */
	enum class ETNFaunaPhase : uint8
	{
		Run,      ///< Carrera por el suelo lejos del peligro.
		Dash,     ///< Carrera corta antes de enterrarse o despegar.
		Climb,    ///< Subida por la pared más empinada que tenga a mano.
		ToWater,  ///< Carrera hacia el agua.
		Dig,      ///< Se entierra sacudiéndose.
		Sink,     ///< Se hunde en el agua.
		TakeOff,  ///< Abre las alas y se agacha para saltar.
		Fly,      ///< Vuelo de huida.
		Rise,     ///< (Emerge) sale del suelo o del agua.
		Fade      ///< (Emerge) aparece poco a poco.
	};

	/** Ficha de una especie. Velocidades en cm/s, distancias en cm, tiempos en s. */
	struct FTNFaunaSpec
	{
		const TCHAR* Name = TEXT("");
		ETNFaunaBody Body = ETNFaunaBody::Quad;
		ETNFaunaFlee Flee = ETNFaunaFlee::Run;
		/** Plan B cuando el modo no se puede (Dive sin agua cerca, Climb sin pared a mano). */
		ETNFaunaFlee Fallback = ETNFaunaFlee::Run;
		ETNFaunaHabitat Habitat = ETNFaunaHabitat::Land;
		ETNFaunaGait Gait = ETNFaunaGait::Walk;
		/** Largo aproximado del animal (informativo). */
		float Length = 50.f;
		float ScaleMin = 0.9f;
		float ScaleMax = 1.1f;
		float WalkSpeed = 100.f;
		float FleeSpeed = 1800.f;
		/** Si un jugador entra en este radio, huye; a 1,8 veces se pone alerta. */
		float AlarmRange = 800.f;
		float WanderRadius = 500.f;
		/** Avance por ciclo de pasos (o por salto). */
		float Stride = 30.f;
		/** Giro máximo (grados/s). */
		float TurnRate = 360.f;
		int32 GroupMin = 1;
		int32 GroupMax = 3;
		float GroupSpread = 250.f;
		/** Probabilidad de colocarse sobre el propio camino (el resto, fuera, hasta ~40 m del borde). */
		float OnPath = 0.25f;
		/** Carrera previa antes de enterrarse o de despegar. */
		float Dash = 0.f;
		/** Aleteos por segundo en vuelo. */
		float FlapHz = 4.f;
		/** Altura de los saltitos (Gait Hop, o al trepar la cabra). */
		float HopHeight = 0.f;
		/** Acciones de reposo (TNFaunaActBit). */
		uint32 Acts = 0;
		/** Busca las alturas al colocarse (águila, cabra montés). */
		bool bHighGround = false;
		/** Cuello largo: al comer baja la cabeza hasta el suelo o el agua. */
		bool bLongNeck = false;
		/** El cuerpo y la cabeza proyectan sombra (solo los grandes). */
		bool bShadow = false;
	};

	/** Ficha de cada especie. */
	inline FTNFaunaSpec TNFaunaMakeSpec(ETNFaunaSpecies Species)
	{
		using FaunaBody = ETNFaunaBody;
		using FaunaFlee = ETNFaunaFlee;
		using FaunaHab = ETNFaunaHabitat;
		using FaunaGait = ETNFaunaGait;
		using FaunaAct = ETNFaunaAct;
		FTNFaunaSpec Sp;
		switch (Species)
		{
			case ETNFaunaSpecies::Crab:
				Sp.Name = TEXT("Cangrejo"); Sp.Body = FaunaBody::Crab; Sp.Flee = FaunaFlee::Burrow; Sp.Fallback = FaunaFlee::Burrow; Sp.Gait = FaunaGait::Side;
				Sp.Length = 35.f; Sp.ScaleMin = 0.85f; Sp.ScaleMax = 1.15f; Sp.WalkSpeed = 70.f; Sp.FleeSpeed = 1300.f; Sp.AlarmRange = 650.f;
				Sp.WanderRadius = 350.f; Sp.Stride = 12.f; Sp.TurnRate = 540.f; Sp.GroupMin = 2; Sp.GroupMax = 4; Sp.GroupSpread = 200.f;
				Sp.OnPath = 0.35f; Sp.Dash = 0.7f; Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Claws) | TNFaunaActBit(FaunaAct::Dig);
				break;
			case ETNFaunaSpecies::FiddlerCrab:
				Sp.Name = TEXT("Cangrejo violinista"); Sp.Body = FaunaBody::Crab; Sp.Flee = FaunaFlee::Burrow; Sp.Fallback = FaunaFlee::Burrow; Sp.Habitat = FaunaHab::Shore;
				Sp.Gait = FaunaGait::Side; Sp.Length = 22.f; Sp.WalkSpeed = 60.f; Sp.FleeSpeed = 1200.f; Sp.AlarmRange = 550.f; Sp.WanderRadius = 250.f;
				Sp.Stride = 9.f; Sp.TurnRate = 540.f; Sp.GroupMin = 3; Sp.GroupMax = 6; Sp.GroupSpread = 180.f; Sp.OnPath = 0.15f; Sp.Dash = 0.45f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Claws) | TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Dig);
				break;
			case ETNFaunaSpecies::BabyTurtle:
				Sp.Name = TEXT("Tortuguita"); Sp.Body = FaunaBody::Turtle; Sp.Flee = FaunaFlee::Dive; Sp.Fallback = FaunaFlee::Burrow; Sp.Length = 22.f;
				Sp.WalkSpeed = 45.f; Sp.FleeSpeed = 1600.f; Sp.AlarmRange = 600.f; Sp.WanderRadius = 250.f; Sp.Stride = 10.f; Sp.TurnRate = 300.f;
				Sp.GroupMin = 3; Sp.GroupMax = 5; Sp.GroupSpread = 180.f; Sp.OnPath = 0.3f; Sp.Acts = TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::SeaTurtle:
				Sp.Name = TEXT("Tortuga marina"); Sp.Body = FaunaBody::Turtle; Sp.Flee = FaunaFlee::Dive; Sp.Fallback = FaunaFlee::Dive; Sp.Habitat = FaunaHab::Water;
				Sp.Gait = FaunaGait::Swim; Sp.Length = 110.f; Sp.WalkSpeed = 70.f; Sp.FleeSpeed = 900.f; Sp.AlarmRange = 900.f; Sp.WanderRadius = 900.f;
				Sp.Stride = 60.f; Sp.TurnRate = 60.f; Sp.GroupMin = 1; Sp.GroupMax = 2; Sp.GroupSpread = 500.f; Sp.OnPath = 0.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Gull:
				Sp.Name = TEXT("Gaviota"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Length = 45.f; Sp.WalkSpeed = 110.f; Sp.FleeSpeed = 1500.f;
				Sp.AlarmRange = 900.f; Sp.WanderRadius = 500.f; Sp.Stride = 18.f; Sp.GroupMin = 2; Sp.GroupMax = 5; Sp.GroupSpread = 300.f;
				Sp.OnPath = 0.35f; Sp.FlapHz = 3.2f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Peck) | TNFaunaActBit(FaunaAct::Preen) | TNFaunaActBit(FaunaAct::Stretch);
				break;
			case ETNFaunaSpecies::Sandpiper:
				Sp.Name = TEXT("Correlimos"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Length = 25.f; Sp.WalkSpeed = 170.f; Sp.FleeSpeed = 1800.f;
				Sp.AlarmRange = 800.f; Sp.WanderRadius = 450.f; Sp.Stride = 10.f; Sp.TurnRate = 720.f; Sp.GroupMin = 3; Sp.GroupMax = 5;
				Sp.GroupSpread = 220.f; Sp.OnPath = 0.2f; Sp.Dash = 0.7f; Sp.FlapHz = 6.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Peck) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Toucan:
				Sp.Name = TEXT("Tucán"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Gait = FaunaGait::Hop; Sp.Length = 50.f; Sp.WalkSpeed = 80.f;
				Sp.FleeSpeed = 1400.f; Sp.AlarmRange = 800.f; Sp.WanderRadius = 300.f; Sp.Stride = 25.f; Sp.GroupMin = 1; Sp.GroupMax = 3;
				Sp.OnPath = 0.2f; Sp.FlapHz = 5.f; Sp.HopHeight = 12.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Preen) | TNFaunaActBit(FaunaAct::Peck);
				break;
			case ETNFaunaSpecies::Heron:
				Sp.Name = TEXT("Garza"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Habitat = FaunaHab::Shallows; Sp.Length = 100.f; Sp.WalkSpeed = 60.f;
				Sp.FleeSpeed = 1300.f; Sp.AlarmRange = 1100.f; Sp.WanderRadius = 400.f; Sp.Stride = 30.f; Sp.TurnRate = 180.f; Sp.GroupMin = 1;
				Sp.GroupMax = 2; Sp.GroupSpread = 400.f; Sp.OnPath = 0.f; Sp.FlapHz = 2.5f; Sp.bLongNeck = true; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Peck) | TNFaunaActBit(FaunaAct::OneLeg);
				break;
			case ETNFaunaSpecies::Flamingo:
				Sp.Name = TEXT("Flamenco"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Habitat = FaunaHab::Shallows; Sp.Length = 130.f; Sp.WalkSpeed = 70.f;
				Sp.FleeSpeed = 1400.f; Sp.AlarmRange = 1100.f; Sp.WanderRadius = 450.f; Sp.Stride = 35.f; Sp.TurnRate = 180.f; Sp.GroupMin = 3;
				Sp.GroupMax = 6; Sp.GroupSpread = 350.f; Sp.OnPath = 0.f; Sp.Dash = 0.6f; Sp.FlapHz = 2.8f; Sp.bLongNeck = true; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::OneLeg) | TNFaunaActBit(FaunaAct::Peck) | TNFaunaActBit(FaunaAct::Preen) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Pelican:
				Sp.Name = TEXT("Pelícano"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Habitat = FaunaHab::Water; Sp.Gait = FaunaGait::Swim; Sp.Length = 130.f;
				Sp.WalkSpeed = 60.f; Sp.FleeSpeed = 1400.f; Sp.AlarmRange = 1000.f; Sp.WanderRadius = 700.f; Sp.Stride = 40.f; Sp.TurnRate = 90.f;
				Sp.GroupMin = 1; Sp.GroupMax = 3; Sp.GroupSpread = 400.f; Sp.OnPath = 0.f; Sp.Dash = 0.5f; Sp.FlapHz = 2.2f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Preen) | TNFaunaActBit(FaunaAct::Peck);
				break;
			case ETNFaunaSpecies::Vulture:
				Sp.Name = TEXT("Buitre"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Length = 75.f; Sp.WalkSpeed = 60.f; Sp.FleeSpeed = 1300.f;
				Sp.AlarmRange = 1100.f; Sp.WanderRadius = 350.f; Sp.Stride = 25.f; Sp.TurnRate = 180.f; Sp.GroupMin = 1; Sp.GroupMax = 3;
				Sp.GroupSpread = 300.f; Sp.OnPath = 0.25f; Sp.Dash = 0.4f; Sp.FlapHz = 2.2f; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Stretch) | TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Peck);
				break;
			case ETNFaunaSpecies::Eagle:
				Sp.Name = TEXT("Águila"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Length = 80.f; Sp.WalkSpeed = 50.f; Sp.FleeSpeed = 1800.f;
				Sp.AlarmRange = 1400.f; Sp.WanderRadius = 200.f; Sp.Stride = 25.f; Sp.TurnRate = 180.f; Sp.GroupMin = 1; Sp.GroupMax = 1;
				Sp.OnPath = 0.f; Sp.FlapHz = 2.5f; Sp.bHighGround = true; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Preen) | TNFaunaActBit(FaunaAct::Stretch);
				break;
			case ETNFaunaSpecies::Pigeon:
				Sp.Name = TEXT("Paloma"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Fly; Sp.Length = 32.f; Sp.WalkSpeed = 90.f; Sp.FleeSpeed = 1500.f;
				Sp.AlarmRange = 600.f; Sp.WanderRadius = 450.f; Sp.Stride = 10.f; Sp.TurnRate = 540.f; Sp.GroupMin = 4; Sp.GroupMax = 7;
				Sp.GroupSpread = 300.f; Sp.OnPath = 0.5f; Sp.FlapHz = 6.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Peck) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Hen:
				Sp.Name = TEXT("Gallina"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Run; Sp.Length = 45.f; Sp.WalkSpeed = 70.f; Sp.FleeSpeed = 1600.f;
				Sp.AlarmRange = 600.f; Sp.WanderRadius = 400.f; Sp.Stride = 14.f; Sp.TurnRate = 540.f; Sp.GroupMin = 3; Sp.GroupMax = 5;
				Sp.GroupSpread = 300.f; Sp.OnPath = 0.4f; Sp.FlapHz = 7.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Peck) | TNFaunaActBit(FaunaAct::Scratch) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Roadrunner:
				Sp.Name = TEXT("Correcaminos"); Sp.Body = FaunaBody::Bird; Sp.Flee = FaunaFlee::Run; Sp.Length = 55.f; Sp.WalkSpeed = 250.f; Sp.FleeSpeed = 2600.f;
				Sp.AlarmRange = 1000.f; Sp.WanderRadius = 700.f; Sp.Stride = 30.f; Sp.TurnRate = 540.f; Sp.GroupMin = 1; Sp.GroupMax = 2;
				Sp.GroupSpread = 300.f; Sp.OnPath = 0.4f; Sp.FlapHz = 5.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Peck);
				break;
			case ETNFaunaSpecies::Monkey:
				Sp.Name = TEXT("Mono"); Sp.Body = FaunaBody::Quad; Sp.Flee = FaunaFlee::Climb; Sp.Fallback = FaunaFlee::Run; Sp.Length = 70.f; Sp.WalkSpeed = 140.f;
				Sp.FleeSpeed = 1800.f; Sp.AlarmRange = 900.f; Sp.WanderRadius = 500.f; Sp.Stride = 45.f; Sp.TurnRate = 540.f; Sp.GroupMin = 2;
				Sp.GroupMax = 4; Sp.GroupSpread = 300.f; Sp.OnPath = 0.3f; Sp.HopHeight = 28.f; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Sit) | TNFaunaActBit(FaunaAct::Scratch) | TNFaunaActBit(FaunaAct::Hop) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Capybara:
				Sp.Name = TEXT("Capibara"); Sp.Body = FaunaBody::Quad; Sp.Flee = FaunaFlee::Run; Sp.Length = 110.f; Sp.WalkSpeed = 60.f; Sp.FleeSpeed = 1600.f;
				Sp.AlarmRange = 800.f; Sp.WanderRadius = 400.f; Sp.Stride = 45.f; Sp.TurnRate = 180.f; Sp.GroupMin = 2; Sp.GroupMax = 4;
				Sp.GroupSpread = 350.f; Sp.OnPath = 0.3f; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Graze) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Meerkat:
				Sp.Name = TEXT("Suricato"); Sp.Body = FaunaBody::Quad; Sp.Flee = FaunaFlee::Burrow; Sp.Fallback = FaunaFlee::Burrow; Sp.Length = 50.f; Sp.WalkSpeed = 90.f;
				Sp.FleeSpeed = 1400.f; Sp.AlarmRange = 900.f; Sp.WanderRadius = 250.f; Sp.Stride = 16.f; Sp.TurnRate = 540.f; Sp.GroupMin = 3;
				Sp.GroupMax = 5; Sp.GroupSpread = 200.f; Sp.OnPath = 0.2f; Sp.Dash = 0.4f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Sentinel) | TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Dig);
				break;
			case ETNFaunaSpecies::Ibex:
				Sp.Name = TEXT("Cabra montés"); Sp.Body = FaunaBody::Quad; Sp.Flee = FaunaFlee::Climb; Sp.Fallback = FaunaFlee::Run; Sp.Length = 130.f; Sp.WalkSpeed = 90.f;
				Sp.FleeSpeed = 1700.f; Sp.AlarmRange = 1200.f; Sp.WanderRadius = 500.f; Sp.Stride = 70.f; Sp.TurnRate = 240.f; Sp.GroupMin = 2;
				Sp.GroupMax = 3; Sp.GroupSpread = 350.f; Sp.OnPath = 0.15f; Sp.HopHeight = 30.f; Sp.bHighGround = true; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Graze) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Marmot:
				Sp.Name = TEXT("Marmota"); Sp.Body = FaunaBody::Quad; Sp.Flee = FaunaFlee::Burrow; Sp.Fallback = FaunaFlee::Burrow; Sp.Length = 55.f; Sp.WalkSpeed = 70.f;
				Sp.FleeSpeed = 1200.f; Sp.AlarmRange = 1000.f; Sp.WanderRadius = 300.f; Sp.Stride = 20.f; Sp.GroupMin = 2; Sp.GroupMax = 4;
				Sp.OnPath = 0.2f; Sp.Dash = 0.5f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Sentinel) | TNFaunaActBit(FaunaAct::Graze) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Cat:
				Sp.Name = TEXT("Gato"); Sp.Body = FaunaBody::Quad; Sp.Flee = FaunaFlee::Climb; Sp.Fallback = FaunaFlee::Run; Sp.Length = 75.f; Sp.WalkSpeed = 90.f;
				Sp.FleeSpeed = 1900.f; Sp.AlarmRange = 700.f; Sp.WanderRadius = 350.f; Sp.Stride = 30.f; Sp.GroupMin = 1; Sp.GroupMax = 2;
				Sp.OnPath = 0.35f; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Sit) | TNFaunaActBit(FaunaAct::Scratch) | TNFaunaActBit(FaunaAct::Wag) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Rabbit:
				Sp.Name = TEXT("Conejo"); Sp.Body = FaunaBody::Quad; Sp.Flee = FaunaFlee::Burrow; Sp.Fallback = FaunaFlee::Burrow; Sp.Gait = FaunaGait::Hop; Sp.Length = 40.f;
				Sp.WalkSpeed = 90.f; Sp.FleeSpeed = 1900.f; Sp.AlarmRange = 800.f; Sp.WanderRadius = 350.f; Sp.Stride = 35.f; Sp.TurnRate = 540.f;
				Sp.GroupMin = 2; Sp.GroupMax = 3; Sp.OnPath = 0.3f; Sp.Dash = 1.1f; Sp.HopHeight = 14.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Graze) | TNFaunaActBit(FaunaAct::Sentinel) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Lizard:
				Sp.Name = TEXT("Lagartija"); Sp.Body = FaunaBody::Lizard; Sp.Flee = FaunaFlee::Climb; Sp.Fallback = FaunaFlee::Burrow; Sp.Length = 45.f; Sp.WalkSpeed = 120.f;
				Sp.FleeSpeed = 2000.f; Sp.AlarmRange = 600.f; Sp.WanderRadius = 350.f; Sp.Stride = 18.f; Sp.TurnRate = 720.f; Sp.GroupMin = 1;
				Sp.GroupMax = 3; Sp.OnPath = 0.35f;
				Sp.Acts = TNFaunaActBit(FaunaAct::PushUp) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Salamander:
				Sp.Name = TEXT("Salamandra de fuego"); Sp.Body = FaunaBody::Lizard; Sp.Flee = FaunaFlee::Burrow; Sp.Fallback = FaunaFlee::Burrow; Sp.Length = 45.f;
				Sp.WalkSpeed = 50.f; Sp.FleeSpeed = 1100.f; Sp.AlarmRange = 500.f; Sp.WanderRadius = 250.f; Sp.Stride = 16.f; Sp.GroupMin = 1;
				Sp.GroupMax = 3; Sp.GroupSpread = 200.f; Sp.OnPath = 0.3f; Sp.Dash = 0.3f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Wag);
				break;
			case ETNFaunaSpecies::MarineIguana:
				Sp.Name = TEXT("Iguana marina"); Sp.Body = FaunaBody::Lizard; Sp.Flee = FaunaFlee::Dive; Sp.Fallback = FaunaFlee::Climb; Sp.Length = 100.f; Sp.WalkSpeed = 50.f;
				Sp.FleeSpeed = 1500.f; Sp.AlarmRange = 700.f; Sp.WanderRadius = 300.f; Sp.Stride = 30.f; Sp.TurnRate = 240.f; Sp.GroupMin = 2;
				Sp.GroupMax = 4; Sp.GroupSpread = 300.f; Sp.OnPath = 0.25f; Sp.bShadow = true;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::PushUp);
				break;
			case ETNFaunaSpecies::DartFrog:
				Sp.Name = TEXT("Rana dardo"); Sp.Body = FaunaBody::Frog; Sp.Flee = FaunaFlee::Dive; Sp.Fallback = FaunaFlee::Burrow; Sp.Gait = FaunaGait::Hop; Sp.Length = 16.f;
				Sp.WalkSpeed = 60.f; Sp.FleeSpeed = 1300.f; Sp.AlarmRange = 450.f; Sp.WanderRadius = 250.f; Sp.Stride = 30.f; Sp.TurnRate = 540.f;
				Sp.GroupMin = 2; Sp.GroupMax = 4; Sp.GroupSpread = 180.f; Sp.OnPath = 0.3f; Sp.HopHeight = 15.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Croak) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::TreeFrog:
				Sp.Name = TEXT("Rana de ojos rojos"); Sp.Body = FaunaBody::Frog; Sp.Flee = FaunaFlee::Dive; Sp.Fallback = FaunaFlee::Burrow; Sp.Habitat = FaunaHab::Shore;
				Sp.Gait = FaunaGait::Hop; Sp.Length = 18.f; Sp.WalkSpeed = 60.f; Sp.FleeSpeed = 1300.f; Sp.AlarmRange = 550.f; Sp.WanderRadius = 250.f;
				Sp.Stride = 32.f; Sp.TurnRate = 540.f; Sp.GroupMin = 2; Sp.GroupMax = 4; Sp.GroupSpread = 180.f; Sp.OnPath = 0.15f; Sp.HopHeight = 16.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Croak) | TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Fish:
				Sp.Name = TEXT("Pez saltador"); Sp.Body = FaunaBody::Fish; Sp.Flee = FaunaFlee::Dive; Sp.Fallback = FaunaFlee::Dive; Sp.Habitat = FaunaHab::Water;
				Sp.Gait = FaunaGait::Swim; Sp.Length = 45.f; Sp.WalkSpeed = 150.f; Sp.FleeSpeed = 900.f; Sp.AlarmRange = 600.f; Sp.WanderRadius = 500.f;
				Sp.GroupMin = 2; Sp.GroupMax = 4; Sp.GroupSpread = 400.f; Sp.OnPath = 0.f;
				break;
			case ETNFaunaSpecies::Mudskipper:
				Sp.Name = TEXT("Pez del fango"); Sp.Body = FaunaBody::Fish; Sp.Flee = FaunaFlee::Dive; Sp.Fallback = FaunaFlee::Burrow; Sp.Habitat = FaunaHab::Shore;
				Sp.Gait = FaunaGait::Hop; Sp.Length = 22.f; Sp.WalkSpeed = 50.f; Sp.FleeSpeed = 1300.f; Sp.AlarmRange = 500.f; Sp.WanderRadius = 250.f;
				Sp.Stride = 20.f; Sp.TurnRate = 540.f; Sp.GroupMin = 2; Sp.GroupMax = 4; Sp.GroupSpread = 200.f; Sp.OnPath = 0.1f; Sp.HopHeight = 8.f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look) | TNFaunaActBit(FaunaAct::Hop);
				break;
			case ETNFaunaSpecies::FireBeetle:
				Sp.Name = TEXT("Escarabajo de fuego"); Sp.Body = FaunaBody::Beetle; Sp.Flee = FaunaFlee::Burrow; Sp.Fallback = FaunaFlee::Burrow; Sp.Length = 25.f;
				Sp.WalkSpeed = 40.f; Sp.FleeSpeed = 1000.f; Sp.AlarmRange = 400.f; Sp.WanderRadius = 200.f; Sp.Stride = 8.f; Sp.GroupMin = 2;
				Sp.GroupMax = 4; Sp.GroupSpread = 150.f; Sp.OnPath = 0.35f; Sp.Dash = 0.3f;
				Sp.Acts = TNFaunaActBit(FaunaAct::Look);
				break;
			case ETNFaunaSpecies::Bat:
				Sp.Name = TEXT("Murciélago"); Sp.Body = FaunaBody::Bat; Sp.Flee = FaunaFlee::Fly; Sp.Fallback = FaunaFlee::Fly; Sp.Gait = FaunaGait::Hover; Sp.Length = 55.f;
				Sp.WalkSpeed = 250.f; Sp.FleeSpeed = 1800.f; Sp.AlarmRange = 900.f; Sp.WanderRadius = 300.f; Sp.GroupMin = 2; Sp.GroupMax = 4;
				Sp.GroupSpread = 400.f; Sp.OnPath = 0.3f; Sp.FlapHz = 7.f;
				break;
			default:
				break;
		}
		return Sp;
	}

	/** Ficha de una especie (tabla construida una vez). */
	inline const FTNFaunaSpec& TNFaunaSpec(ETNFaunaSpecies Species)
	{
		struct FTNFaunaSpecTable
		{
			FTNFaunaSpec Specs[TNFaunaNumSpecies];
			FTNFaunaSpecTable()
			{
				for (int32 i = 0; i < TNFaunaNumSpecies; ++i) { Specs[i] = TNFaunaMakeSpec(static_cast<ETNFaunaSpecies>(i)); }
			}
		};
		static const FTNFaunaSpecTable Table;
		return Table.Specs[FMath::Clamp(static_cast<int32>(Species), 0, TNFaunaNumSpecies - 1)];
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Especies por bioma
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaBiomeEntry
	{
		ETNFaunaSpecies Species = ETNFaunaSpecies::Crab;
		float Weight = 0.f;
	};

	/** Especies de un bioma con su peso al sortear grupos y cuántos animales por módulo del camino. */
	struct FTNFaunaBiomeTable
	{
		static constexpr int32 MaxEntries = 6;
		FTNFaunaBiomeEntry Entries[MaxEntries];
		int32 Num = 0;
		int32 PerModule = 10;

		void Add(ETNFaunaSpecies InSpecies, float InWeight)
		{
			if (Num >= MaxEntries) { return; }
			Entries[Num].Species = InSpecies;
			Entries[Num].Weight = InWeight;
			++Num;
		}
	};

	/**
	 * Fauna característica de cada bioma. Las especies de agua solo aparecen donde la hay de verdad (el
	 * reparto comprueba la cota del terreno): los peces de la playa y del manglar, solo si el mar queda cerca.
	 */
	inline FTNFaunaBiomeTable TNFaunaBiomeTableOf(ETNProcBiome Biome)
	{
		using FaunaSp = ETNFaunaSpecies;
		FTNFaunaBiomeTable Table;
		switch (Biome)
		{
			case ETNProcBiome::Jungle:
				Table.PerModule = 11;
				Table.Add(FaunaSp::Monkey, 3.f); Table.Add(FaunaSp::Toucan, 2.f); Table.Add(FaunaSp::DartFrog, 2.f); Table.Add(FaunaSp::Capybara, 2.f);
				break;
			case ETNProcBiome::Beach:
				Table.PerModule = 12;
				Table.Add(FaunaSp::Crab, 3.f); Table.Add(FaunaSp::BabyTurtle, 2.f); Table.Add(FaunaSp::Gull, 3.f); Table.Add(FaunaSp::Sandpiper, 2.f); Table.Add(FaunaSp::Fish, 1.f);
				break;
			case ETNProcBiome::Desert:
				Table.PerModule = 9;
				Table.Add(FaunaSp::Lizard, 3.f); Table.Add(FaunaSp::Meerkat, 3.f); Table.Add(FaunaSp::Roadrunner, 2.f); Table.Add(FaunaSp::Vulture, 2.f);
				break;
			case ETNProcBiome::Volcanic:
				Table.PerModule = 8;
				Table.Add(FaunaSp::Salamander, 3.f); Table.Add(FaunaSp::FireBeetle, 3.f); Table.Add(FaunaSp::Bat, 2.f); Table.Add(FaunaSp::MarineIguana, 2.f);
				break;
			case ETNProcBiome::Water:
				Table.PerModule = 9;
				Table.Add(FaunaSp::Fish, 3.f); Table.Add(FaunaSp::SeaTurtle, 2.f); Table.Add(FaunaSp::Pelican, 2.f); Table.Add(FaunaSp::Flamingo, 2.f);
				break;
			case ETNProcBiome::Rocky:
				Table.PerModule = 9;
				Table.Add(FaunaSp::Ibex, 3.f); Table.Add(FaunaSp::Marmot, 3.f); Table.Add(FaunaSp::Eagle, 1.f); Table.Add(FaunaSp::Lizard, 2.f);
				break;
			case ETNProcBiome::Mangrove:
				Table.PerModule = 11;
				Table.Add(FaunaSp::FiddlerCrab, 3.f); Table.Add(FaunaSp::Heron, 2.f); Table.Add(FaunaSp::TreeFrog, 2.f); Table.Add(FaunaSp::Mudskipper, 2.f);
				Table.Add(FaunaSp::Flamingo, 1.f); Table.Add(FaunaSp::Fish, 1.f);
				break;
			case ETNProcBiome::Human:
			default:
				Table.PerModule = 11;
				Table.Add(FaunaSp::Cat, 2.f); Table.Add(FaunaSp::Pigeon, 3.f); Table.Add(FaunaSp::Hen, 3.f); Table.Add(FaunaSp::Rabbit, 2.f);
				break;
		}
		return Table;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Piezas
	// ─────────────────────────────────────────────────────────────────────────

	/** Medidas del esqueleto que necesita la animación. */
	struct FTNFaunaRig
	{
		/** Altura del pivote del cuerpo sobre la raíz. */
		double BodyZ = 20.0;
		/** Semilongitud del cuerpo (giro por la cadera al sentarse). */
		double HalfLen = 15.0;
		/** Alto total aproximado: lo que se hunde al enterrarse. */
		double Height = 30.0;
		/** Cuánto queda la raíz bajo la superficie al flotar. */
		double Draft = 0.0;
	};

	/** Una pieza: malla con el pivote en su origen y el canal que la anima. */
	struct FTNFaunaPart
	{
		ETNFaunaBone Bone = ETNFaunaBone::Body;
		/** Pivote: en el espacio de la raíz para el cuerpo; en el del cuerpo para las demás. */
		FVector Pivot = FVector::ZeroVector;
		FTNProcMeshBuffers Mesh;
		/** Con M_ProcGlow (brilla). */
		bool bGlow = false;
		bool bShadow = false;
	};

	inline void TNFaunaAddPart(TArray<FTNFaunaPart>& Out, ETNFaunaBone Bone, const FVector& Pivot, FTNProcMeshBuffers&& Mesh, bool bGlow = false, bool bShadow = false)
	{
		if (Mesh.IsEmpty()) { return; }
		FTNFaunaPart& Part = Out.AddDefaulted_GetRef();
		Part.Bone = Bone;
		Part.Pivot = Pivot;
		Part.Mesh = MoveTemp(Mesh);
		Part.bGlow = bGlow;
		Part.bShadow = bShadow;
	}

	/**
	 * Elipsoide de caras planas con el eje polar en X (cuerpos, cabezas, hocicos, aletas): NumSeg lados por
	 * anillo y NumBands franjas de polo a polo, anillos alternos girados medio lado. La mitad de abajo lleva
	 * el color del vientre y lo más alto sale un poco más claro.
	 */
	inline void TNFaunaBlob(FTNProcMeshBuffers& M, const FVector& Center, const FVector& Radii, const FLinearColor& TopColor, const FLinearColor& BellyColor,
		int32 NumSeg = 8, int32 NumBands = 4)
	{
		const int32 SegCount = FMath::Max(3, NumSeg);
		const int32 BandCount = FMath::Max(2, NumBands);
		auto BlobPt = [&](int32 LatIdx, int32 LonIdx)
		{
			const double Lat = -UE_HALF_PI + UE_PI * static_cast<double>(LatIdx) / BandCount;
			const double Lon = TNProcMap::TwoPi * (static_cast<double>(LonIdx) + 0.5 * (LatIdx % 2)) / SegCount;
			const double Ring = FMath::Cos(Lat);
			return Center + FVector(Radii.X * FMath::Sin(Lat), Radii.Y * Ring * FMath::Cos(Lon), Radii.Z * Ring * FMath::Sin(Lon));
		};
		for (int32 Bi = 0; Bi < BandCount; ++Bi)
		{
			for (int32 Ki = 0; Ki < SegCount; ++Ki)
			{
				const FVector P00 = BlobPt(Bi, Ki);
				const FVector P01 = BlobPt(Bi, Ki + 1);
				const FVector P11 = BlobPt(Bi + 1, Ki + 1);
				const FVector P10 = BlobPt(Bi + 1, Ki);
				const FVector Mid = (P00 + P01 + P11 + P10) * 0.25;
				const double Up = (Mid.Z - Center.Z) / FMath::Max(0.01, Radii.Z);
				const FLinearColor Col = Up < -0.3 ? BellyColor : (Up > 0.55 ? TopColor * 1.07f : TopColor);
				// En los polos dos vértices coinciden: AddTri descarta el triángulo degenerado.
				M.AddTri(P00, P01, P11, Mid - Center, Col);
				M.AddTri(P00, P11, P10, Mid - Center, Col);
			}
		}
	}

	/** Lámina de dos caras en abanico desde el primer punto (alas, aletas, orejas, colas, crestas). */
	inline void TNFaunaSheet(FTNProcMeshBuffers& M, std::initializer_list<FVector> Pts, const FVector& FaceDir, const FLinearColor& FaceColor, const FLinearColor& BackColor)
	{
		const int32 NumPts = static_cast<int32>(Pts.size());
		if (NumPts < 3) { return; }
		const FVector* Pt = Pts.begin();
		for (int32 i = 1; i + 1 < NumPts; ++i)
		{
			M.AddTri(Pt[0], Pt[i], Pt[i + 1], FaceDir, FaceColor);
			M.AddTri(Pt[0], Pt[i], Pt[i + 1], -FaceDir, BackColor);
		}
	}

	/** Ojos de dibujo a cada lado (Y = ±HalfGap): blanco y pupila negra un poco hacia delante. */
	inline void TNFaunaEyes(FTNProcMeshBuffers& M, const FVector& Center, double HalfGap, double EyeSize)
	{
		const FLinearColor EyeWhite(0.97f, 0.97f, 0.95f);
		const FLinearColor EyeDark(0.03f, 0.03f, 0.04f);
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector EyeAt = Center + FVector(0.0, Side * HalfGap, 0.0);
			M.AddBox(EyeAt, FVector::ForwardVector, FVector(EyeSize, EyeSize * 0.45, EyeSize), EyeWhite);
			M.AddBox(EyeAt + FVector(EyeSize * 0.3, Side * EyeSize * 0.4, EyeSize * 0.1), FVector::ForwardVector,
				FVector(EyeSize * 0.55, EyeSize * 0.14, EyeSize * 0.62), EyeDark);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Aves
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaBirdLook
	{
		double Len = 16.0;      ///< Semilongitud del cuerpo.
		double Girth = 8.0;     ///< Semialto del cuerpo (el semiancho es el 85 %).
		double Neck = 5.0;
		double NeckR = 3.0;
		double NeckFwd = 0.3;   ///< Inclinación del cuello hacia delante (× Neck).
		double Head = 5.0;      ///< Radio de la cabeza.
		double Beak = 6.0;
		double BeakR = 1.5;
		double BeakDroop = 0.0; ///< 0..1: pico curvado hacia abajo (flamenco, rapaces).
		double Leg = 9.0;
		double LegR = 0.9;
		double Wing = 40.0;     ///< Semienvergadura.
		double Tail = 10.0;
		double TailUp = 10.0;   ///< Grados sobre la horizontal.
		FLinearColor Plumage = FLinearColor(0.9f, 0.9f, 0.9f);
		FLinearColor Belly = FLinearColor(0.95f, 0.95f, 0.95f);
		FLinearColor WingC = FLinearColor(0.6f, 0.6f, 0.65f);
		FLinearColor WingTip = FLinearColor(0.15f, 0.15f, 0.15f);
		FLinearColor HeadC = FLinearColor(0.9f, 0.9f, 0.9f);
		FLinearColor NeckC = FLinearColor(0.9f, 0.9f, 0.9f);
		FLinearColor BeakC = FLinearColor(0.95f, 0.7f, 0.1f);
		FLinearColor BeakTip = FLinearColor(0.95f, 0.7f, 0.1f);
		FLinearColor LegC = FLinearColor(0.9f, 0.55f, 0.4f);
		FLinearColor TailC = FLinearColor(0.6f, 0.6f, 0.65f);
		/** Adorno: 0 nada, 1 cresta y barbas (gallina), 2 moño y antifaz (correcaminos), 3 bolsa (pelícano), 4 gorguera (buitre), 5 antifaz azul y babero (tucán). */
		int32 Extra = 0;
		FLinearColor ExtraC = FLinearColor(0.85f, 0.1f, 0.1f);
	};

	/**
	 * Pico de abajo de un ave que lo abre (las gaviotas y el pelícano de la playa: TNFaunaBuildBird con OutJaw). Mesh va en el
	 * espacio de su pivote, la base del pico en el espacio de la cabeza (Pivot): girándolo hacia abajo se abre el pico.
	 */
	struct FTNFaunaBirdJaw
	{
		FTNProcMeshBuffers Mesh;
		FVector Pivot = FVector::ZeroVector;
	};

	/**
	 * Mitad de arriba (bUpper) o de abajo del tramo de pico de TNProcAddCylinder de 4 lados entre A y B (radios RA y RB):
	 * las dos juntas son ese mismo tramo. La cara plana del corte, la de dentro de la boca, se pinta de Inside.
	 */
	inline void TNFaunaBeakHalf(FTNProcMeshBuffers& M, const FVector& A, const FVector& B, double RA, double RB, bool bUpper,
		const FLinearColor& Color, const FLinearColor& Inside)
	{
		const FVector Ax = (B - A).GetSafeNormal();
		if (Ax.IsNearlyZero()) { return; }
		// Los mismos ejes que TNProcAddCylinder: U de lado y V hacia abajo (sus vértices a 0, 90, 180 y 270 grados).
		const FVector U = FVector::CrossProduct(Ax, FMath::Abs(Ax.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		const FVector V = FVector::CrossProduct(Ax, U);
		const FVector Apex = bUpper ? -V : V;
		const FVector A0 = A + U * RA, A1 = A + Apex * RA, A2 = A - U * RA;
		const FVector B0 = B + U * RB, B1 = B + Apex * RB, B2 = B - U * RB;
		M.AddQuad(A0, A1, B1, B0, U + Apex, Color);
		M.AddQuad(A1, A2, B2, B1, Apex - U, Color);
		M.AddQuad(A0, A2, B2, B0, -Apex, Inside);
		M.AddTri(A0, A1, A2, -Ax, Color * 0.9f);
		M.AddTri(B0, B1, B2, Ax, Color * 1.05f);
	}

	/**
	 * Ave: cuerpo con la cola, cabeza con el cuello y el pico, dos alas (abiertas en la malla) y dos patas. Con OutJaw, el
	 * pico se abre: la cabeza lleva solo la mitad de arriba y la de abajo (con la bolsa del pelícano) sale en OutJaw. Cerrado
	 * es el mismo pico de una pieza (antes, una mandíbula suelta debajo del pico entero se veía como un segundo pico).
	 */
	inline void TNFaunaBuildBird(const FTNFaunaBirdLook& L, bool bShadow, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig, FTNFaunaBirdJaw* OutJaw = nullptr)
	{
		const double HipDrop = L.Girth * 0.45;
		const double BodyZ = L.Leg + HipDrop;
		Rig.BodyZ = BodyZ;
		Rig.HalfLen = L.Len;
		Rig.Height = BodyZ + L.Girth + L.Neck + L.Head * 2.0;
		Rig.Draft = BodyZ - L.Girth * 0.35;

		// Cuerpo con la cola en abanico (y la gorguera del buitre o el babero del tucán).
		{
			FTNProcMeshBuffers M;
			TNFaunaBlob(M, FVector::ZeroVector, FVector(L.Len, L.Girth * 0.85, L.Girth), L.Plumage, L.Belly, 8, 4);
			const double TailA = FMath::DegreesToRadians(L.TailUp);
			const FVector TailRoot(-L.Len * 0.7, 0.0, L.Girth * 0.25);
			const FVector TailEnd = TailRoot + FVector(-FMath::Cos(TailA), 0.0, FMath::Sin(TailA)) * L.Tail;
			const double TailW = FMath::Max(L.Girth * 0.5, L.Tail * 0.3);
			TNFaunaSheet(M, { TailRoot + FVector(0.0, -L.Girth * 0.3, 0.0), TailEnd + FVector(0.0, -TailW, 0.0), TailEnd + FVector(-L.Tail * 0.15, 0.0, 0.0),
				TailEnd + FVector(0.0, TailW, 0.0), TailRoot + FVector(0.0, L.Girth * 0.3, 0.0) }, FVector::UpVector, L.TailC, L.TailC * 0.8f);
			if (L.Extra == 4)
			{
				TNFaunaBlob(M, FVector(L.Len * 0.72, 0.0, L.Girth * 0.5), FVector(L.Girth * 0.4, L.Girth * 0.75, L.Girth * 0.45), L.ExtraC, L.ExtraC * 0.9f, 7, 3);
			}
			else if (L.Extra == 5)
			{
				TNFaunaBlob(M, FVector(L.Len * 0.74, 0.0, L.Girth * 0.1), FVector(L.Girth * 0.35, L.Girth * 0.62, L.Girth * 0.55), L.ExtraC, L.ExtraC, 6, 3);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M), false, bShadow);
		}

		// Cabeza: cuello, cabeza, pico (recto o curvado) y ojos; su pivote es la base del cuello.
		{
			FTNProcMeshBuffers M;
			const FVector NeckTop(L.Neck * L.NeckFwd, 0.0, L.Neck);
			TNProcAddCylinder(M, FVector(-L.NeckR * 0.3, 0.0, -L.NeckR * 0.6), NeckTop, L.NeckR, L.NeckR * 0.8, 5, L.NeckC, false);
			const FVector HeadAt = NeckTop + FVector(L.Head * 0.25, 0.0, L.Head * 0.35);
			TNFaunaBlob(M, HeadAt, FVector(L.Head * 1.15, L.Head * 0.85, L.Head * 0.9), L.HeadC, L.HeadC * 0.85f, 7, 3);
			const FVector Beak0 = HeadAt + FVector(L.Head * 0.95, 0.0, -L.Head * 0.1);
			const FVector BeakMid = Beak0 + FVector(L.Beak * 0.5, 0.0, -L.Beak * 0.1 * L.BeakDroop);
			const FVector BeakEnd = BeakMid + FVector(L.Beak * 0.5 * (1.0 - 0.45 * L.BeakDroop), 0.0, -L.Beak * 0.42 * L.BeakDroop);
			if (OutJaw)
			{
				// El pico que se abre: arriba, en la cabeza; abajo, en su pieza (con su pivote en la base del pico).
				const FLinearColor Palate(0.45f, 0.08f, 0.1f);
				const FLinearColor MouthFloor(0.55f, 0.1f, 0.12f);
				TNFaunaBeakHalf(M, Beak0, BeakMid, L.BeakR, L.BeakR * 0.75, true, L.BeakC, Palate);
				TNFaunaBeakHalf(M, BeakMid, BeakEnd, L.BeakR * 0.75, L.BeakR * 0.2, true, L.BeakTip, Palate);
				FTNProcMeshBuffers& J = OutJaw->Mesh;
				J = FTNProcMeshBuffers();
				OutJaw->Pivot = Beak0;
				TNFaunaBeakHalf(J, FVector::ZeroVector, BeakMid - Beak0, L.BeakR, L.BeakR * 0.75, false, L.BeakC * 0.92f, MouthFloor);
				TNFaunaBeakHalf(J, BeakMid - Beak0, BeakEnd - Beak0, L.BeakR * 0.75, L.BeakR * 0.2, false, L.BeakTip * 0.9f, MouthFloor);
				if (L.Extra == 3)
				{
					// La bolsa del pelícano cuelga del pico de abajo y baja con él.
					TNFaunaBlob(J, FVector(L.Beak * 0.45, 0.0, -L.BeakR * 1.5), FVector(L.Beak * 0.45, L.BeakR * 1.0, L.BeakR * 1.4), L.ExtraC, L.ExtraC * 0.9f, 6, 3);
				}
			}
			else
			{
				TNProcAddCylinder(M, Beak0, BeakMid, L.BeakR, L.BeakR * 0.75, 4, L.BeakC, true);
				TNProcAddCylinder(M, BeakMid, BeakEnd, L.BeakR * 0.75, L.BeakR * 0.2, 4, L.BeakTip, true);
			}
			const FVector EyeAt = HeadAt + FVector(L.Head * 0.35, 0.0, L.Head * 0.25);
			if (L.Extra == 5)
			{
				for (const double Side : { -1.0, 1.0 })
				{
					M.AddBox(EyeAt + FVector(0.0, Side * L.Head * 0.74, 0.0), FVector::ForwardVector, FVector(L.Head * 0.34, L.Head * 0.07, L.Head * 0.34), FLinearColor(0.2f, 0.55f, 0.95f));
				}
			}
			TNFaunaEyes(M, EyeAt, L.Head * 0.8, L.Head * 0.2);
			if (L.Extra == 1)
			{
				// Cresta y barbas rojas.
				for (int32 k = 0; k < 3; ++k)
				{
					M.AddBox(HeadAt + FVector(L.Head * (0.35 - 0.35 * k), 0.0, L.Head * (k == 1 ? 1.05 : 0.95)), FVector::ForwardVector,
						FVector(L.Head * 0.2, L.Head * 0.1, L.Head * 0.28), L.ExtraC);
				}
				TNFaunaBlob(M, HeadAt + FVector(L.Head * 0.85, 0.0, -L.Head * 0.75), FVector(L.Head * 0.22, L.Head * 0.14, L.Head * 0.32), L.ExtraC, L.ExtraC, 5, 3);
			}
			else if (L.Extra == 2)
			{
				// Moño hacia atrás y antifaz azul detrás del ojo.
				TNFaunaSheet(M, { HeadAt + FVector(-L.Head * 0.1, 0.0, L.Head * 0.75), HeadAt + FVector(-L.Head * 1.4, 0.0, L.Head * 1.6),
					HeadAt + FVector(-L.Head * 0.95, 0.0, L.Head * 0.55) }, FVector::RightVector, L.ExtraC, L.ExtraC * 0.85f);
				for (const double Side : { -1.0, 1.0 })
				{
					M.AddBox(HeadAt + FVector(-L.Head * 0.25, Side * L.Head * 0.74, L.Head * 0.2), FVector::ForwardVector, FVector(L.Head * 0.3, L.Head * 0.07, L.Head * 0.14), FLinearColor(0.2f, 0.5f, 0.95f));
				}
			}
			else if (L.Extra == 3 && !OutJaw)
			{
				// Bolsa del pelícano bajo el pico (si el pico se abre, va con el de abajo).
				TNFaunaBlob(M, Beak0 + FVector(L.Beak * 0.45, 0.0, -L.BeakR * 1.5), FVector(L.Beak * 0.45, L.BeakR * 1.0, L.BeakR * 1.4), L.ExtraC, L.ExtraC * 0.9f, 6, 3);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Head, FVector(L.Len * 0.7, 0.0, L.Girth * 0.45), MoveTemp(M), false, bShadow);
		}

		// Alas abiertas a lo largo de ±Y: la animación las pliega hacia atrás contra el costado.
		for (const double Side : { -1.0, 1.0 })
		{
			FTNProcMeshBuffers M;
			const FVector P0(L.Len * 0.2, 0.0, 0.0);
			const FVector P1(L.Len * 0.05, Side * L.Wing * 0.55, 0.6);
			const FVector P2(-L.Len * 0.35, Side * L.Wing, 0.0);
			const FVector P3(-L.Len * 0.7, Side * L.Wing * 0.45, 0.0);
			const FVector P4(-L.Len * 0.65, 0.0, 0.0);
			// Plegada, el ala enseña su cara de abajo: casi del mismo color que la de arriba.
			TNFaunaSheet(M, { P0, P1, P3, P4 }, FVector::UpVector, L.WingC, L.WingC * 0.92f);
			TNFaunaSheet(M, { P1, P2, P3 }, FVector::UpVector, L.WingTip, L.WingTip * 0.92f);
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::WingL : ETNFaunaBone::WingR, FVector(L.Len * 0.25, Side * L.Girth * 0.7, L.Girth * 0.45), MoveTemp(M));
		}

		// Patas con muslo de plumas (las cortas) y tres dedos.
		for (const double Side : { -1.0, 1.0 })
		{
			FTNProcMeshBuffers M;
			if (L.Leg < 20.0)
			{
				TNFaunaBlob(M, FVector(0.0, 0.0, -L.Leg * 0.08), FVector(L.LegR * 2.6, L.LegR * 2.2, FMath::Max(L.LegR * 2.0, L.Leg * 0.2)), L.Belly, L.Belly * 0.9f, 6, 3);
			}
			TNProcAddCylinder(M, FVector(0.0, 0.0, L.LegR), FVector(0.0, 0.0, -L.Leg + L.LegR * 0.5), L.LegR, L.LegR * 0.8, 4, L.LegC, true);
			const double Toe = FMath::Max(2.0, L.Leg * 0.18 + L.LegR * 2.5);
			const FVector Heel(0.0, 0.0, -L.Leg + L.LegR * 0.5);
			for (int32 k = -1; k <= 1; ++k)
			{
				M.AddBeam(Heel, Heel + FVector(Toe, k * Toe * 0.45, -L.LegR * 0.4), FMath::Max(0.3, L.LegR * 0.45), L.LegC * 0.9f);
			}
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::LegBL : ETNFaunaBone::LegBR, FVector(0.0, Side * L.Girth * 0.4, -HipDrop), MoveTemp(M));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Cuadrúpedos y lagartos
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaQuadLook
	{
		double Len = 20.0;      ///< Semilongitud del cuerpo.
		double Width = 9.0;     ///< Semiancho.
		double Girth = 10.0;    ///< Semialto.
		double LegF = 16.0;
		double LegB = 16.0;
		double LegR = 2.5;
		double Head = 8.0;      ///< Radio de la cabeza.
		double Snout = 5.0;
		double HeadUp = 0.35;   ///< Altura del cuello sobre el centro del cuerpo (× Girth).
		double Ear = 4.0;
		int32 EarKind = 0;      ///< 0 redondas, 1 puntiagudas (gato), 2 largas (conejo), 3 ninguna, 4 de lado (cabra).
		double Tail = 20.0;
		double TailR = 2.0;
		double TailUp = 25.0;   ///< Grados sobre la horizontal en la base.
		double TailCurl = 0.0;  ///< Grados que se enrosca hasta la punta.
		double Horn = 0.0;      ///< Cuernos curvados hacia atrás (cabra montés).
		bool bSprawl = false;   ///< Patas abiertas a los lados y vientre bajo (lagartos).
		bool bCrest = false;    ///< Cresta de púas (iguana).
		bool bMask = false;     ///< Antifaz oscuro (suricato).
		bool bBeard = false;    ///< Perilla (cabra).
		bool bPuffTail = false; ///< Borla blanca (conejo).
		bool bGlowSpots = false;///< Manchas que brillan (salamandra de fuego).
		FLinearColor Fur = FLinearColor(0.5f, 0.35f, 0.2f);
		FLinearColor Belly = FLinearColor(0.8f, 0.7f, 0.5f);
		FLinearColor HeadC = FLinearColor(0.5f, 0.35f, 0.2f);
		FLinearColor SnoutC = FLinearColor(0.8f, 0.7f, 0.5f);
		FLinearColor Nose = FLinearColor(0.1f, 0.08f, 0.07f);
		FLinearColor LegC = FLinearColor(0.45f, 0.32f, 0.18f);
		FLinearColor Paw = FLinearColor(0.3f, 0.2f, 0.12f);
		FLinearColor TailC = FLinearColor(0.5f, 0.35f, 0.2f);
		FLinearColor TailTip = FLinearColor(0.4f, 0.28f, 0.16f);
		FLinearColor EarC = FLinearColor(0.5f, 0.35f, 0.2f);
		FLinearColor HornC = FLinearColor(0.4f, 0.33f, 0.25f);
		FLinearColor MaskC = FLinearColor(0.25f, 0.18f, 0.12f);
		FLinearColor CrestC = FLinearColor(0.55f, 0.55f, 0.5f);
		FLinearColor GlowC = FLinearColor(1.f, 0.48f, 0.06f);
	};

	/** Cuadrúpedo (o lagarto con bSprawl): cuerpo, cabeza con el cuello, cuatro patas y cola. */
	inline void TNFaunaBuildQuad(const FTNFaunaQuadLook& L, bool bShadow, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig)
	{
		const double HipZ = L.bSprawl ? -L.Girth * 0.1 : -L.Girth * 0.45;
		const double BodyZ = L.bSprawl ? L.Girth * 1.35 : L.LegB - HipZ;
		Rig.BodyZ = BodyZ;
		Rig.HalfLen = L.Len;
		Rig.Height = BodyZ + L.Girth + L.Head * 1.6 + L.Horn * 0.5 + (L.EarKind == 2 ? L.Ear : 0.0);
		Rig.Draft = BodyZ;

		// Cuerpo (con la cresta de la iguana).
		{
			FTNProcMeshBuffers M;
			TNFaunaBlob(M, FVector::ZeroVector, FVector(L.Len, L.Width, L.Girth), L.Fur, L.Belly, 8, 4);
			if (L.bCrest)
			{
				for (int32 k = 0; k < 7; ++k)
				{
					const double SpikeX = L.Len * (0.75 - 0.24 * k);
					const double Back = L.Girth * FMath::Sqrt(FMath::Max(0.05, 1.0 - FMath::Square(SpikeX / L.Len))) * 0.92;
					TNFaunaSheet(M, { FVector(SpikeX + L.Len * 0.09, 0.0, Back - 0.5), FVector(SpikeX, 0.0, Back + L.Girth * (0.55 - 0.05 * k)),
						FVector(SpikeX - L.Len * 0.09, 0.0, Back - 0.5) }, FVector::RightVector, L.CrestC, L.CrestC * 0.85f);
				}
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M), false, bShadow);
		}
		// Manchas que brillan sobre el lomo (pieza aparte con M_ProcGlow que sigue al cuerpo).
		if (L.bGlowSpots)
		{
			FTNProcMeshBuffers M;
			for (int32 k = 0; k < 6; ++k)
			{
				const double SpotX = L.Len * (0.62 - 0.25 * k);
				const double SpotY = (k % 2 == 0 ? 0.32 : -0.32) * L.Width;
				const double Rel = FMath::Square(SpotX / L.Len) + FMath::Square(SpotY / L.Width);
				const double SpotZ = L.Girth * FMath::Sqrt(FMath::Max(0.05, 1.0 - Rel)) * 0.9;
				M.AddBox(FVector(SpotX, SpotY, SpotZ), FVector::ForwardVector, FVector(L.Len * 0.08, L.Width * 0.15, L.Girth * 0.16), L.GlowC);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M), true, false);
		}

		// Cabeza: cuello, cráneo, hocico con la nariz, ojos, orejas, cuernos y perilla.
		{
			FTNProcMeshBuffers M;
			const FVector HeadAt(L.Head * 0.55, 0.0, L.Head * 0.2);
			if (!L.bSprawl)
			{
				TNProcAddCylinder(M, FVector(-L.Head * 0.7, 0.0, -L.Head * 0.35), HeadAt, L.Head * 0.55, L.Head * 0.5, 6, L.Fur, false);
			}
			TNFaunaBlob(M, HeadAt, FVector(L.Head, L.Head * (L.bSprawl ? 0.8 : 0.88), L.Head * (L.bSprawl ? 0.6 : 0.85)), L.HeadC, L.HeadC * 0.88f, 7, 3);
			const FVector SnoutAt = HeadAt + FVector(L.Head * 0.7 + L.Snout * 0.4, 0.0, -L.Head * 0.18);
			TNFaunaBlob(M, SnoutAt, FVector(L.Snout * 0.6, L.Head * 0.5, L.Head * (L.bSprawl ? 0.3 : 0.42)), L.SnoutC, L.SnoutC * 0.9f, 6, 3);
			M.AddBox(SnoutAt + FVector(L.Snout * 0.55, 0.0, L.Head * 0.12), FVector::ForwardVector, FVector(L.Head * 0.1, L.Head * 0.16, L.Head * 0.1), L.Nose);
			const FVector EyeAt = HeadAt + FVector(L.Head * 0.45, 0.0, L.Head * 0.3);
			if (L.bMask)
			{
				for (const double Side : { -1.0, 1.0 })
				{
					M.AddBox(EyeAt + FVector(-L.Head * 0.05, Side * L.Head * 0.66, 0.0), FVector::ForwardVector, FVector(L.Head * 0.32, L.Head * 0.1, L.Head * 0.26), L.MaskC);
				}
			}
			TNFaunaEyes(M, EyeAt, L.Head * (L.bSprawl ? 0.72 : 0.74), L.Head * 0.17);
			for (const double Side : { -1.0, 1.0 })
			{
				switch (L.EarKind)
				{
					case 0:
						TNFaunaBlob(M, HeadAt + FVector(-L.Head * 0.15, Side * L.Head * 0.72, L.Head * 0.72), FVector(L.Ear * 0.3, L.Ear * 0.22, L.Ear * 0.45), L.EarC, L.EarC * 0.85f, 6, 3);
						break;
					case 1:
						TNFaunaSheet(M, { HeadAt + FVector(L.Head * 0.1, Side * L.Head * 0.25, L.Head * 0.7), HeadAt + FVector(-L.Head * 0.15, Side * L.Head * 0.6, L.Head * 0.7 + L.Ear),
							HeadAt + FVector(-L.Head * 0.4, Side * L.Head * 0.78, L.Head * 0.5) }, FVector::ForwardVector, L.EarC, L.EarC * 0.8f);
						break;
					case 2:
						TNFaunaBlob(M, HeadAt + FVector(-L.Head * 0.35, Side * L.Head * 0.35, L.Head * 0.8 + L.Ear * 0.45), FVector(L.Ear * 0.16, L.Ear * 0.1, L.Ear * 0.5), L.EarC, L.EarC * 0.9f, 6, 3);
						break;
					case 4:
						TNFaunaSheet(M, { HeadAt + FVector(-L.Head * 0.2, Side * L.Head * 0.6, L.Head * 0.5), HeadAt + FVector(-L.Head * 0.55, Side * (L.Head * 0.6 + L.Ear), L.Head * 0.65),
							HeadAt + FVector(-L.Head * 0.45, Side * L.Head * 0.6, L.Head * 0.25) }, FVector::UpVector, L.EarC, L.EarC * 0.8f);
						break;
					default:
						break;
				}
			}
			if (L.Horn > 0.0)
			{
				for (const double Side : { -1.0, 1.0 })
				{
					FVector Prev = HeadAt + FVector(L.Head * 0.25, Side * L.Head * 0.3, L.Head * 0.75);
					for (int32 k = 0; k < 5; ++k)
					{
						const double Ang = FMath::DegreesToRadians(80.0 - 32.0 * k);
						const FVector Next = Prev + FVector(-FMath::Cos(Ang), Side * 0.12, FMath::Sin(Ang)) * (L.Horn / 5.0);
						TNProcAddCylinder(M, Prev, Next, L.Head * (0.2 - 0.035 * k), L.Head * (0.2 - 0.035 * (k + 1)), 5, L.HornC, true);
						Prev = Next;
					}
				}
			}
			if (L.bBeard)
			{
				M.AddBeam(SnoutAt + FVector(-L.Snout * 0.1, 0.0, -L.Head * 0.3), SnoutAt + FVector(-L.Snout * 0.3, 0.0, -L.Head * 0.9), L.Head * 0.1, L.MaskC);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Head, FVector(L.Len * 0.82, 0.0, L.bSprawl ? L.Girth * 0.1 : L.Girth * L.HeadUp), MoveTemp(M), false, bShadow);
		}

		// Patas: rectas bajo el cuerpo (mamíferos) o abiertas en codo (lagartos).
		for (int32 LegIdx = 0; LegIdx < 4; ++LegIdx)
		{
			const bool bFront = LegIdx < 2;
			const double Side = (LegIdx % 2 == 0) ? -1.0 : 1.0;
			const double LegLen = bFront ? L.LegF : L.LegB;
			FTNProcMeshBuffers M;
			FVector Pivot = FVector::ZeroVector;
			if (L.bSprawl)
			{
				Pivot = FVector(bFront ? L.Len * 0.5 : -L.Len * 0.55, Side * L.Width * 0.75, HipZ);
				const double Drop = BodyZ + HipZ;
				const FVector Elbow(bFront ? LegLen * 0.1 : -LegLen * 0.1, Side * LegLen * 0.6, LegLen * 0.1);
				const FVector Foot(bFront ? LegLen * 0.3 : -LegLen * 0.15, Side * LegLen * 0.8, -Drop + L.LegR * 0.5);
				TNProcAddCylinder(M, FVector::ZeroVector, Elbow, L.LegR * 1.2, L.LegR, 5, L.LegC, true);
				TNProcAddCylinder(M, Elbow, Foot, L.LegR, L.LegR * 0.8, 5, L.LegC, true);
				for (int32 Toe = -1; Toe <= 1; ++Toe)
				{
					M.AddBeam(Foot, Foot + FVector(LegLen * 0.25, Side * (Toe * LegLen * 0.12 + LegLen * 0.05), 0.0), FMath::Max(0.3, L.LegR * 0.35), L.Paw);
				}
			}
			else
			{
				const double TopZ = bFront ? LegLen - BodyZ : HipZ;
				Pivot = FVector(bFront ? L.Len * 0.55 : -L.Len * 0.55, Side * L.Width * 0.55, TopZ);
				TNFaunaBlob(M, FVector(0.0, 0.0, -LegLen * 0.12), FVector(L.LegR * 2.0, L.LegR * 1.6, LegLen * 0.28), L.Fur, L.Fur * 0.9f, 6, 3);
				TNProcAddCylinder(M, FVector(0.0, 0.0, -LegLen * 0.1), FVector(0.0, 0.0, -LegLen + L.LegR * 0.6), L.LegR * 1.1, L.LegR * 0.8, 5, L.LegC, false);
				M.AddBox(FVector(L.LegR * 0.5, 0.0, -LegLen + L.LegR * 0.5), FVector::ForwardVector, FVector(L.LegR * 1.3, L.LegR * 1.05, L.LegR * 0.55), L.Paw);
			}
			const ETNFaunaBone Bone = bFront ? (Side < 0.0 ? ETNFaunaBone::LegFL : ETNFaunaBone::LegFR) : (Side < 0.0 ? ETNFaunaBone::LegBL : ETNFaunaBone::LegBR);
			TNFaunaAddPart(Out, Bone, Pivot, MoveTemp(M));
		}

		// Cola por tramos que se afinan (y se enroscan); la de los lagartos baja hasta el suelo.
		if (L.Tail > 0.0)
		{
			const double Up0 = L.bSprawl ? -FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(BodyZ * 0.9 / L.Tail, 0.0, 0.9))) : L.TailUp;
			const FVector TailPivot(-L.Len * 0.92, 0.0, L.bSprawl ? 0.0 : L.Girth * 0.15);
			constexpr int32 TailSegs = 4;
			FTNProcMeshBuffers M;
			FTNProcMeshBuffers Glow;
			FVector Prev = FVector::ZeroVector;
			for (int32 k = 0; k < TailSegs; ++k)
			{
				const double Ang = FMath::DegreesToRadians(Up0 + L.TailCurl * k / (TailSegs - 1));
				const FVector Next = Prev + FVector(-FMath::Cos(Ang), 0.0, FMath::Sin(Ang)) * (L.Tail / TailSegs);
				const double R0 = L.TailR * (1.0 - 0.6 * k / TailSegs);
				const double R1 = L.TailR * (1.0 - 0.6 * (k + 1) / TailSegs);
				TNProcAddCylinder(M, Prev, Next, R0, R1, 5, k == TailSegs - 1 ? L.TailTip : L.TailC, true);
				if (L.bGlowSpots && k < TailSegs - 1)
				{
					const FVector Mid = (Prev + Next) * 0.5 + FVector(0.0, 0.0, R0 * 0.8);
					Glow.AddBox(Mid, FVector::ForwardVector, FVector(L.Tail * 0.04, R0 * 0.5, R0 * 0.35), L.GlowC);
				}
				Prev = Next;
			}
			if (L.bPuffTail)
			{
				TNFaunaBlob(M, Prev, FVector(L.TailR * 1.6, L.TailR * 1.5, L.TailR * 1.5), L.TailTip, L.TailTip, 6, 3);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Tail, TailPivot, MoveTemp(M));
			TNFaunaAddPart(Out, ETNFaunaBone::Tail, TailPivot, MoveTemp(Glow), true, false);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Cangrejos
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaCrabLook
	{
		double W = 11.0;      ///< Semiancho del caparazón.
		double D = 8.0;       ///< Semifondo.
		double H = 5.0;       ///< Alto.
		double ClawL = 1.0;   ///< Tamaño relativo de cada pinza (el violinista tiene una enorme).
		double ClawR = 1.0;
		FLinearColor ShellTop = FLinearColor(0.95f, 0.3f, 0.1f);
		FLinearColor Shell = FLinearColor(0.98f, 0.62f, 0.38f);
		FLinearColor LegC = FLinearColor(0.92f, 0.38f, 0.16f);
		FLinearColor ClawC = FLinearColor(0.97f, 0.32f, 0.12f);
		FLinearColor ClawBigC = FLinearColor(0.97f, 0.32f, 0.12f);
		FLinearColor ClawTip = FLinearColor(0.99f, 0.86f, 0.72f);
	};

	/** Cangrejo: caparazón con ojos en pedúnculo, tres patas por lado (un hueso por lado) y dos pinzas. */
	inline void TNFaunaBuildCrab(const FTNFaunaCrabLook& L, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig)
	{
		const double BodyZ = L.H * 1.2;
		Rig.BodyZ = BodyZ;
		Rig.HalfLen = L.D;
		Rig.Height = BodyZ + L.H * 1.6;
		Rig.Draft = BodyZ;
		{
			FTNProcMeshBuffers M;
			TNFaunaBlob(M, FVector::ZeroVector, FVector(L.D, L.W, L.H * 0.6), L.ShellTop, L.Shell, 8, 4);
			for (const double Side : { -1.0, 1.0 })
			{
				const FVector StalkBase(L.D * 0.7, Side * L.W * 0.25, L.H * 0.2);
				const FVector StalkTop = StalkBase + FVector(L.D * 0.08, Side * L.W * 0.04, L.H * 0.85);
				M.AddBeam(StalkBase, StalkTop, L.H * 0.1, L.ShellTop);
				M.AddBox(StalkTop, FVector::ForwardVector, FVector(L.H * 0.22, L.H * 0.2, L.H * 0.22), FLinearColor(0.03f, 0.03f, 0.04f));
				M.AddBox(StalkTop + FVector(L.H * 0.14, Side * L.H * 0.06, L.H * 0.1), FVector::ForwardVector, FVector(L.H * 0.1, L.H * 0.08, L.H * 0.08), FLinearColor(0.97f, 0.97f, 0.95f));
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M));
		}
		for (const double Side : { -1.0, 1.0 })
		{
			FTNProcMeshBuffers M;
			const double FootZ = -(BodyZ - L.H * 0.05);
			for (int32 k = 0; k < 3; ++k)
			{
				const double LegX = L.D * (0.5 - 0.5 * k);
				const FVector Knee(LegX * 1.2, Side * L.W * 0.55, L.H * 0.55);
				const FVector Foot(LegX * 1.45 - L.D * 0.12, Side * L.W * 1.05, FootZ);
				M.AddBeam(FVector(LegX, 0.0, 0.0), Knee, L.H * 0.13, L.LegC);
				M.AddBeam(Knee, Foot, L.H * 0.1, L.LegC * 0.9f);
			}
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::LegFL : ETNFaunaBone::LegFR, FVector(0.0, Side * L.W * 0.78, -L.H * 0.05), MoveTemp(M));
		}
		for (const double Side : { -1.0, 1.0 })
		{
			const double Sz = Side < 0.0 ? L.ClawL : L.ClawR;
			const FLinearColor Col = Sz > 1.4 ? L.ClawBigC : L.ClawC;
			FTNProcMeshBuffers M;
			const FVector Elbow(L.D * 0.3 * Sz, Side * L.W * 0.22 * Sz, L.H * 0.25);
			M.AddBeam(FVector::ZeroVector, Elbow, L.H * 0.14 * FMath::Sqrt(Sz), Col * 0.9f);
			const FVector Palm = Elbow + FVector(L.D * 0.4 * Sz, Side * L.W * 0.04, L.H * 0.1 * Sz);
			TNFaunaBlob(M, Palm, FVector(L.D * 0.38 * Sz, L.H * 0.3 * Sz, L.H * 0.38 * Sz), Col, Col * 0.85f, 6, 3);
			const FVector Finger = Palm + FVector(L.D * 0.32 * Sz, 0.0, 0.0);
			TNProcAddCylinder(M, Finger + FVector(0.0, 0.0, L.H * 0.1 * Sz), Finger + FVector(L.D * 0.42 * Sz, Side * L.W * 0.02, L.H * 0.3 * Sz), L.H * 0.16 * Sz, L.H * 0.03, 4, L.ClawTip, true);
			TNProcAddCylinder(M, Finger - FVector(0.0, 0.0, L.H * 0.08 * Sz), Finger + FVector(L.D * 0.38 * Sz, Side * L.W * 0.02, -L.H * 0.18 * Sz), L.H * 0.13 * Sz, L.H * 0.03, 4, L.ClawTip * 0.92f, true);
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::ClawL : ETNFaunaBone::ClawR, FVector(L.D * 0.72, Side * L.W * 0.45, -L.H * 0.05), MoveTemp(M));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Ranas
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaFrogLook
	{
		double Len = 7.0;   ///< Semilongitud del cuerpo.
		double W = 5.5;
		double H = 3.8;
		FLinearColor Skin = FLinearColor(0.3f, 0.75f, 0.2f);
		FLinearColor Belly = FLinearColor(0.95f, 0.9f, 0.6f);
		FLinearColor Spot = FLinearColor(0.03f, 0.03f, 0.05f);
		FLinearColor LegC = FLinearColor(0.28f, 0.7f, 0.2f);
		FLinearColor Foot = FLinearColor(1.f, 0.55f, 0.1f);
		FLinearColor EyeBall = FLinearColor(0.95f, 0.12f, 0.06f);
		bool bSpots = false;
	};

	/** Rana sentada: cuerpo y cabeza en una pieza, ojos saltones, patas traseras plegadas en Z y delanteras cortas. */
	inline void TNFaunaBuildFrog(const FTNFaunaFrogLook& L, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig)
	{
		const double BodyZ = L.H * 1.05;
		Rig.BodyZ = BodyZ;
		Rig.HalfLen = L.Len;
		Rig.Height = BodyZ + L.H * 1.3;
		Rig.Draft = BodyZ;
		{
			FTNProcMeshBuffers M;
			TNFaunaBlob(M, FVector(-L.Len * 0.15, 0.0, 0.0), FVector(L.Len * 0.9, L.W, L.H), L.Skin, L.Belly, 8, 4);
			TNFaunaBlob(M, FVector(L.Len * 0.6, 0.0, L.H * 0.05), FVector(L.Len * 0.55, L.W * 0.85, L.H * 0.72), L.Skin, L.Belly, 7, 3);
			for (const double Side : { -1.0, 1.0 })
			{
				const FVector Bulge(L.Len * 0.68, Side * L.W * 0.5, L.H * 0.72);
				TNFaunaBlob(M, Bulge, FVector(L.H * 0.42, L.H * 0.4, L.H * 0.42), L.EyeBall, L.EyeBall * 0.85f, 6, 3);
				M.AddBox(Bulge + FVector(L.H * 0.32, Side * L.H * 0.1, L.H * 0.05), FVector::ForwardVector, FVector(L.H * 0.08, L.H * 0.18, L.H * 0.22), FLinearColor(0.03f, 0.03f, 0.04f));
			}
			if (L.bSpots)
			{
				for (int32 k = 0; k < 6; ++k)
				{
					const double SpotX = L.Len * (0.35 - 0.2 * k);
					const double SpotY = (k % 2 == 0 ? 0.35 : -0.3) * L.W;
					const double Rel = FMath::Square((SpotX + L.Len * 0.15) / (L.Len * 0.9)) + FMath::Square(SpotY / L.W);
					const double SpotZ = L.H * FMath::Sqrt(FMath::Max(0.05, 1.0 - Rel)) * 0.95;
					M.AddBox(FVector(SpotX, SpotY, SpotZ), FVector::ForwardVector, FVector(L.Len * 0.09, L.W * 0.11, L.H * 0.14), L.Spot);
				}
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M));
		}
		// Patas traseras plegadas junto al cuerpo: muslo hacia delante, espinilla hacia atrás y pie en el suelo.
		for (const double Side : { -1.0, 1.0 })
		{
			FTNProcMeshBuffers M;
			const double FootZ = -(BodyZ - L.H * 0.35);
			const FVector Knee(L.Len * 0.6, Side * L.W * 0.45, -L.H * 0.2);
			const FVector Ankle(-L.Len * 0.15, Side * L.W * 0.7, FootZ + L.H * 0.15);
			const FVector Toes(L.Len * 0.5, Side * L.W * 0.9, FootZ);
			M.AddBeam(FVector::ZeroVector, Knee, L.H * 0.3, L.LegC);
			M.AddBeam(Knee, Ankle, L.H * 0.2, L.LegC * 0.92f);
			M.AddBeam(Ankle, Toes, L.H * 0.14, L.Foot);
			TNFaunaSheet(M, { Toes, Toes + FVector(L.Len * 0.2, -Side * L.W * 0.15, 0.0), Toes + FVector(L.Len * 0.15, Side * L.W * 0.25, 0.0) }, FVector::UpVector, L.Foot, L.Foot * 0.8f);
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::LegBL : ETNFaunaBone::LegBR, FVector(-L.Len * 0.55, Side * L.W * 0.75, -L.H * 0.35), MoveTemp(M));
		}
		for (const double Side : { -1.0, 1.0 })
		{
			FTNProcMeshBuffers M;
			const double HandZ = -(BodyZ - L.H * 0.4);
			const FVector Hand(L.Len * 0.15, Side * L.W * 0.25, HandZ);
			M.AddBeam(FVector::ZeroVector, Hand, L.H * 0.14, L.LegC);
			M.AddBox(Hand + FVector(L.Len * 0.08, 0.0, L.H * 0.05), FVector::ForwardVector, FVector(L.Len * 0.12, L.W * 0.12, L.H * 0.06), L.Foot);
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::LegFL : ETNFaunaBone::LegFR, FVector(L.Len * 0.6, Side * L.W * 0.6, -L.H * 0.4), MoveTemp(M));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Peces
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaFishLook
	{
		double Len = 22.0;   ///< Semilongitud.
		double W = 5.0;
		double H = 8.5;
		FLinearColor Back = FLinearColor(0.1f, 0.52f, 0.86f);
		FLinearColor Belly = FLinearColor(0.9f, 0.93f, 0.96f);
		FLinearColor Fin = FLinearColor(1.f, 0.82f, 0.2f);
		FLinearColor Stripe = FLinearColor(1.f, 0.82f, 0.2f);
		/** Pez del fango: ojos arriba, aleta dorsal alta y aletas pectorales que hacen de patas. */
		bool bSkipper = false;
	};

	/** Pez: cuerpo aplanado con aletas y ojos, y la cola (hueso Tail); el del fango, con aletas-pata. */
	inline void TNFaunaBuildFish(const FTNFaunaFishLook& L, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig)
	{
		const double BodyZ = L.bSkipper ? L.H * 0.9 : 0.0;
		Rig.BodyZ = BodyZ;
		Rig.HalfLen = L.Len;
		Rig.Height = BodyZ + L.H * 1.6;
		Rig.Draft = 0.0;
		{
			FTNProcMeshBuffers M;
			TNFaunaBlob(M, FVector::ZeroVector, FVector(L.Len, L.W, L.H), L.Back, L.Belly, 8, 4);
			if (L.bSkipper)
			{
				TNFaunaSheet(M, { FVector(-L.Len * 0.45, 0.0, L.H * 0.7), FVector(L.Len * 0.1, 0.0, L.H * 0.85), FVector(-L.Len * 0.05, 0.0, L.H * 1.9),
					FVector(-L.Len * 0.5, 0.0, L.H * 1.6) }, FVector::RightVector, L.Stripe, L.Stripe * 0.85f);
				for (const double Side : { -1.0, 1.0 })
				{
					const FVector Bulge(L.Len * 0.55, Side * L.W * 0.35, L.H * 0.85);
					TNFaunaBlob(M, Bulge, FVector(L.H * 0.3, L.H * 0.28, L.H * 0.3), FLinearColor(0.85f, 0.82f, 0.7f), FLinearColor(0.7f, 0.66f, 0.55f), 6, 3);
					M.AddBox(Bulge + FVector(L.H * 0.22, Side * L.H * 0.1, L.H * 0.08), FVector::ForwardVector, FVector(L.H * 0.08, L.H * 0.14, L.H * 0.16), FLinearColor(0.03f, 0.03f, 0.04f));
				}
			}
			else
			{
				TNFaunaSheet(M, { FVector(-L.Len * 0.35, 0.0, L.H * 0.75), FVector(L.Len * 0.15, 0.0, L.H * 0.85), FVector(-L.Len * 0.25, 0.0, L.H * 1.55) },
					FVector::RightVector, L.Fin, L.Fin * 0.85f);
				for (const double Side : { -1.0, 1.0 })
				{
					TNFaunaSheet(M, { FVector(L.Len * 0.3, Side * L.W * 0.8, -L.H * 0.2), FVector(L.Len * 0.05, Side * L.W * 1.8, -L.H * 0.45), FVector(L.Len * 0.12, Side * L.W * 0.8, -L.H * 0.45) },
						FVector(0.0, Side, 0.0), L.Fin, L.Fin * 0.85f);
				}
				TNFaunaEyes(M, FVector(L.Len * 0.62, 0.0, L.H * 0.2), L.W * 0.75, L.H * 0.14);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M));
		}
		{
			FTNProcMeshBuffers M;
			if (L.bSkipper)
			{
				TNFaunaSheet(M, { FVector::ZeroVector, FVector(-L.Len * 0.4, 0.0, L.H * 0.6), FVector(-L.Len * 0.55, 0.0, 0.0), FVector(-L.Len * 0.4, 0.0, -L.H * 0.5) },
					FVector::RightVector, L.Fin, L.Fin * 0.85f);
			}
			else
			{
				TNFaunaSheet(M, { FVector::ZeroVector, FVector(-L.Len * 0.55, 0.0, L.H * 0.95), FVector(-L.Len * 0.3, 0.0, 0.0) }, FVector::RightVector, L.Fin, L.Fin * 0.85f);
				TNFaunaSheet(M, { FVector::ZeroVector, FVector(-L.Len * 0.3, 0.0, 0.0), FVector(-L.Len * 0.55, 0.0, -L.H * 0.95) }, FVector::RightVector, L.Fin, L.Fin * 0.85f);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Tail, FVector(-L.Len * 0.85, 0.0, 0.0), MoveTemp(M));
		}
		if (L.bSkipper)
		{
			for (const double Side : { -1.0, 1.0 })
			{
				FTNProcMeshBuffers M;
				const double Drop = BodyZ - L.H * 0.35;
				TNFaunaSheet(M, { FVector::ZeroVector, FVector(L.Len * 0.28, Side * L.W * 0.5, -Drop + 0.3), FVector(-L.Len * 0.12, Side * L.W * 0.55, -Drop + 0.3) },
					FVector(0.0, Side, 0.0), L.Fin, L.Fin * 0.85f);
				TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::LegFL : ETNFaunaBone::LegFR, FVector(L.Len * 0.3, Side * L.W * 0.85, -L.H * 0.35), MoveTemp(M));
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Tortugas
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaTurtleLook
	{
		double Len = 8.0;    ///< Semilongitud del caparazón.
		double W = 6.5;
		double H = 3.8;      ///< Alto de la cúpula.
		double Head = 2.4;
		double FlipF = 7.5;  ///< Largo de las aletas delanteras.
		double FlipB = 3.5;
		FLinearColor Shell = FLinearColor(0.33f, 0.27f, 0.16f);
		FLinearColor Scute = FLinearColor(0.45f, 0.36f, 0.2f);
		FLinearColor Rim = FLinearColor(0.25f, 0.2f, 0.13f);
		FLinearColor Belly = FLinearColor(0.82f, 0.76f, 0.58f);
		FLinearColor Skin = FLinearColor(0.32f, 0.31f, 0.3f);
	};

	/** Tortuga: cúpula de escudos alternos con el peto, cabeza con el cuello y cuatro aletas. */
	inline void TNFaunaBuildTurtle(const FTNFaunaTurtleLook& L, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig)
	{
		const double BodyZ = L.H * 0.45;
		Rig.BodyZ = BodyZ;
		Rig.HalfLen = L.Len;
		Rig.Height = BodyZ + L.H * 1.1;
		Rig.Draft = BodyZ + L.H * 0.25;
		{
			FTNProcMeshBuffers M;
			constexpr int32 ShellSeg = 10;
			FVector RingRim[ShellSeg];
			FVector RingMid[ShellSeg];
			FVector RingLow[ShellSeg];
			for (int32 k = 0; k < ShellSeg; ++k)
			{
				const double Ang = TNProcMap::TwoPi * (k + 0.5) / ShellSeg;
				const double Cx = FMath::Cos(Ang);
				const double Sy = FMath::Sin(Ang);
				RingRim[k] = FVector(Cx * L.Len, Sy * L.W, 0.0);
				RingMid[k] = FVector(Cx * L.Len * 0.72, Sy * L.W * 0.7, L.H * 0.62);
				RingLow[k] = FVector(Cx * L.Len * 0.85, Sy * L.W * 0.8, -L.H * 0.3);
			}
			const FVector DomeTop(0.0, 0.0, L.H);
			const FVector PlateBottom(0.0, 0.0, -L.H * 0.32);
			for (int32 k = 0; k < ShellSeg; ++k)
			{
				const int32 K1 = (k + 1) % ShellSeg;
				const bool bEven = (k % 2) == 0;
				M.AddQuad(RingRim[k], RingRim[K1], RingMid[K1], RingMid[k], (RingRim[k] + RingMid[K1]) * 0.5, bEven ? L.Scute : L.Shell);
				M.AddTri(DomeTop, RingMid[k], RingMid[K1], FVector::UpVector + (RingMid[k] + RingMid[K1]) * 0.01, bEven ? L.Shell * 1.05f : L.Scute * 1.05f);
				M.AddQuad(RingLow[k], RingLow[K1], RingRim[K1], RingRim[k], (RingRim[k] + RingLow[K1]) * 0.5, L.Rim);
				M.AddTri(PlateBottom, RingLow[K1], RingLow[k], -FVector::UpVector, L.Belly);
			}
			TNProcAddCylinder(M, FVector(-L.Len * 0.88, 0.0, -L.H * 0.1), FVector(-L.Len * 1.15, 0.0, -L.H * 0.2), L.H * 0.2, L.H * 0.05, 4, L.Skin, true);
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M));
		}
		{
			FTNProcMeshBuffers M;
			TNProcAddCylinder(M, FVector(-L.Head * 0.6, 0.0, 0.0), FVector(L.Head * 0.8, 0.0, L.Head * 0.25), L.Head * 0.55, L.Head * 0.5, 6, L.Skin, false);
			const FVector HeadAt(L.Head * 1.3, 0.0, L.Head * 0.35);
			TNFaunaBlob(M, HeadAt, FVector(L.Head * 1.05, L.Head * 0.8, L.Head * 0.72), L.Skin, L.Skin * 0.85f, 7, 3);
			TNFaunaEyes(M, HeadAt + FVector(L.Head * 0.35, 0.0, L.Head * 0.22), L.Head * 0.66, L.Head * 0.2);
			TNFaunaAddPart(Out, ETNFaunaBone::Head, FVector(L.Len * 0.9, 0.0, -L.H * 0.05), MoveTemp(M));
		}
		for (int32 FlipIdx = 0; FlipIdx < 4; ++FlipIdx)
		{
			const bool bFront = FlipIdx < 2;
			const double Side = (FlipIdx % 2 == 0) ? -1.0 : 1.0;
			const double FlipLen = bFront ? L.FlipF : L.FlipB;
			FTNProcMeshBuffers M;
			if (bFront)
			{
				TNFaunaBlob(M, FVector(-FlipLen * 0.12, Side * FlipLen * 0.55, 0.0), FVector(FlipLen * 0.26, FlipLen * 0.58, FMath::Max(0.6, FlipLen * 0.07)), L.Skin, L.Skin * 0.85f, 6, 3);
			}
			else
			{
				TNFaunaBlob(M, FVector(-FlipLen * 0.3, Side * FlipLen * 0.4, 0.0), FVector(FlipLen * 0.45, FlipLen * 0.4, FMath::Max(0.5, FlipLen * 0.08)), L.Skin, L.Skin * 0.85f, 6, 3);
			}
			const FVector Pivot = bFront ? FVector(L.Len * 0.55, Side * L.W * 0.8, -L.H * 0.12) : FVector(-L.Len * 0.7, Side * L.W * 0.65, -L.H * 0.12);
			const ETNFaunaBone Bone = bFront ? (Side < 0.0 ? ETNFaunaBone::LegFL : ETNFaunaBone::LegFR) : (Side < 0.0 ? ETNFaunaBone::LegBL : ETNFaunaBone::LegBR);
			TNFaunaAddPart(Out, Bone, Pivot, MoveTemp(M));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Escarabajo y murciélago
	// ─────────────────────────────────────────────────────────────────────────

	struct FTNFaunaBeetleLook
	{
		double Len = 9.0;
		double W = 6.0;
		double H = 4.5;
		FLinearColor Shell = FLinearColor(0.1f, 0.07f, 0.08f);
		FLinearColor Glow = FLinearColor(1.f, 0.46f, 0.06f);
		FLinearColor LegC = FLinearColor(0.32f, 0.08f, 0.05f);
		FLinearColor HornC = FLinearColor(0.16f, 0.1f, 0.1f);
	};

	/** Escarabajo rinoceronte con élitros de rayas que brillan (pieza con M_ProcGlow) y tres patas por lado. */
	inline void TNFaunaBuildBeetle(const FTNFaunaBeetleLook& L, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig)
	{
		const double BodyZ = L.H * 0.95;
		Rig.BodyZ = BodyZ;
		Rig.HalfLen = L.Len;
		Rig.Height = BodyZ + L.H * 1.3;
		Rig.Draft = BodyZ;
		{
			FTNProcMeshBuffers M;
			TNFaunaBlob(M, FVector(-L.Len * 0.15, 0.0, 0.0), FVector(L.Len * 0.85, L.W, L.H), L.Shell, L.Shell * 0.8f, 8, 4);
			TNFaunaBlob(M, FVector(L.Len * 0.55, 0.0, -L.H * 0.05), FVector(L.Len * 0.35, L.W * 0.75, L.H * 0.7), L.Shell * 1.2f, L.Shell, 7, 3);
			TNFaunaBlob(M, FVector(L.Len * 0.9, 0.0, -L.H * 0.2), FVector(L.Len * 0.2, L.W * 0.45, L.H * 0.45), L.Shell * 1.3f, L.Shell, 6, 3);
			TNProcAddCylinder(M, FVector(L.Len * 0.95, 0.0, 0.0), FVector(L.Len * 1.35, 0.0, L.H * 0.9), L.H * 0.18, L.H * 0.05, 5, L.HornC, true);
			for (const double Side : { -1.0, 1.0 })
			{
				M.AddBeam(FVector(L.Len * 1.0, Side * L.W * 0.2, -L.H * 0.05), FVector(L.Len * 1.35, Side * L.W * 0.55, L.H * 0.35), L.H * 0.05, L.LegC);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M));
		}
		{
			FTNProcMeshBuffers M;
			for (int32 k = -1; k <= 1; ++k)
			{
				const double StripeY = k * L.W * 0.5;
				const double StripeZ = L.H * (0.98 - 0.35 * FMath::Square(StripeY / L.W));
				M.AddBox(FVector(-L.Len * 0.15, StripeY, StripeZ), FVector::ForwardVector, FVector(L.Len * 0.6, L.W * 0.07, L.H * 0.08), L.Glow);
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector(0.0, 0.0, BodyZ), MoveTemp(M), true, false);
		}
		for (const double Side : { -1.0, 1.0 })
		{
			FTNProcMeshBuffers M;
			const double FootZ = -(BodyZ - L.H * 0.3);
			for (int32 k = 0; k < 3; ++k)
			{
				const double LegX = L.Len * (0.45 - 0.45 * k);
				const FVector Knee(LegX, Side * L.W * 0.6, L.H * 0.25);
				const FVector Foot(LegX * 1.3 - L.Len * 0.05, Side * L.W * 1.1, FootZ);
				M.AddBeam(FVector(LegX * 0.8, 0.0, 0.0), Knee, L.H * 0.09, L.LegC);
				M.AddBeam(Knee, Foot, L.H * 0.07, L.LegC * 0.9f);
			}
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::LegFL : ETNFaunaBone::LegFR, FVector(L.Len * 0.15, Side * L.W * 0.7, -L.H * 0.3), MoveTemp(M));
		}
	}

	struct FTNFaunaBatLook
	{
		double Len = 5.0;
		double W = 3.6;
		double H = 3.6;
		double Span = 26.0;   ///< Semienvergadura.
		double Ear = 3.2;
		FLinearColor Fur = FLinearColor(0.3f, 0.2f, 0.22f);
		FLinearColor WingC = FLinearColor(0.3f, 0.19f, 0.28f);
		FLinearColor BoneC = FLinearColor(0.18f, 0.12f, 0.16f);
		FLinearColor EarC = FLinearColor(0.36f, 0.23f, 0.26f);
	};

	/** Murciélago: cuerpo con cabeza y orejas, y dos membranas con los dedos marcados. La raíz es el cuerpo (vuela). */
	inline void TNFaunaBuildBat(const FTNFaunaBatLook& L, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig)
	{
		Rig.BodyZ = 0.0;
		Rig.HalfLen = L.Len;
		Rig.Height = L.H * 3.0;
		Rig.Draft = 0.0;
		{
			FTNProcMeshBuffers M;
			TNFaunaBlob(M, FVector::ZeroVector, FVector(L.Len, L.W, L.H), L.Fur, L.Fur * 0.85f, 7, 4);
			const FVector HeadAt(L.Len * 0.9, 0.0, L.H * 0.35);
			TNFaunaBlob(M, HeadAt, FVector(L.H * 0.75, L.H * 0.7, L.H * 0.7), L.Fur * 1.1f, L.Fur, 6, 3);
			for (const double Side : { -1.0, 1.0 })
			{
				TNFaunaSheet(M, { HeadAt + FVector(0.0, Side * L.H * 0.2, L.H * 0.4), HeadAt + FVector(-L.H * 0.2, Side * L.H * 0.55, L.H * 0.45 + L.Ear),
					HeadAt + FVector(-L.H * 0.45, Side * L.H * 0.6, L.H * 0.3) }, FVector::ForwardVector, L.EarC, L.EarC * 0.8f);
				M.AddBox(HeadAt + FVector(L.H * 0.55, Side * L.H * 0.32, L.H * 0.15), FVector::ForwardVector, FVector(L.H * 0.1, L.H * 0.1, L.H * 0.1), FLinearColor(0.9f, 0.2f, 0.15f));
			}
			TNFaunaAddPart(Out, ETNFaunaBone::Body, FVector::ZeroVector, MoveTemp(M));
		}
		for (const double Side : { -1.0, 1.0 })
		{
			FTNProcMeshBuffers M;
			const FVector P0(L.Len * 0.4, 0.0, 0.0);
			const FVector P1(L.Len * 0.25, Side * L.Span * 0.5, 0.5);
			const FVector P2(-L.Len * 0.1, Side * L.Span, 0.0);
			const FVector P3(-L.Len * 0.9, Side * L.Span * 0.72, 0.0);
			const FVector P4(-L.Len * 0.55, Side * L.Span * 0.45, 0.0);
			const FVector P5(-L.Len * 1.1, Side * L.Span * 0.3, 0.0);
			const FVector P6(-L.Len * 0.9, 0.0, 0.0);
			TNFaunaSheet(M, { P0, P1, P4 }, FVector::UpVector, L.WingC, L.WingC * 0.8f);
			TNFaunaSheet(M, { P1, P2, P3 }, FVector::UpVector, L.WingC, L.WingC * 0.8f);
			TNFaunaSheet(M, { P1, P3, P4 }, FVector::UpVector, L.WingC * 0.95f, L.WingC * 0.78f);
			TNFaunaSheet(M, { P0, P4, P6 }, FVector::UpVector, L.WingC, L.WingC * 0.8f);
			TNFaunaSheet(M, { P4, P5, P6 }, FVector::UpVector, L.WingC * 0.95f, L.WingC * 0.78f);
			M.AddBeam(P0 + FVector(0.0, 0.0, 0.3), P2 + FVector(0.0, 0.0, 0.3), 0.35, L.BoneC);
			M.AddBeam(P1 + FVector(0.0, 0.0, 0.3), P3 + FVector(0.0, 0.0, 0.3), 0.3, L.BoneC);
			TNFaunaAddPart(Out, Side < 0.0 ? ETNFaunaBone::WingL : ETNFaunaBone::WingR, FVector(L.Len * 0.15, Side * L.W * 0.8, L.H * 0.2), MoveTemp(M));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Receta de cada especie
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Piezas y esqueleto de una especie. Con OutJaw, un ave lleva el pico de abajo aparte, para abrirlo (TNFaunaBuildBird);
	 * los demás animales lo dejan vacío.
	 */
	inline void TNFaunaBuildSpecies(ETNFaunaSpecies Species, TArray<FTNFaunaPart>& Out, FTNFaunaRig& Rig, FTNFaunaBirdJaw* OutJaw = nullptr)
	{
		using FaunaSp = ETNFaunaSpecies;
		const FTNFaunaSpec& Sp = TNFaunaSpec(Species);
		const bool bShadow = Sp.bShadow;
		auto Rgb = [](float R, float G, float B) { return FLinearColor(R, G, B); };
		switch (Species)
		{
			case FaunaSp::Crab:
			{
				FTNFaunaCrabLook L;
				TNFaunaBuildCrab(L, Out, Rig);
				break;
			}
			case FaunaSp::FiddlerCrab:
			{
				FTNFaunaCrabLook L;
				L.W = 7.5; L.D = 5.5; L.H = 4.0; L.ClawL = 0.8; L.ClawR = 2.3;
				L.ShellTop = Rgb(0.32f, 0.36f, 0.9f); L.Shell = Rgb(0.85f, 0.82f, 0.7f); L.LegC = Rgb(0.55f, 0.42f, 0.78f);
				L.ClawC = Rgb(0.55f, 0.42f, 0.78f); L.ClawBigC = Rgb(1.f, 0.78f, 0.12f); L.ClawTip = Rgb(1.f, 0.95f, 0.75f);
				TNFaunaBuildCrab(L, Out, Rig);
				break;
			}
			case FaunaSp::BabyTurtle:
			{
				FTNFaunaTurtleLook L;
				TNFaunaBuildTurtle(L, Out, Rig);
				break;
			}
			case FaunaSp::SeaTurtle:
			{
				FTNFaunaTurtleLook L;
				L.Len = 50.0; L.W = 40.0; L.H = 19.0; L.Head = 11.0; L.FlipF = 46.0; L.FlipB = 18.0;
				L.Shell = Rgb(0.3f, 0.4f, 0.17f); L.Scute = Rgb(0.48f, 0.44f, 0.2f); L.Rim = Rgb(0.25f, 0.3f, 0.14f);
				L.Belly = Rgb(0.92f, 0.86f, 0.6f); L.Skin = Rgb(0.58f, 0.52f, 0.34f);
				TNFaunaBuildTurtle(L, Out, Rig);
				break;
			}
			case FaunaSp::Gull:
			{
				FTNFaunaBirdLook L;
				L.Len = 16.0; L.Girth = 8.0; L.Neck = 4.0; L.NeckR = 3.2; L.Head = 5.5; L.Beak = 6.0; L.BeakR = 1.4; L.Leg = 9.0; L.LegR = 0.9;
				L.Wing = 45.0; L.Tail = 10.0; L.TailUp = 8.0;
				L.Plumage = Rgb(0.96f, 0.96f, 0.94f); L.Belly = Rgb(0.98f, 0.98f, 0.96f); L.WingC = Rgb(0.62f, 0.66f, 0.72f); L.WingTip = Rgb(0.12f, 0.12f, 0.14f);
				L.HeadC = Rgb(0.97f, 0.97f, 0.95f); L.NeckC = L.HeadC; L.BeakC = Rgb(1.f, 0.8f, 0.15f); L.BeakTip = Rgb(0.9f, 0.2f, 0.1f);
				L.LegC = Rgb(0.95f, 0.66f, 0.52f); L.TailC = Rgb(0.9f, 0.9f, 0.9f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Sandpiper:
			{
				FTNFaunaBirdLook L;
				L.Len = 9.0; L.Girth = 5.0; L.Neck = 2.5; L.NeckR = 2.0; L.Head = 3.6; L.Beak = 5.5; L.BeakR = 0.6; L.Leg = 8.0; L.LegR = 0.5;
				L.Wing = 20.0; L.Tail = 5.0; L.TailUp = 5.0;
				L.Plumage = Rgb(0.64f, 0.52f, 0.36f); L.Belly = Rgb(0.96f, 0.94f, 0.9f); L.WingC = Rgb(0.55f, 0.44f, 0.3f); L.WingTip = Rgb(0.28f, 0.22f, 0.16f);
				L.HeadC = L.Plumage; L.NeckC = Rgb(0.8f, 0.72f, 0.6f); L.BeakC = Rgb(0.12f, 0.1f, 0.1f); L.BeakTip = L.BeakC;
				L.LegC = Rgb(0.25f, 0.22f, 0.2f); L.TailC = Rgb(0.4f, 0.33f, 0.24f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Toucan:
			{
				FTNFaunaBirdLook L;
				L.Len = 13.0; L.Girth = 8.0; L.Neck = 2.0; L.NeckR = 3.5; L.Head = 6.0; L.Beak = 17.0; L.BeakR = 4.0; L.Leg = 6.0; L.LegR = 1.0;
				L.Wing = 24.0; L.Tail = 13.0; L.TailUp = -10.0; L.Extra = 5;
				L.Plumage = Rgb(0.07f, 0.07f, 0.08f); L.Belly = Rgb(0.1f, 0.1f, 0.1f); L.WingC = Rgb(0.08f, 0.08f, 0.09f); L.WingTip = Rgb(0.05f, 0.05f, 0.06f);
				L.HeadC = L.Plumage; L.NeckC = Rgb(1.f, 0.86f, 0.2f); L.BeakC = Rgb(1.f, 0.58f, 0.05f); L.BeakTip = Rgb(0.9f, 0.12f, 0.05f);
				L.LegC = Rgb(0.35f, 0.45f, 0.7f); L.TailC = L.Plumage; L.ExtraC = Rgb(1.f, 0.86f, 0.2f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Heron:
			{
				FTNFaunaBirdLook L;
				L.Len = 17.0; L.Girth = 9.0; L.Neck = 36.0; L.NeckR = 2.6; L.NeckFwd = 0.35; L.Head = 5.5; L.Beak = 15.0; L.BeakR = 1.3; L.Leg = 45.0; L.LegR = 1.1;
				L.Wing = 55.0; L.Tail = 7.0; L.TailUp = -5.0;
				L.Plumage = Rgb(0.97f, 0.97f, 0.95f); L.Belly = L.Plumage; L.WingC = L.Plumage; L.WingTip = Rgb(0.92f, 0.92f, 0.94f);
				L.HeadC = L.Plumage; L.NeckC = L.Plumage; L.BeakC = Rgb(1.f, 0.82f, 0.12f); L.BeakTip = L.BeakC;
				L.LegC = Rgb(0.12f, 0.12f, 0.12f); L.TailC = L.Plumage;
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Flamingo:
			{
				FTNFaunaBirdLook L;
				L.Len = 20.0; L.Girth = 11.0; L.Neck = 48.0; L.NeckR = 2.6; L.NeckFwd = 0.45; L.Head = 5.5; L.Beak = 11.0; L.BeakR = 2.0; L.BeakDroop = 1.0;
				L.Leg = 70.0; L.LegR = 1.3; L.Wing = 50.0; L.Tail = 7.0;
				L.Plumage = Rgb(1.f, 0.52f, 0.62f); L.Belly = Rgb(1.f, 0.64f, 0.72f); L.WingC = Rgb(0.98f, 0.42f, 0.52f); L.WingTip = Rgb(0.08f, 0.08f, 0.08f);
				L.HeadC = Rgb(1.f, 0.56f, 0.66f); L.NeckC = L.HeadC; L.BeakC = Rgb(0.96f, 0.88f, 0.84f); L.BeakTip = Rgb(0.08f, 0.08f, 0.08f);
				L.LegC = Rgb(0.98f, 0.5f, 0.58f); L.TailC = Rgb(0.98f, 0.45f, 0.55f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Pelican:
			{
				FTNFaunaBirdLook L;
				L.Len = 32.0; L.Girth = 17.0; L.Neck = 18.0; L.NeckR = 5.0; L.NeckFwd = 0.2; L.Head = 8.0; L.Beak = 34.0; L.BeakR = 3.0; L.BeakDroop = 0.1;
				L.Leg = 14.0; L.LegR = 2.0; L.Wing = 85.0; L.Tail = 9.0; L.Extra = 3;
				L.Plumage = Rgb(0.93f, 0.93f, 0.9f); L.Belly = Rgb(0.96f, 0.96f, 0.94f); L.WingC = Rgb(0.78f, 0.78f, 0.8f); L.WingTip = Rgb(0.15f, 0.15f, 0.16f);
				L.HeadC = Rgb(0.97f, 0.96f, 0.88f); L.NeckC = Rgb(0.95f, 0.95f, 0.93f); L.BeakC = Rgb(1.f, 0.78f, 0.25f); L.BeakTip = Rgb(0.95f, 0.5f, 0.15f);
				L.LegC = Rgb(0.95f, 0.55f, 0.2f); L.TailC = Rgb(0.85f, 0.85f, 0.85f); L.ExtraC = Rgb(1.f, 0.6f, 0.22f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Vulture:
			{
				FTNFaunaBirdLook L;
				L.Len = 26.0; L.Girth = 15.0; L.Neck = 10.0; L.NeckR = 3.0; L.NeckFwd = 0.5; L.Head = 6.0; L.Beak = 6.0; L.BeakR = 1.6; L.BeakDroop = 0.5;
				L.Leg = 16.0; L.LegR = 1.6; L.Wing = 80.0; L.Tail = 13.0; L.Extra = 4;
				L.Plumage = Rgb(0.18f, 0.13f, 0.1f); L.Belly = Rgb(0.22f, 0.16f, 0.12f); L.WingC = Rgb(0.2f, 0.15f, 0.11f); L.WingTip = Rgb(0.08f, 0.06f, 0.05f);
				L.HeadC = Rgb(0.88f, 0.38f, 0.32f); L.NeckC = Rgb(0.85f, 0.55f, 0.5f); L.BeakC = Rgb(0.88f, 0.84f, 0.72f); L.BeakTip = Rgb(0.25f, 0.2f, 0.18f);
				L.LegC = Rgb(0.55f, 0.5f, 0.45f); L.TailC = Rgb(0.15f, 0.1f, 0.08f); L.ExtraC = Rgb(0.92f, 0.9f, 0.84f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Eagle:
			{
				FTNFaunaBirdLook L;
				L.Len = 26.0; L.Girth = 14.0; L.Neck = 7.0; L.NeckR = 5.0; L.Head = 8.0; L.Beak = 8.0; L.BeakR = 2.2; L.BeakDroop = 0.6;
				L.Leg = 14.0; L.LegR = 2.0; L.Wing = 85.0; L.Tail = 15.0;
				L.Plumage = Rgb(0.36f, 0.23f, 0.12f); L.Belly = Rgb(0.32f, 0.2f, 0.1f); L.WingC = Rgb(0.33f, 0.21f, 0.11f); L.WingTip = Rgb(0.14f, 0.09f, 0.05f);
				L.HeadC = Rgb(0.97f, 0.97f, 0.95f); L.NeckC = L.HeadC; L.BeakC = Rgb(1.f, 0.8f, 0.12f); L.BeakTip = Rgb(0.95f, 0.7f, 0.1f);
				L.LegC = Rgb(1.f, 0.8f, 0.15f); L.TailC = Rgb(0.97f, 0.97f, 0.95f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Pigeon:
			{
				FTNFaunaBirdLook L;
				L.Len = 13.0; L.Girth = 8.0; L.Neck = 5.0; L.NeckR = 3.5; L.NeckFwd = 0.25; L.Head = 4.8; L.Beak = 3.0; L.BeakR = 0.9; L.Leg = 5.0; L.LegR = 0.8;
				L.Wing = 30.0; L.Tail = 10.0;
				L.Plumage = Rgb(0.56f, 0.59f, 0.67f); L.Belly = Rgb(0.64f, 0.66f, 0.74f); L.WingC = Rgb(0.64f, 0.67f, 0.74f); L.WingTip = Rgb(0.25f, 0.26f, 0.3f);
				L.HeadC = Rgb(0.45f, 0.48f, 0.56f); L.NeckC = Rgb(0.32f, 0.6f, 0.52f); L.BeakC = Rgb(0.22f, 0.22f, 0.24f); L.BeakTip = L.BeakC;
				L.LegC = Rgb(0.92f, 0.36f, 0.36f); L.TailC = Rgb(0.4f, 0.42f, 0.5f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Hen:
			{
				FTNFaunaBirdLook L;
				L.Len = 18.0; L.Girth = 13.0; L.Neck = 8.0; L.NeckR = 4.5; L.NeckFwd = 0.2; L.Head = 6.0; L.Beak = 4.0; L.BeakR = 1.4; L.Leg = 11.0; L.LegR = 1.3;
				L.Wing = 24.0; L.Tail = 13.0; L.TailUp = 55.0; L.Extra = 1;
				L.Plumage = Rgb(0.74f, 0.36f, 0.15f); L.Belly = Rgb(0.82f, 0.47f, 0.22f); L.WingC = Rgb(0.64f, 0.3f, 0.12f); L.WingTip = Rgb(0.46f, 0.2f, 0.08f);
				L.HeadC = Rgb(0.76f, 0.38f, 0.17f); L.NeckC = Rgb(0.82f, 0.5f, 0.2f); L.BeakC = Rgb(1.f, 0.8f, 0.25f); L.BeakTip = L.BeakC;
				L.LegC = Rgb(1.f, 0.8f, 0.22f); L.TailC = Rgb(0.28f, 0.14f, 0.08f); L.ExtraC = Rgb(0.92f, 0.12f, 0.1f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Roadrunner:
			{
				FTNFaunaBirdLook L;
				L.Len = 16.0; L.Girth = 7.0; L.Neck = 8.0; L.NeckR = 2.8; L.NeckFwd = 0.45; L.Head = 5.5; L.Beak = 8.0; L.BeakR = 1.2; L.Leg = 17.0; L.LegR = 1.0;
				L.Wing = 24.0; L.Tail = 28.0; L.TailUp = 22.0; L.Extra = 2;
				L.Plumage = Rgb(0.46f, 0.36f, 0.22f); L.Belly = Rgb(0.92f, 0.88f, 0.78f); L.WingC = Rgb(0.36f, 0.28f, 0.2f); L.WingTip = Rgb(0.2f, 0.15f, 0.1f);
				L.HeadC = Rgb(0.42f, 0.32f, 0.2f); L.NeckC = Rgb(0.7f, 0.62f, 0.5f); L.BeakC = Rgb(0.22f, 0.2f, 0.18f); L.BeakTip = L.BeakC;
				L.LegC = Rgb(0.42f, 0.52f, 0.68f); L.TailC = Rgb(0.3f, 0.25f, 0.2f); L.ExtraC = Rgb(0.28f, 0.2f, 0.14f);
				TNFaunaBuildBird(L, bShadow, Out, Rig, OutJaw);
				break;
			}
			case FaunaSp::Monkey:
			{
				FTNFaunaQuadLook L;
				L.Len = 16.0; L.Width = 10.0; L.Girth = 11.0; L.LegF = 30.0; L.LegB = 22.0; L.LegR = 3.2; L.Head = 9.5; L.Snout = 4.5; L.HeadUp = 0.55;
				L.Ear = 4.0; L.EarKind = 0; L.Tail = 48.0; L.TailR = 1.8; L.TailUp = 55.0; L.TailCurl = 90.0;
				L.Fur = Rgb(0.46f, 0.29f, 0.15f); L.Belly = Rgb(0.82f, 0.66f, 0.46f); L.HeadC = L.Fur; L.SnoutC = Rgb(0.94f, 0.8f, 0.62f);
				L.Nose = Rgb(0.28f, 0.16f, 0.1f); L.LegC = Rgb(0.42f, 0.26f, 0.14f); L.Paw = Rgb(0.3f, 0.18f, 0.1f); L.TailC = Rgb(0.44f, 0.28f, 0.15f);
				L.TailTip = Rgb(0.36f, 0.22f, 0.12f); L.EarC = Rgb(0.9f, 0.72f, 0.55f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Capybara:
			{
				FTNFaunaQuadLook L;
				L.Len = 40.0; L.Width = 18.0; L.Girth = 21.0; L.LegF = 17.0; L.LegB = 17.0; L.LegR = 5.5; L.Head = 14.0; L.Snout = 13.0; L.HeadUp = 0.35;
				L.Ear = 3.0; L.EarKind = 0; L.Tail = 0.0;
				L.Fur = Rgb(0.6f, 0.41f, 0.24f); L.Belly = Rgb(0.64f, 0.46f, 0.3f); L.HeadC = Rgb(0.56f, 0.38f, 0.22f); L.SnoutC = Rgb(0.48f, 0.32f, 0.19f);
				L.Nose = Rgb(0.15f, 0.1f, 0.08f); L.LegC = Rgb(0.48f, 0.32f, 0.18f); L.Paw = Rgb(0.25f, 0.18f, 0.12f); L.EarC = Rgb(0.42f, 0.28f, 0.18f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Meerkat:
			{
				FTNFaunaQuadLook L;
				L.Len = 12.0; L.Width = 6.0; L.Girth = 7.0; L.LegF = 9.0; L.LegB = 10.0; L.LegR = 1.8; L.Head = 5.5; L.Snout = 4.5; L.HeadUp = 0.45;
				L.Ear = 2.0; L.EarKind = 0; L.Tail = 20.0; L.TailR = 1.2; L.TailUp = 15.0; L.bMask = true;
				L.Fur = Rgb(0.8f, 0.66f, 0.46f); L.Belly = Rgb(0.92f, 0.82f, 0.64f); L.HeadC = Rgb(0.86f, 0.73f, 0.54f); L.SnoutC = L.HeadC;
				L.Nose = Rgb(0.1f, 0.08f, 0.07f); L.LegC = Rgb(0.72f, 0.58f, 0.4f); L.Paw = Rgb(0.3f, 0.22f, 0.15f); L.TailC = Rgb(0.76f, 0.62f, 0.42f);
				L.TailTip = Rgb(0.18f, 0.14f, 0.1f); L.EarC = Rgb(0.3f, 0.22f, 0.15f); L.MaskC = Rgb(0.28f, 0.2f, 0.14f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Ibex:
			{
				FTNFaunaQuadLook L;
				L.Len = 42.0; L.Width = 16.0; L.Girth = 21.0; L.LegF = 48.0; L.LegB = 48.0; L.LegR = 4.0; L.Head = 12.0; L.Snout = 10.0; L.HeadUp = 0.75;
				L.Ear = 6.0; L.EarKind = 4; L.Tail = 7.0; L.TailR = 2.5; L.TailUp = 40.0; L.Horn = 55.0; L.bBeard = true;
				L.Fur = Rgb(0.6f, 0.52f, 0.42f); L.Belly = Rgb(0.92f, 0.9f, 0.84f); L.HeadC = Rgb(0.56f, 0.48f, 0.39f); L.SnoutC = Rgb(0.5f, 0.43f, 0.35f);
				L.Nose = Rgb(0.15f, 0.12f, 0.1f); L.LegC = Rgb(0.46f, 0.39f, 0.31f); L.Paw = Rgb(0.14f, 0.11f, 0.09f); L.TailC = Rgb(0.35f, 0.28f, 0.22f);
				L.TailTip = Rgb(0.2f, 0.16f, 0.12f); L.EarC = L.HeadC; L.HornC = Rgb(0.42f, 0.35f, 0.27f); L.MaskC = Rgb(0.25f, 0.2f, 0.16f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Marmot:
			{
				FTNFaunaQuadLook L;
				L.Len = 20.0; L.Width = 13.0; L.Girth = 13.0; L.LegF = 9.0; L.LegB = 9.0; L.LegR = 3.5; L.Head = 9.0; L.Snout = 5.0; L.HeadUp = 0.5;
				L.Ear = 2.2; L.EarKind = 0; L.Tail = 15.0; L.TailR = 3.0; L.TailUp = 10.0;
				L.Fur = Rgb(0.64f, 0.46f, 0.26f); L.Belly = Rgb(0.8f, 0.64f, 0.42f); L.HeadC = Rgb(0.48f, 0.35f, 0.22f); L.SnoutC = Rgb(0.84f, 0.76f, 0.64f);
				L.Nose = Rgb(0.12f, 0.1f, 0.08f); L.LegC = Rgb(0.45f, 0.32f, 0.2f); L.Paw = Rgb(0.25f, 0.18f, 0.12f); L.TailC = Rgb(0.38f, 0.27f, 0.16f);
				L.TailTip = Rgb(0.24f, 0.17f, 0.1f); L.EarC = Rgb(0.4f, 0.3f, 0.2f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Cat:
			{
				FTNFaunaQuadLook L;
				L.Len = 21.0; L.Width = 8.0; L.Girth = 9.5; L.LegF = 19.0; L.LegB = 21.0; L.LegR = 2.3; L.Head = 7.5; L.Snout = 3.2; L.HeadUp = 0.6;
				L.Ear = 5.5; L.EarKind = 1; L.Tail = 30.0; L.TailR = 1.8; L.TailUp = 55.0; L.TailCurl = 45.0;
				L.Fur = Rgb(0.98f, 0.58f, 0.2f); L.Belly = Rgb(0.98f, 0.93f, 0.86f); L.HeadC = L.Fur; L.SnoutC = Rgb(0.99f, 0.95f, 0.9f);
				L.Nose = Rgb(0.95f, 0.5f, 0.55f); L.LegC = Rgb(0.96f, 0.56f, 0.19f); L.Paw = Rgb(0.99f, 0.96f, 0.92f); L.TailC = Rgb(0.96f, 0.55f, 0.18f);
				L.TailTip = Rgb(0.85f, 0.45f, 0.12f); L.EarC = L.Fur;
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Rabbit:
			{
				FTNFaunaQuadLook L;
				L.Len = 15.0; L.Width = 10.0; L.Girth = 11.0; L.LegF = 8.0; L.LegB = 12.0; L.LegR = 3.0; L.Head = 7.5; L.Snout = 3.5; L.HeadUp = 0.6;
				L.Ear = 14.0; L.EarKind = 2; L.Tail = 3.0; L.TailR = 3.0; L.TailUp = 30.0; L.bPuffTail = true;
				L.Fur = Rgb(0.72f, 0.62f, 0.52f); L.Belly = Rgb(0.95f, 0.92f, 0.88f); L.HeadC = L.Fur; L.SnoutC = Rgb(0.93f, 0.89f, 0.84f);
				L.Nose = Rgb(0.95f, 0.55f, 0.6f); L.LegC = Rgb(0.68f, 0.58f, 0.48f); L.Paw = Rgb(0.93f, 0.9f, 0.86f); L.TailC = Rgb(0.98f, 0.98f, 0.96f);
				L.TailTip = L.TailC; L.EarC = Rgb(0.74f, 0.63f, 0.53f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Lizard:
			{
				FTNFaunaQuadLook L;
				L.bSprawl = true; L.Len = 11.0; L.Width = 4.5; L.Girth = 3.8; L.LegF = 8.0; L.LegB = 9.0; L.LegR = 1.3; L.Head = 4.5; L.Snout = 3.5;
				L.EarKind = 3; L.Tail = 28.0; L.TailR = 2.0;
				L.Fur = Rgb(0.1f, 0.72f, 0.62f); L.Belly = Rgb(0.96f, 0.86f, 0.36f); L.HeadC = Rgb(1.f, 0.76f, 0.16f); L.SnoutC = L.HeadC;
				L.Nose = Rgb(0.2f, 0.15f, 0.05f); L.LegC = Rgb(0.1f, 0.6f, 0.52f); L.Paw = Rgb(0.08f, 0.45f, 0.4f); L.TailC = Rgb(0.1f, 0.66f, 0.58f);
				L.TailTip = Rgb(0.05f, 0.36f, 0.36f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::Salamander:
			{
				FTNFaunaQuadLook L;
				L.bSprawl = true; L.Len = 13.0; L.Width = 5.5; L.Girth = 4.5; L.LegF = 8.0; L.LegB = 9.0; L.LegR = 1.6; L.Head = 5.0; L.Snout = 2.5;
				L.EarKind = 3; L.Tail = 24.0; L.TailR = 2.4; L.bGlowSpots = true;
				L.Fur = Rgb(0.08f, 0.07f, 0.08f); L.Belly = Rgb(0.2f, 0.12f, 0.1f); L.HeadC = Rgb(0.1f, 0.08f, 0.09f); L.SnoutC = L.HeadC;
				L.Nose = Rgb(0.03f, 0.03f, 0.03f); L.LegC = Rgb(0.09f, 0.08f, 0.09f); L.Paw = Rgb(0.14f, 0.1f, 0.1f); L.TailC = L.Fur; L.TailTip = L.Fur;
				L.GlowC = Rgb(1.f, 0.48f, 0.06f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::MarineIguana:
			{
				FTNFaunaQuadLook L;
				L.bSprawl = true; L.Len = 24.0; L.Width = 9.0; L.Girth = 8.0; L.LegF = 13.0; L.LegB = 15.0; L.LegR = 2.6; L.Head = 8.0; L.Snout = 3.0;
				L.EarKind = 3; L.Tail = 45.0; L.TailR = 4.0; L.bCrest = true;
				L.Fur = Rgb(0.2f, 0.2f, 0.22f); L.Belly = Rgb(0.42f, 0.24f, 0.2f); L.HeadC = Rgb(0.32f, 0.32f, 0.3f); L.SnoutC = Rgb(0.3f, 0.3f, 0.28f);
				L.Nose = Rgb(0.1f, 0.1f, 0.1f); L.LegC = Rgb(0.18f, 0.18f, 0.2f); L.Paw = Rgb(0.14f, 0.14f, 0.15f); L.TailC = L.Fur;
				L.TailTip = Rgb(0.16f, 0.16f, 0.18f); L.CrestC = Rgb(0.55f, 0.55f, 0.5f);
				TNFaunaBuildQuad(L, bShadow, Out, Rig);
				break;
			}
			case FaunaSp::DartFrog:
			{
				FTNFaunaFrogLook L;
				L.Len = 7.0; L.W = 5.5; L.H = 3.8; L.bSpots = true;
				L.Skin = Rgb(0.12f, 0.42f, 0.98f); L.Belly = Rgb(0.06f, 0.22f, 0.62f); L.Spot = Rgb(0.03f, 0.03f, 0.05f);
				L.LegC = Rgb(0.1f, 0.34f, 0.88f); L.Foot = Rgb(0.08f, 0.26f, 0.7f); L.EyeBall = Rgb(0.05f, 0.05f, 0.06f);
				TNFaunaBuildFrog(L, Out, Rig);
				break;
			}
			case FaunaSp::TreeFrog:
			{
				FTNFaunaFrogLook L;
				L.Len = 8.0; L.W = 6.0; L.H = 4.0;
				TNFaunaBuildFrog(L, Out, Rig);
				break;
			}
			case FaunaSp::Fish:
			{
				FTNFaunaFishLook L;
				TNFaunaBuildFish(L, Out, Rig);
				break;
			}
			case FaunaSp::Mudskipper:
			{
				FTNFaunaFishLook L;
				L.Len = 10.0; L.W = 4.0; L.H = 4.0; L.bSkipper = true;
				L.Back = Rgb(0.52f, 0.47f, 0.36f); L.Belly = Rgb(0.78f, 0.74f, 0.64f); L.Fin = Rgb(0.44f, 0.4f, 0.32f); L.Stripe = Rgb(0.22f, 0.62f, 1.f);
				TNFaunaBuildFish(L, Out, Rig);
				break;
			}
			case FaunaSp::FireBeetle:
			{
				FTNFaunaBeetleLook L;
				TNFaunaBuildBeetle(L, Out, Rig);
				break;
			}
			case FaunaSp::Bat:
			{
				FTNFaunaBatLook L;
				TNFaunaBuildBat(L, Out, Rig);
				break;
			}
			default:
				break;
		}
	}
}
