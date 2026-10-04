#include "Settings/TN_RayTracingQualitySubsystem.h"
#include "Core/TN_Log.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreMisc.h"

namespace TNRayTracingQuality
{
	bool ShouldEnable(int32 GlobalIlluminationQuality)
	{
		return GlobalIlluminationQuality >= MinQualityWithRayTracing;
	}
}

namespace TNRayTracingQualityDetail
{
	const TCHAR* const QualityCVarName = TEXT("sg.GlobalIlluminationQuality");
	const TCHAR* const RayTracingCVarName = TEXT("r.RayTracing.Enable");
}

bool UTN_RayTracingQualitySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Sin pantalla no hay nada que trazar: ni en el servidor dedicado ni en los commandlets (cocinado).
	return !IsRunningDedicatedServer() && !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UTN_RayTracingQualitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	IConsoleVariable* Quality = IConsoleManager::Get().FindConsoleVariable(TNRayTracingQualityDetail::QualityCVarName);
	if (!Quality)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Calidad] No existe %s: el ray tracing no sigue a la calidad."), TNRayTracingQualityDetail::QualityCVarName);
		return;
	}
	QualityChangedHandle = Quality->OnChangedDelegate().AddUObject(this, &UTN_RayTracingQualitySubsystem::HandleGlobalIlluminationQualityChanged);
	HandleGlobalIlluminationQualityChanged(Quality);
}

void UTN_RayTracingQualitySubsystem::Deinitialize()
{
	if (IConsoleVariable* Quality = IConsoleManager::Get().FindConsoleVariable(TNRayTracingQualityDetail::QualityCVarName))
	{
		Quality->OnChangedDelegate().Remove(QualityChangedHandle);
	}
	QualityChangedHandle.Reset();
	Super::Deinitialize();
}

void UTN_RayTracingQualitySubsystem::HandleGlobalIlluminationQualityChanged(IConsoleVariable* Variable)
{
	// Sin RHI_RAYTRACING (Android, Quest) la cvar no existe: nada que hacer.
	IConsoleVariable* RayTracing = IConsoleManager::Get().FindConsoleVariable(TNRayTracingQualityDetail::RayTracingCVarName);
	if (!Variable || !RayTracing) { return; }

	// Un valor puesto con más prioridad (consola, Engine.ini, línea de órdenes) manda: no se pisa.
	const uint32 SetBy = static_cast<uint32>(RayTracing->GetFlags()) & ECVF_SetByMask;
	if (SetBy > static_cast<uint32>(ECVF_SetByScalability)) { return; }

	const int32 Quality = Variable->GetInt();
	const bool bWanted = TNRayTracingQuality::ShouldEnable(Quality);
	if ((RayTracing->GetInt() != 0) == bWanted) { return; }
	RayTracing->Set(bWanted ? 1 : 0, ECVF_SetByScalability);
	UE_LOG(LogTortunabo, Log, TEXT("[Calidad] Iluminación global %d: ray tracing %s."), Quality, bWanted ? TEXT("encendido") : TEXT("apagado"));
}
