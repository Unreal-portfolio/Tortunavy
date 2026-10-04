// Arte del buggy del Rally hecho en C++ (#297): las carrocerías de tortuga de la tienda por piezas (caparazón de placas,
// cabeza con ojos que hacen de faros, aletas por guardabarros, cola de escape, alerón...), sus ruedas y la antena con el
// banderín del equipo, para los modelos de pago de TNBuggyCosmetics. El modelo de serie es SM_TN_BuggyBody (Art/Source,
// #290): aquí solo están sus rutas para el escaparate. Mallas de caras planas (el estilo low poly del juego) construidas
// una vez por pieza y compartidas por todos los buggies; las pinta M_BuggyPaint (Scripts/build_buggy_paint.py) con los
// parámetros de la pintura, también sobre el modelo de serie. Cada pieza tiene un nombre estable (PieceName) para poder
// sustituirla por arte más adelante.
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/TN_BuggyCosmetics.h"
#include "../World/ProcMap/TN_ProcMapMeshKit.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

namespace TNBuggyArt
{
	/** Piezas: las de carrocería van colgadas del chasis; Wheel y Antenna, en sus propios componentes. */
	enum class EPiece : uint8
	{
		Chassis,  // bajos, suspensión y pilotos traseros
		Cockpit,  // bañera, asientos, timón, salpicadero, caja de cocos y barandillas de la artillera
		Shell,    // caparazón con sus placas, el hueco de los pies de la artillera y su sillín
		Head,     // cuello, cabeza, ojos-faro y boca
		Fenders,  // las cuatro aletas que hacen de guardabarros
		Tail,     // cola de escape
		Rear,     // alerón de vieira, rueda de repuesto o alerón de carreras
		Extras,   // parachoques, barra de luces, tubo de buceo, escapes laterales...
		Wheel,
		Antenna,
		Count
	};

	/** Piezas de carrocería (de Chassis a Extras, en este orden). */
	constexpr int32 NumBodyPieces = static_cast<int32>(EPiece::Wheel);

	/** Nombre estable de la pieza para el catálogo de arte (#319): «Rally.Buggy.<Pieza>.<Modelo>». */
	FName PieceName(ETNBuggyBodyStyle Style, EPiece Piece);

	/**
	 * Códigos de zona en el alfa del color de vértice, en octavos (los lee M_BuggyPaint): pintura (el RGB son las
	 * máscaras de carrocería, placas y piel, con su sombreado), pintura sin dibujo (llantas), color del equipo, luz (faros
	 * y pilotos: brillan con LightGlow), metal y mate (el RGB es el color lineal).
	 */
	namespace Zone
	{
		constexpr float Matte = 0.f;
		constexpr float Metal = 0.25f;
		constexpr float Light = 0.5f;
		constexpr float Team = 0.75f;
		constexpr float PaintPlain = 0.875f;
		constexpr float Paint = 1.f;
	}

	/**
	 * Medidas del buggy de serie (espacio del chasis, cm; Art/Source/Vehicles/Buggy/build_buggy.py): ruedas de
	 * SK_TN_BuggyChassis, asientos de SM_TN_BuggyBody, barandillas y torreta. Las tortugas se sientan igual en todos los
	 * modelos y la torreta (TNBuggyTurretMesh) es la misma: las carrocerías de tortuga dejan sitio a las dos y dan a la
	 * torreta sus barandillas.
	 */
	namespace Frame
	{
		const FVector FrontWheel(168.3, 124.1, 51.1);
		const FVector RearWheel(-135.2, 139.8, 50.8);
		constexpr double WheelRadius = 51.0;
		constexpr double WheelWidth = 35.0;
		/** Caderas de las tortugas sentadas: los sockets Seat_Driver y Seat_Gunner (ATN_Buggy::DriverSeatLocal y GunnerSeatLocal). */
		const FVector DriverHip(22.0, 0.0, 92.38);
		const FVector GunnerHip(-80.0, 0.0, 127.38);
		/** Cojines (15,4 cm bajo la cadera) y suelos donde apoyan los pies (FLOOR_Z y GUNNER_FLOOR_Z). */
		constexpr double DriverCushionZ = 77.0;
		constexpr double GunnerCushionZ = 112.0;
		constexpr double DriverFloorZ = 55.0;
		constexpr double GunnerFloorZ = 90.0;
		/** Centro del volante: la cadera de la conductora más (52, 0, 25), donde caen sus manos. */
		const FVector Helm(74.0, 0.0, 117.38);
		/** Barandillas de la artillera, donde se sujeta el aro de la torreta (ROLL_Y y GUNNER_RAIL_Z). */
		constexpr double RailY = 55.0;
		constexpr double RailZ = 145.0;
		constexpr double RailFrontX = -8.0;
		constexpr double RailBackX = -122.0;
		/** Pivote de la torreta (artillera + UTN_BuggyTurretComponent::PivotAboveSeatCm). */
		constexpr double TurretPivotZ = 214.38;
		/**
		 * Lo que barre la torreta alrededor de su eje vertical: el aro y el carro (radio 49) desde un poco por debajo del
		 * aro, y el cañón (radio 49 también, con la caña a un lado) por encima del respaldo de la artillera. El aro va
		 * TNBuggyTurretMesh::RingDropCm por debajo del asiento, sobre dos patas a los lados de la cadera que bajan al suelo
		 * de la carrocería trasera; en las tortugas se meten en el caparazón.
		 */
		constexpr double TurretSweepRadius = 49.0;
		constexpr double TurretRingBottomZ = 119.0;
		/** Por dentro del aro solo pueden estar el sillín y el respaldo de la artillera, hasta esta altura. */
		constexpr double GunnerBackrestTopZ = 143.0;
	}

	/** Buffers de una pieza (pura: sin UObjects; la usan los tests). La rueda y la antena, en su espacio local. */
	TNProcMesh::FTNProcMeshBuffers BuildPiece(ETNBuggyBodyStyle Style, EPiece Piece);

	/** Dónde va la antena en el chasis (su pie): en las tortugas, la aleta trasera izquierda; en el de serie, el parachoques. */
	FVector AntennaMount(ETNBuggyBodyStyle Style);

	/** Punta del escape del modelo (cm, chasis), de donde sale la llama del turbo; en el de serie, Default. */
	FVector ExhaustLocal(ETNBuggyBodyStyle Style, const FVector& Default);

	/** Sustituye los códigos de zona por los colores de la pintura (M_CosmeticVertexColor, si falta M_BuggyPaint). */
	void BakePaint(TNProcMesh::FTNProcMeshBuffers& Buffers, const FTNBuggyPaintInfo& Paint, const FLinearColor& TeamColor);

	/** M_BuggyPaint (null si el asset no existe). */
	UMaterialInterface* PaintMaterial();

	/**
	 * Malla de la pieza, construida una vez y compartida (fuera del recolector). Con BakePaint, la versión con los colores
	 * horneados para M_CosmeticVertexColor. Null si la pieza no tiene nada en ese modelo (el de serie no tiene ninguna).
	 */
	UStaticMesh* GetPieceMesh(ETNBuggyBodyStyle Style, EPiece Piece, const FTNBuggyPaintInfo* BakedPaint = nullptr);

	/**
	 * Escribe la pintura y el color del equipo en una instancia de M_BuggyPaint. bStockZones: para SM_TN_BuggyBody y
	 * SM_TN_BuggyTire, que marcan sus zonas como M_TN_BuggyZones (pintura, detalle, chasis, neumático y luz).
	 */
	void ApplyPaint(UMaterialInstanceDynamic* MID, const FTNBuggyPaintInfo& Paint, const FLinearColor& TeamColor, bool bStockZones = false);

	/** El buggy de serie de Art/Source para el escaparate (las mismas rutas que ATN_Buggy). */
	namespace Stock
	{
		UStaticMesh* BodyMesh();
		UStaticMesh* TireMesh();
		/** Skin del equipo (Mar, Alga, Medusa en ciclo, como ATN_Buggy); sin equipo, la primera. */
		UMaterialInterface* Skin(int32 TeamIndex);
	}
}
