#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "Core/TN_CoopScore.h"
#include "TN_PuzzleScoreSubsystem.generated.h"

/**
 * Registro de los puzles del nivel para la eficiencia de puzle de la puntuación final del Coop (#789). Solo en el
 * servidor: los puzles se apuntan al empezar (Register), avisan de su primer avance (NotifyProgress: la primera placa
 * pisada o el primer botón pulsado) y de cuándo se resuelven (NotifySolved). La cuenta la hace
 * TNCoopScore::PuzzleEfficiency.
 *
 * Avisan: ATN_ButtonGroupManager, ATN_PressurePlateGroupManager y el interruptor del muro de lanzamiento (ATN_ProcSwitch).
 * Un puzle destruido sigue contando (la partida lo tuvo).
 */
UCLASS()
class TORTUNABO_API UTN_PuzzleScoreSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Servidor: el puzle existe en el nivel (no hace nada en un cliente ni si ya estaba). */
	static void Register(const AActor* Puzzle);

	/** Servidor: primer avance del puzle (los siguientes no cambian nada). Lo registra si no lo estaba. */
	static void NotifyProgress(const AActor* Puzzle);

	/** Servidor: el puzle se ha resuelto (solo cuenta la primera vez). Lo registra si no lo estaba. */
	static void NotifySolved(const AActor* Puzzle);

	/** Los puzles registrados, en el orden en que se apuntaron. */
	TArray<TNCoopScore::FPuzzleRun> GetRuns() const;

	/** Eficiencia de puzle del nivel; -1 si no hay puzles. */
	float GetEfficiency() const { return TNCoopScore::PuzzleEfficiency(GetRuns()); }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** El subsistema del mundo del puzle si este es el servidor; nullptr si no. */
	static UTN_PuzzleScoreSubsystem* ForServer(const AActor* Puzzle);

	TNCoopScore::FPuzzleRun& FindOrAdd(const AActor* Puzzle);
	float Now() const;

	TArray<FObjectKey> Order;
	TMap<FObjectKey, TNCoopScore::FPuzzleRun> Runs;
};
