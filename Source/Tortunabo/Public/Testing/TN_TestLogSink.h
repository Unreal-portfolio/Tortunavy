#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"

class FJsonObject;

/**
 * Sumidero del registro para las pruebas de monkey y de estrés: cuenta errores, asserts y ensures, los avisos del instrumento
 * TN.Shell.Debug, las correcciones de red, y guarda las primeras líneas distintas de cada clase.
 * Se engancha a GLog mientras dura la sesión. Serialize puede llamarse desde cualquier hilo.
 */
class TORTUNABO_API FTNTestLogSink : public FOutputDevice
{
public:
	struct FEntry
	{
		FString Category;
		FString Message;
		int32 Count = 0;
	};

	virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override;
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	/** Clasifica una línea (sin tocar el estado); para tests. */
	enum class EKind : uint8 { Other, Ensure, ShellSpin, ShellSunk, ShellVelocityJump, NetCorrection };
	static EKind Classify(const FString& Message, const FName& Category, ELogVerbosity::Type Verbosity);

	int32 GetErrorCount() const;
	int32 GetEnsureCount() const;
	int32 GetShellSunkCount() const;
	int32 GetNetCorrectionCount() const;

	/** Vuelca los contadores y las entradas al objeto JSON (clave por clave; sin sobrescribir lo que ya hubiera). */
	void WriteJson(FJsonObject& Out) const;

private:
	static constexpr int32 MaxEntries = 40;

	void AddEntry(TArray<FEntry>& Into, const FName& Category, const FString& Message) const;

	mutable FCriticalSection Lock;
	int32 Errors = 0;
	int32 Warnings = 0;
	int32 Ensures = 0;
	int32 ShellSpin = 0;
	int32 ShellSunk = 0;
	int32 ShellVelocityJump = 0;
	int32 NetCorrections = 0;
	TMap<FName, int32> WarningsByCategory;
	TMap<FName, int32> ErrorsByCategory;
	TArray<FEntry> ErrorEntries;
	TArray<FEntry> EnsureEntries;
	TArray<FEntry> WarningEntries;
};
