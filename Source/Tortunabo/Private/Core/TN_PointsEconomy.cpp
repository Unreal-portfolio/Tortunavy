#include "Core/TN_PointsEconomy.h"
#include "Core/TN_Log.h"

float UTN_PointsEconomy::GetTargetSeconds(FName MapName) const
{
	if (const float* Found = LevelTargetSeconds.Find(MapName))
	{
		return FMath::Max(0.f, *Found);
	}
	return FMath::Max(0.f, DefaultTargetSeconds);
}

const UTN_PointsEconomy& UTN_PointsEconomy::Get()
{
	const UTN_EconomySettings* Settings = GetDefault<UTN_EconomySettings>();
	if (Settings && !Settings->PointsEconomy.IsNull())
	{
		if (const UTN_PointsEconomy* Loaded = Settings->PointsEconomy.LoadSynchronous())
		{
			return *Loaded;
		}
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogTortunabo, Warning, TEXT("[Economía] No se carga '%s': se usan los valores de serie de la decisión del 07-10."),
				*Settings->PointsEconomy.ToString());
		}
	}
	return *GetDefault<UTN_PointsEconomy>();
}

UTN_EconomySettings::UTN_EconomySettings()
{
	CategoryName = TEXT("Tortunavy");
	SectionName = TEXT("Economía");
}
