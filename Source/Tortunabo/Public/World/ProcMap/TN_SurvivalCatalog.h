#pragma once

#include "CoreMinimal.h"

/**
 * Catálogo de mapas de Supervivencia (#515, decisión en #143): 50 semillas elegidas entre 750 exportadas con
 * TN.Survival.Export, 9 por dificultad de la 1 a la 4 y 14 en la 5, cada una con un tipo de terreno y las trampas
 * del Clásico que le encajan. Ninguna semilla se repite: la misma semilla da casi el mismo terreno en todas las
 * dificultades.
 *
 * Las trampas van en % del recorrido del camino principal (0 = salida, 100 = meta). Las colocan #516 y #517; el
 * nivel N de la partida juega una entrada de su dificultad (#518). La densidad se dobló tras probarlo (#516): de
 * 2 a 3 puntos con trampa por mapa en la dificultad 1 a 4-5, y de 4-5 a 8-10 en la 5; los añadidos rellenan los
 * tramos vacíos con la trampa que pide el terreno (cáscaras en curvas y bajadas, medusas en estrechos, zonas
 * lentas en subidas, cangrejos en rectas).
 *
 * Cada entrada guarda la huella de su layout (TNProcMap::LayoutFingerprint). Si el generador cambia, el test
 * Tortunabo.Survival.Catalogo.Huellas falla: hay que revisar los mapas que cambian y, si siguen valiendo, poner
 * las huellas nuevas que imprime el test.
 */

namespace TNSurvivalCatalog
{
	/** Tipo de terreno: lo que da identidad al mapa. */
	enum class EMapKind : uint8
	{
		Plain,        ///< Llanura: abierto, recto y con poco desnivel.
		Gorge,        ///< Desfiladero: estrecho y encajado, sin poder rodear.
		Serpent,      ///< Serpiente: muchas curvas.
		Jumps,        ///< Saltos: muchos huecos y largos.
		Summit,       ///< Cumbre: mucho desnivel.
		Crossroads    ///< Encrucijada: tiene una rama.
	};

	/** Trampas del Clásico que se ponen sobre el mapa. */
	enum class ETrap : uint8
	{
		BananaPeel,       ///< Cáscaras de plátano (ATN_BananaPeel).
		SlowZone,         ///< Zona lenta (ATN_SlowZoneVolume). Nunca justo antes de un hueco de salto.
		Jellyfish,        ///< Medusa saltarina (ATN_JellyfishActor).
		Crab,             ///< Cangrejo gigante de la playa (ATN_BeachGiantCrab, #734; antes, zonas de cangrejos pequeños).
		Seagull,          ///< Zonas de gaviotas de la playa (ATN_BeachGullZone, #733; antes, ATN_SeagullSpawnZone) con sombrillas.
		Quad,             ///< Quad que cruza el camino (ATN_QuadActor).
		BreakableBridge,  ///< Puente que se rompe (ATN_BreakablePlatform), en lugar de la viga de un hueco.
		PressurePlate,    ///< Placa que abre el atajo de la rama (ATN_PressurePlate), resoluble con un jugador.
		// Criaturas y peligros del Excel de diseño (lote #691): al final, para no mover los valores de las anteriores.
		Quicksand,        ///< Arenas movedizas (ATN_Quicksand): ralentizan cada vez más y atrapan (#684).
		DragCrab,         ///< Cangrejo arrastrador (ATN_BeachDragCrab) (#685).
		BurrowCrab,       ///< Cangrejo subterráneo (ATN_BeachBurrowCrab) (#686).
		UrchinSpikes,     ///< Erizo enterrado (ATN_BeachUrchinSpikes) (#687).
		TankTrap,         ///< Erizo checo (ATN_BeachTankTrap) (#688).
		TrashPile,        ///< Montón de basura que se rompe (ATN_BeachTrashPile) (#690).
		Trench,           ///< Agujero de trinchera con rampa (ATN_BeachTrench) (#690).
		// Piezas de la carrera de la playa (#731, #732): al final, para no mover los valores de las anteriores.
		Seaweed,          ///< Algas que enredan (ATN_BeachSeaweed), de lado a lado del camino.
		BarbedWire,       ///< Alambre de espino (ATN_BeachBarbedWire) atravesado; deja el paso libre por un lado.
		ClamTrap,         ///< Concha que atrapa (ATN_BeachClamTrap).
		ShellGate,        ///< Puerta de conchas (ATN_BeachShellGate) en la entrada de una rama: la otra forma de atajo.
		Mine,             ///< Mina (ATN_BeachMine): en Supervivencia elimina.
		SeaUrchin,        ///< Erizo de mar (ATN_BeachSeaUrchin): en Supervivencia elimina.
		HermitCrab        ///< Cangrejo ermitaño bola (ATN_BeachHermitCrab) en un tramo recto: en Supervivencia elimina.
	};

	/** La última trampa del enum (para recorrerlas todas en las pruebas). */
	inline constexpr ETrap LastTrap = ETrap::HermitCrab;

	/** Nombre de la trampa para el registro (en español, sin traducir: no se ve en pantalla). */
	inline const TCHAR* TrapName(ETrap T)
	{
		switch (T)
		{
			case ETrap::BananaPeel:      return TEXT("cáscaras");
			case ETrap::SlowZone:        return TEXT("zonas lentas");
			case ETrap::Jellyfish:       return TEXT("medusas");
			case ETrap::Crab:            return TEXT("cangrejos gigantes");
			case ETrap::Seagull:         return TEXT("zonas de gaviotas");
			case ETrap::Quad:            return TEXT("quads");
			case ETrap::BreakableBridge: return TEXT("puentes que se rompen");
			case ETrap::PressurePlate:   return TEXT("placas");
			case ETrap::Quicksand:       return TEXT("arenas movedizas");
			case ETrap::DragCrab:        return TEXT("cangrejos arrastradores");
			case ETrap::BurrowCrab:      return TEXT("cangrejos subterráneos");
			case ETrap::UrchinSpikes:    return TEXT("erizos enterrados");
			case ETrap::TankTrap:        return TEXT("erizos checos");
			case ETrap::TrashPile:       return TEXT("montones de basura");
			case ETrap::Trench:          return TEXT("trincheras");
			case ETrap::Seaweed:         return TEXT("algas");
			case ETrap::BarbedWire:      return TEXT("alambres de espino");
			case ETrap::ClamTrap:        return TEXT("conchas que atrapan");
			case ETrap::ShellGate:       return TEXT("puertas de conchas");
			case ETrap::Mine:            return TEXT("minas");
			case ETrap::SeaUrchin:       return TEXT("erizos de mar");
			case ETrap::HermitCrab:      return TEXT("cangrejos ermitaños");
			default:                     return TEXT("?");
		}
	}

	/** Una trampa del catálogo: en un punto (From == To) o a lo largo de un tramo del recorrido. */
	struct FTrapSpot
	{
		uint32 Seed;
		ETrap Trap;
		uint8 FromPct;
		uint8 ToPct;
		/**
		 * Cuántas: cáscaras, quads, medusas, placas o piezas de la playa del punto o del tramo. Cangrejos gigantes, como mucho
		 * uno cada 20 m del tramo (GiantCrabCount).
		 */
		uint8 Count = 1;
		/** Solo gaviotas: sombrillas del tramo (2 si no se dice). */
		uint8 Umbrellas = 0;
	};

	struct FMapEntry
	{
		uint32 Seed;
		int32 Difficulty;
		/** Nombre de trabajo (logs y pruebas). Si se enseña en pantalla, va con NSLOCTEXT y traducido (#518). */
		const TCHAR* Name;
		EMapKind Kind;
		/** Huella del layout de (Seed, Difficulty) con el generador del catálogo. */
		uint64 Fingerprint;
	};

	inline constexpr FMapEntry Maps[] = {
		// ── Dificultad 1 ──
		{  63, 1, TEXT("La Ladera Abierta"),         EMapKind::Plain,      0x4874FB2DE5F9892Dull },
		{  52, 1, TEXT("Paseo de los Cangrejos"),    EMapKind::Plain,      0x457F04B5526C2081ull },
		{  33, 1, TEXT("El Arroyo Lento"),           EMapKind::Serpent,    0x183B98BCE7BC2217ull },
		{ 121, 1, TEXT("Bajada en Eses"),            EMapKind::Serpent,    0xDA338888CFB5EE74ull },
		{  71, 1, TEXT("El Mirador"),                EMapKind::Summit,     0xBDE7AD78D08AA731ull },
		{ 130, 1, TEXT("Lomo de Ballena"),           EMapKind::Summit,     0x5B5F361B8452616Aull },
		{ 101, 1, TEXT("La Garganta"),               EMapKind::Gorge,      0xFA239663654FB40Bull },
		{ 116, 1, TEXT("El Primer Salto"),           EMapKind::Jumps,      0x32B0AB7885A1E9A1ull },
		{  94, 1, TEXT("Las Dos Sendas"),            EMapKind::Crossroads, 0x3CE9C1D2C004A9CAull },
		// ── Dificultad 2 ──
		{  30, 2, TEXT("La Cornisa"),                EMapKind::Plain,      0x7828AE0F5F51564Cull },
		{  45, 2, TEXT("Las Revueltas"),             EMapKind::Serpent,    0xAD33F862ADEF2659ull },
		{  75, 2, TEXT("El Laberinto Llano"),        EMapKind::Serpent,    0x177A0718D0BCCD36ull },
		{ 113, 2, TEXT("El Pico"),                   EMapKind::Summit,     0x0B8A4359A0899917ull },
		{  99, 2, TEXT("La Meseta Cerrada"),         EMapKind::Summit,     0x0E6275D41A5DC219ull },
		{ 111, 2, TEXT("El Cañón"),                  EMapKind::Gorge,      0x5C0713AF481692F3ull },
		{ 119, 2, TEXT("El Doble Tajo"),             EMapKind::Jumps,      0x6699063AF8AF9B60ull },
		{ 147, 2, TEXT("Escalones al Mar"),          EMapKind::Jumps,      0xD4277C7C120B86D9ull },
		{  11, 2, TEXT("El Desvío"),                 EMapKind::Crossroads, 0x85FA52909F8D9FE1ull },
		// ── Dificultad 3 ──
		{  40, 3, TEXT("La Recta de los Quads"),     EMapKind::Plain,      0xD2A0465393B6E4C2ull },
		{  70, 3, TEXT("La Montaña Rusa"),           EMapKind::Serpent,    0x93C9F50D3BBF7722ull },
		{  57, 3, TEXT("El Puerto"),                 EMapKind::Summit,     0xC511E64AA355EA6Dull },
		{  49, 3, TEXT("El Callejón"),               EMapKind::Gorge,      0x464F87522E5BA40Aull },
		{  32, 3, TEXT("La Hoz"),                    EMapKind::Gorge,      0xBC2F1A4B85593D9Dull },
		{  37, 3, TEXT("Tres Saltos"),               EMapKind::Jumps,      0xC3D5F8868E47EF5Full },
		{  88, 3, TEXT("El Rosario"),                EMapKind::Jumps,      0xF3BA8C27E9E67910ull },
		{ 135, 3, TEXT("La Explanada"),              EMapKind::Plain,      0x03C4F7EC1FF7E4FAull },
		{  35, 3, TEXT("El Cruce del Puerto"),       EMapKind::Crossroads, 0xBB2C2B058723FBAFull },
		// ── Dificultad 4 ──
		{  74, 4, TEXT("La Playa Larga"),            EMapKind::Plain,      0x87F68190C230E3B5ull },
		{ 144, 4, TEXT("El Gran Zigzag"),            EMapKind::Serpent,    0x6CC3DE519A55017Dull },
		{  95, 4, TEXT("El Descenso"),               EMapKind::Summit,     0xB8EDAFB11FAE98AEull },
		{ 120, 4, TEXT("La Grieta"),                 EMapKind::Gorge,      0xFAB36B0B2F6501F7ull },
		{ 126, 4, TEXT("Las Tres Bocas"),            EMapKind::Gorge,      0xA02E66A37477B343ull },
		{  66, 4, TEXT("El Salto del Ángel"),        EMapKind::Jumps,      0x3788864D143A2107ull },
		{  60, 4, TEXT("Escalera de Saltos"),        EMapKind::Jumps,      0x39FE54655E8445D2ull },
		{ 143, 4, TEXT("La Culebra Alta"),           EMapKind::Serpent,    0xE6540012DD18A554ull },
		{  72, 4, TEXT("La Bifurcación"),            EMapKind::Crossroads, 0x56F486C0127AC83Dull },
		// ── Dificultad 5 (la más jugada) ──
		{  48, 5, TEXT("Tierra de Nadie"),           EMapKind::Plain,      0x36C98B64C8D7B139ull },
		{  27, 5, TEXT("El Embudo Final"),           EMapKind::Plain,      0x47D87405849F4E3Aull },
		{ 125, 5, TEXT("La Pesadilla"),              EMapKind::Serpent,    0x9758EDD2458D6122ull },
		{ 129, 5, TEXT("Las Curvas Ciegas"),         EMapKind::Serpent,    0x3ED00FB24A01ED46ull },
		{  80, 5, TEXT("La Cresta"),                 EMapKind::Summit,     0xE40BD566915A08ABull },
		{  14, 5, TEXT("El Volcán"),                 EMapKind::Summit,     0x77E016A24FABAC0Aull },
		{   3, 5, TEXT("El Tajo"),                   EMapKind::Gorge,      0x70EF8C9F20F271B8ull },
		{  58, 5, TEXT("La Garganta del Diablo"),    EMapKind::Gorge,      0x1E176BDF57E5D687ull },
		{  73, 5, TEXT("El Desfiladero con Escape"), EMapKind::Gorge,      0x3CF330F985191C7Dull },
		{  46, 5, TEXT("Los Cinco Abismos"),         EMapKind::Jumps,      0x14316CB4947F7328ull },
		{ 149, 5, TEXT("El Campo de Saltos"),        EMapKind::Jumps,      0x0F48B0C5F67C8FA4ull },
		{ 104, 5, TEXT("El Gran Salto"),             EMapKind::Jumps,      0x19BAA5D4394BA5E0ull },
		{ 137, 5, TEXT("Las Marismas"),              EMapKind::Crossroads, 0x3D2A7FCD846B0AB1ull },
		{ 142, 5, TEXT("El Último Cruce"),           EMapKind::Crossroads, 0xBA0AC3F8F62B4093ull },
	};

	/**
	 * Mapa de pruebas: una trampa de cada tipo, en orden y cada una en su tramo, sobre una semilla que tiene viga en
	 * el camino principal (puente que se rompe), rama (placa) y playa (quad). No es del catálogo: no cuenta en el
	 * reparto ni sale en las partidas (#518). Se abre con ?ProcMode=Survival?ProcSeed=6?ProcDifficulty=Hard.
	 */
	inline constexpr FMapEntry TestMaps[] = {
		{   6, 5, TEXT("Banco de Pruebas"),          EMapKind::Crossroads, 0xBBF470D3445AC62Cull },
	};

	using enum ETrap;

	inline constexpr FTrapSpot Traps[] = {
		// ── Dificultad 1 ──
		{  63, BananaPeel, 7, 7, 3 }, {  63, BananaPeel, 24, 24, 3 }, {  63, Seagull, 30, 70, 1, 3 }, {  63, BananaPeel, 89, 89, 3 },
		{  63, BarbedWire, 52, 52 },
		{  52, SlowZone, 11, 11 }, {  52, Jellyfish, 33, 33 }, {  52, Crab, 40, 60, 2 }, {  52, BananaPeel, 72, 72, 3 }, {  52, Quad, 85, 85 },
		{  52, BarbedWire, 52, 52 },
		{  33, Jellyfish, 17, 17 }, {  33, Crab, 30, 30 }, {  33, SlowZone, 34, 34 }, {  33, Jellyfish, 34, 34 }, {  33, BananaPeel, 59, 59, 3 }, {  33, SlowZone, 74, 74 },
		{  33, Seaweed, 61, 61 },
		{ 121, BananaPeel, 5, 5, 3 }, { 121, BananaPeel, 19, 19, 3 }, { 121, Crab, 37, 37 }, { 121, SlowZone, 46, 46 }, { 121, Jellyfish, 56, 56 }, { 121, Crab, 66, 66 }, { 121, BananaPeel, 75, 75, 3 }, { 121, Crab, 84, 84 },
		{ 121, Seaweed, 48, 48 },
		{  71, Jellyfish, 3, 3 }, {  71, Seagull, 14, 25, 1, 2 }, {  71, BananaPeel, 41, 41, 3 }, {  71, Jellyfish, 66, 66 },
		{  71, Seaweed, 43, 43 },
		{ 130, SlowZone, 5, 5 }, { 130, Jellyfish, 27, 27 }, { 130, BananaPeel, 58, 58, 3 }, { 130, BananaPeel, 76, 76, 3 },
		{ 130, Seaweed, 60, 60 },
		{ 101, SlowZone, 10, 10 }, { 101, BananaPeel, 28, 28, 3 }, { 101, Crab, 45, 45 }, { 101, BananaPeel, 72, 72, 3 },
		{ 101, Seaweed, 47, 47 },
		{ 116, Jellyfish, 10, 10 }, { 116, Crab, 20, 20 }, { 116, Seagull, 30, 70, 1, 2 }, { 116, Crab, 78, 78 },
		{ 116, BarbedWire, 52, 52 },
		{  94, BananaPeel, 26, 26, 3 }, {  94, PressurePlate, 44, 44 }, {  94, Crab, 50, 60, 2 },
		{  94, ClamTrap, 46, 46 },
		// ── Dificultad 2 ──
		{  30, Seagull, 10, 60, 1, 2 }, {  30, BananaPeel, 66, 66, 3 }, {  30, Crab, 74, 74 }, {  30, Jellyfish, 81, 81 }, {  30, Quad, 85, 85 }, {  30, BananaPeel, 92, 92, 3 },
		{  30, BarbedWire, 37, 37 }, {  30, ClamTrap, 76, 76 },
		{  45, Crab, 19, 19 }, {  45, BananaPeel, 22, 22, 3 }, {  45, SlowZone, 43, 43 }, {  45, Jellyfish, 58, 58 }, {  45, BananaPeel, 72, 72, 3 }, {  45, Jellyfish, 85, 85 },
		{  45, BarbedWire, 24, 24 }, {  45, Seaweed, 74, 74 },
		{  75, SlowZone, 14, 14 }, {  75, Jellyfish, 40, 40 }, {  75, Crab, 54, 54 }, {  75, BananaPeel, 68, 68, 3 }, {  75, Jellyfish, 83, 83 }, {  75, SlowZone, 83, 83 },
		{  75, BarbedWire, 16, 16 }, {  75, Seaweed, 70, 70 },
		{ 113, BananaPeel, 15, 15, 3 }, { 113, Jellyfish, 37, 37 }, { 113, Seagull, 42, 52, 1, 2 }, { 113, BananaPeel, 55, 55, 3 }, { 113, Jellyfish, 68, 68 }, { 113, BananaPeel, 80, 80, 3 },
		{ 113, BarbedWire, 17, 17 }, { 113, Seaweed, 70, 70 },
		{  99, BananaPeel, 1, 1, 3 }, {  99, SlowZone, 13, 13 }, {  99, Jellyfish, 28, 28 }, {  99, BananaPeel, 43, 43, 3 }, {  99, Crab, 56, 56 }, {  99, Crab, 82, 82 },
		{  99, BarbedWire, 30, 30 }, {  99, Seaweed, 84, 84 },
		{ 111, BananaPeel, 7, 7, 3 }, { 111, Jellyfish, 35, 35 }, { 111, BananaPeel, 50, 50, 3 }, { 111, Crab, 52, 52, 2 }, { 111, BananaPeel, 75, 75, 3 },
		{ 111, Seaweed, 37, 37 }, { 111, ClamTrap, 77, 77 },
		{ 119, BreakableBridge, 4, 4 }, { 119, BananaPeel, 35, 35, 3 }, { 119, Jellyfish, 49, 49 }, { 119, BananaPeel, 81, 81, 3 },
		{ 119, BarbedWire, 37, 37 }, { 119, ClamTrap, 83, 83 },
		{ 147, Jellyfish, 13, 13 }, { 147, BananaPeel, 28, 28, 3 }, { 147, Seagull, 40, 75, 1, 2 }, { 147, Crab, 81, 81 },
		{ 147, BarbedWire, 30, 30 }, { 147, ClamTrap, 83, 83 },
		{  11, BananaPeel, 14, 14, 3 }, {  11, BananaPeel, 37, 37, 3 }, {  11, ShellGate, 57, 57 }, {  11, Quad, 65, 65 },
		{  11, ClamTrap, 16, 16 }, {  11, Seaweed, 67, 67 },
		// ── Dificultad 3 ──
		{  40, TrashPile, 45, 45 }, {  40, UrchinSpikes, 55, 55 },
		{  40, Crab, 7, 7, 2 }, {  40, BananaPeel, 14, 14, 3 }, {  40, SlowZone, 18, 18 }, {  40, Jellyfish, 22, 22 }, {  40, Crab, 26, 26, 2 }, {  40, Seagull, 30, 70, 1, 2 }, {  40, Quad, 75, 90, 2 },
		{  40, BarbedWire, 24, 24 }, {  40, SeaUrchin, 84, 84 },
		{  70, Jellyfish, 14, 14 }, {  70, Crab, 21, 21 }, {  70, Jellyfish, 32, 32 }, {  70, Crab, 43, 43 }, {  70, Jellyfish, 51, 51 }, {  70, Crab, 60, 60, 2 }, {  70, BananaPeel, 70, 70, 3 }, {  70, Crab, 86, 86, 2 },
		{  70, BarbedWire, 23, 23 }, {  70, Seaweed, 72, 72 },
		{  57, BurrowCrab, 64, 64 },
		{  57, BananaPeel, 12, 12, 3 }, {  57, SlowZone, 24, 24 }, {  57, BananaPeel, 37, 37, 3 }, {  57, Jellyfish, 45, 45 }, {  57, SlowZone, 58, 58 }, {  57, Crab, 70, 70 }, {  57, SlowZone, 82, 82 },
		{  57, HermitCrab, 26, 26 }, {  57, Seaweed, 72, 72 },
		{  49, BreakableBridge, 8, 8 }, {  49, Crab, 10, 30, 2 }, {  49, BananaPeel, 37, 37, 3 }, {  49, BananaPeel, 65, 65, 3 }, {  49, BananaPeel, 83, 83, 3 },
		{  49, Seaweed, 22, 22 }, {  49, ClamTrap, 67, 67 },
		{  32, SlowZone, 3, 3 }, {  32, Crab, 6, 6, 2 }, {  32, Jellyfish, 18, 18 }, {  32, BananaPeel, 27, 27, 3 }, {  32, Seagull, 40, 70, 1, 2 }, {  32, BananaPeel, 82, 82, 3 },
		{  32, Seaweed, 29, 29 }, {  32, ClamTrap, 84, 84 },
		{  37, BananaPeel, 13, 13, 3 }, {  37, Jellyfish, 23, 23 }, {  37, Crab, 40, 40 }, {  37, BananaPeel, 53, 53, 3 }, {  37, Jellyfish, 68, 68 }, {  37, Crab, 82, 82, 2 },
		{  37, BarbedWire, 25, 25 }, {  37, ClamTrap, 70, 70 },
		{  88, Seagull, 20, 45, 1, 2 }, {  88, BananaPeel, 66, 66, 3 }, {  88, BananaPeel, 75, 75, 3 }, {  88, Quad, 85, 85 },
		{  88, BarbedWire, 34, 34 }, {  88, ClamTrap, 77, 77 },
		{ 135, BananaPeel, 7, 7, 3 }, { 135, Seagull, 20, 40, 1, 2 }, { 135, Crab, 40, 70, 3 }, { 135, BananaPeel, 74, 74, 3 }, { 135, Quad, 85, 85 },
		{ 135, BarbedWire, 32, 32 }, { 135, SeaUrchin, 76, 76 },
		{  35, SlowZone, 15, 15 }, {  35, PressurePlate, 27, 27 }, {  35, Crab, 38, 38, 2 }, {  35, Jellyfish, 48, 48 }, {  35, BananaPeel, 61, 61, 3 }, {  35, Jellyfish, 70, 70 }, {  35, Crab, 82, 82, 2 },
		{  35, SeaUrchin, 29, 29 }, {  35, Seaweed, 72, 72 },
		// ── Dificultad 4 ──
		{  74, DragCrab, 35, 35 }, {  74, Trench, 58, 58 },
		{  74, SlowZone, 4, 4 }, {  74, Seagull, 20, 45, 1, 2 }, {  74, Quad, 50, 50 }, {  74, BananaPeel, 66, 66, 4 }, {  74, Jellyfish, 81, 81 }, {  74, Quad, 85, 85 },
		{  74, BarbedWire, 6, 6 }, {  74, SeaUrchin, 52, 52 }, {  74, Mine, 83, 83, 2 },
		{ 144, BreakableBridge, 11, 11 }, { 144, Jellyfish, 22, 22 }, { 144, Crab, 33, 33, 2 }, { 144, SlowZone, 44, 44 }, { 144, Crab, 59, 59 }, { 144, BananaPeel, 60, 60, 3 }, { 144, BananaPeel, 78, 78, 4 },
		{ 144, BarbedWire, 13, 13 }, { 144, Seaweed, 46, 46 }, { 144, Mine, 80, 80, 2 },
		{  95, BananaPeel, 3, 20, 5 }, {  95, Jellyfish, 27, 27 }, {  95, Crab, 34, 34, 2 }, {  95, SlowZone, 42, 42 }, {  95, Jellyfish, 49, 49 }, {  95, Seagull, 50, 60, 1, 2 }, {  95, Crab, 72, 72 }, {  95, BananaPeel, 78, 78, 4 }, {  95, SlowZone, 84, 84 }, {  95, Jellyfish, 90, 90 },
		{  95, HermitCrab, 14, 14 }, {  95, BarbedWire, 51, 51 }, {  95, Seaweed, 80, 80 },
		{ 120, BananaPeel, 9, 9, 3 }, { 120, Seagull, 22, 32, 1, 1 }, { 120, Crab, 27, 27 }, { 120, Jellyfish, 46, 46 }, { 120, BananaPeel, 64, 64, 4 }, { 120, Crab, 83, 83, 2 },
		{ 120, Seaweed, 11, 11 }, { 120, ClamTrap, 48, 48 }, { 120, SeaUrchin, 85, 85 },
		{ 126, SlowZone, 21, 21 }, { 126, Crab, 28, 28 }, { 126, Crab, 49, 49 }, { 126, BananaPeel, 56, 56, 4 }, { 126, BananaPeel, 76, 76, 4 },
		{ 126, Seaweed, 23, 23 }, { 126, ClamTrap, 51, 51 }, { 126, SeaUrchin, 78, 78 },
		{  66, Jellyfish, 13, 13 }, {  66, Seagull, 26, 70, 1, 2 }, {  66, BananaPeel, 45, 45, 3 }, {  66, BreakableBridge, 61, 61 }, {  66, BananaPeel, 65, 65, 4 }, {  66, Jellyfish, 75, 75 }, {  66, Quad, 85, 85 },
		{  66, BarbedWire, 15, 15 }, {  66, ClamTrap, 50, 50 }, {  66, Mine, 77, 77, 2 },
		{  60, BananaPeel, 3, 3, 3 }, {  60, BananaPeel, 24, 24, 4 }, {  60, Crab, 35, 35 }, {  60, SlowZone, 49, 49 }, {  60, Jellyfish, 58, 58 }, {  60, BananaPeel, 78, 78, 4 },
		{  60, BarbedWire, 26, 26 }, {  60, ClamTrap, 51, 51 }, {  60, Mine, 80, 80, 2 },
		{ 143, BananaPeel, 17, 17, 4 }, { 143, Jellyfish, 29, 29 }, { 143, BananaPeel, 41, 41, 3 }, { 143, Jellyfish, 52, 52 }, { 143, Jellyfish, 70, 70 }, { 143, Crab, 79, 79, 2 },
		{ 143, BarbedWire, 19, 19 }, { 143, Seaweed, 54, 54 }, { 143, Mine, 81, 81, 2 },
		{  72, SlowZone, 26, 26 }, {  72, ShellGate, 42, 42 }, {  72, Crab, 42, 42, 2 }, {  72, Quad, 50, 60 }, {  72, Jellyfish, 71, 71 }, {  72, BananaPeel, 82, 82, 4 },
		{  72, SeaUrchin, 28, 28 }, {  72, Seaweed, 44, 44 }, {  72, Mine, 84, 84, 2 },
		// ── Dificultad 5 ──
		{  48, TankTrap, 30, 30 }, {  48, Quicksand, 64, 64 },
		{  48, Jellyfish, 7, 7 }, {  48, BananaPeel, 18, 18, 3 }, {  48, Seagull, 25, 80, 1, 1 }, {  48, Quad, 40, 40 }, {  48, Jellyfish, 50, 50 }, {  48, Crab, 60, 60, 2 }, {  48, Jellyfish, 71, 71 }, {  48, Crab, 82, 82, 3 }, {  48, Quad, 85, 85 }, {  48, BananaPeel, 91, 91, 4 },
		{  48, BarbedWire, 20, 20 }, {  48, SeaUrchin, 52, 52 }, {  48, Mine, 84, 84, 2 },
		{  27, TrashPile, 30, 30 }, {  27, BurrowCrab, 40, 40 }, {  27, UrchinSpikes, 50, 50 },
		{  27, Crab, 11, 11, 3 }, {  27, Seagull, 20, 60, 1, 2 }, {  27, Crab, 64, 64, 2 }, {  27, SlowZone, 69, 69 }, {  27, BananaPeel, 75, 75, 4 }, {  27, Quad, 85, 85 }, {  27, BananaPeel, 92, 92, 4 },
		{  27, BarbedWire, 13, 13 }, {  27, SeaUrchin, 52, 52 }, {  27, Mine, 77, 77, 2 },
		{ 125, DragCrab, 45, 45 },
		{ 125, Jellyfish, 11, 36, 2 }, { 125, BreakableBridge, 49, 49 }, { 125, BananaPeel, 54, 54, 4 }, { 125, Jellyfish, 62, 62 }, { 125, Crab, 69, 69, 2 }, { 125, SlowZone, 76, 76 }, { 125, Crab, 88, 88, 3 },
		{ 125, BarbedWire, 26, 26 }, { 125, Seaweed, 51, 51 }, { 125, Mine, 78, 78, 2 },
		{ 129, BreakableBridge, 3, 3 }, { 129, Jellyfish, 11, 11 }, { 129, BananaPeel, 19, 19, 4 }, { 129, Crab, 30, 30 }, { 129, Jellyfish, 40, 40 }, { 129, BananaPeel, 51, 51, 3 }, { 129, Crab, 56, 56 }, { 129, Jellyfish, 70, 70 }, { 129, Crab, 84, 84, 3 },
		{ 129, BarbedWire, 21, 21 }, { 129, Seaweed, 53, 53 }, { 129, Mine, 86, 86, 2 },
		{  80, BreakableBridge, 12, 12 }, {  80, Seagull, 14, 35, 1, 1 }, {  80, BananaPeel, 36, 36, 3 }, {  80, Crab, 41, 41, 2 }, {  80, Jellyfish, 52, 52 }, {  80, BananaPeel, 63, 63, 4 }, {  80, Crab, 84, 84, 3 },
		{  80, HermitCrab, 14, 14 }, {  80, BarbedWire, 54, 54 }, {  80, Seaweed, 86, 86 },
		{  14, TankTrap, 25, 25 },
		{  14, SlowZone, 5, 5 }, {  14, Jellyfish, 18, 18 }, {  14, Seagull, 30, 40, 1, 1 }, {  14, BananaPeel, 39, 39, 3 }, {  14, Crab, 46, 46, 3 }, {  14, Jellyfish, 54, 54 }, {  14, SlowZone, 62, 62 }, {  14, Crab, 69, 69, 2 }, {  14, BananaPeel, 78, 78, 4 }, {  14, Crab, 85, 85, 3 },
		{  14, HermitCrab, 20, 20 }, {  14, BarbedWire, 48, 48 }, {  14, Seaweed, 80, 80 },
		{   3, Trench, 50, 50 },
		{   3, BananaPeel, 3, 3, 3 }, {   3, BananaPeel, 17, 17, 4 }, {   3, Crab, 25, 45, 2 }, {   3, Jellyfish, 58, 58 }, {   3, Crab, 64, 64, 3 }, {   3, SlowZone, 71, 71 }, {   3, Jellyfish, 78, 78 }, {   3, BananaPeel, 84, 84, 4 },
		{   3, Seaweed, 19, 19 }, {   3, ClamTrap, 52, 52 }, {   3, SeaUrchin, 80, 80 },
		{  58, BreakableBridge, 5, 5 }, {  58, Seagull, 22, 32, 1, 1 }, {  58, BananaPeel, 25, 29, 4 }, {  58, Crab, 40, 40, 2 }, {  58, Jellyfish, 51, 51 }, {  58, BananaPeel, 62, 62, 4 }, {  58, BananaPeel, 83, 83, 4 },
		{  58, Seaweed, 7, 7 }, {  58, ClamTrap, 53, 53 }, {  58, SeaUrchin, 85, 85 },
		{  73, Jellyfish, 3, 3 }, {  73, SlowZone, 14, 14 }, {  73, PressurePlate, 23, 23 }, {  73, Jellyfish, 34, 34 }, {  73, SlowZone, 46, 46 }, {  73, BananaPeel, 60, 60, 3 }, {  73, BananaPeel, 82, 82, 4 },
		{  73, Seaweed, 16, 16 }, {  73, ClamTrap, 48, 48 }, {  73, SeaUrchin, 84, 84 },
		{  46, Seagull, 11, 25, 1, 1 }, {  46, SlowZone, 31, 31 }, {  46, BananaPeel, 43, 43, 3 }, {  46, BananaPeel, 56, 56, 4 }, {  46, Crab, 56, 56, 2 }, {  46, SlowZone, 60, 60 }, {  46, Jellyfish, 72, 72 }, {  46, BananaPeel, 83, 83, 4 },
		{  46, BarbedWire, 20, 20 }, {  46, ClamTrap, 45, 45 }, {  46, Mine, 85, 85, 2 },
		{ 149, Seagull, 7, 35, 1, 1 }, { 149, Jellyfish, 14, 22, 2 }, { 149, BananaPeel, 38, 38, 4 }, { 149, BreakableBridge, 59, 59 }, { 149, Jellyfish, 72, 72 }, { 149, Quad, 85, 85 },
		{ 149, BarbedWire, 20, 20 }, { 149, ClamTrap, 40, 40 }, { 149, Mine, 87, 87, 2 },
		{ 104, BreakableBridge, 10, 10 }, { 104, BananaPeel, 19, 19, 4 }, { 104, Crab, 30, 30, 2 }, { 104, Jellyfish, 41, 41 }, { 104, Jellyfish, 47, 47 }, { 104, BananaPeel, 48, 48, 3 }, { 104, Seagull, 55, 75, 1, 1 }, { 104, Crab, 83, 83, 3 }, { 104, Jellyfish, 90, 90 },
		{ 104, BarbedWire, 21, 21 }, { 104, ClamTrap, 49, 49 }, { 104, Mine, 85, 85, 2 },
		{ 137, Quicksand, 70, 70 },
		{ 137, SlowZone, 10, 60, 3 }, { 137, PressurePlate, 32, 32 }, { 137, BananaPeel, 37, 37, 3 }, { 137, Crab, 45, 45, 2 }, { 137, BananaPeel, 56, 56, 4 }, { 137, Seagull, 60, 80, 1, 1 }, { 137, Quad, 85, 85 }, { 137, BananaPeel, 92, 92, 4 },
		{ 137, SeaUrchin, 34, 34 }, { 137, Seaweed, 47, 47 }, { 137, Mine, 87, 87, 2 },
		{ 142, BananaPeel, 3, 3, 3 }, { 142, BananaPeel, 22, 22, 4 }, { 142, BananaPeel, 38, 38, 4 }, { 142, ShellGate, 58, 58 }, { 142, Crab, 63, 63, 2 }, { 142, Quad, 85, 85 },
		{ 142, SeaUrchin, 24, 24 }, { 142, Seaweed, 40, 40 }, { 142, Mine, 87, 87, 2 },
		// ── Mapa de pruebas (TestMaps) ──
		{   6, BananaPeel, 5, 5, 3 }, {   6, BreakableBridge, 10, 10 }, {   6, SlowZone, 17, 17 }, {   6, Jellyfish, 24, 24 },
		{   6, Crab, 30, 30, 2 }, {   6, PressurePlate, 44, 44 }, {   6, Seagull, 62, 76, 1, 2 }, {   6, Quad, 88, 88 },
		{   6, UrchinSpikes, 38, 38 }, {   6, Quicksand, 48, 48 }, {   6, DragCrab, 52, 52 }, {   6, BurrowCrab, 57, 57 },
		{   6, TankTrap, 67, 67 }, {   6, TrashPile, 72, 72 }, {   6, Trench, 82, 82 },
		{   6, Seaweed, 14, 14 }, {   6, BarbedWire, 20, 20 },
		{   6, ClamTrap, 41, 41 }, {   6, Mine, 50, 50, 2 }, {   6, SeaUrchin, 60, 60 }, {   6, HermitCrab, 78, 78 },
	};

	/** La entrada de una semilla del catálogo o del mapa de pruebas, o nullptr si no está. */
	inline const FMapEntry* FindMap(uint32 Seed)
	{
		for (const FMapEntry& M : Maps) { if (M.Seed == Seed) { return &M; } }
		for (const FMapEntry& M : TestMaps) { if (M.Seed == Seed) { return &M; } }
		return nullptr;
	}

	/**
	 * Si un punto del catálogo cuenta para la densidad de trampas (ScaleTrapSpots). Fuera, lo que depende de la forma del
	 * mapa: los atajos de la rama (placas o puerta de conchas), los puentes que se rompen (van en los huecos) y las gaviotas
	 * (un tramo largo).
	 */
	inline bool ScalesWithDensity(ETrap T)
	{
		return T != ETrap::PressurePlate && T != ETrap::ShellGate && T != ETrap::BreakableBridge && T != ETrap::Seagull;
	}

	/**
	 * Los puntos con trampa de un mapa con DensityPct % de puntos (100 = los del catálogo; el que haga falta para la densidad
	 * de la dificultad, TNSurvivalCatalog::DensityPctForTarget, #730). Con más de 100 se añaden copias de los que cuentan
	 * (ScalesWithDensity): a cada punto le tocan las mismas (las que sobran, repartidas), y sus copias van espaciadas por igual
	 * en el tramo hasta el siguiente (o hasta el final del recorrido). Los que no cuentan quedan igual. Determinista: todas
	 * las máquinas sacan los mismos.
	 */
	inline TArray<FTrapSpot> ScaleTrapSpots(const TArray<FTrapSpot>& Spots, int32 DensityPct)
	{
		TArray<FTrapSpot> Out = Spots;
		TArray<FTrapSpot> Scalable = Spots.FilterByPredicate([](const FTrapSpot& T) { return ScalesWithDensity(T.Trap); });
		if (DensityPct <= 100 || Scalable.Num() == 0)
		{
			return Out;
		}
		auto CenterOf = [](const FTrapSpot& T) { return (static_cast<int32>(T.FromPct) + static_cast<int32>(T.ToPct)) * 0.5; };
		Scalable.StableSort([&CenterOf](const FTrapSpot& A, const FTrapSpot& B) { return CenterOf(A) < CenterOf(B); });

		const int32 N = Scalable.Num();
		const int64 Extra = FMath::RoundToInt64(N * (DensityPct - 100) / 100.0);
		for (int32 i = 0; i < N; ++i)
		{
			// Las de este punto: el reparto entero de Extra entre N (las que sobran caen repartidas por la lista).
			const int32 Copies = static_cast<int32>((Extra * (i + 1)) / N - (Extra * i) / N);
			if (Copies <= 0)
			{
				continue;
			}
			const FTrapSpot& Source = Scalable[i];
			const double Center = CenterOf(Source);
			const double Next = i + 1 < N ? CenterOf(Scalable[i + 1]) : 97.0;
			for (int32 j = 1; j <= Copies; ++j)
			{
				const int32 Shift = FMath::Max(1, FMath::RoundToInt32((Next - Center) * j / (Copies + 1.0)));
				FTrapSpot Copy = Source;
				Copy.FromPct = static_cast<uint8>(FMath::Clamp(static_cast<int32>(Source.FromPct) + Shift, 0, 97));
				Copy.ToPct = static_cast<uint8>(FMath::Clamp(static_cast<int32>(Source.ToPct) + Shift, static_cast<int32>(Copy.FromPct), 97));
				Out.Add(Copy);
			}
		}
		return Out;
	}

	/** Las trampas de una semilla del catálogo, en el orden del catálogo (con DensityPct, más copias al final). */
	inline TArray<FTrapSpot> TrapsOf(uint32 Seed, int32 DensityPct = 100)
	{
		TArray<FTrapSpot> Out;
		for (const FTrapSpot& T : Traps) { if (T.Seed == Seed) { Out.Add(T); } }
		return ScaleTrapSpots(Out, DensityPct);
	}
}
