#include "Player/TN_DizzyBirdsComponent.h"
#include "Audio/TN_AudioVoices.h"
#include "Core/TN_ProjectMaterials.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"
#include "UObject/Package.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include <atomic>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Sonido del mareo (hilo de audio: C++ puro, sin UObjects ni asignaciones)
// ─────────────────────────────────────────────────────────────────────────────

namespace TNDizzyAudio
{
	struct FDizzySharedParams
	{
		/** 1 = suena el mareo; 0 = se apaga con cola. */
		std::atomic<float> Active{ 0.f };
		std::atomic<float> Volume{ 0.75f };
	};

	constexpr float TwoPi = 6.2831853f;

	/** Corrección polyBLEP de la discontinuidad de un diente de sierra (menos aliasing en las cuerdas). */
	inline float PolyBlep(float T, float Dt)
	{
		if (T < Dt) { const float X = T / Dt; return X + X - X * X - 1.f; }
		if (T > 1.f - Dt) { const float X = (T - 1.f) / Dt; return X * X + X + X + 1.f; }
		return 0.f;
	}

	/**
	 * Cuerdas mareadas (dos violines a una tercera, tres sierras un poco desafinadas por voz, glissando circular y
	 * vibrato, pasadas por un paso bajo de dos polos) y trinos de pájaro (senos que suben o bajan en 60-110 ms, a veces
	 * dobles), con una envolvente general que sigue a Active y una saturación suave al final.
	 */
	class FDizzyEngine
	{
	public:
		void Init(float InRate)
		{
			Rate = FMath::Max(8000.f, InRate);
			Inv = 1.f / Rate;
			LowPassK = 1.f - std::exp(-TwoPi * 2100.f / Rate);
			AttackStep = 1.f / (0.3f * Rate);
			ReleaseStep = 1.f / (0.55f * Rate);
		}

		void Render(float* Out, int32 Frames, int32 Channels, const FDizzySharedParams& P)
		{
			const float Target = FMath::Clamp(P.Active.load(std::memory_order_relaxed), 0.f, 1.f);
			const float VolTarget = FMath::Clamp(P.Volume.load(std::memory_order_relaxed), 0.f, 1.5f);
			for (int32 f = 0; f < Frames; ++f)
			{
				Env += Target > Env ? FMath::Min(AttackStep, Target - Env) : -FMath::Min(ReleaseStep, Env - Target);
				Vol += (VolTarget - Vol) * 0.0005f;
				float Mix = 0.f;
				if (Env > 0.f || Target > 0.f)
				{
					Clock += Inv;
					Mix = (Strings() + Tweets(Target > 0.5f)) * Env * Vol;
					Mix = std::tanh(Mix * 1.3f) / 1.3f;
				}
				for (int32 c = 0; c < Channels; ++c) { Out[f * Channels + c] = Mix; }
			}
		}

	private:
		float Strings()
		{
			// «Uuuh-uuuh»: el tono sube y baja en círculo (0,42 vueltas por segundo) con vibrato de violín.
			const float Glide = std::exp2(0.2f * std::sin(TwoPi * 0.42f * Clock));
			const float Vib = std::exp2(0.012f * std::sin(TwoPi * 5.8f * Clock));
			const float Base[2] = { 392.f, 493.88f };
			float S = 0.f;
			for (int32 v = 0; v < 2; ++v)
			{
				for (int32 k = 0; k < 3; ++k)
				{
					const int32 i = v * 3 + k;
					const float Freq = Base[v] * Glide * Vib * (1.f + 0.0045f * static_cast<float>(k - 1));
					const float Dt = FMath::Min(0.45f, Freq * Inv);
					Phase[i] += Dt;
					if (Phase[i] >= 1.f) { Phase[i] -= 1.f; }
					S += (2.f * Phase[i] - 1.f - PolyBlep(Phase[i], Dt)) * (v == 0 ? 1.f : 0.8f);
				}
			}
			S /= 5.4f;
			LowPass1 += (S - LowPass1) * LowPassK;
			LowPass2 += (LowPass1 - LowPass2) * LowPassK;
			const float Swell = 0.72f + 0.28f * std::sin(TwoPi * 0.42f * Clock + 1.2f);
			return LowPass2 * Swell * 0.34f;
		}

		float Tweets(bool bAllowNew)
		{
			NextTweet -= Inv;
			if (NextTweet <= 0.f && bAllowNew && TweetT < 0.f)
			{
				const float R = Rand();
				const bool bUp = R > 0.35f;
				TweetT = 0.f;
				TweetDur = 0.06f + 0.05f * Rand();
				TweetF0 = bUp ? 2400.f + 800.f * Rand() : 4200.f + 500.f * Rand();
				TweetF1 = bUp ? TweetF0 * 1.45f : TweetF0 * 0.7f;
				TweetAmp = 0.55f + 0.45f * Rand();
				// A veces el pájaro repite enseguida (trino doble); si no, calla un rato.
				NextTweet = Rand() < 0.35f ? TweetDur + 0.035f : 0.16f + 0.42f * Rand();
			}
			if (TweetT < 0.f) { return 0.f; }
			const float X = TweetT / TweetDur;
			if (X >= 1.f)
			{
				TweetT = -1.f;
				return 0.f;
			}
			const float Freq = (TweetF0 + (TweetF1 - TweetF0) * X) * (1.f + 0.03f * std::sin(TwoPi * 38.f * TweetT));
			TweetPhase += Freq * Inv;
			if (TweetPhase >= 1.f) { TweetPhase -= 1.f; }
			const float Shape = FMath::Min(1.f, X * 14.f) * (1.f - X) * (1.f - X);
			TweetT += Inv;
			return (std::sin(TwoPi * TweetPhase) + 0.22f * std::sin(2.f * TwoPi * TweetPhase)) * Shape * TweetAmp * 0.3f;
		}

		float Rand()
		{
			Seed ^= Seed << 13;
			Seed ^= Seed >> 17;
			Seed ^= Seed << 5;
			return static_cast<float>(Seed & 0xFFFFFFu) / 16777216.f;
		}

		float Rate = 48000.f;
		float Inv = 1.f / 48000.f;
		float LowPassK = 0.25f;
		float AttackStep = 0.f;
		float ReleaseStep = 0.f;
		float Env = 0.f;
		float Vol = 0.75f;
		float Clock = 0.f;
		float Phase[6] = { 0.f, 0.17f, 0.39f, 0.08f, 0.52f, 0.73f };
		float LowPass1 = 0.f;
		float LowPass2 = 0.f;
		float NextTweet = 0.15f;
		float TweetT = -1.f;
		float TweetDur = 0.08f;
		float TweetF0 = 3000.f;
		float TweetF1 = 4000.f;
		float TweetPhase = 0.f;
		float TweetAmp = 1.f;
		uint32 Seed = 0x2545F491u;
	};

	class FTNDizzySynthGenerator final : public ISoundGenerator
	{
	public:
		FTNDizzySynthGenerator(float InSampleRate, int32 InNumChannels, const TSharedPtr<FDizzySharedParams, ESPMode::ThreadSafe>& InParams)
			: Params(InParams)
			, OutChannels(FMath::Max(1, InNumChannels))
		{
			DspEngine.Init(InSampleRate);
		}

		virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
		{
			const int32 Frames = NumSamples / OutChannels;
			DspEngine.Render(OutAudio, Frames, OutChannels, *Params);
			for (int32 i = Frames * OutChannels; i < NumSamples; ++i) { OutAudio[i] = 0.f; }
			return NumSamples;
		}

	private:
		TSharedPtr<FDizzySharedParams, ESPMode::ThreadSafe> Params;
		int32 OutChannels = 1;
		FDizzyEngine DspEngine;
	};
}

UTN_DizzySynthComponent::UTN_DizzySynthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = false;
	NumChannels = 1;
	bAllowSpatialization = true;
	bOverrideAttenuation = true;
	FSoundAttenuationSettings& Att = AttenuationOverrides;
	Att.bAttenuate = true;
	Att.bSpatialize = true;
	Att.AttenuationShape = EAttenuationShape::Sphere;
	Att.AttenuationShapeExtents = FVector(300.f, 0.f, 0.f);
	Att.FalloffDistance = 2200.f;
	Att.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Att.dBAttenuationAtMax = -50.f;
	Att.NonSpatializedRadiusStart = 180.f;
	Att.NonSpatializedRadiusEnd = 60.f;
	SharedParams = MakeShared<TNDizzyAudio::FDizzySharedParams, ESPMode::ThreadSafe>();
}

void UTN_DizzySynthComponent::SetDizzySound(bool bInActive)
{
	if (SharedParams.IsValid()) { SharedParams->Active.store(bInActive ? 1.f : 0.f, std::memory_order_relaxed); }
}

void UTN_DizzySynthComponent::SetDizzyVolume(float InVolume)
{
	if (SharedParams.IsValid()) { SharedParams->Volume.store(FMath::Clamp(InVolume, 0.f, 1.5f), std::memory_order_relaxed); }
}

bool UTN_DizzySynthComponent::Init(int32& /*SampleRate*/)
{
	NumChannels = 1;
	return true;
}

ISoundGeneratorPtr UTN_DizzySynthComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	return MakeShared<TNDizzyAudio::FTNDizzySynthGenerator, ESPMode::ThreadSafe>(InParams.SampleRate, InParams.NumChannels, SharedParams);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pajaritos y estrellitas
// ─────────────────────────────────────────────────────────────────────────────

namespace TNDizzyBirdsDetail
{
	/** Color sRGB 0xRRGGBB para M_CosmeticVertexColor (MakeStaticMesh decodifica una vez más: así llega lineal). */
	FLinearColor Pal(uint32 Hex)
	{
		return FLinearColor(TNProcRuntimeMesh::SRGBToLinear(((Hex >> 16) & 255) / 255.f), TNProcRuntimeMesh::SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			TNProcRuntimeMesh::SRGBToLinear((Hex & 255) / 255.f), 1.f);
	}

	UMaterialInterface* VertexColorMaterial()
	{
		return TNMaterials::VertexColor();
	}

	/** Rombo de caras planas (cuerpo y cabeza de los pájaros): Center, semiejes Rx (largo), Ry (ancho) y Rz (alto). */
	void AddDiamond(TNProcMesh::FTNProcMeshBuffers& B, const FVector& Center, double Rx, double Ry, double Rz, const FLinearColor& Top, const FLinearColor& Bottom)
	{
		const FVector Px = Center + FVector(Rx, 0.0, 0.0), Nx = Center - FVector(Rx, 0.0, 0.0);
		const FVector Py = Center + FVector(0.0, Ry, 0.0), Ny = Center - FVector(0.0, Ry, 0.0);
		const FVector Pz = Center + FVector(0.0, 0.0, Rz), Nz = Center - FVector(0.0, 0.0, Rz * 0.7);
		const FVector Ring[4] = { Px, Py, Nx, Ny };
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector& A = Ring[k];
			const FVector& C = Ring[(k + 1) % 4];
			const FVector Out = (A + C) * 0.5 - Center;
			B.AddTri(Pz, A, C, Out + FVector(0.0, 0.0, Rz), Top);
			B.AddTri(Nz, A, C, Out - FVector(0.0, 0.0, Rz), Bottom);
		}
	}

	/** Triángulo con las dos caras (alas, cola, estrellas finas). */
	void AddTwoSided(TNProcMesh::FTNProcMeshBuffers& B, const FVector& A, const FVector& C, const FVector& D, const FVector& Normal, const FLinearColor& Color)
	{
		B.AddTri(A, C, D, Normal, Color);
		B.AddTri(A, C, D, -Normal, Color * 0.85f);
	}

	/** Cuerpo del pájaro (sin alas) mirando a +X, unos 14 cm de pico a cola. */
	UStaticMesh* BuildBirdBody(uint32 BodyHex, uint32 BellyHex)
	{
		TNProcMesh::FTNProcMeshBuffers B;
		const FLinearColor Body = Pal(BodyHex);
		const FLinearColor Belly = Pal(BellyHex);
		AddDiamond(B, FVector(0.0, 0.0, 0.0), 6.0, 3.4, 3.6, Body, Belly);
		AddDiamond(B, FVector(4.6, 0.0, 3.6), 2.8, 2.5, 2.6, Body, Body);
		// Pico naranja.
		const FLinearColor Beak = Pal(0xFF7A1A);
		const FVector Tip(9.6, 0.0, 3.2), BaseTop(6.8, 0.0, 4.2), BaseL(6.8, 0.9, 3.2), BaseR(6.8, -0.9, 3.2), BaseBot(6.8, 0.0, 2.4);
		B.AddTri(Tip, BaseTop, BaseL, FVector(0.3, 0.6, 0.6), Beak);
		B.AddTri(Tip, BaseR, BaseTop, FVector(0.3, -0.6, 0.6), Beak);
		B.AddTri(Tip, BaseL, BaseBot, FVector(0.3, 0.6, -0.6), Beak);
		B.AddTri(Tip, BaseBot, BaseR, FVector(0.3, -0.6, -0.6), Beak);
		// Ojos (puntitos oscuros a los lados de la cabeza).
		const FLinearColor Eye = Pal(0x13233B);
		for (const double Side : { 1.0, -1.0 })
		{
			const FVector C(5.4, 2.2 * Side, 4.4);
			B.AddTri(C + FVector(0.6, 0.0, 0.0), C + FVector(0.0, 0.0, 0.7), C + FVector(-0.6, 0.0, 0.0), FVector(0.0, Side, 0.0), Eye);
			B.AddTri(C + FVector(0.6, 0.0, 0.0), C + FVector(-0.6, 0.0, 0.0), C + FVector(0.0, 0.0, -0.7), FVector(0.0, Side, 0.0), Eye);
		}
		// Cola en abanico.
		AddTwoSided(B, FVector(-5.2, 0.0, 0.6), FVector(-10.5, 2.6, 2.4), FVector(-10.5, -2.6, 2.4), FVector(0.0, 0.0, 1.0), Body * 0.9f);
		return TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), B, VertexColorMaterial());
	}

	/** Ala izquierda (hacia +Y) con la raíz en el origen; la derecha es la misma con escala Y = -1. */
	UStaticMesh* BuildWing(uint32 WingHex)
	{
		TNProcMesh::FTNProcMeshBuffers B;
		const FLinearColor Wing = Pal(WingHex);
		AddTwoSided(B, FVector(2.2, 0.0, 0.0), FVector(-2.8, 0.0, 0.0), FVector(-1.2, 9.5, 1.2), FVector(0.0, 0.0, 1.0), Wing);
		AddTwoSided(B, FVector(2.2, 0.0, 0.0), FVector(-1.2, 9.5, 1.2), FVector(1.6, 6.5, 0.8), FVector(0.0, 0.0, 1.0), Wing * 1.1f);
		return TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), B, VertexColorMaterial());
	}

	/** Estrellita de cinco puntas en el plano XZ (de cara a ±Y), con grosor. */
	UStaticMesh* BuildStar()
	{
		TNProcMesh::FTNProcMeshBuffers B;
		const FLinearColor Gold = Pal(0xFFE14D);
		const FLinearColor Edge = Pal(0xF5A623);
		TArray<FVector> Rim;
		for (int32 k = 0; k < 10; ++k)
		{
			const double A = UE_DOUBLE_PI * 0.5 + UE_DOUBLE_PI * k / 5.0;
			const double R = (k % 2) == 0 ? 5.5 : 2.3;
			Rim.Add(FVector(FMath::Cos(A) * R, 0.0, FMath::Sin(A) * R));
		}
		const FVector Front(0.0, 0.9, 0.0), Back(0.0, -0.9, 0.0);
		for (int32 k = 0; k < 10; ++k)
		{
			const FVector& A = Rim[k];
			const FVector& C = Rim[(k + 1) % 10];
			B.AddTri(Front, A, C, FVector(0.0, 1.0, 0.0), Gold);
			B.AddTri(Back, A, C, FVector(0.0, -1.0, 0.0), Edge);
		}
		return TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), B, VertexColorMaterial());
	}

	/** Colores de los tres pájaros: amarillo, azul y rosa, con su tripa y sus alas. */
	struct FBirdColors { uint32 Body; uint32 Belly; uint32 Wing; };
	const FBirdColors BirdColors[3] = { { 0xFFD23F, 0xFFF1A8, 0xFFB020 }, { 0x5EC8F2, 0xD6F3FF, 0x2E8FD6 }, { 0xFF8FB8, 0xFFE0EC, 0xE8578F } };

	/** Mallas compartidas por todas las tortugas (se rehacen si el recolector se las lleva). */
	UStaticMesh* SharedMesh(int32 Index)
	{
		static TWeakObjectPtr<UStaticMesh> Cache[7];
		if (!Cache[Index].IsValid())
		{
			UStaticMesh* Built = nullptr;
			if (Index < 3) { Built = BuildBirdBody(BirdColors[Index].Body, BirdColors[Index].Belly); }
			else if (Index < 6) { Built = BuildWing(BirdColors[Index - 3].Wing); }
			else { Built = BuildStar(); }
			Cache[Index] = Built;
		}
		return Cache[Index].Get();
	}

	UStaticMeshComponent* MakePart(USceneComponent* Parent, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Parent->GetOwner(), NAME_None, RF_Transient);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetCanEverAffectNavigation(false);
		Part->SetupAttachment(Parent);
		Part->RegisterComponent();
		Part->SetVisibility(false);
		return Part;
	}
}

UTN_DizzyBirdsComponent::UTN_DizzyBirdsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Sigue a la cabeza en el mundo: sin heredar la escala 2,5 ni el giro de la malla.
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
}

void UTN_DizzyBirdsComponent::OnUnregister()
{
	for (UStaticMeshComponent* Part : Birds) { if (Part) { Part->DestroyComponent(); } }
	for (UStaticMeshComponent* Part : Stars) { if (Part) { Part->DestroyComponent(); } }
	Birds.Reset();
	Stars.Reset();
	if (Synth)
	{
		Synth->DestroyComponent();
		Synth = nullptr;
	}
	Super::OnUnregister();
}

void UTN_DizzyBirdsComponent::SetDizzy(bool bInDizzy)
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer) { return; }
	const bool bWasDizzy = bDizzy;
	bDizzy = bInDizzy;
	if (bDizzy)
	{
		EnsureVisuals();
		EnsureSound();
		SetComponentTickEnabled(true);
	}
	else if (bWasDizzy)
	{
		// Se apaga con cola y, cuando ha callado, el sintetizador suelta su voz (TickComponent).
		SoundTailLeft = SoundTailSeconds;
		SetComponentTickEnabled(true);
	}
	if (Synth) { Synth->SetDizzySound(bDizzy); }
}

void UTN_DizzyBirdsComponent::EnsureVisuals()
{
	using namespace TNDizzyBirdsDetail;
	if (Birds.Num() > 0) { return; }
	// Por pájaro: cuerpo y dos alas (hijas del cuerpo, para batirlas).
	for (int32 i = 0; i < 3; ++i)
	{
		UStaticMeshComponent* BodyPart = MakePart(this, SharedMesh(i));
		Birds.Add(BodyPart);
		for (const double Side : { 1.0, -1.0 })
		{
			UStaticMeshComponent* WingPart = MakePart(BodyPart, SharedMesh(3 + i));
			WingPart->SetRelativeLocation(FVector(0.4, 1.8 * Side, 2.2));
			WingPart->SetRelativeScale3D(FVector(1.0, Side, 1.0));
			Birds.Add(WingPart);
		}
	}
	for (int32 j = 0; j < 3; ++j)
	{
		Stars.Add(MakePart(this, SharedMesh(6)));
	}
}

void UTN_DizzyBirdsComponent::EnsureSound()
{
	if (!FApp::CanEverRenderAudio()) { return; }
	if (!Synth)
	{
		UTN_DizzySynthComponent* NewSynth = NewObject<UTN_DizzySynthComponent>(GetOwner(), NAME_None, RF_Transient);
		NewSynth->SetupAttachment(this);
		NewSynth->RegisterComponent();
		NewSynth->SetDizzyVolume(SoundVolume);
		Synth = NewSynth;
	}
	SoundTailLeft = 0.f;
	if (!Synth->IsActive())
	{
		// Antes se arrancaba una vez y no se paraba nunca: cada tortuga mareada alguna vez se quedaba con una voz del
		// mezclador en silencio para siempre (#737). Ahora suena solo mientras dura el mareo y su cola. La propia tortuga
		// tiene voz reservada; las demás compiten como el resto del mundo.
		TNAudioVoices::Apply(*Synth, TNAudioVoices::RankForOwner(GetOwner()));
		Synth->Start();
	}
}

bool UTN_DizzyBirdsComponent::IsSoundPlaying() const
{
	return Synth && Synth->IsActive();
}

FVector UTN_DizzyBirdsComponent::HeadTop() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh) { return GetOwner() ? GetOwner()->GetActorLocation() + FVector(0.0, 0.0, 90.0) : FVector::ZeroVector; }
	// Centro de la cabeza: entre el hueso de la cabeza y su punta (también con el ragdoll, que mueve los huesos).
	const int32 HeadIndex = Mesh->GetBoneIndex(HeadBone);
	const int32 TopIndex = Mesh->GetBoneIndex(TEXT("HeadTop_End"));
	if (HeadIndex != INDEX_NONE && TopIndex != INDEX_NONE)
	{
		return (Mesh->GetBoneLocation(HeadBone) + Mesh->GetBoneLocation(TEXT("HeadTop_End"))) * 0.5;
	}
	if (HeadIndex != INDEX_NONE) { return Mesh->GetBoneLocation(HeadBone) + FVector(0.0, 0.0, 20.0); }
	return Mesh->Bounds.Origin + FVector(0.0, 0.0, Mesh->Bounds.BoxExtent.Z * 0.7);
}

void UTN_DizzyBirdsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Time += DeltaTime;
	Presence = FMath::FInterpConstantTo(Presence, bDizzy ? 1.f : 0.f, DeltaTime, bDizzy ? 4.f : 3.f);
	const bool bShow = Presence > 0.001f;
	for (UStaticMeshComponent* Part : Birds) { if (Part && Part->IsVisible() != bShow) { Part->SetVisibility(bShow); } }
	for (UStaticMeshComponent* Part : Stars) { if (Part && Part->IsVisible() != bShow) { Part->SetVisibility(bShow); } }
	// Fin del mareo: tras la cola (el generador se apaga en ~0,55 s), el sintetizador se para y suelta su voz.
	if (!bDizzy && IsSoundPlaying())
	{
		SoundTailLeft -= DeltaTime;
		if (SoundTailLeft <= 0.f)
		{
			Synth->Stop();
		}
	}
	if (!bShow)
	{
		if (!bDizzy && !IsSoundPlaying()) { SetComponentTickEnabled(false); }
		return;
	}

	SetWorldLocation(HeadTop() + FVector(0.0, 0.0, OrbitHeight));
	// Entra con un pequeño rebote y sale encogiéndose.
	const float Ease = Presence * Presence * (3.f - 2.f * Presence);
	const float Pop = Ease * (1.f + 0.18f * FMath::Sin(Presence * PI));
	const float Spin = Time * TurnsPerSecond * 2.f * PI;

	// Pájaros: cuerpo cada tres componentes (cuerpo, ala izquierda, ala derecha).
	for (int32 i = 0; i * 3 + 2 < Birds.Num(); ++i)
	{
		const float A = Spin + i * 2.f * PI / 3.f;
		const FVector Pos(FMath::Cos(A) * OrbitRadius, FMath::Sin(A) * OrbitRadius, 4.f * FMath::Sin(2.f * A + i));
		// Mira hacia donde vuela (tangente del círculo), inclinado hacia dentro y cabeceando un poco.
		const FRotator Rot(6.f * FMath::Sin(Time * 9.f + i), FMath::RadiansToDegrees(A) + 90.f, -18.f);
		Birds[i * 3]->SetRelativeLocationAndRotation(Pos, Rot);
		Birds[i * 3]->SetRelativeScale3D(FVector(Pop));
		const float Flap = 38.f * FMath::Sin(Time * 24.f + i * 1.7f);
		Birds[i * 3 + 1]->SetRelativeRotation(FRotator(0.f, 0.f, -Flap));
		Birds[i * 3 + 2]->SetRelativeRotation(FRotator(0.f, 0.f, Flap));
	}
	// Estrellitas: por dentro del corro, al revés y girando sobre sí mismas.
	for (int32 j = 0; j < Stars.Num(); ++j)
	{
		const float A = -Spin * 1.6f + j * 2.f * PI / Stars.Num();
		const FVector Pos(FMath::Cos(A) * OrbitRadius * 0.6f, FMath::Sin(A) * OrbitRadius * 0.6f, 9.f + 3.f * FMath::Sin(Time * 5.f + j));
		Stars[j]->SetRelativeLocationAndRotation(Pos, FRotator(0.f, Time * 300.f + j * 40.f, 0.f));
		Stars[j]->SetRelativeScale3D(FVector(Pop * 0.85f));
	}
}
