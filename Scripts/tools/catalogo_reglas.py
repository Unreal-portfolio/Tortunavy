"""Reglas de origen y estado del catálogo de assets (#126), sin depender de `unreal`.

catalogo_assets.py (commandlet del editor) las importa; Scripts/tests/test_catalogo_reglas.py las prueba con pytest.
"""

import catalogo_pendientes as pendientes

HUMANO = "Humano"
IA = "IA"
SCRIPT = "Script"
MOTOR = "Motor o plantilla"
ORIGIN_ORDER = (HUMANO, IA, SCRIPT, MOTOR)

# (prefijo de ruta /Game, origen, fuente por defecto, nota). Gana la primera que encaja: lo concreto va antes.
# Revisadas con el historial de git a 2026-10-04 (autores del primer commit de cada carpeta entre paréntesis) y con las
# rutas que crea cada script de Scripts/.
MODELING_MODE_NOTE = "Modelado a mano en el editor (Modeling Mode, carpetas _GENERATED de Alvaro2rh)"
ORIGIN_RULES = (
    ("/Game/Art/IA/", IA, "", "Biblioteca IA (Art/Library/IA/_pipeline: imagen a malla y limpieza en Blender)"),
    ("/Game/Art/Source/", SCRIPT, "", "Modelado por script de Blender en Art/Source"),
    ("/Game/Art/DA_Arte_", SCRIPT, "Scripts/arte/rellenar_catalogos.py", "Catálogo de arte rellenado por script"),
    ("/Game/Generated/Meshes/Buggy/", SCRIPT, "", "Buggy por script de Blender, copiado de HellYeah (01979da1c)"),
    ("/Game/Generated/", SCRIPT, "", "Materiales del buggy por script, copiados de HellYeah (01979da1c)"),
    ("/Game/Terrain/", SCRIPT, "Scripts/gen_terrain_volume.py",
     "Terreno generado e importado por import_terrain_mesh.py"),
    ("/Game/Environment/Water/", SCRIPT, "Scripts/build_water.py",
     "Agua (superficie, materiales y texturas) generada por script"),
    ("/Game/Cosmetics/Helmets/", SCRIPT, "Scripts/cosmetics_meshes.py",
     "Cascos modelados en Python (Mokius, f4e675c1d)"),
    ("/Game/Cosmetics/", SCRIPT, "Scripts/build_cosmetics.py",
     "Materiales de la tortuga y los cosméticos creados por script"),
    ("/Game/Blueprints/Gameplay/Cosmetics/DT_", SCRIPT, "Scripts/build_cosmetics.py",
     "Tabla de cosméticos rellenada por script"),
    ("/Game/UI/Shop/", SCRIPT, "Scripts/build_cosmetics.py",
     "Material de la vista previa de la tienda creado por script"),
    ("/Game/UI/HUD/", SCRIPT, "Scripts/build_ui_assets.py", "Materiales del HUD creados por script"),
    ("/Game/Audio/Rally/", SCRIPT, "Art/Source/Audio/Rally/gen_rally_sfx.py", "Sonidos sintetizados por script"),
    ("/Game/ProcMap/Materials/M_PoopSplatDecal", SCRIPT, "Scripts/create_poop_decal.py",
     "Calcomanía creada por script"),
    ("/Game/ProcMap/", SCRIPT, "Scripts/build_procmap_assets.py",
     "Materiales y datos del mapa procedural creados por script"),
    ("/Game/Textures/Terrain/", SCRIPT, "Scripts/gen_terrain_textures.py",
     "Texturas de detalle del terreno generadas por script"),
    ("/Game/Blueprints/Gameplay/GridMap/", SCRIPT, "Scripts/build_grid_demo_assets.py",
     "Materiales del terreno y del generador en grid creados por script"),
    ("/Game/Maps/Dev/", SCRIPT, "Scripts/tools/galeria_assets.py", "Galería de assets generada por script (#312)"),
    ("/Game/Maps/Rally/", SCRIPT, "Scripts/build_rally_level.py", "Nivel del Rally creado por script"),
    ("/Game/Maps/Run/LVL_BeachRace", SCRIPT, "Scripts/build_beach_race.py", "Mapa de la carrera creado por script"),
    ("/Game/Maps/Run/LVL_Demo01", SCRIPT, "Scripts/build_demo_level.py", "Mapa de demo creado por script"),
    ("/Game/Maps/Run/LVL_Mapa01", SCRIPT, "Scripts/import_terrain_mesh.py", "Mapa con el terreno importado por script"),
    ("/Game/Maps/Run/LVL_ProcMap", SCRIPT, "Scripts/build_procmap_assets.py", "Mapa procedural creado por script"),
    ("/Game/Vehicles/", MOTOR, "", "Plantilla Vehicle de UE (OffroadCar), copiada de HellYeah"),
    ("/Game/StarterContent/", MOTOR, "", "Starter Content de UE"),
    ("/Game/Characters/Mannequins/", MOTOR, "", "Maniquí de las plantillas de UE"),
    ("/Game/Audio/EffectSounds/FootstepsMiniPack/", MOTOR, "", "Pack externo de pasos; licencia sin registrar"),
    ("/Game/Animations/Character/TortugaDemo/Anim/", MOTOR, "",
     "Animaciones de Mixamo (biblioteca externa) de la tortuga de demo"),
    ("/Game/Animations/", HUMANO, "", "Animation Blueprint y Blend Space hechos en el editor (Alvaro2rh)"),
    ("/Game/Meshses/", HUMANO, "", "Modelado a mano por los artistas (Alvaro2rh; María en la rama Arte)"),
    ("/Game/Maps/_GENERATED/", HUMANO, "", MODELING_MODE_NOTE),
    ("/Game/Maps/Lobby/_GENERATED/", HUMANO, "", MODELING_MODE_NOTE),
    ("/Game/Maps/Run/_GENERATED/", HUMANO, "", MODELING_MODE_NOTE),
    ("/Game/Maps/", HUMANO, "", "Mapa montado a mano en el editor"),
    ("/Game/Blueprints/Characters/Meshes/", HUMANO, "", "Modelos a mano de María, subidos por Mokius (abril)"),
    ("/Game/Blueprints/Characters/Textures/", HUMANO, "", "Materiales a mano de María, subidos por Mokius (abril)"),
    ("/Game/Blueprints/Characters/", HUMANO, "", "Tortuga del jugador: piezas a mano fusionadas en el editor"),
    ("/Game/Blueprints/Builder/", HUMANO, "", "Piezas del blockout del lobby montadas en el editor (Alvaro2rh)"),
    ("/Game/Blueprints/", HUMANO, "", "Hecho en el editor por el equipo"),
    ("/Game/Materials/Grid/", HUMANO, "", "Materiales de cuadrícula del blockout (Alvaro2rh)"),
    ("/Game/Materials/", HUMANO, "", "Materiales hechos en el editor (Alvaro2rh)"),
    ("/Game/Textures/TextureGrid/", HUMANO, "", "Texturas de cuadrícula del blockout (Alvaro2rh)"),
    ("/Game/Textures/", HUMANO, "", "Imágenes del equipo (MiguelilloElPillo)"),
    ("/Game/Audio/", HUMANO, "", "Sonidos elegidos por el equipo (Mokius, MiguelilloElPillo); licencia sin registrar"),
)


FINAL = "final"
PLACEHOLDER = "placeholder"
DEPRECADO = "deprecado"
ESTADO_ORDER = (FINAL, PLACEHOLDER, DEPRECADO)
# (prefijo de ruta /Game, motivo): contenido provisional que se sustituirá por el definitivo. Lo concreto va antes.
PLACEHOLDER_RULES = (
    ("/Game/Meshses/Characters/Player/TotugaDemo", "Tortuga de demo hasta la final (Tortuga_V1 en ArteDev)"),
    ("/Game/Animations/Character/TortugaDemo/", "Animación de la tortuga de demo"),
    ("/Game/Materials/Characters/Player/", "Material de la tortuga de demo"),
    ("/Game/Materials/Grid/", "Greybox: cuadrícula del blockout"),
    ("/Game/Textures/TextureGrid/", "Greybox: cuadrícula del blockout"),
    ("/Game/Maps/_GENERATED/", "Greybox: pieza del Modeling Mode"),
    ("/Game/Maps/Lobby/_GENERATED/", "Greybox: pieza del Modeling Mode"),
    ("/Game/Maps/Run/_GENERATED/", "Greybox: pieza del Modeling Mode"),
    ("/Game/Blueprints/Builder/", "Greybox: pieza del blockout del lobby"),
)
REDIRECTOR_REASON = "Redirector de un asset movido o renombrado: se quita con Fix Up Redirectors"
OLD_FOLDER_REASON = "Carpeta OLD: pendiente de pasar a /Game/_Deprecado"
MOTOR_REASON = "Contenido de plantilla o externo (origen Motor o plantilla)"


def origin_of(package):
    for prefix, origin, source, note in ORIGIN_RULES:
        if package.startswith(prefix):
            return origin, source, note
    raise RuntimeError(f"{package} no encaja en ninguna regla de ORIGIN_RULES: añade una")


def estado_de(row):
    """(estado, motivo) de una fila con ruta, tipo, origen y uso; ver el docstring del módulo."""
    package = row["ruta"]
    if row["tipo"] == "ObjectRedirector":
        return DEPRECADO, REDIRECTOR_REASON
    if "/OLD/" in package + "/":
        return DEPRECADO, OLD_FOLDER_REASON
    if row["uso"] == pendientes.UNUSED and pendientes.issue_for(package) == pendientes.CLEANUP_ISSUE:
        return DEPRECADO, f"Sin usar y pendiente de la limpieza (#{pendientes.CLEANUP_ISSUE})"
    for prefix, reason in PLACEHOLDER_RULES:
        if package.startswith(prefix):
            return PLACEHOLDER, reason
    if row["origen"] == MOTOR:
        return PLACEHOLDER, MOTOR_REASON
    return FINAL, ""
