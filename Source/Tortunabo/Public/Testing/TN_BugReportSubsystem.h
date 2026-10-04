#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TN_BugReportSubsystem.generated.h"

class IInputProcessor;

/**
 * Informe de bug con F8 (E0-06; solo compilaciones que no son Shipping). Al pulsar F8 en la ventana del juego, con la consola
 * (TN.BugReport) o tras N segundos con -TNBugReportAfter=<s>, crea Saved/BugReports/<fecha>/ con captura.png (si el proceso
 * dibuja), log.txt (últimas 2000 líneas), partida.json (GameState, semillas, mapa, commit y red), jugador.json (jugadores
 * locales) e informe.md, listo para pegar en una issue con la plantilla «Fallo». F8 se lee con un IInputProcessor de Slate,
 * antes que el PlayerController y el HUD; en PIE solo responde la ventana con el foco. Docs/Pruebas_Red_Local.md.
 */
UCLASS()
class TORTUNABO_API UTN_BugReportSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Crea el informe ahora. Devuelve la carpeta (vacía si no se ha podido). Trigger: «F8», «consola»… */
	FString CreateReport(const FString& Trigger);

	/** Carpeta del último informe creado (vacía si no hay). */
	const FString& GetLastReportFolder() const { return LastFolder; }

private:
	/** F8 desde Slate: true si este juego tiene el foco y se ha creado el informe (la tecla no sigue). */
	bool HandleF8();
	bool HasViewportFocus() const;
	bool TickAutoReport(float DeltaTime);
	FString MakeFolder() const;

	TSharedPtr<IInputProcessor> InputProcessor;
	FTSTicker::FDelegateHandle AutoReportHandle;
	double AutoReportAt = 0.0;
	FString LastFolder;
};
