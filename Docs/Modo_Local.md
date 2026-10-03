# Modo local: hasta 4 jugadores a pantalla partida

Issue #311 (01-10-2026). El menú principal pregunta primero **Local** u **Online**. Online es lo de siempre (salas por
Steam, anfitrión o invitado, hasta 8). Local es el mismo juego en este PC, **sin conexión** (Standalone, sin Steam ni
sesión), con **hasta 4 jugadores a pantalla partida**: cada uno con su `ULocalPlayer`, su `PlayerController`, su tortuga,
su cámara, su HUD y sus controles, y las mismas reglas en el lobby y en todos los modos.

## Cómo se juega

- **Empezar**: menú principal > «Local». El jugador 1 va derecho al lobby con lo que usó para elegir: el teclado y el
  ratón (siempre suyos) y, si eligió con un mando, ese mando. Los demás mandos quedan libres.
- **Entrar**: en el lobby, cualquier mando libre pulsa **Start** y entra su tortuga, como si entrara en la sala (hasta 4).
  La pantalla se reparte al momento. En una partida empezada no se entra.
- **Salir**: un invitado **mantiene B 1,5 s** en el lobby (un toque de B es meterse en el caparazón, por eso se mantiene;
  sale una barra en su vista) o elige «Dejar de jugar» en su menú de pausa. Su tortuga y su vista desaparecen. El jugador 1
  no sale así: cierra la partida desde la pausa («Menú principal»).
- **Reparto**: 1, la pantalla entera; 2, en horizontal (arriba y abajo); 3 y 4, en cuadrantes. Con 3, el cuadrante libre
  dice «Pulsa Start en otro mando para unirte». Al cambiar el reparto sale unos segundos «Jugador N» en cada vista.
- **Pausa**: la abre cualquiera (Start o la tecla elegida); sale **a toda la pantalla y la partida se para para todos**.
  Solo la maneja quien la abrió (su mando; el ratón, que es del jugador 1, no toca la de un invitado) y solo él la cierra.
- **Tutorial**: no sale solo. Se hace desde la pausa del lobby («Hacer el tutorial») y se salta desde ella.
- **Volver al menú**: quita a los invitados (tortugas, mandos y vistas) y deja la pantalla, los mandos, la calidad y la
  escala de la interfaz como estaban.

## Qué es de cada jugador y qué se guarda

| | Jugador 1 | Invitados (2 a 4) |
|---|---|---|
| Aspecto (casco, color, caparazón, ojos) | Su perfil guardado | Empiezan con el de serie; lo que cambien en el probador o la tienda dura la partida |
| Ajustes de jugador: sensibilidad e inversión de la cámara, teclas y botones, tecla del menú, temblor, campo de visión | Los suyos, se guardan | Los suyos, en memoria (empiezan de serie) |
| Ajustes del PC: sonido, gráficos, brillo, idioma, accesibilidad, ojo de pez, VR | Los cambia él y se guardan | No los ven |
| Puntos de la tienda y tutorial hecho | Se guardan | No se guardan |

La regla es `TNLocalPlay::ShouldSave` (en red, todo se guarda como siempre). El menú de pausa de un invitado enseña solo
Controles y Juego (cámara), con una nota que lo dice.

## Lo que no hay en local

Salas, códigos, expulsar, ping, Steam (nada espera a Steam: funciona sin internet) y **chat de voz** (no se pone el
componente de voz a las tortugas; sin pestaña de voz ni tecla de hablar). **VR** solo con un jugador. Con 3-4 vistas baja
un punto la distancia de dibujo y las sombras (calidad temporal de `Scalability`: no se guarda en `GameUserSettings.ini`).

## Piezas

| Pieza | Archivo | Qué hace |
|---|---|---|
| `TNLocalPlay` | `Multiplayer/TN_LocalPlayRules.*` | Reglas puras: reparto, límite de 4, quién entra y con qué mando, quién sale, qué se guarda, VR, escala de la interfaz, calidad y qué ajustes son de cada jugador. Tests `Tortunabo.LocalPlay.*`. |
| `UTN_LocalPlaySubsystem` | `Multiplayer/TN_LocalPlaySubsystem.*` | La partida local: empezar y acabar, mandos (`IPlatformInputDeviceMapper`: suelta los que no son del jugador 1 y los devuelve al acabar), Start y B (procesador de entrada de Slate), `CreateLocalPlayer`/`RemoveLocalPlayer`, reparto en `UGameViewportClient::SplitscreenInfo` (solo mientras dura), calidad, nombres «Jugador N». |
| `UTN_LocalPlayerProfile` | `Multiplayer/TN_LocalPlayerProfile.*` | `ULocalPlayerSubsystem`: número, aspecto y ajustes de jugador de cada invitado (no se guardan). |
| `TNLocalViews` | `Multiplayer/TN_LocalViews.*` | Cámara o tortuga local más cercana: lo que antes miraba al primer jugador (efectos, sonidos de cerca, carteles, tenderos, tormenta). |
| `UTN_LocalSplitOverlay` | `UI/HUD/TN_LocalSplitOverlay.*` | Encima de las vistas: el cuadrante libre, «Pulsa Start...», «Jugador N» y la barra de salir con B. |
| `TNVR::AddToScreen` / `AddToFullScreen` | `VR/TN_VRMode.*` | En local, el widget de un jugador va a su trozo (`AddToPlayerScreen`); pausa, carga, FPS y pantallas de la carrera, a toda la pantalla. |
| `UTN_GameSettingsSubsystem` | `Settings/TN_GameSettingsSubsystem.*` | Por jugador: copia de `IMC_Player`, entrada del menú, cámara; menú de pausa del que lo abre (`GetEditedSettings`) y pausa del mundo. |
| `UMP_GameInstance` | `Multiplayer/MP_GameInstance.*` | `StartLocalGame`, aspecto por jugador (`...For(PC)`), cuatro plazas, vuelta al menú limpia. |
| `UMP_MainMenuWidget` | `UI/Menu/MP_MainMenuWidget.*` | «Local» / «Online» con los botones del Blueprint; en Online, «Volver». |

## Probar

Comandos en [Comandos_Prueba.md](Comandos_Prueba.md) («Partida local»): `TN.Local.AddGuest`, `TN.Local.RemoveGuest`,
`TN.Local.Info`.

- **Sin mandos**: PIE en «Standalone Game» (o `-game`), menú > Local; en el lobby, `TN.Local.AddGuest 3` y mirar el
  reparto con 2, 3 y 4 (`TN.Local.RemoveGuest` para quitar).
- **Con mandos**: elegir Local con el teclado; en el lobby, Start en cada mando. Cada mando mueve solo su tortuga; el
  teclado, solo la del jugador 1. Mantener B en un mando invitado: sale.
- **Pausa**: abrirla con un mando invitado: a toda la pantalla, la partida parada, el ratón no hace nada, cambiar su
  sensibilidad o una tecla afecta solo a él; cerrarla y abrirla con el jugador 1: sus ajustes de siempre.
- **Online intacto**: Online > Crear partida / Unirse como siempre.
