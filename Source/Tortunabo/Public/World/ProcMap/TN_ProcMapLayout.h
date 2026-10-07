#pragma once

#include "CoreMinimal.h"
#include "World/ProcMap/TN_ProcMapEnums.h"
#include "World/ProcMap/TN_ProcMapMath.h"

/**
 * Tipos compartidos del antiguo mapa procedural que siguen en uso: los biomas por índice y las formaciones temáticas
 * (EFormation, TN_ProcMapFormationMeshes.h), que reutiliza el valle del lobby. Sin mundo ni actores.
 */

namespace TNProcMap
{
	constexpr int32 NumBiomes = static_cast<int32>(ETNProcBiome::Count);

	inline int32 BiomeIndex(ETNProcBiome B) { return static_cast<int32>(B); }
	inline ETNProcBiome BiomeFromIndex(int32 I) { return static_cast<ETNProcBiome>(FMath::Clamp(I, 0, NumBiomes - 1)); }

	/** Tipo de formación temática (rocas, ruinas e hitos del paisaje). */
	enum class EFormation : int32
	{
		// Arcos que cruzan el camino (se pasa por debajo).
		StoneArch,      ///< Arco natural de roca (arenisca, granito, musgo, obsidiana según el bioma).
		WhaleRibs,      ///< Costillar de ballena (playa).
		RootArch,       ///< Raíces gigantes en arco (manglar).
		TempleGate,     ///< Pórtico de templo en ruinas (selva).
		FallenTrunk,    ///< Tronco colosal caído de pared a pared, con raíces, musgo y lianas (selva, manglar).
		RuinedAqueduct, ///< Tramo de acueducto en ruinas que cruza el cañón (desierto, roca, pueblos).
		// En explanadas del camino, con carriles libres a los lados.
		Shipwreck,      ///< Barco varado de costado con el mástil roto (playa).
		StoneHead,      ///< Cabeza colosal de piedra (selva).
		BasaltColumns,  ///< Columnas hexagonales de basalto (volcán).
		Fumarole,       ///< Cono de fumarola con azufre (volcán).
		Hoodoo,         ///< Chimenea de hadas: roca en capas con sombrero (desierto, roca).
		BalancedRock,   ///< Peñasco en equilibrio sobre un pedestal (desierto, roca).
		Wagon,          ///< Carreta de lona abandonada (desierto).
		Cannon,         ///< Cañón antiguo con balas apiladas (guerra: acantilados).
		Sandbags,       ///< Parapeto de sacos terreros (guerra: zona humana).
		Bunker,         ///< Búnker de hormigón con tronera (guerra: zona humana).
		WatchTower,     ///< Torre de vigía de madera (guerra: zona humana).
		TankWreck,      ///< Carro de combate abandonado (guerra: zona humana).
		GiantShell,     ///< Caracola gigante de pie sobre su boca (playa).
		Anchor,         ///< Ancla oxidada clavada en la arena con su cadena (playa).
		StoneCircle,    ///< Círculo de piedras en pie con dinteles y altar (roca, selva).
		Obelisk,        ///< Obelisco con bandas de símbolos y punta dorada (desierto).
		FossilSkull,    ///< Cráneo fósil gigante medio enterrado, con cuernos (desierto).
		ObsidianSpires, ///< Agujas de obsidiana (volcán).
		ColossalTurtle, ///< Tortuga colosal de piedra sobre su pedestal (selva, desierto).
		WaterTower,     ///< Depósito de agua de madera sobre patas (zona humana).
		// Hitos lejanos del paisaje (sin colisión).
		Pyramid,        ///< Pirámide escalonada con escalinata (selva).
		Lighthouse,     ///< Faro a rayas junto a la costa (playa).
		Mesa,           ///< Mesa de techo plano con estratos (desierto).
		SeaStack,       ///< Farallón en el mar (playa, roca).
		CastleRuin,     ///< Castillo en ruinas con torre (roca).
		Windmill,       ///< Molino de viento (zona humana).
		StiltHut,       ///< Palafito de pescador (manglar, lagunas).
		Count
	};
}
