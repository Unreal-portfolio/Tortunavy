#include "Core/TN_GameplayPreload.h"
#include "Core/TN_Log.h"
#include "Engine/DataTable.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPtr.h"

namespace TNPreloadDetail
{
	/** La ruta de DT_Items y la del material de la tienda. */
	const TCHAR* const ItemCatalogPath = TEXT("/Game/Blueprints/Gameplay/Items/DT_Items.DT_Items");
	const TCHAR* const PreviewMaterialPath = TEXT("/Game/UI/Shop/M_UI_Preview.M_UI_Preview");

	/** Lo ya cargado; si no lo está, lo carga (y avisa: es un tirón en partida que debería haberse precargado). */
	template <typename T>
	T* Resolve(const TCHAR* Path)
	{
		const TSoftObjectPtr<T> Soft{FSoftObjectPath(Path)};
		if (T* Loaded = Soft.Get())
		{
			return Loaded;
		}
		UE_LOG(LogTortunabo, Warning, TEXT("[Precarga] %s no estaba cargado: carga síncrona en partida."), Path);
		return Soft.LoadSynchronous();
	}
}

const UDataTable* TNPreload::ItemCatalog()
{
	return TNPreloadDetail::Resolve<UDataTable>(TNPreloadDetail::ItemCatalogPath);
}

UMaterialInterface* TNPreload::PreviewMaterial()
{
	return TNPreloadDetail::Resolve<UMaterialInterface>(TNPreloadDetail::PreviewMaterialPath);
}

void UTN_GameplayPreloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const double T0 = FPlatformTime::Seconds();
	for (const TCHAR* Path : { TNPreloadDetail::ItemCatalogPath, TNPreloadDetail::PreviewMaterialPath })
	{
		if (UObject* Loaded = TSoftObjectPtr<UObject>(FSoftObjectPath(Path)).LoadSynchronous())
		{
			Retained.Add(Loaded);
		}
		else
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Precarga] No se ha podido cargar %s."), Path);
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Precarga] %d recursos de partida cargados en %.1f ms."),
		Retained.Num(), (FPlatformTime::Seconds() - T0) * 1000.0);
}
