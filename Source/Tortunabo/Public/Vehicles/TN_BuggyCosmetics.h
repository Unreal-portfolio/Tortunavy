// Catálogo de aspectos del buggy del Rally (#114): modelos de carrocería y pinturas con nombre, frase del tendero y
// precio, y las reglas puras que usan la tienda, el probador y el servidor para validar un FTN_BuggyLook. Sin
// DataTable: el catálogo vive en C++ para que el servidor y los tests lo lean sin assets (Docs/Tienda_Probador.md).
#pragma once

#include "CoreMinimal.h"
#include "Core/TN_CosmeticsTypes.h"

/**
 * Dibujo de la pintura (parámetro Pattern de M_BuggyPaint). Los ocho primeros son los mismos de ETNShellPattern (y del
 * caparazón de la tortuga, con el mismo HLSL); los tres últimos son solo del buggy.
 */
enum class ETNBuggyPattern : uint8
{
	Plain,
	Scutes,
	Spots,
	Waves,
	Stars,
	Lava,
	Checker,
	Melon,
	/** Dos franjas de carreras a lo largo del capó y del caparazón, y una banda en los costados. */
	Stripes,
	/** Llamas de hot rod que salen del morro hacia atrás. */
	Flames,
	/** Camuflaje de manchas. */
	Camo,
	Count
};

/** Carrocería de cada modelo: la de serie es SM_TN_BuggyBody (Art/Source, #290); las de tortuga, TNBuggyArt en C++. */
enum class ETNBuggyBodyStyle : uint8
{
	/** El buggy de serie de Art/Source: chasis de tubos y pontones, con la skin de su equipo o una pintura de la tienda. */
	Stock,
	/** Tortuga común: caparazón de placas hexagonales, aletas, ojos grandes, cola de escape y alerón de vieira. */
	Classic,
	/** Tortuga caimán: placas con pinchos, pico de hierro, barra de luces, tubo de buceo y rueda de repuesto. */
	Offroad,
	/** Tortuga laúd: caparazón bajo de crestas, morro en cuña, alerón grande y escapes laterales. */
	Racer,
	Count
};

/** Un modelo de la tienda. */
struct FTNBuggyModelInfo
{
	/** NAME_None = el de serie. */
	FName Id;
	ETNBuggyBodyStyle Style = ETNBuggyBodyStyle::Stock;
	/** Conchas (0 = gratis). */
	int32 Price = 0;
	FText Name;
	/** Lo que dice el tendero al enseñarlo. */
	FText Description;
};

/** Una pintura de la tienda: colores (lineales), dibujo y brillo para M_BuggyPaint. */
struct FTNBuggyPaintInfo
{
	/** NAME_None = la de serie. */
	FName Id;
	int32 Price = 0;
	FText Name;
	FText Description;
	/** Carrocería y caparazón entre las placas. */
	FLinearColor Base = FLinearColor::White;
	/** Placas del caparazón y alerón. */
	FLinearColor Plates = FLinearColor::White;
	/** «Piel» de la tortuga: cabeza, aletas, cola y llantas. */
	FLinearColor Accent = FLinearColor::White;
	/** Color del dibujo (sobre la carrocería y las placas). */
	FLinearColor PatternColor = FLinearColor::White;
	ETNBuggyPattern Pattern = ETNBuggyPattern::Plain;
	/** Tamaño del dibujo (1 = el de serie). */
	float PatternScale = 1.f;
	/** 0 mate, 1 metal pulido. */
	float Shine = 0.f;
	/** Luz propia del dibujo (lava, estrellas, abisal...). */
	float Glow = 0.f;
};

namespace TNBuggyCosmetics
{
	/** Prefijos de los identificadores: los del buggy no se mezclan con los de DT_Helmets ni DT_Skins. */
	inline const TCHAR* ModelPrefix() { return TEXT("BuggyModel_"); }
	inline const TCHAR* PaintPrefix() { return TEXT("BuggyPaint_"); }

	/** Cota de lo desbloqueado que acepta el servidor (el catálogo entero cabe de sobra). */
	constexpr int32 MaxUnlocked = 64;

	/** Modelos: el primero es el de serie (Id NAME_None). */
	TORTUNABO_API const TArray<FTNBuggyModelInfo>& Models();

	/** Pinturas: la primera es la de serie (Id NAME_None). */
	TORTUNABO_API const TArray<FTNBuggyPaintInfo>& Paints();

	/** NAME_None = el de serie; un Id que no existe = nullptr. */
	TORTUNABO_API const FTNBuggyModelInfo* FindModel(FName Id);
	TORTUNABO_API const FTNBuggyPaintInfo* FindPaint(FName Id);

	/** Nunca nulo: un Id que no existe da el de serie. */
	TORTUNABO_API const FTNBuggyModelInfo& ResolveModel(FName Id);
	TORTUNABO_API const FTNBuggyPaintInfo& ResolvePaint(FName Id);

	/** Ids a la venta de una categoría del buggy, en orden de catálogo y sin el de serie (vacío para las demás). */
	TORTUNABO_API TArray<FName> CatalogIds(ETNCosmeticCategory Category);

	/** Precio (0 para NAME_None y para lo que no existe). */
	TORTUNABO_API int32 PriceOf(ETNCosmeticCategory Category, FName Id);

	/** NAME_None o una fila de esa categoría. */
	TORTUNABO_API bool IsKnown(ETNCosmeticCategory Category, FName Id);

	/** Identificador de modelo o de pintura del catálogo (no NAME_None). */
	TORTUNABO_API bool IsKnownBuggyId(FName Id);

	/** Categoría de un Id del catálogo (BuggyModel o BuggyPaint); false si no es del catálogo. */
	TORTUNABO_API bool CategoryOf(FName Id, ETNCosmeticCategory& OutCategory);

	/** Se puede poner: cada pieza es la de serie o una del catálogo gratis o desbloqueada. */
	TORTUNABO_API bool CanEquip(const FTN_BuggyLook& Look, const TSet<FName>& Unlocked);

	/** Lo que no está en el catálogo pasa a ser el de serie (perfil viejo o de otra versión). */
	TORTUNABO_API FTN_BuggyLook Sanitize(const FTN_BuggyLook& Look);

	/**
	 * Servidor: deja en OutKnown solo los Ids del catálogo. False, sin tocar OutKnown, si la lista supera MaxIds
	 * (cliente manipulado); true y OutKnown sustituido en otro caso.
	 */
	TORTUNABO_API bool FilterKnownIds(const TArray<FName>& Ids, int32 MaxIds, TSet<FName>& OutKnown);

	/** Clave estable de un aspecto (para no repetir el trabajo de aplicarlo). */
	TORTUNABO_API FString LookKey(const FTN_BuggyLook& Look);
}
