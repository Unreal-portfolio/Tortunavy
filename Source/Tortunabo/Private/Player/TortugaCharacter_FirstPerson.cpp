// Primera persona de la tortuga (Docs/Modo_VR.md, «Primera persona»), con gafas y sin ellas: los ojos van en la cabeza
// (también tumbada en el ragdoll) o en el centro del caparazón, y del cuerpo propio se ve todo menos la cabeza: el cuerpo,
// las aletas, la lengua y las gotas de sudor al mirar abajo. Dentro del caparazón la vista es mucho más oscura. En VR los
// brazos del cuerpo van a los mandos (IK en UTN_TurtleAnimInstance, manos de ATN_VRRig).

#include "Player/TortugaCharacter.h"
#include "Core/TN_Log.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"

static TAutoConsoleVariable<int32> CVarTNCamera(TEXT("TN.Camera"), -1,
	TEXT("Cámara sin gafas: -1 la del ajuste «Cámara» (de serie), 0 tercera persona, 1 primera persona."), ECVF_Default);
static TAutoConsoleVariable<float> CVarTNShellLight(TEXT("TN.FirstPerson.ShellLight"), 0.2f,
	TEXT("Luz que queda dentro del caparazón en primera persona y en VR (0 negro, 1 como fuera)."), ECVF_Default);

namespace TNFirstPersonDetail
{
	const FName HeadBone(TEXT("Head"));
	/** Centro del caparazón: el pecho de la tortuga metida dentro. */
	const FName ShellCenterBone(TEXT("Spine1"));
	/** Ojos respecto del hueso de la cabeza (cm): delante y encima, dentro de la cabeza (que no se pinta para uno mismo). */
	constexpr float EyeForward = 6.f;
	constexpr float EyeUp = 6.f;
	/** Suavizado de los ojos (1/s): de pie quita el vaivén de la cabeza al andar; tumbada va casi pegada a ella. */
	constexpr float EyeFollowStanding = 14.f;
	constexpr float EyeFollowRagdoll = 30.f;
	/** Cuánto tarda en oscurecerse al meterse en el caparazón y en aclararse al salir (1/s). */
	constexpr float ShellDarkenSpeed = 6.f;
	/** Tono de dentro del caparazón: la concha por dentro, cálida. */
	const FVector ShellTint(1.0, 0.9, 0.74);
}

bool ATortugaCharacter::WantsFirstPersonView() const
{
	const int32 Forced = CVarTNCamera.GetValueOnGameThread();
	if (Forced >= 0)
	{
		return Forced == 1;
	}
	const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);
	return Settings && Settings->GetSettings().CameraView == 1;
}

void ATortugaCharacter::ToggleCameraView()
{
	const bool bFirst = !WantsFirstPersonView();
	if (CVarTNCamera.GetValueOnGameThread() >= 0)
	{
		// Forzada por consola: se cambia ahí (el ajuste guardado no se toca).
		CVarTNCamera->Set(bFirst ? 1 : 0, ECVF_SetByConsole);
		return;
	}
	if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this))
	{
		Settings->EditSettings([bFirst](FTNGameSettings& Data) { Data.CameraView = bFirst ? 1 : 0; });
	}
}

void ATortugaCharacter::SetFirstPersonView(bool bOn)
{
	if (!IsLocallyControlled())
	{
		bOn = false;
	}
	if (bOn == bFirstPersonActive)
	{
		return;
	}
	bFirstPersonActive = bOn;
	if (bOn)
	{
		if (!FirstPersonCamera)
		{
			FirstPersonCamera = NewObject<UCameraComponent>(this, TEXT("FirstPersonCamera"), RF_Transient);
			// Colgada de la cápsula (los ojos se ponen cada fotograma respecto de ella): lo que mueva la cápsula después del
			// Tick (bases que se mueven, correcciones de red) la lleva consigo, sin ir un fotograma por detrás.
			FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
			FirstPersonCamera->bUsePawnControlRotation = true;
			FirstPersonCamera->RegisterComponent();
		}
		bFirstPersonEyeValid = false;
		FirstPersonCamera->SetWorldLocation(ComputeFirstPersonEye(false));
		FirstPersonCamera->SetFieldOfView(FollowCamera ? FollowCamera->FieldOfView : 90.f);
		// La cámara activa es la que ve el juego (AActor::CalcCamera coge la primera activa).
		if (FollowCamera)
		{
			FollowCamera->SetActive(false);
		}
		FirstPersonCamera->SetActive(true);
	}
	else
	{
		if (FirstPersonCamera)
		{
			ApplyShellDarkness(FirstPersonCamera, 0.f);
			FirstPersonCamera->SetActive(false);
		}
		// Con VR la cámara es la de las gafas: la de siempre solo vuelve sin ella.
		if (FollowCamera && !bVRViewActive)
		{
			FollowCamera->SetActive(true);
		}
	}

	// La tortuga mira hacia donde mira la cámara: aquí al momento y en el servidor (y de ahí a los demás).
	if (bFirstPersonPlayer != bOn)
	{
		bFirstPersonPlayer = bOn;
		ApplyVRRotationMode();
		if (!HasAuthority())
		{
			ServerSetFirstPersonPlayer(bOn);
		}
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Cámara] %s: primera persona %s."), *GetName(), bOn ? TEXT("encendida") : TEXT("apagada"));
}

bool ATortugaCharacter::IsLocalViewTarget() const
{
	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (!PC || !PC->IsLocalController() || PC->GetViewTarget() != this)
	{
		return false;
	}
	// Con un cambio de vista con fundido en marcha (al probador, a una cámara de escena), la vista ya se está yendo.
	const APlayerCameraManager* CameraManager = PC->PlayerCameraManager;
	const AActor* Pending = CameraManager ? CameraManager->PendingViewTarget.Target.Get() : nullptr;
	return !Pending || Pending == this;
}

bool ATortugaCharacter::WasCameraToggleJustPressed(const APlayerController* PC) const
{
	if (!PC)
	{
		return false;
	}
	// La fila «Cambiar de cámara» de los controles (T y clic del stick derecho de serie; nunca la de hablar).
	const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);
	const FTNGameSettings Defaults;
	for (int32 Device = 0; Device < 2; ++Device)
	{
		const FKey Key = Settings ? Settings->GetCameraToggleKey(Device == 1) : FKey(Device == 1 ? Defaults.CameraPadKey : Defaults.CameraKey);
		if (Key.IsValid() && PC->WasInputKeyJustPressed(Key))
		{
			return true;
		}
	}
	return false;
}

void ATortugaCharacter::ServerSetFirstPersonPlayer_Implementation(bool bOn)
{
	bFirstPersonPlayer = bOn;
	ApplyVRRotationMode();
}

void ATortugaCharacter::OnRep_FirstPersonPlayer()
{
	ApplyVRRotationMode();
}

FVector ATortugaCharacter::ComputeFirstPersonEye(bool bHeadsetStable) const
{
	using namespace TNFirstPersonDetail;
	const FVector Stable = GetActorLocation() + VREyeOffset;
	const USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
	{
		return Stable;
	}
	if (IsInShell())
	{
		// Dentro del caparazón: en su centro (el pecho), vaya como vaya la caja.
		return Body->GetBoneIndex(ShellCenterBone) != INDEX_NONE ? Body->GetBoneLocation(ShellCenterBone) : Body->Bounds.Origin;
	}
	const bool bRagdoll = bIsKnockedDown || bIsDead || Body->IsSimulatingPhysics();
	// Con gafas y de pie, a la altura fija de la cápsula: la cabeza real ya la pone el seguimiento de las gafas (que la
	// vista se meneara con el paso marearía).
	if (bHeadsetStable && !bRagdoll)
	{
		return Stable;
	}
	if (Body->GetBoneIndex(HeadBone) == INDEX_NONE)
	{
		return Stable;
	}
	const FVector Head = Body->GetBoneLocation(HeadBone);
	if (bRagdoll)
	{
		// Tumbada: pegada a la cabeza, esté donde esté el cuerpo.
		return Head + FVector(0.0, 0.0, EyeUp);
	}
	return Head + GetActorForwardVector() * EyeForward + FVector(0.0, 0.0, EyeUp);
}

void ATortugaCharacter::ApplyFirstPersonBody(EFirstPersonBody NewBody)
{
	using namespace TNFirstPersonDetail;
	if (NewBody == FirstPersonBody)
	{
		return;
	}
	USkeletalMeshComponent* Body = GetMesh();
	if (Body)
	{
		// Ocultar un hueso lo oculta en esta máquina para todas las vistas: con la pantalla dividida la otra vista vería la
		// tortuga sin cabeza, así que entonces no se oculta (por dentro de la cabeza no se ve nada: las caras miran afuera).
		const UGameInstance* GameInstance = GetGameInstance();
		const bool bSplitScreen = GameInstance && GameInstance->GetNumLocalPlayers() > 1;
		if (NewBody == EFirstPersonBody::Headless && !bSplitScreen)
		{
			Body->HideBoneByName(HeadBone, PBO_None);
		}
		else
		{
			Body->UnHideBoneByName(HeadBone);
		}
		// Dentro del caparazón no se pinta nada del cuerpo propio (se ve el mundo desde dentro); la sombra, sí.
		const bool bNoSee = NewBody == EFirstPersonBody::Hidden;
		Body->SetOwnerNoSee(bNoSee);
		Body->bCastHiddenShadow = bNoSee;
		Body->MarkRenderStateDirty();
	}
	// El casco va en la cabeza: en primera persona taparía la vista.
	if (HelmetMeshComp)
	{
		HelmetMeshComp->SetOwnerNoSee(NewBody != EFirstPersonBody::Full);
	}
	FirstPersonBody = NewBody;
}

void ATortugaCharacter::ApplyShellDarkness(UCameraComponent* Camera, float Alpha) const
{
	using namespace TNFirstPersonDetail;
	if (!Camera)
	{
		return;
	}
	FPostProcessSettings& Post = Camera->PostProcessSettings;
	const bool bOn = Alpha > KINDA_SMALL_NUMBER;
	const float Light = FMath::Lerp(1.f, FMath::Clamp(CVarTNShellLight.GetValueOnGameThread(), 0.f, 1.f), Alpha);
	Post.bOverride_ColorGain = bOn;
	Post.ColorGain = FVector4(FMath::Lerp(1.0, ShellTint.X, static_cast<double>(Alpha)) * Light,
		FMath::Lerp(1.0, ShellTint.Y, static_cast<double>(Alpha)) * Light,
		FMath::Lerp(1.0, ShellTint.Z, static_cast<double>(Alpha)) * Light, 1.0);
	// Los bordes, aún más oscuros: el borde de la concha alrededor de la vista.
	Post.bOverride_VignetteIntensity = bOn;
	Post.VignetteIntensity = FMath::Lerp(0.f, 1.f, Alpha);
	Camera->PostProcessBlendWeight = 1.f;
}

void ATortugaCharacter::TickFirstPersonView(float DeltaTime)
{
	using namespace TNFirstPersonDetail;
	if (!IsLocallyControlled())
	{
		// Ya no es la tortuga propia (se ha dejado de controlar, p. ej. al pasar a fantasma): vuelve a verse entera.
		if (bFirstPersonActive)
		{
			bFirstPersonActive = false;
			if (FirstPersonCamera)
			{
				ApplyShellDarkness(FirstPersonCamera, 0.f);
				FirstPersonCamera->SetActive(false);
			}
			if (FollowCamera)
			{
				FollowCamera->SetActive(true);
			}
		}
		ApplyFirstPersonBody(EFirstPersonBody::Full);
		return;
	}

	// Tecla de cambio («Cambiar de cámara» en los controles: T y el clic del stick derecho de serie), sin gafas (con ellas la
	// vista siempre es en primera persona) y sin un menú o una rueda a la vista.
	if (const APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (!bVRViewActive && !PC->ShouldShowMouseCursor() && WasCameraToggleJustPressed(PC))
		{
			ToggleCameraView();
		}
	}
	SetFirstPersonView(!bVRViewActive && WantsFirstPersonView());

	const bool bFirstPerson = bVRViewActive || bFirstPersonActive;
	if (!bFirstPerson)
	{
		ApplyFirstPersonBody(EFirstPersonBody::Full);
		ShellDarknessAlpha = 0.f;
		bFirstPersonEyeValid = false;
		return;
	}

	// Los ojos: suavizados respecto de la cápsula (quitan el vaivén del paso sin quedarse atrás al correr).
	const bool bInShell = IsInShell();
	const bool bHeadsetStable = bVRViewActive && bVRHeadsetView;
	const FVector Eye = ComputeFirstPersonEye(bHeadsetStable);
	const FVector WantedOffset = Eye - GetActorLocation();
	const USkeletalMeshComponent* Body = GetMesh();
	const bool bRagdoll = bIsKnockedDown || bIsDead || (Body && Body->IsSimulatingPhysics());
	if (!bFirstPersonEyeValid || DeltaTime <= 0.f)
	{
		FirstPersonEyeOffset = WantedOffset;
		bFirstPersonEyeValid = true;
	}
	else
	{
		FirstPersonEyeOffset = FMath::VInterpTo(FirstPersonEyeOffset, WantedOffset, DeltaTime,
			bRagdoll || bInShell ? EyeFollowRagdoll : EyeFollowStanding);
	}
	// Relativos a la cápsula (de la que cuelgan el origen VR y la cámara): si algo la mueve después de este Tick, los ojos
	// van con ella en el mismo fotograma.
	const FVector EyeWorld = GetActorLocation() + FirstPersonEyeOffset;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const FVector EyeRelative = Capsule ? Capsule->GetComponentTransform().InverseTransformPosition(EyeWorld) : FirstPersonEyeOffset;

	UCameraComponent* ActiveCamera = nullptr;
	if (bVRViewActive)
	{
		if (VROrigin)
		{
			VROrigin->SetRelativeLocation(EyeRelative);
		}
		ActiveCamera = VRCamera;
	}
	else if (FirstPersonCamera)
	{
		FirstPersonCamera->SetRelativeLocation(EyeRelative);
		// El campo de visión del ajuste y el del esprint, como la cámara de siempre (que sigue calculándolo).
		if (FollowCamera)
		{
			FirstPersonCamera->SetFieldOfView(FMath::Max(FollowCamera->FieldOfView, 80.f));
		}
		ActiveCamera = FirstPersonCamera;
	}

	// Del cuerpo propio: todo menos la cabeza (en VR, con los brazos siguiendo a los mandos); dentro del caparazón, nada.
	// Solo si se ve desde esta tortuga: ocultar el hueso de la cabeza vale para todas las cámaras, y desde otra (el probador,
	// la almeja, el gusano) se vería sin cabeza y con el casco flotando. Al volver a otra vista se pinta entera.
	if (!IsLocalViewTarget())
	{
		ApplyFirstPersonBody(EFirstPersonBody::Full);
	}
	else
	{
		ApplyFirstPersonBody(bInShell ? EFirstPersonBody::Hidden : EFirstPersonBody::Headless);
	}

	// Dentro del caparazón, mucho más oscuro.
	ShellDarknessAlpha = FMath::FInterpConstantTo(ShellDarknessAlpha, bInShell ? 1.f : 0.f, DeltaTime, ShellDarkenSpeed);
	ApplyShellDarkness(ActiveCamera, ShellDarknessAlpha);
}
