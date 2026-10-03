"""Crea el material del charco de sirope de las zonas lentas que coloca el generador (ATN_SlowZoneVolume, #516).

Se ejecuta DENTRO del editor de Unreal o sin ventana (el C++ carga el material por ruta y, si no existe, la zona no se
ve pero frena igual):
    exec(open(r"<repo>/Scripts/create_slowzone_decal.py", encoding="utf-8").read())
    UnrealEditor-Cmd <uproject> -run=pythonscript -script="<repo>/Scripts/create_slowzone_decal.py" -nullrhi -unattended

Crea en /Game/ProcMap/Materials:
  - M_SlowZoneSyrupDecal: material de decal (dominio "Deferred Decal", mezcla "Translucent") con un charco de sirope
    dibujado con código, sin texturas: ámbar brillante, vetas más oscuras, borde irregular que llena casi toda la
    caja (lo que se ve es lo que frena) y un reborde más claro. Parámetro Seed: la forma del borde (el C++ pone uno
    distinto a cada zona). El C++ lo proyecta hacia abajo con un UDecalComponent del tamaño de la zona.

Idempotente: si el asset existe, se le rehace el grafo. Lo guarda al acabar.
"""

import unreal

FOLDER = "/Game/ProcMap/Materials"
NAME = "M_SlowZoneSyrupDecal"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

_warnings = []


def expr(material, cls, x, y):
    return mel.create_material_expression(material, cls, x, y)


def fresh_material(name):
    path = f"{FOLDER}/{name}"
    if asset_lib.does_asset_exist(path):
        material = asset_lib.load_asset(path)
        mel.delete_all_material_expressions(material)
    else:
        asset_lib.make_directory(FOLDER)
        material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    return material


def set_first(obj, names, value, what):
    """Pone la primera propiedad que exista de names (el nombre en Python cambia según la versión); avisa si ninguna."""
    last = None
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception as error:  # noqa: BLE001 - el editor lanza excepciones de varios tipos
            last = error
    _warnings.append(f"{what}: no se pudo poner ({last}). Ponlo a mano en el editor.")
    return False


SYRUP_HLSL = r"""
// Charco de sirope de una zona lenta. UV: 0..1 en la cara del decal (la caja de la zona vista desde arriba); Seed cambia
// el borde de una zona a otra. Salidas: color base, Alpha y Rough.
struct FTNSyrup
{
	float SHash(float2 P)
	{
		float3 P3 = frac(float3(P.x, P.y, P.x) * 0.1031);
		P3 += dot(P3, float3(P3.y, P3.z, P3.x) + 33.33);
		return frac((P3.x + P3.y) * P3.z);
	}
	float SNoise(float2 X)
	{
		float2 I = floor(X);
		float2 F = X - I;
		float2 U = F * F * (3.0 - 2.0 * F);
		return lerp(lerp(SHash(I), SHash(I + float2(1.0, 0.0)), U.x), lerp(SHash(I + float2(0.0, 1.0)), SHash(I + float2(1.0, 1.0)), U.x), U.y);
	}
	float SFbm(float2 X)
	{
		return 0.55 * SNoise(X) + 0.3 * SNoise(X * 2.1 + 7.3) + 0.15 * SNoise(X * 4.3 + 1.9);
	}
};
FTNSyrup TN;
float2 P = UV;
// Distancia al borde de la caja (0 en el borde, 0.5 en el centro) con un borde ondulado: el charco llena casi toda la
// zona, que es lo que frena.
float Edge = min(min(P.x, 1.0 - P.x), min(P.y, 1.0 - P.y));
float Wobble = (TN.SFbm(P * 6.0 + Seed * 3.1) - 0.5) * 0.08;
float Inside = saturate((Edge - 0.03 + Wobble) * 40.0);
// Gotas sueltas justo fuera del borde.
float Drops = step(0.80, TN.SNoise(P * 22.0 + Seed * 5.7)) * saturate((Edge + 0.02) * 30.0) * (1.0 - Inside);
float Cover = max(Inside, Drops);
Alpha = Cover * 0.88;

// Ámbar con vetas oscuras que se arremolinan y un reborde más claro (lineal).
float3 Amber = float3(0.62, 0.28, 0.03);
float3 Deep = float3(0.22, 0.07, 0.01);
float3 Rim = float3(0.95, 0.62, 0.12);
float Swirl = TN.SFbm(float2(P.x * 3.0 + TN.SFbm(P * 4.0 + Seed) * 1.5, P.y * 3.0) + Seed * 2.3);
float3 Col = lerp(Amber, Deep, smoothstep(0.45, 0.75, Swirl) * 0.7);
float RimBand = saturate(1.0 - abs(Edge - 0.035 - Wobble) * 30.0);
Col = lerp(Col, Rim, RimBand * 0.6);
// Muy brillante en el centro (pegajoso), algo menos en el reborde.
Rough = lerp(0.08, 0.3, RimBand);
return Col;
"""


def build_syrup_decal():
    m = fresh_material(NAME)
    set_first(m, ["material_domain"], unreal.MaterialDomain.MD_DEFERRED_DECAL, "Material Domain = Deferred Decal")
    set_first(m, ["blend_mode"], unreal.BlendMode.BLEND_TRANSLUCENT, "Blend Mode = Translucent")

    uv = expr(m, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    uv.set_editor_property("coordinate_index", 0)
    seed = expr(m, unreal.MaterialExpressionScalarParameter, -900, 200)
    seed.set_editor_property("parameter_name", "Seed")
    seed.set_editor_property("default_value", 1.0)

    custom = expr(m, unreal.MaterialExpressionCustom, -500, 100)
    custom.set_editor_property("code", SYRUP_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "SlowZoneSyrup")
    inputs = []
    for name in ("UV", "Seed"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", name)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    outputs = []
    for name in ("Alpha", "Rough"):
        co = unreal.CustomOutput()
        co.set_editor_property("output_name", name)
        co.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        outputs.append(co)
    custom.set_editor_property("additional_outputs", outputs)
    mel.connect_material_expressions(uv, "", custom, "UV")
    mel.connect_material_expressions(seed, "", custom, "Seed")

    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(custom, "Alpha", unreal.MaterialProperty.MP_OPACITY)
    mel.connect_material_property(custom, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = expr(m, unreal.MaterialExpressionConstant, -500, 400)
    spec.set_editor_property("r", 0.7)
    mel.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    asset_lib.save_loaded_asset(m)
    return m


def main():
    decal = build_syrup_decal()
    unreal.log(f"[create_slowzone_decal] {decal.get_path_name()}")
    for warning in _warnings:
        unreal.log_warning(f"[create_slowzone_decal] {warning}")
    unreal.log("[create_slowzone_decal] Hecho. El material tarda 1-2 min en compilar sus sombreadores.")


main()
