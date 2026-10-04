// Aspecto del buggy del Rally (#297, #114): el modelo y la pintura de la tienda. El modelo de serie es el de Art/Source
// (SM_TN_BuggyBody y SM_TN_BuggyTire, #290): con la pintura de serie lleva la skin de su equipo y con una de la tienda,
// M_BuggyPaint con sus zonas y la antena con el banderín del equipo. Los de pago son las carrocerías de tortuga de
// TNBuggyArt por piezas, con sus ruedas. Lo usan ATN_Buggy (colgado del chasis, con su carrocería y sus neumáticos) y el
// escaparate de la tienda (con su propio buggy de serie, ruedas y torreta). Solo visual: no tiene colisión ni red.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/TN_CosmeticsTypes.h"
#include "TN_BuggyLookComponent.generated.h"

class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(ClassGroup = (Rally), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_BuggyLookComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UTN_BuggyLookComponent();

	/**
	 * Carrocería y neumáticos del ATN_Buggy (en el eje de cada rueda Chaos, los derechos girados 180 grados): el componente
	 * los viste en vez de crear los suyos y deja la torreta al buggy. Sin llamarlo (escaparate), crea su propio buggy de
	 * serie, sus cuatro ruedas y la torreta.
	 */
	void UseVehicleParts(UStaticMeshComponent* InBody, const TArray<UStaticMeshComponent*>& InTires);

	/** Escaparate: las piezas solo salen en las capturas y las ilumina el canal de luz del estudio. */
	void SetStudio(const FLightingChannels& Channels);

	/**
	 * Viste el buggy (TeamIndex < 0 = sin equipo: banderín e iris blancos). Con bForce lo rehace aunque no cambie.
	 * Devuelve si ha tocado algo.
	 */
	bool ApplyLook(const FTN_BuggyLook& InLook, int32 InTeamIndex, bool bForce = false);

	const FTN_BuggyLook& GetLook() const { return Look; }

	/** Ya se ha vestido alguna vez. */
	bool HasBuiltLook() const { return !AppliedKey.IsEmpty(); }

	/**
	 * El buggy de serie con la pintura de serie: carrocería y neumáticos llevan la skin del equipo, que pone ATN_Buggy
	 * (ApplyTint). En el escaparate la pone el propio componente.
	 */
	bool UsesTeamSkin() const { return bUsesTeamSkin; }

	/** Punta del escape del modelo puesto (cm, espacio del chasis), para la llama del turbo; en el de serie, Default. */
	FVector GetExhaustLocal(const FVector& Default) const;

	/** Todas las piezas propias que se pueden ver (carrocerías, ruedas y torreta del escaparate, antena). */
	void GetPrimitives(TArray<UPrimitiveComponent*>& Out) const;

	/** Pieza de carrocería de tortuga por índice (TNBuggyArt::EPiece, de Chassis a Extras): para engancharle arte (#319). */
	UStaticMeshComponent* GetBodyPiece(int32 Index) const { return BodyPieces.IsValidIndex(Index) ? BodyPieces[Index].Get() : nullptr; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void EnsureParts();
	UStaticMeshComponent* MakePart(const TCHAR* Name, USceneComponent* Parent);
	void SetupPart(UStaticMeshComponent* Part) const;
	/** Escaparate: aro, carro, cañón y caña de la torreta de ATN_Buggy (TNBuggyTurretMesh) en su pivote. */
	void BuildStudioTurret();
	void UpdateAntenna(float DeltaTime);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BodyPieces;

	/** Carrocería del buggy de serie: la del ATN_Buggy o, en el escaparate, la propia. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> StockBody;

	/** Neumáticos: los del ATN_Buggy o, en el escaparate, los propios. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Wheels;

	/** Neumático de serie, para volver a él al dejar una carrocería de tortuga. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> StockTireMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> TurretParts;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> AntennaPivot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Antenna;

	/** M_BuggyPaint con las zonas de las tortugas (también la antena). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PaintMID;

	/** M_BuggyPaint con las zonas del buggy de serie. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StockPaintMID;

	/** Escaparate: la skin del equipo con su color, como la pone ATN_Buggy. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StudioSkinMID;

	FTN_BuggyLook Look;
	int32 TeamIndex = INDEX_NONE;
	FString AppliedKey;
	uint8 AppliedStyle = 0;
	bool bVehicleParts = false;
	bool bStudio = false;
	bool bUsesTeamSkin = true;
	FLightingChannels StudioChannels;

	// Antena: muelle amortiguado que se inclina con la aceleración del chasis.
	FVector LastLocation = FVector::ZeroVector;
	FVector LastVelocity = FVector::ZeroVector;
	FVector2D Lean = FVector2D::ZeroVector;
	FVector2D LeanSpeed = FVector2D::ZeroVector;
	bool bHasLastLocation = false;
	float AntennaTime = 0.f;
};
