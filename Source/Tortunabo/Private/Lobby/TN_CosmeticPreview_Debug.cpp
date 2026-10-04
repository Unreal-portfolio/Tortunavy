// Escaparate de la tienda: fotos de prueba del buggy (TN.Buggy.Photos), fuera de Shipping. Hace lo mismo que la tienda
// (captura en HDR con las luces del estudio y la curva de M_UI_Preview) y lo guarda en PNG sobre un fondo de cartel.
//   UnrealEditor-Win64-DebugGame.exe <uproject> /Game/Maps/Rally/LVL_Rally -game -windowed -ResX=1280 -ResY=720 -NoSteam
//     -ExecCmds="TN.Buggy.Photos C:/ruta 768"

#include "Lobby/TN_CosmeticPreview.h"

#if !UE_BUILD_SHIPPING

#include "Vehicles/TN_BuggyCosmetics.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogTNBuggyPhotos, Log, All);

namespace TNBuggyPhotosDetail
{
	/** Guarda una captura del escaparate (HDR con alfa de cobertura) como PNG sobre un degradado de mar. */
	bool SaveCapturePng(UTextureRenderTarget2D* RT, const FString& File)
	{
		TArray<FFloat16Color> Pixels;
		FTextureRenderTargetResource* Resource = RT ? RT->GameThread_GetRenderTargetResource() : nullptr;
		const int32 Size = RT ? RT->SizeX : 0;
		if (!Resource || !Resource->ReadFloat16Pixels(Pixels) || Size <= 1 || Pixels.Num() != Size * RT->SizeY || RT->SizeY != Size) { return false; }

		// Como M_UI_Preview: exposición, curva ACES y cobertura = 1 - alfa; detrás, un degradado de mar.
		TArray<FColor> Out;
		Out.SetNumUninitialized(Size * Size);
		for (int32 y = 0; y < Size; ++y)
		{
			const float T = static_cast<float>(y) / (Size - 1);
			const FLinearColor Bg = FMath::Lerp(FLinearColor::FromSRGBColor(FColor(0x12, 0x30, 0x5A)), FLinearColor::FromSRGBColor(FColor(0x1E, 0x9C, 0xC6)), T);
			for (int32 x = 0; x < Size; ++x)
			{
				const FFloat16Color& P = Pixels[y * Size + x];
				FLinearColor C(P.R.GetFloat(), P.G.GetFloat(), P.B.GetFloat(), P.A.GetFloat());
				const auto Aces = [](float X) { X = FMath::Max(X * 1.6f, 0.f); return FMath::Clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.f, 1.f); };
				const float Cover = FMath::Clamp(1.f - C.A, 0.f, 1.f);
				const FLinearColor Mapped(Aces(C.R), Aces(C.G), Aces(C.B), 1.f);
				FLinearColor Final = Mapped * Cover + Bg * (1.f - Cover);
				Final.A = 1.f;
				Out[y * Size + x] = Final.ToFColor(true);
			}
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size, Size, TArrayView64<const FColor>(Out.GetData(), Out.Num()), Png);
		return FFileHelper::SaveArrayToFile(Png, *File);
	}
}

bool ATN_CosmeticPreview::DebugSavePhoto(const FString& File, int32 Size, float Yaw)
{
	UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, Size, Size, RTF_RGBA16f, FLinearColor(0.f, 0.f, 0.f, 1.f));
	if (!RT) { return false; }
	UTextureRenderTarget2D* OldTarget = Capture->TextureTarget;
	const bool bOldEvery = Capture->bCaptureEveryFrame;
	Capture->bCaptureEveryFrame = false;
	Capture->TextureTarget = RT;
	SpinDeg = Yaw;
	ManualSpinHold = 1000.f;
	Turntable->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
	Capture->CaptureScene();
	const bool bSaved = TNBuggyPhotosDetail::SaveCapturePng(RT, File);
	Capture->TextureTarget = OldTarget;
	Capture->bCaptureEveryFrame = bOldEvery;
	return bSaved;
}

bool ATN_CosmeticPreview::DebugSaveBuggyThumbs(const FString& Dir)
{
	TArray<TPair<FString, UTextureRenderTarget2D*>> Thumbs;
	for (const ETNCosmeticCategory Category : { ETNCosmeticCategory::BuggyModel, ETNCosmeticCategory::BuggyPaint })
	{
		TArray<FName> Ids = TNBuggyCosmetics::CatalogIds(Category);
		Ids.Insert(NAME_None, 0);
		for (const FName Id : Ids)
		{
			const FString Name = FString::Printf(TEXT("miniatura_%s_%s"), Category == ETNCosmeticCategory::BuggyModel ? TEXT("modelo") : TEXT("pintura"),
				Id.IsNone() ? TEXT("Serie") : *Id.ToString().Replace(TEXT("BuggyModel_"), TEXT("")).Replace(TEXT("BuggyPaint_"), TEXT("")));
			Thumbs.Emplace(Name, GetThumbnail(Category, Id));
		}
	}
	if (PendingBuggyThumbs.Num() > 0) { return false; }
	for (const TPair<FString, UTextureRenderTarget2D*>& Thumb : Thumbs)
	{
		const bool bOk = TNBuggyPhotosDetail::SaveCapturePng(Thumb.Value, Dir / (Thumb.Key + TEXT(".png")));
		UE_LOG(LogTNBuggyPhotos, Display, TEXT("[BuggyPhotos] %s %s"), bOk ? TEXT("ok") : TEXT("FALLO"), *Thumb.Key);
	}
	return true;
}

bool ATN_CosmeticPreview::DebugIsBuggyPrecaching() const
{
	TArray<UPrimitiveComponent*> Parts;
	BuggyPrimitives(Parts);
	bool bPrecaching = false;
	for (UPrimitiveComponent* Part : Parts)
	{
		bPrecaching |= Part && Part->IsVisible() && Part->CheckPSOPrecachingAndBoostPriority(EPSOPrecachePriority::Highest);
	}
	return bPrecaching;
}

namespace TNBuggyPhotosDetail
{
	struct FShot
	{
		FTN_BuggyLook Look;
		float Yaw = 35.f;
		FString Name;
	};

	TArray<FShot> Plan()
	{
		using namespace TNBuggyCosmetics;
		TArray<FShot> Shots;
		const auto Add = [&Shots](FName Model, FName Paint, float Yaw, const FString& Name)
		{
			FShot S;
			S.Look.ModelId = Model;
			S.Look.PaintId = Paint;
			S.Yaw = Yaw;
			S.Name = Name;
			Shots.Add(S);
		};
		const TArray<FName> ModelIds = { NAME_None, FName(TEXT("BuggyModel_Clasico")), FName(TEXT("BuggyModel_Caiman")), FName(TEXT("BuggyModel_Laud")) };
		const TCHAR* ModelNames[] = { TEXT("serie"), TEXT("clasico"), TEXT("caiman"), TEXT("laud") };
		const FName Signature[] = { FName(TEXT("BuggyPaint_Llamas")), FName(TEXT("BuggyPaint_Coral")), FName(TEXT("BuggyPaint_Alga")),
			FName(TEXT("BuggyPaint_Carreras")) };
		for (int32 m = 0; m < ModelIds.Num(); ++m)
		{
			Add(ModelIds[m], NAME_None, 35.f, FString::Printf(TEXT("modelo_%s_frente"), ModelNames[m]));
			Add(ModelIds[m], NAME_None, 150.f, FString::Printf(TEXT("modelo_%s_detras"), ModelNames[m]));
			Add(ModelIds[m], NAME_None, -90.f, FString::Printf(TEXT("modelo_%s_lado"), ModelNames[m]));
			Add(ModelIds[m], Signature[m], 20.f, FString::Printf(TEXT("modelo_%s_%s"), ModelNames[m], *Signature[m].ToString().Replace(TEXT("BuggyPaint_"), TEXT(""))));
		}
		// Las pinturas, en el de serie (sus zonas de Art/Source) y en el clásico (las de las tortugas).
		for (const FTNBuggyPaintInfo& Paint : Paints())
		{
			const FString PaintName = Paint.Id.IsNone() ? FString(TEXT("Serie")) : Paint.Id.ToString().Replace(TEXT("BuggyPaint_"), TEXT(""));
			Add(NAME_None, Paint.Id, 55.f, FString::Printf(TEXT("pintura_serie_%s"), *PaintName));
			Add(ModelIds[1], Paint.Id, 55.f, FString::Printf(TEXT("pintura_clasico_%s"), *PaintName));
		}
		return Shots;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTNBuggyPhotosCommand(
	TEXT("TN.Buggy.Photos"),
	TEXT("Pruebas: TN.Buggy.Photos [carpeta] [tamaño = 768] [espera = 4]: fotos del buggy en el escaparate de la tienda (modelos y pinturas) y cierra el juego."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Dir = Args.IsValidIndex(0) ? Args[0] : FPaths::ProjectSavedDir() / TEXT("BuggyPhotos");
		const int32 Size = Args.IsValidIndex(1) ? FMath::Clamp(FCString::Atoi(*Args[1]), 128, 2048) : 768;
		const float Wait = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : 4.f;
		IFileManager::Get().MakeDirectory(*Dir, true);
		// Cada 0,25 s una foto; antes, la espera en pasos negativos.
		TSharedRef<int32> Step = MakeShared<int32>(-FMath::CeilToInt(Wait / 0.25f) - 1);
		TSharedRef<TArray<TNBuggyPhotosDetail::FShot>> Shots = MakeShared<TArray<TNBuggyPhotosDetail::FShot>>(TNBuggyPhotosDetail::Plan());
		// Pasos desde que se viste cada foto (-1 = aún sin vestir): una pieza nueva sale gris mientras precarga sus PSO.
		TSharedRef<int32> Settle = MakeShared<int32>(-1);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Dir, Size, Step, Shots, Settle](float) -> bool
		{
			UWorld* World = nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()) { World = Context.World(); break; }
			}
			ATN_CosmeticPreview* Stage = World ? ATN_CosmeticPreview::Get(World) : nullptr;
			if (!Stage) { return true; }
			int32& I = Step.Get();
			if (I < -1)
			{
				++I;
				return true;
			}
			if (I == -1)
			{
#if WITH_EDITOR
				if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
#endif
				Stage->SetLiveCapture(true);
				Stage->SetBuggyMode(true);
				I = 0;
				return true;
			}
			if (I >= Shots->Num())
			{
				// Después, las miniaturas de la tienda (con su tope por si alguna no acaba de pintarse).
				if (!Stage->DebugSaveBuggyThumbs(Dir) && I < Shots->Num() + 400)
				{
					++I;
					return true;
				}
				UE_LOG(LogTNBuggyPhotos, Display, TEXT("[BuggyPhotos] %d fotos en %s"), Shots->Num(), *Dir);
				FPlatformMisc::RequestExit(false, TEXT("TN.Buggy.Photos"));
				return false;
			}
			const TNBuggyPhotosDetail::FShot& Shot = (*Shots)[I];
			int32& Settled = Settle.Get();
			if (Settled < 0)
			{
				Stage->SetBuggyLook(Shot.Look);
#if WITH_EDITOR
				if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
#endif
				Settled = 0;
				return true;
			}
			if (Stage->DebugIsBuggyPrecaching() && ++Settled < 60)
			{
				return true;
			}
			Settled = -1;
			const FString File = Dir / (Shot.Name + TEXT(".png"));
			const bool bOk = Stage->DebugSavePhoto(File, Size, Shot.Yaw);
			UE_LOG(LogTNBuggyPhotos, Display, TEXT("[BuggyPhotos] %s %s"), bOk ? TEXT("ok") : TEXT("FALLO"), *File);
			++I;
			return true;
		}), 0.25f);
	}));

#endif
