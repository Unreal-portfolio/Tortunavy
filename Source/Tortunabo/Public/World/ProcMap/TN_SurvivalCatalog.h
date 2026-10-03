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
		Crab,             ///< Cangrejos (ATN_CrabSpawnZone).
		Seagull,          ///< Gaviotas (ATN_SeagullSpawnZone) con sombrillas.
		Quad,             ///< Quad que cruza el camino (ATN_QuadActor).
		BreakableBridge,  ///< Puente que se rompe (ATN_BreakablePlatform), en lugar de la viga de un hueco.
		PressurePlate     ///< Placa que abre el atajo de la rama (ATN_PressurePlate), resoluble con un jugador.
	};

	/** Una trampa del catálogo: en un punto (From == To) o a lo largo de un tramo del recorrido. */
	struct FTrapSpot
	{
		uint32 Seed;
		ETrap Trap;
		uint8 FromPct;
		uint8 ToPct;
		/** Cuántas: cáscaras, cangrejos, quads, medusas o placas del punto o del tramo. */
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

	using enum ETrap;

	inline constexpr FTrapSpot Traps[] = {
		// ── Dificultad 1 ──
		{  63, BananaPeel, 7, 7, 3 }, {  63, BananaPeel, 24, 24, 3 }, {  63, Seagull, 30, 70, 1, 3 }, {  63, BananaPeel, 89, 89, 3 },
		{  52, SlowZone, 11, 11 }, {  52, Jellyfish, 33, 33 }, {  52, Crab, 40, 60, 2 }, {  52, BananaPeel, 72, 72, 3 }, {  52, Quad, 85, 85 },
		{  33, Jellyfish, 17, 17 }, {  33, Crab, 30, 30 }, {  33, SlowZone, 34, 34 }, {  33, Jellyfish, 34, 34 }, {  33, BananaPeel, 59, 59, 3 }, {  33, SlowZone, 74, 74 },
		{ 121, BananaPeel, 5, 5, 3 }, { 121, BananaPeel, 19, 19, 3 }, { 121, Crab, 37, 37 }, { 121, SlowZone, 46, 46 }, { 121, Jellyfish, 56, 56 }, { 121, Crab, 66, 66 }, { 121, BananaPeel, 75, 75, 3 }, { 121, Crab, 84, 84 },
		{  71, Jellyfish, 3, 3 }, {  71, Seagull, 14, 25, 1, 2 }, {  71, BananaPeel, 41, 41, 3 }, {  71, Jellyfish, 66, 66 },
		{ 130, SlowZone, 5, 5 }, { 130, Jellyfish, 27, 27 }, { 130, BananaPeel, 58, 58, 3 }, { 130, BananaPeel, 76, 76, 3 },
		{ 101, SlowZone, 10, 10 }, { 101, BananaPeel, 28, 28, 3 }, { 101, Crab, 45, 45 }, { 101, BananaPeel, 72, 72, 3 },
		{ 116, Jellyfish, 10, 10 }, { 116, Crab, 20, 20 }, { 116, Seagull, 30, 70, 1, 2 }, { 116, Crab, 78, 78 },
		{  94, BananaPeel, 26, 26, 3 }, {  94, PressurePlate, 44, 44 }, {  94, Crab, 50, 60, 2 },
		// ── Dificultad 2 ──
		{  30, Seagull, 10, 60, 1, 2 }, {  30, BananaPeel, 66, 66, 3 }, {  30, Crab, 74, 74 }, {  30, Jellyfish, 81, 81 }, {  30, Quad, 85, 85 }, {  30, BananaPeel, 92, 92, 3 },
		{  45, Crab, 19, 19 }, {  45, BananaPeel, 22, 22, 3 }, {  45, SlowZone, 43, 43 }, {  45, Jellyfish, 58, 58 }, {  45, BananaPeel, 72, 72, 3 }, {  45, Jellyfish, 85, 85 },
		{  75, SlowZone, 14, 14 }, {  75, Jellyfish, 40, 40 }, {  75, Crab, 54, 54 }, {  75, BananaPeel, 68, 68, 3 }, {  75, Jellyfish, 83, 83 }, {  75, SlowZone, 83, 83 },
		{ 113, BananaPeel, 15, 15, 3 }, { 113, Jellyfish, 37, 37 }, { 113, Seagull, 42, 52, 1, 2 }, { 113, BananaPeel, 55, 55, 3 }, { 113, Jellyfish, 68, 68 }, { 113, BananaPeel, 80, 80, 3 },
		{  99, BananaPeel, 1, 1, 3 }, {  99, SlowZone, 13, 13 }, {  99, Jellyfish, 28, 28 }, {  99, BananaPeel, 43, 43, 3 }, {  99, Crab, 56, 56 }, {  99, Crab, 82, 82 },
		{ 111, BananaPeel, 7, 7, 3 }, { 111, Jellyfish, 35, 35 }, { 111, BananaPeel, 50, 50, 3 }, { 111, Crab, 52, 52, 2 }, { 111, BananaPeel, 75, 75, 3 },
		{ 119, BreakableBridge, 4, 4 }, { 119, BananaPeel, 35, 35, 3 }, { 119, Jellyfish, 49, 49 }, { 119, BananaPeel, 81, 81, 3 },
		{ 147, Jellyfish, 13, 13 }, { 147, BananaPeel, 28, 28, 3 }, { 147, Seagull, 40, 75, 1, 2 }, { 147, Crab, 81, 81 },
		{  11, BananaPeel, 14, 14, 3 }, {  11, BananaPeel, 37, 37, 3 }, {  11, PressurePlate, 57, 57, 2 }, {  11, Quad, 65, 65 },
		// ── Dificultad 3 ──
		{  40, Crab, 7, 7, 2 }, {  40, BananaPeel, 14, 14, 3 }, {  40, SlowZone, 18, 18 }, {  40, Jellyfish, 22, 22 }, {  40, Crab, 26, 26, 2 }, {  40, Seagull, 30, 70, 1, 2 }, {  40, Quad, 75, 90, 2 },
		{  70, Jellyfish, 14, 14 }, {  70, Crab, 21, 21 }, {  70, Jellyfish, 32, 32 }, {  70, Crab, 43, 43 }, {  70, Jellyfish, 51, 51 }, {  70, Crab, 60, 60, 2 }, {  70, BananaPeel, 70, 70, 3 }, {  70, Crab, 86, 86, 2 },
		{  57, BananaPeel, 12, 12, 3 }, {  57, SlowZone, 24, 24 }, {  57, BananaPeel, 37, 37, 3 }, {  57, Jellyfish, 45, 45 }, {  57, SlowZone, 58, 58 }, {  57, Crab, 70, 70 }, {  57, SlowZone, 82, 82 },
		{  49, BreakableBridge, 8, 8 }, {  49, Crab, 10, 30, 2 }, {  49, BananaPeel, 37, 37, 3 }, {  49, BananaPeel, 65, 65, 3 }, {  49, BananaPeel, 83, 83, 3 },
		{  32, SlowZone, 3, 3 }, {  32, Crab, 6, 6, 2 }, {  32, Jellyfish, 18, 18 }, {  32, BananaPeel, 27, 27, 3 }, {  32, Seagull, 40, 70, 1, 2 }, {  32, BananaPeel, 82, 82, 3 },
		{  37, BananaPeel, 13, 13, 3 }, {  37, Jellyfish, 23, 23 }, {  37, Crab, 40, 40 }, {  37, BananaPeel, 53, 53, 3 }, {  37, Jellyfish, 68, 68 }, {  37, Crab, 82, 82, 2 },
		{  88, Seagull, 20, 45, 1, 2 }, {  88, BreakableBridge, 64, 64 }, {  88, BananaPeel, 75, 75, 3 }, {  88, Quad, 85, 85 },
		{ 135, BananaPeel, 7, 7, 3 }, { 135, Seagull, 20, 40, 1, 2 }, { 135, Crab, 40, 70, 3 }, { 135, BananaPeel, 74, 74, 3 }, { 135, Quad, 85, 85 },
		{  35, SlowZone, 15, 15 }, {  35, PressurePlate, 27, 27 }, {  35, Crab, 38, 38, 2 }, {  35, Jellyfish, 48, 48 }, {  35, BananaPeel, 61, 61, 3 }, {  35, Jellyfish, 70, 70 }, {  35, Crab, 82, 82, 2 },
		// ── Dificultad 4 ──
		{  74, SlowZone, 4, 4 }, {  74, Seagull, 20, 45, 1, 2 }, {  74, Quad, 50, 50 }, {  74, BananaPeel, 66, 66, 4 }, {  74, Jellyfish, 81, 81 }, {  74, Quad, 85, 85 },
		{ 144, BreakableBridge, 11, 11 }, { 144, Jellyfish, 22, 22 }, { 144, Crab, 33, 33, 2 }, { 144, SlowZone, 44, 44 }, { 144, Crab, 59, 59 }, { 144, BananaPeel, 60, 60, 3 }, { 144, BananaPeel, 78, 78, 4 },
		{  95, BananaPeel, 3, 20, 5 }, {  95, Jellyfish, 27, 27 }, {  95, Crab, 34, 34, 2 }, {  95, SlowZone, 42, 42 }, {  95, Jellyfish, 49, 49 }, {  95, Seagull, 50, 60, 1, 2 }, {  95, Crab, 72, 72 }, {  95, BananaPeel, 78, 78, 4 }, {  95, SlowZone, 84, 84 }, {  95, Jellyfish, 90, 90 },
		{ 120, BananaPeel, 9, 9, 3 }, { 120, Seagull, 22, 32, 1, 1 }, { 120, Crab, 27, 27 }, { 120, Jellyfish, 46, 46 }, { 120, BananaPeel, 64, 64, 4 }, { 120, Crab, 83, 83, 2 },
		{ 126, SlowZone, 21, 21 }, { 126, Crab, 28, 28 }, { 126, Crab, 49, 49 }, { 126, BreakableBridge, 54, 54 }, { 126, BananaPeel, 76, 76, 4 },
		{  66, Jellyfish, 13, 13 }, {  66, Seagull, 26, 70, 1, 2 }, {  66, BananaPeel, 45, 45, 3 }, {  66, BreakableBridge, 61, 61 }, {  66, BananaPeel, 65, 65, 4 }, {  66, Jellyfish, 75, 75 }, {  66, Quad, 85, 85 },
		{  60, BananaPeel, 3, 3, 3 }, {  60, BananaPeel, 24, 24, 4 }, {  60, Crab, 35, 35 }, {  60, SlowZone, 49, 49 }, {  60, Jellyfish, 58, 58 }, {  60, BananaPeel, 78, 78, 4 },
		{ 143, BananaPeel, 17, 17, 4 }, { 143, Jellyfish, 29, 29 }, { 143, BananaPeel, 41, 41, 3 }, { 143, Jellyfish, 52, 52 }, { 143, Jellyfish, 70, 70 }, { 143, Crab, 79, 79, 2 },
		{  72, SlowZone, 26, 26 }, {  72, PressurePlate, 42, 42 }, {  72, Crab, 42, 42, 2 }, {  72, Quad, 50, 60 }, {  72, Jellyfish, 71, 71 }, {  72, BananaPeel, 82, 82, 4 },
		// ── Dificultad 5 ──
		{  48, Jellyfish, 7, 7 }, {  48, BananaPeel, 18, 18, 3 }, {  48, Seagull, 25, 80, 1, 1 }, {  48, Quad, 40, 40 }, {  48, Jellyfish, 50, 50 }, {  48, Crab, 60, 60, 2 }, {  48, Jellyfish, 71, 71 }, {  48, Crab, 82, 82, 3 }, {  48, Quad, 85, 85 }, {  48, BananaPeel, 91, 91, 4 },
		{  27, Crab, 11, 11, 3 }, {  27, Seagull, 20, 60, 1, 2 }, {  27, Crab, 64, 64, 2 }, {  27, SlowZone, 69, 69 }, {  27, BananaPeel, 75, 75, 4 }, {  27, Quad, 85, 85 }, {  27, BananaPeel, 92, 92, 4 },
		{ 125, Jellyfish, 11, 36, 2 }, { 125, BreakableBridge, 49, 49 }, { 125, BananaPeel, 54, 54, 4 }, { 125, Jellyfish, 62, 62 }, { 125, Crab, 69, 69, 2 }, { 125, SlowZone, 76, 76 }, { 125, Crab, 88, 88, 3 },
		{ 129, BreakableBridge, 3, 3 }, { 129, Jellyfish, 11, 11 }, { 129, BananaPeel, 19, 19, 4 }, { 129, Crab, 30, 30 }, { 129, Jellyfish, 40, 40 }, { 129, BananaPeel, 51, 51, 3 }, { 129, Crab, 56, 56 }, { 129, Jellyfish, 70, 70 }, { 129, Crab, 84, 84, 3 },
		{  80, BreakableBridge, 12, 12 }, {  80, Seagull, 14, 35, 1, 1 }, {  80, BananaPeel, 36, 36, 3 }, {  80, Crab, 41, 41, 2 }, {  80, Jellyfish, 52, 52 }, {  80, BananaPeel, 63, 63, 4 }, {  80, Crab, 84, 84, 3 },
		{  14, SlowZone, 5, 5 }, {  14, Jellyfish, 18, 18 }, {  14, Seagull, 30, 40, 1, 1 }, {  14, BananaPeel, 39, 39, 3 }, {  14, Crab, 46, 46, 3 }, {  14, Jellyfish, 54, 54 }, {  14, SlowZone, 62, 62 }, {  14, Crab, 69, 69, 2 }, {  14, BananaPeel, 78, 78, 4 }, {  14, Crab, 85, 85, 3 },
		{   3, BananaPeel, 3, 3, 3 }, {   3, BreakableBridge, 14, 14 }, {   3, Crab, 25, 45, 2 }, {   3, Jellyfish, 58, 58 }, {   3, Crab, 64, 64, 3 }, {   3, SlowZone, 71, 71 }, {   3, Jellyfish, 78, 78 }, {   3, BananaPeel, 84, 84, 4 },
		{  58, BreakableBridge, 5, 5 }, {  58, Seagull, 22, 32, 1, 1 }, {  58, BananaPeel, 25, 29, 4 }, {  58, Crab, 40, 40, 2 }, {  58, Jellyfish, 51, 51 }, {  58, BananaPeel, 62, 62, 4 }, {  58, BananaPeel, 83, 83, 4 },
		{  73, Jellyfish, 3, 3 }, {  73, SlowZone, 14, 14 }, {  73, PressurePlate, 23, 23 }, {  73, Jellyfish, 34, 34 }, {  73, SlowZone, 46, 46 }, {  73, BananaPeel, 60, 60, 3 }, {  73, BananaPeel, 82, 82, 4 },
		{  46, Seagull, 11, 25, 1, 1 }, {  46, SlowZone, 31, 31 }, {  46, BananaPeel, 43, 43, 3 }, {  46, BreakableBridge, 53, 53 }, {  46, Crab, 56, 56, 2 }, {  46, SlowZone, 60, 60 }, {  46, Jellyfish, 72, 72 }, {  46, BananaPeel, 83, 83, 4 },
		{ 149, Seagull, 7, 35, 1, 1 }, { 149, Jellyfish, 14, 22, 2 }, { 149, BananaPeel, 38, 38, 4 }, { 149, BreakableBridge, 59, 59 }, { 149, Jellyfish, 72, 72 }, { 149, Quad, 85, 85 },
		{ 104, BreakableBridge, 10, 10 }, { 104, BananaPeel, 19, 19, 4 }, { 104, Crab, 30, 30, 2 }, { 104, Jellyfish, 41, 41 }, { 104, Jellyfish, 47, 47 }, { 104, BananaPeel, 48, 48, 3 }, { 104, Seagull, 55, 75, 1, 1 }, { 104, Crab, 83, 83, 3 }, { 104, Jellyfish, 90, 90 },
		{ 137, SlowZone, 10, 60, 3 }, { 137, PressurePlate, 32, 32 }, { 137, BananaPeel, 37, 37, 3 }, { 137, Crab, 45, 45, 2 }, { 137, BananaPeel, 56, 56, 4 }, { 137, Seagull, 60, 80, 1, 1 }, { 137, Quad, 85, 85 }, { 137, BananaPeel, 92, 92, 4 },
		{ 142, BananaPeel, 3, 3, 3 }, { 142, BananaPeel, 22, 22, 4 }, { 142, BananaPeel, 38, 38, 4 }, { 142, PressurePlate, 58, 58 }, { 142, Crab, 63, 63, 2 }, { 142, Quad, 85, 85 },
	};

	/** La entrada de una semilla del catálogo, o nullptr si no está. */
	inline const FMapEntry* FindMap(uint32 Seed)
	{
		for (const FMapEntry& M : Maps) { if (M.Seed == Seed) { return &M; } }
		return nullptr;
	}

	/** Las trampas de una semilla del catálogo, en el orden del catálogo. */
	inline TArray<FTrapSpot> TrapsOf(uint32 Seed)
	{
		TArray<FTrapSpot> Out;
		for (const FTrapSpot& T : Traps) { if (T.Seed == Seed) { Out.Add(T); } }
		return Out;
	}
}
