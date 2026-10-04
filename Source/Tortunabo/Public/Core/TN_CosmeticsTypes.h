#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Materials/MaterialInterface.h"
#include "TN_CosmeticsTypes.generated.h"

class UStaticMesh;
class UTexture2D;

/**
 * Categoría de cosmético de la tienda y del probador del lobby. Un casco es una fila de DT_Helmets; el caparazón, el
 * color del cuerpo y los ojos son filas de DT_Skins, y se equipan por separado (EquippedShellId, EquippedSkinId y
 * EquippedEyesId). El modelo y la pintura del buggy del Rally salen del catálogo en C++ de Vehicles/TN_BuggyCosmetics.h
 * y se equipan juntos (EquippedBuggyLook).
 */
UENUM(BlueprintType)
enum class ETNCosmeticCategory : uint8
{
	Helmet     UMETA(DisplayName = "Casco"),
	Shell      UMETA(DisplayName = "Caparazón"),
	Body       UMETA(DisplayName = "Color"),
	Eyes       UMETA(DisplayName = "Ojos"),
	BuggyModel UMETA(DisplayName = "Modelo de buggy"),
	BuggyPaint UMETA(DisplayName = "Pintura de buggy"),
};

/** Categorías del buggy del Rally (no son de la tortuga). */
inline bool TNIsBuggyCategory(ETNCosmeticCategory Category)
{
	return Category == ETNCosmeticCategory::BuggyModel || Category == ETNCosmeticCategory::BuggyPaint;
}

/** Tipo de ojo (parámetro EyeStyle de M_TurtleBody). */
UENUM(BlueprintType)
enum class ETNEyeStyle : uint8
{
	Classic UMETA(DisplayName = "Clásicos"),
	Iris    UMETA(DisplayName = "Iris de color"),
	Star    UMETA(DisplayName = "Pupila de estrella"),
	Heart   UMETA(DisplayName = "Pupila de corazón"),
	Toon    UMETA(DisplayName = "De dibujo"),
	Spiral  UMETA(DisplayName = "Espiral"),
	Cat     UMETA(DisplayName = "De gato"),
	Galaxy  UMETA(DisplayName = "Galaxia"),
};

/** Dibujo del caparazón (parámetro ShellPattern de M_TurtleBody). */
UENUM(BlueprintType)
enum class ETNShellPattern : uint8
{
	Plain   UMETA(DisplayName = "Liso"),
	Scutes  UMETA(DisplayName = "Escamas"),
	Spots   UMETA(DisplayName = "Lunares"),
	Waves   UMETA(DisplayName = "Olas"),
	Stars   UMETA(DisplayName = "Estrellas"),
	Lava    UMETA(DisplayName = "Grietas de lava"),
	Checker UMETA(DisplayName = "Ajedrez"),
	Melon   UMETA(DisplayName = "Sandía"),
};

/**
 * Fila de DataTable que describe un casco cosmético (DT_Helmets en Content/Blueprints/Gameplay/Cosmetics/).
 * La RowName debe coincidir con el HelmetId usado en MP_GameInstance y PlayerState.
 *
 * El casco va en el socket "Sombrero" de la malla si lo tiene; si no, en el hueso "Head", con el ajuste de abajo
 * medido desde la coronilla en el espacio de la malla (UTN_CosmeticLook::AttachHelmet). Los cascos de la tienda
 * (SM_Helmet_*, Scripts/build_cosmetics.py) ya están modelados sobre la coronilla: ajuste a cero.
 */
USTRUCT(BlueprintType)
struct FTN_HelmetData : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * Identificador único del casco.
	 * Debe coincidir con la RowName en DT_Helmets y con los IDs en DefaultUnlockedHelmets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName HelmetId = NAME_None;

	/** Nombre legible mostrado en el menú de cosméticos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FText DisplayName;

	/**
	 * Mesh 3D que se instancia en el socket "Sombrero" del personaje al equiparlo.
	 * Si es null, se muestra el nombre pero sin mesh (útil para placeholders).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	TObjectPtr<UStaticMesh> DisplayMesh = nullptr;

	/**
	 * Icono 2D para el grid del menú de cosméticos.
	 * Si es null, el widget BP puede usar un placeholder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Escala del casco sobre la de la malla de la tortuga ((1,1,1) = la del modelo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FVector MeshScale = FVector::OneVector;

	/** Desplazamiento desde la coronilla, en unidades de la malla (sin la escala del personaje). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FVector MeshOffset = FVector::ZeroVector;

	/** Giro sobre la coronilla (grados). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FRotator MeshRotation = FRotator::ZeroRotator;

	/** Precio en la tienda (0 = gratis; la economía de conchas o estrellas llegará más adelante). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Shop", meta = (ClampMin = "0"))
	int32 Price = 0;

	/** Lo que dice el tendero al enseñarlo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Shop")
	FText Description;

	bool IsValid() const { return HelmetId != NAME_None; }
};

/**
 * Fila de DataTable que describe un skin de personaje.
 * Crear DT_Skins en Content/Blueprints/Gameplay/Cosmetics/ con esta struct.
 * La RowName debe coincidir con el SkinId usado en MP_GameInstance y PlayerState.
 *
 * Mapeo de slots del SkM unificado (5 slots):
 *   Slot 0 → Barriga                         (BellyMaterial)
 *   Slot 1 → Brillo de los ojos              (EyeShineMaterial)
 *   Slot 2 → Ojos y boca                     (EyesMouthMaterial)
 *   Slot 3 → Cabeza, patas, brazos, cola     (SkinMaterial)
 *   Slot 4 → Caparazón principal             (ShellMaterial)
 *
 * Cada material es opcional: si está null se mantiene el material por defecto
 * de ese slot (cacheado en BeginPlay). SkinId == NAME_None → sin skin.
 */
USTRUCT(BlueprintType)
struct FTN_SkinData : public FTableRowBase
{
	GENERATED_BODY()

	/** Identificador único del skin. Debe coincidir con la RowName en DT_Skins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName SkinId = NAME_None;

	/** Nombre legible mostrado en el menú de cosméticos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FText DisplayName;

	/** Slot 0 — Barriga. Si null, se mantiene el material por defecto del SkM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Slots")
	TObjectPtr<UMaterialInterface> BellyMaterial = nullptr;

	/** Slot 1 — Brillo de los ojos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Slots")
	TObjectPtr<UMaterialInterface> EyeShineMaterial = nullptr;

	/** Slot 2 — Ojos y boca. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Slots")
	TObjectPtr<UMaterialInterface> EyesMouthMaterial = nullptr;

	/** Slot 3 — Cabeza, patas, brazos, cola. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Slots")
	TObjectPtr<UMaterialInterface> SkinMaterial = nullptr;

	/** Slot 4 — Caparazón principal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Slots")
	TObjectPtr<UMaterialInterface> ShellMaterial = nullptr;

	/** Icono 2D para el selector de skins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/**
	 * Qué es en la tienda y el probador: Shell cambia solo el caparazón y se equipa aparte (EquippedShellId); Body es
	 * el color del cuerpo (EquippedSkinId).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Shop")
	ETNCosmeticCategory Category = ETNCosmeticCategory::Body;

	/** Precio en la tienda (0 = gratis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Shop", meta = (ClampMin = "0"))
	int32 Price = 0;

	/** Lo que dice el tendero al enseñarlo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Shop")
	FText Description;

	// ── Aspecto en la tortuga de demo (TotugaDemo_Rig: el caparazón comparte material con el cuerpo) ──
	// UTN_CosmeticLook pinta con estos valores M_TurtleBody (/Game/Cosmetics/Materials). Los materiales por ranura de
	// arriba son para la malla unificada de 5 ranuras y, si están puestos, mandan sobre estos.

	/** Color principal: el del cuerpo (Body) o el de fondo del caparazón (Shell). También es la muestra de la tienda. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look")
	FLinearColor Color = FLinearColor(0.045f, 0.33f, 0.05f, 1.f);

	/** Segundo color: el de la barriga (Body) o el del dibujo del caparazón (Shell). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look")
	FLinearColor Color2 = FLinearColor(0.9f, 0.77f, 0.38f, 1.f);

	/** Body: cuánto se nota la barriga (0 = del mismo color que el cuerpo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look", meta = (ClampMin = "0", ClampMax = "1"))
	float BellyAmount = 0.f;

	/** Shell: dibujo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look")
	ETNShellPattern Pattern = ETNShellPattern::Plain;

	/** Shell: tamaño del dibujo (1 = el de serie). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look", meta = (ClampMin = "0.2", ClampMax = "4"))
	float PatternScale = 1.f;

	/** Shell: brillo metálico (0 mate, 1 metal pulido). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look", meta = (ClampMin = "0", ClampMax = "1"))
	float Shine = 0.f;

	/** Shell: luz propia del dibujo (lava, galaxia). Eyes: luz propia del iris (galaxia). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look", meta = (ClampMin = "0", ClampMax = "20"))
	float Glow = 0.f;

	/** Eyes: tipo de ojo (Color = iris o pupila de color; Color2 = segundo color: brillos de la galaxia). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics|Look")
	ETNEyeStyle EyeStyle = ETNEyeStyle::Classic;

	bool IsValid() const { return SkinId != NAME_None; }
};

/**
 * Conjunto de cosméticos de una tortuga: lo que replica el PlayerState y lo que enseñan la tienda, el probador y el
 * tendero. NAME_None = el de serie (casco rojo, cuerpo verde, caparazón del color del cuerpo y ojos clásicos).
 */
USTRUCT(BlueprintType)
struct FTN_TurtleLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName HelmetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName ShellId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName SkinId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName EyesId = NAME_None;

	FName Get(ETNCosmeticCategory Category) const
	{
		switch (Category)
		{
		case ETNCosmeticCategory::Helmet:     return HelmetId;
		case ETNCosmeticCategory::Shell:      return ShellId;
		case ETNCosmeticCategory::Eyes:       return EyesId;
		case ETNCosmeticCategory::BuggyModel:
		case ETNCosmeticCategory::BuggyPaint: return NAME_None;
		default:                              return SkinId;
		}
	}

	/** Las categorías del buggy no son de la tortuga: se ignoran (van en FTN_BuggyLook). */
	void Set(ETNCosmeticCategory Category, FName Id)
	{
		switch (Category)
		{
		case ETNCosmeticCategory::Helmet:     HelmetId = Id; break;
		case ETNCosmeticCategory::Shell:      ShellId = Id; break;
		case ETNCosmeticCategory::Eyes:       EyesId = Id; break;
		case ETNCosmeticCategory::BuggyModel:
		case ETNCosmeticCategory::BuggyPaint: break;
		default:                              SkinId = Id; break;
		}
	}

	bool Equals(const FTN_TurtleLook& Other) const
	{
		return HelmetId == Other.HelmetId && ShellId == Other.ShellId && SkinId == Other.SkinId && EyesId == Other.EyesId;
	}
};

/**
 * Aspecto del buggy del Rally de una jugadora: el modelo de carrocería y la pintura (colores de la carrocería, de las
 * placas y de las aletas, dibujo y brillo). NAME_None = el de serie (Buggy Clásico en Verde de serie). Lo replica el
 * PlayerState (EquippedBuggyLook), lo guarda el perfil cosmético y lo pinta ATN_Buggy con el de su conductora. El
 * catálogo (nombres, precios y colores) está en Vehicles/TN_BuggyCosmetics.h.
 */
USTRUCT(BlueprintType)
struct FTN_BuggyLook
{
	GENERATED_BODY()

	/** Fila de modelo (BuggyModel_*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName ModelId = NAME_None;

	/** Fila de pintura (BuggyPaint_*): colores, dibujo y brillo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmetics")
	FName PaintId = NAME_None;

	FName Get(ETNCosmeticCategory Category) const
	{
		return Category == ETNCosmeticCategory::BuggyModel ? ModelId : (Category == ETNCosmeticCategory::BuggyPaint ? PaintId : NAME_None);
	}

	/** Las categorías de la tortuga se ignoran. */
	void Set(ETNCosmeticCategory Category, FName Id)
	{
		if (Category == ETNCosmeticCategory::BuggyModel) { ModelId = Id; }
		else if (Category == ETNCosmeticCategory::BuggyPaint) { PaintId = Id; }
	}

	bool Equals(const FTN_BuggyLook& Other) const { return ModelId == Other.ModelId && PaintId == Other.PaintId; }
	bool operator==(const FTN_BuggyLook& Other) const { return Equals(Other); }
	bool operator!=(const FTN_BuggyLook& Other) const { return !Equals(Other); }
};
