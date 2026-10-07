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
		// NetCore: vectores cuantizados de la pose del ragdoll del derribo en red (FTNRagdollRootPose::NetSerialize, #153).
		PrivateDependencyModuleNames.AddRange(new string[] { "NetCore" });
		// Monkey y estrés (Source/Tortunabo/Private/Testing): tiempos de hilo de juego, de render y de GPU (GGameThreadTime, RHIGetGPUFrameCycles).
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("AssetRegistry");
		}
		// FUniqueNetIdWrapper::ToString (ajustes de voz por compañero en el menú de pausa).
		// ApplicationCore: portapapeles (FPlatformApplicationMisc) para copiar y pegar el código de sala (Docs/Salas.md).
		PrivateDependencyModuleNames.AddRange(new string[] { "CoreOnline", "ApplicationCore" });

		// Steam solo existe en escritorio: en el resto de plataformas el juego usa el subsistema en línea NULL.
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

		// Mandos que no son de Xbox (#743, Docs/Mandos.md): un lector propio por DirectInput (IInputDeviceModule) para el
		// DualShock 4, el DualSense, el Switch Pro y los HID genéricos en el editor y sin Steam Input. dinput8 viene con Windows.
		PrivateDependencyModuleNames.Add("InputDevice");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.AddRange(new string[] { "dinput8.lib", "dxguid.lib" });
		}
	}
}
