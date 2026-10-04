// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class Tortunabo : ModuleRules
{
	public Tortunabo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			// Lista de idiomas editable desde Config/DefaultGame.ini (UTN_LanguageSettings, Docs/Localizacion.md).
			"DeveloperSettings",
			"InputCore",
			// Material físico del caparazón con física propia (UPhysicalMaterial, FPhysicsInterface::UpdateMaterial).
			"PhysicsCore",
			"EnhancedInput",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"UMG",
			// Pantalla de carga del huevo durante los LoadMap bloqueantes fuera del editor.
			"MoviePlayer",
			"Slate",
			"SlateCore",
			"AudioCapture",
			"AudioCaptureCore",
			"AudioMixer",
			"SignalProcessing",
			"Niagara",
			"NiagaraCore",
			// Mapa procedural por módulos (World/ProcMap): terreno en runtime y grafos PCG por bioma.
			"ProceduralMeshComponent",
			"PCG",
			// Vegetación procedural: mallas estáticas construidas en ejecución.
			"MeshDescription",
			"StaticMeshDescription"
		});

		// Json: ATN_MapVariantLoader lee manifest.json e index.json de Scripts/terrain_volumes/Variants/.
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
		// Monkey y estrés (Source/Tortunabo/Private/Testing): tiempos de hilo de juego, de render y de GPU (GGameThreadTime, RHIGetGPUFrameCycles).
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("AssetRegistry");
		}
		// FUniqueNetIdWrapper::ToString (ajustes de voz por compañero en el menú de pausa).
		// ApplicationCore: portapapeles (FPlatformApplicationMisc) para copiar y pegar el código de sala (Docs/Salas.md).
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreOnline", "ApplicationCore" });

		// Modo VR (Docs/Modo_VR.md): gafas y mandos (HeadMountedDisplay: UMotionControllerComponent; XRBase: funciones de
		// las gafas y su pantalla de carga) y la textura del huevo para esa pantalla (RenderCore: BeginCleanup).
		PrivateDependencyModuleNames.AddRange(new string[] { "HeadMountedDisplay", "XRBase", "RenderCore" });

		// Rally Tortuga (Docs/Rally_MVP.md): buggy biplaza sobre Chaos Vehicles (ATN_Buggy y sus ruedas).
		PrivateDependencyModuleNames.AddRange(new string[] { "ChaosVehicles", "ChaosVehiclesCore" });

		// Steam solo existe en escritorio: en Android (Meta Quest) el juego usa el subsistema en línea NULL.
		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac)
		{
			DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
		}

		// Mando y Steam Deck (#347, #354): tipo de mando de Steam Input y teclado en pantalla de ISteamUtils. Solo en Win64
		// (también la Steam Deck, que corre la build de Windows con Proton): steam_api64.dll va con carga diferida y el juego
		// solo la llama con el subsistema de Steam en marcha (TNSteamGamepadInput::IsSteamActive).
		bool bWithSteamworks = Target.Platform == UnrealTargetPlatform.Win64;
		if (bWithSteamworks)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks");
		}
		PrivateDefinitions.Add("TN_WITH_STEAMWORKS=" + (bWithSteamworks ? "1" : "0"));
	}
}
