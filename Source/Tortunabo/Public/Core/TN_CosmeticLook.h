#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_CosmeticLook.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Viste a una tortuga con un FTN_TurtleLook. El personaje, el tendero, el general, el podio y las vistas previas de la
 * tienda y el probador usan esto mismo, así que se ven igual en todas partes. También le pega las piezas de Arte de la
 * tortuga (TNTurtleArt::ApplyPieces: caparazón, casco de serie, ojos y lengua del catálogo); el casco de la tienda esconde
 * el casco de serie de Arte.
 *
 * Las ranuras que se pintan van por nombre y se configuran en UTN_ArtSettings («Tortuga|Cosméticos»); una malla sin ellas
 * (la de Arte) se queda con sus materiales, sin casco pintado, sin cara animada y sin colores de la tienda.
 * Malla de demo (TotugaDemo_Rig, 2 ranuras): "lambert2" = casco rojo de serie + lengua; "lambert4" = cuerpo, ojos y
 * caparazón. La ranura del casco es siempre una instancia de M_TurtleHelmetSlot: pinta el casco de serie (o lo recorta
 * si se lleva uno de la tienda) y esconde la lengua rígida de la malla, que no tiene hueso (la del jugador la dibuja
 * UTN_TurtleFaceComponent). La del cuerpo es siempre una instancia de M_TurtleBody con los colores, el dibujo del
 * caparazón, los ojos de las filas y la boca pintada (las zonas salen de la posición local: ver
 * Scripts/build_cosmetics.py). Así los ojos nunca salen del color de la piel, aunque no se lleve nada de la tienda.
 * Malla unificada (5 ranuras: barriga, brillo de ojos, ojos y boca, piel, caparazón): los materiales por ranura.
 */
UCLASS()
class TORTUNABO_API UTN_CosmeticLook : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Aplica el conjunto completo (casco, caparazón y color) partiendo de los materiales originales.
	 * @param WorldContext  Para llegar a las tablas de la GameInstance (si es null se usa Body).
	 * @param Body          Malla de la tortuga.
	 * @param Helmet        Componente del casco (se engancha aquí a la cabeza); puede ser null.
	 * @param Defaults      Materiales originales de Body; si viene vacío se rellena ahora con los que tenga.
	 */
	static void ApplyLook(const UObject* WorldContext, USkeletalMeshComponent* Body, UStaticMeshComponent* Helmet, const FTN_TurtleLook& Look,
		TArray<TObjectPtr<UMaterialInterface>>& Defaults);

	/**
	 * Engancha Helmet a la cabeza de Body: al socket "Sombrero" si la malla lo tiene; si no, al hueso "Head", con el
	 * casco colocado en la coronilla de la postura de referencia (así sigue la animación de la cabeza).
	 */
	static void AttachHelmet(USkeletalMeshComponent* Body, UStaticMeshComponent* Helmet, const FTN_HelmetData* Row);

	/**
	 * Animación de los ojos sobre la instancia de M_TurtleBody que puso ApplyLook: Blink (0 abiertos, 1 cerrados, el
	 * párpado baja desde arriba) y Dizzy (1 = ojos en espiral, noqueada). Barato: solo escribe dos parámetros.
	 */
	static void SetEyeState(USkeletalMeshComponent* Body, float Blink, float Dizzy);

	/**
	 * Instancia de M_TurtleBody que puso ApplyLook en el cuerpo (null con la malla unificada o si aún no se ha vestido).
	 * UTN_TurtleFaceComponent escribe ahí la cara: EyeTired, EyeSqueeze, MouthOpen, MouthSmile y FaceBlush.
	 */
	static UMaterialInstanceDynamic* GetBodyMaterial(USkeletalMeshComponent* Body);

	/**
	 * La malla tiene las dos ranuras de la de demo (casco y lengua; cuerpo, ojos y caparazón; nombres en UTN_ArtSettings),
	 * con la boca que conoce la cara.
	 */
	static bool IsDemoTurtle(const USkeletalMeshComponent* Body);

	/** Nombre para la tienda y el probador (NAME_None = el de serie). */
	UFUNCTION(BlueprintPure, Category = "Cosmetics", meta = (WorldContext = "WorldContext"))
	static FText GetDisplayName(const UObject* WorldContext, ETNCosmeticCategory Category, FName Id);

	/** Lo que dice el tendero de él. */
	UFUNCTION(BlueprintPure, Category = "Cosmetics", meta = (WorldContext = "WorldContext"))
	static FText GetDescription(const UObject* WorldContext, ETNCosmeticCategory Category, FName Id);
};
