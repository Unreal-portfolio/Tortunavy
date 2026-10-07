#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "World/Beach/TN_BeachTypes.h"
#include "TN_BeachPropMeshes.h"

class UPrimitiveComponent;
class UStaticMesh;

namespace TNBeachLayout
{
	struct FItem;
	struct FRoundLayout;
}

/**
 * Recetas del decorado de la playa ya montadas (mallas estáticas en ejecución, compartidas por todos los ejemplares y
 * fuera del recolector) y cómo se coloca y se anima cada ejemplar. Lo comparten ATN_BeachDecor (un actor por pieza: lo
 * que crea TN.Beach.Place) y ATN_BeachDecorField (el decorado de cada ronda, instanciado y local en cada máquina), para
 * que una pieza salga igual por los dos caminos. Definido en TN_BeachDecor.cpp (Docs/Modo_Carrera.md, «Decorado
 * gigante» y «Rendimiento y red»).
 */
namespace TNBeachDecorKit
{
	/** Mallas de una receta (elemento y variante, o pieza de un tramo) y cómo se coloca cada ejemplar. */
	struct FRecipe
	{
		/** Malla fija, con la colisión simple de la receta en su BodySetup (null si la receta no ha salido). */
		UStaticMesh* Body = nullptr;
		/** Parte que se mueve (sin colisión; null si no tiene). */
		UStaticMesh* Moving = nullptr;
		TNBeachProp::FPropInfo Info;
		bool bCollision = false;
	};

	/** Receta de la variante Variant de Element: se monta la primera vez que se pide y queda para toda la partida. */
	FRecipe Single(ETNBeachElement Element, int32 Variant);

	/** Receta de la pieza PieceIndex de un tramo (módulo de pasarela; palo o cuerda del caminito). */
	FRecipe Piece(ETNBeachElement Element, int32 PieceIndex);

	/** Si la receta ya está montada (montarla cuesta unos milisegundos: el campo de decorado lo reparte en fotogramas). */
	bool IsSingleCached(ETNBeachElement Element, int32 Variant);
	bool IsPieceCached(ETNBeachElement Element, int32 PieceIndex);

	/** Recetas montadas en esta partida (TN.Beach.Perf). */
	int32 NumCachedRecipes();

	/** Variante de un ejemplar (sale de su semilla: 4 por pieza, 8 rocas, 3 grupos de rocas...). */
	int32 VariantOf(ETNBeachElement Element, int32 Seed);

	/** Tamaño con el que se construye: el de Spec.SizeScale, entre 0,5 y 1,6 (fuera, los escalones dejarían de subirse). */
	float ClampSize(float SizeScale);

	/**
	 * Malla fija de un ejemplar respecto a su origen (el del elemento del reparto, en la arena): giro, inclinación y
	 * hundimiento (de su semilla, iguales en todas las máquinas) y tamaño. El actor no se escala.
	 */
	FTransform BodyPlacement(const TNBeachProp::FPropInfo& Info, int32 Seed, float Size);

	/**
	 * Si el ejemplar del reparto no gira al azar: el castillo enorme de la pasada de castillos (EItemRole::Castle) tiene
	 * patio (#741) y su puerta mira hacia quien llega (FixedYawOf).
	 */
	bool HasFixedYaw(const TNBeachLayout::FItem& Item);

	/** Giro (grados) del castillo con HasFixedYaw: 180° ± 12° según su semilla (el reparto no lo sabe: así su huella no cambia). */
	double FixedYawOf(const TNBeachLayout::FItem& Item);

	/** BodyPlacement de un ejemplar del reparto: como BodyPlacement, pero sin el giro al azar si HasFixedYaw. */
	FTransform ItemBodyPlacement(const TNBeachProp::FPropInfo& Info, const TNBeachLayout::FItem& Item, float Size);

	/** Inclinación máxima (grados) del decorado suelto que sigue la cuesta: más, y quedaría de canto. */
	constexpr double LitterMaxTilt = 30.0;

	/**
	 * Origen de una pieza del reparto en el espacio del generador: a la cota de su asiento, girada como en el reparto (lo
	 * mismo que SpawnElement). El decorado suelto pequeño (TNBeachLayout::IsLitter) no tiene asiento que se vea en la malla
	 * de 3 m: se apoya en la arena tal como se dibuja (TNBeachLayout::MeshSandZ) y se inclina con ella hasta LitterMaxTilt.
	 */
	FTransform ItemPlacement(const TNBeachLayout::FRoundLayout& Layout, const TNBeachLayout::FItem& Item);

	/**
	 * Piezas de un tramo (pasarela o caminito de palos) a lo largo de Extent por el eje X local, centrado en el origen,
	 * agrupadas por pieza (clave: PieceIndex). A lo largo y a lo ancho van a su tamaño; el alto no.
	 */
	void TilePlacements(ETNBeachElement Element, int32 Seed, float Size, float Extent, TMap<int32, TArray<FTransform>>& OutByPiece);

	/** Distancia a la que deja de dibujarse (cm; 0 = nunca): lo pequeño desde 120 m; lo de más de 10 m de huella, nunca. */
	float CullDistanceFor(ETNBeachElement Element, float Size);

	/** Distancia a una cámara local hasta la que se mueve la parte animada (60-200 m según el tamaño). */
	float AnimRangeFor(ETNBeachElement Element, float Size);

	/** Colisión de una pieza: bloquea todo y deja pasar la cámara salvo en lo grande y macizo (no da tirones). */
	void SetupCollision(UPrimitiveComponent* Comp, bool bCollision, bool bBlocksCamera);

	// ── Piezas de arte (Docs/Arte_Assets.md): las variantes de un elemento comparten nombre ──

	/** Malla fija del elemento («Beach.Decor.Coconut»...; NAME_None si no es decorado). Pivote: su origen en la arena. */
	FName BodySlot(ETNBeachElement Element);

	/** Parte que se mueve del elemento (tapa de la almeja, banderas...; NAME_None si no tiene). Pivote: el de su animación. */
	FName MovingSlot(ETNBeachElement Element);

	/** Pieza PieceIndex de un tramo: módulo o bajada de la pasarela, palo o cuerda del caminito (NAME_None si no es un tramo). */
	FName PieceSlot(ETNBeachElement Element, int32 PieceIndex);

	/** Estado de la animación de un ejemplar entre fotogramas (qué ciclo va la almeja y si alguien la tenía encima). */
	struct FAnimState
	{
		int32 ClamCycle = -1;
		bool bClamHeld = false;
	};

	/** Fase de la animación de un ejemplar (sale de su semilla: no se mueven todos a la vez). */
	float AnimPhaseOf(int32 Seed);

	/**
	 * Pose de la parte animada en el instante Time (s desde que empezó a moverse): su transformación respecto a la malla
	 * fija (en el pivote Info.AnimPivot, con el giro y la escala del movimiento). IsSomeoneOnTop se pregunta al empezar
	 * cada ciclo de la almeja: si hay alguien encima, ese ciclo no se abre.
	 */
	FTransform AnimPose(const TNBeachProp::FPropInfo& Info, float Time, float Phase, int32 Seed, FAnimState& State,
		TFunctionRef<bool()> IsSomeoneOnTop);

	// ── Montículos de arena removida de los rebuscables (ATN_BeachSearchRegistry, TN_BeachSearchMounds.cpp) ──

	/** Variantes: liso, con una chapa, con un palito de helado y con un trozo de concha asomando. */
	constexpr int32 NumSearchMoundVariants = 4;

	/** Radio y alto (cm) del montículo con tamaño 1 (la tortuga mide ~1,4 m). */
	constexpr double SearchMoundRadius = 100.0;
	constexpr double SearchMoundHeight = 38.0;

	/**
	 * Malla del montículo (Variant) o, con bFlat, el mismo ya aplanado (arena removida casi a ras, con marcas de
	 * escarbar). Sin colisión; se monta una vez por partida.
	 */
	UStaticMesh* SearchMoundMesh(int32 Variant, bool bFlat);
}
