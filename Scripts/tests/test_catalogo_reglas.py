"""Reglas de estado y origen del catálogo de assets (#126, Scripts/tools/catalogo_reglas.py).

    uv run pytest Scripts/tests/test_catalogo_reglas.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import catalogo_reglas as cr  # noqa: E402


def row(path, tipo="StaticMesh", origen=cr.HUMANO, uso="usado en 1 mapa"):
    return {"ruta": path, "tipo": tipo, "origen": origen, "uso": uso}


def test_redirector_es_deprecado_aunque_este_en_uso():
    estado, motivo = cr.estado_de(row("/Game/Art/IA/rocas/SM_Roca", tipo="ObjectRedirector", origen=cr.IA))
    assert estado == cr.DEPRECADO
    assert motivo == cr.REDIRECTOR_REASON


def test_carpeta_old_es_deprecado():
    assert cr.estado_de(row("/Game/Meshses/OLD/SM_Viejo")) == (cr.DEPRECADO, cr.OLD_FOLDER_REASON)


def test_sin_usar_con_la_limpieza_31_es_deprecado():
    estado, motivo = cr.estado_de(row("/Game/Materials/Grid/M_Grid", uso="sin usar"))
    assert estado == cr.DEPRECADO
    assert "#31" in motivo


def test_greybox_en_uso_es_placeholder():
    assert cr.estado_de(row("/Game/Materials/Grid/M_Grid")) == (cr.PLACEHOLDER, "Greybox: cuadrícula del blockout")


def test_tortuga_de_demo_es_placeholder_aunque_sea_de_motor():
    estado, motivo = cr.estado_de(row("/Game/Animations/Character/TortugaDemo/Anim/A_Run", origen=cr.MOTOR))
    assert estado == cr.PLACEHOLDER
    assert motivo == "Animación de la tortuga de demo"


def test_motor_o_plantilla_es_placeholder():
    assert cr.estado_de(row("/Game/StarterContent/Props/SM_Chair", origen=cr.MOTOR)) == (cr.PLACEHOLDER, cr.MOTOR_REASON)


def test_lo_demas_es_final():
    assert cr.estado_de(row("/Game/Art/IA/rocas/SM_Roca", origen=cr.IA)) == (cr.FINAL, "")


@pytest.mark.parametrize("path, origin", [
    ("/Game/Art/IA/rocas/SM_Roca", cr.IA),
    ("/Game/Maps/Run/LVL_Demo01", cr.SCRIPT),
    ("/Game/Maps/Run/_GENERATED/SM_Pieza", cr.HUMANO),
    ("/Game/Maps/Lobby/LVL_Lobby", cr.HUMANO),
    ("/Game/Animations/Character/TortugaDemo/Anim/A_Run", cr.MOTOR),
    ("/Game/Animations/ABP_Tortuga", cr.HUMANO),
])
def test_origen_gana_la_regla_mas_concreta(path, origin):
    assert cr.origin_of(path)[0] == origin


def test_origen_sin_regla_detiene_el_script():
    with pytest.raises(RuntimeError):
        cr.origin_of("/Game/CarpetaNueva/SM_X")
