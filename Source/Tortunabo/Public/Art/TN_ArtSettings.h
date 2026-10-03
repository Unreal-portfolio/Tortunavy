#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TN_ArtSettings.generated.h"

class ACharacter;
class UAnimSequence;
class UTN_ArtCatalog;

/**
 * Catálogos de arte que usa el juego (Ajustes del proyecto > Tortunavy > Arte; Config/DefaultGame.ini,
 * [/Script/Tortunabo.TN_ArtSettings]). Cada pieza generada desde C++ busca su sustituto en todos ellos (Docs/Arte_Assets.md).
 * /Game/Art se cocina entero (DirectoriesToAlwaysCook): todas las máquinas resuelven lo mismo.
 *
 * También dice de dónde sale la tortuga (Docs/Arte_Assets.md, «La tortuga»): su personaje, sus animaciones y las ranuras de
 * material que pintan los cosméticos. Solo rutas blandas: el objeto de los ajustes vive fuera del recolector y no puede
 * retener assets cargados (#328); TNTurtleArt los carga cuando se piden.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Arte"))
class TORTUNABO_API UTN_ArtSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTN_ArtSettings();

	/**
	 * Catálogos por zona: DA_Arte_Lobby (Lobby.*), DA_Arte_ProcMap (ProcMap.* y Beach.*, los dos mapas procedurales) y
	 * DA_Arte_Tortuga (Turtle.*, las piezas que van pegadas a la tortuga). Si una pieza sale en dos, gana el primero.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Catálogos")
	TArray<TSoftObjectPtr<UTN_ArtCatalog>> Catalogs;

	/**
	 * Personaje de la tortuga (BP_TortugaCharacter). Su componente Mesh (malla esquelética, materiales y transformación) es
	 * la única fuente de la tortuga: el jugador y todas sus copias (tendero, general, escaparate de cosméticos, podio) la
	 * dibujan con esa malla. Para cambiar la tortuga se cambia la malla de ese Blueprint, nada más.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tortuga")
	TSoftClassPtr<ACharacter> TurtleCharacter;

	/** Espera en bucle (jugador quieto, tendero, general, escaparate). Del esqueleto de la malla de la tortuga. */
	UPROPERTY(Config, EditAnywhere, Category = "Tortuga|Animaciones")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	/** Andar del jugador (se mezcla con la espera según la velocidad). */
	UPROPERTY(Config, EditAnywhere, Category = "Tortuga|Animaciones")
	TSoftObjectPtr<UAnimSequence> WalkAnim;

	/** Celebrar: el emote de gritar del jugador y la pose de celebración del escaparate. */
	UPROPERTY(Config, EditAnywhere, Category = "Tortuga|Animaciones")
	TSoftObjectPtr<UAnimSequence> CheerAnim;

	/** Saludo del tendero y del general y pose de saludo del escaparate. */
	UPROPERTY(Config, EditAnywhere, Category = "Tortuga|Animaciones")
	TSoftObjectPtr<UAnimSequence> SaluteAnim;

	/**
	 * Ranura de material de la malla de la tortuga que lleva el casco de serie y la lengua: los cosméticos le ponen
	 * M_TurtleHelmetSlot (recorta el casco si se lleva otro y esconde la lengua rígida). Si la malla no tiene una ranura con
	 * este nombre, no se toca.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tortuga|Cosméticos")
	FName HelmetMaterialSlot = TEXT("lambert2");

	/**
	 * Ranura del cuerpo: los cosméticos le ponen M_TurtleBody (colores, dibujo del caparazón, ojos y boca pintados, medidos
	 * sobre la malla de demo). Con esta ranura y la del casco, la cara (lengua, sudor, parpadeo) se anima; si la malla no
	 * tiene alguna de las dos, la tortuga se queda con sus propios materiales.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tortuga|Cosméticos")
	FName BodyMaterialSlot = TEXT("lambert4");

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
