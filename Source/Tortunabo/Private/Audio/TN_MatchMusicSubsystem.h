#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/TN_MusicSynthComponent.h"
#include "TN_MatchMusicDirector.h"
#include "TN_MatchMusicSubsystem.generated.h"

class APlayerController;

/**
 * Música de fin de partida (victoria, derrota y eliminado) en cada máquina con jugador, para su jugador local: 2D, en
 * un UTN_MusicSynthComponent colgado del PlayerController local (como la música de la tienda). Sin RPC ni cambios en
 * el GameMode: vigila diez veces por segundo los estados que ya se replican (MatchFlowState, CountdownValue y
 * RaceResults del ATN_CoopGameState; bHasFinishedRun, bIsEliminated y bIsAlive de los ATN_CoopPlayerState) y aplica lo que decide TNMatchMusic::FDirector
 * (TN_MatchMusicDirector.h, lógica pura probada fuera del motor).
 *
 * Vive con el mundo (un subsistema por mundo de juego): al empezar a desmontarse el mundo (vuelta al lobby, salida al
 * menú o cualquier otro cambio de mapa) quita su componente del PlayerController, que viaja al mundo nuevo sin él, así
 * que la música se para siempre al cambiar de mapa y la tienda del lobby nunca se la encuentra. Para entonces ya está
 * en silencio: el director funde a silencio en el último segundo de la cuenta atrás de los resultados. En el lobby y
 * en los mapas de transición no suena nada porque el director solo se arma al ver la partida en marcha.
 *
 * Pruebas por consola: TN.Music.Play Victory|Defeat|Eliminated|Shop|Booth|None (también en español: Victoria,
 * Derrota, Eliminado, Tienda, Probador, Silencio) y TN.Music.MatchVolume (1 por defecto).
 */
UCLASS()
class TORTUNABO_API UTN_MatchMusicSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Pruebas (TN.Music.Play): hace sonar InTrack ya en el jugador local, haya partida o no. */
	void DebugPlayTrack(ETNMusicTrack InTrack);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Foto del estado replicado para el director y aplicación de lo que pida. */
	void Poll(UWorld& InWorld, double InNowSeconds);

	void ApplyRequest(APlayerController* InLocalController, const TNMatchMusic::FRequest& InRequest);

	/** Componente de música 2D del jugador local; lo crea si no existe o si el PlayerController local ha cambiado. */
	UTN_MusicSynthComponent* EnsureMusic(APlayerController* InLocalController);

	/** Quita el componente del PlayerController (si aún sonaba algo, se corta con el resto del audio del mundo). */
	void ReleaseMusic();

	/** Aplica TN.Music.MatchVolume si ha cambiado. */
	void RefreshVolume();

	void HandleWorldBeginTearDown(UWorld* InWorld);

	TWeakObjectPtr<UTN_MusicSynthComponent> Music;
	TNMatchMusic::FDirector Director;
	FDelegateHandle TearDownHandle;
	double NextPollSeconds = 0.0;
	float AppliedVolume = -1.f;
};
