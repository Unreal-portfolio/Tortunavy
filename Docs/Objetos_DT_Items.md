# Objetos de `DT_Items`: malla, icono y efecto

Inventario de los objetos de siempre (issue #787), leído del `.uasset` de `DT_Items` con el editor sin ventana el 06-10-2026.
`DT_Items.uasset` no se ha tocado: el icono y, si hace falta, la malla se sustituyen en ejecución por código, como en los
objetos de la carrera (`TNRaceItems::ResolveVisuals`) y de Todos contra Todos (`TNTctItems::ResolveVisuals`).

## Cómo funciona

- `TNCatalogItemVisuals::ResolveVisuals` (`Source/Tortunabo/Public/World/TN_CatalogItemVisuals.h`) elige el aspecto por el uso
  de la fila (`UseType`), igual que los sorteos de los modos, y por el `ItemId` en la fila sin uso (`Score`).
- El icono siempre es una pegatina de 128x128 pintada en código con el pintor del HUD (`TN_CatalogItemArt.cpp`), con los
  colores del material de la malla de la fila.
- La malla solo se cambia si es una forma básica del motor (`/Engine/...`); entonces se construye en código con el kit de
  juguete (color de vértice, 20-35 cm, pivote en el centro), a escala 1. Las mallas del proyecto se conservan. La de un
  lanzable nunca se cambia: el proyectil la manda a las demás máquinas por un multicast y una malla construida en ejecución
  no viaja por la red.
- Lo llama `TNRaceItems::ResolveVisuals`, al que ya llaman el inventario (servidor al recibir y clientes al replicarse) y los
  pickups. Cada máquina construye lo suyo; el servidor dedicado no dibuja nada.
- Test: `Tortunabo.Items.CatalogVisuals.Rows` recorre todas las filas usadas de `DT_Items` y falla si alguna se queda sin
  icono, con un icono repetido o con un icono o una malla de `/Engine` o de VREditor.

## Inventario

| Fila | Malla (antes → después) | Icono antes | Icono después | Efecto | Modos |
|---|---|---|---|---|---|
| `StaminaBoost` | `/Engine/BasicShapes/Cylinder` (0,25) → barrita naranja de 26 cm con un rayo, en código | `/Engine/EngineResources/AICON-Green` | Barrita naranja con las puntas engarzadas y un rayo | Energía sin fin 4 s y 2 s de penalización. Funciona | Cooperativo (rebuscables), carrera (cajas, rebuscables, cofres, lagarto), lobby (cofre y barrita del tutorial), clásico (chunks) |
| `ThrowableBall` | `Piedra1` (se conserva) | `/Engine/MobileResources/HUD/AnalogHat` | Piedra gris redondeada con motas y una grieta | Lanzada a 1800 cm/s; derriba a 600 cm/s o más y marea enemigos; al pararse vuelve a ser un pickup. Funciona | Cooperativo, carrera, Todos contra Todos («Bola»), lobby, clásico |
| `BigHead` | `/Engine/BasicShapes/Sphere` (0,35) → cabeza de tortuga de 32 cm con ojos saltones, en código | `/Engine/EngineResources/AICON-Red` | Cabeza de tortuga grandota con las esquinas de «crece» | Cabeza x3,5 durante 8 s; en el cooperativo, un picotazo de gaviota se lleva la cabezota en vez de matar; al acabar, 3 s de mareo. Funciona | Cooperativo, carrera (peso 0,3, nunca en cofres), Todos contra Todos («Cabezota»), lobby, clásico |
| `Conch` | `ConchaCerrada` (se conserva) | `/Engine/EngineMaterials/BlendFunc_DefBlend` | Concha cerrada malva con el filo dentado de la trampa | Trampa en el suelo: inmoviliza 2,5 s a quien la pisa (también a quien la puso) y marea enemigos; se recicla. Funciona | Cooperativo, carrera, Todos contra Todos («Concha trampa»), lobby |
| `Tinta` | `Calamar` (se conserva) | `/Engine/Functions/.../WindTurblenceVectorAndGustMagnitude` | Calamar morado con aletas, tentáculos y una gota de tinta | Proyectil de tinta: tapa la pantalla de la tortuga alcanzada 5 s y marea o ciega enemigos. **Estaba roto** (ver abajo); corregido | Cooperativo, carrera, lobby; en Todos contra Todos la pistola de tinta dispara su proyectil |
| `Totem` | `Peluche1` (se conserva) | `/Engine/EngineResources/AICON-Green` | Tortuga de peluche verde con costuras, ojos de botón y destellos | En el inventario cancela la muerte; usado a mano revive a una eliminada al azar. Peso 5 (−100 de estamina). Funciona | Cooperativo (peso 0,3), lobby, clásico; en la carrera no sale |
| `Score` | `/Engine/BasicShapes/Sphere` (0,5) → concha de puntos melocotón de 22 cm, en código | `/Engine/MobileResources/HUD/VirtualJoystick_Thumb` | La concha de puntos del contador del HUD | Sin uso (`None`): ningún sorteo la saca | Ninguno (solo aparece en `LVL_GaleriaAssets`) |

El uso `SelfStaminaFull` (barra llena de golpe) no tiene fila; si se añade, sale como una barrita turquesa con su malla.

## Efectos corregidos

- **Tinta de calamar**: `BP_TortugaCharacter` asigna a `InkOverlayMaterial` el `DefaultPostProcessMaterial` del motor, que no
  tapa nada, así que la tinta no se veía. Ahora, si el material es nulo o del motor, `ATortugaCharacter::ApplyInkEffect` pone
  manchas de tinta pintadas en código sobre la pantalla del jugador local (`TNInkScreen`, por debajo del HUD), durante la
  duración de la tinta y desvaneciéndose el último 0,8 s. Se quitan solas aunque la tortuga muera. Si algún día se asigna un
  material de post-proceso del proyecto, se usa ese. Arregla también la pistola de tinta de Todos contra Todos.

## Para decidir (aprobadores)

- **Cabezota y regla de los modos (sin vida, veneno ni curas)**: en el cooperativo funciona como una vida extra contra la
  gaviota. ¿Se queda así, se cambia el efecto o se quita del cooperativo? En la carrera solo da el mareo (peso 0,3): ¿se
  quita del sorteo de la carrera?
- **Tótem y la misma regla**: cancela la muerte y revive. ¿Se queda en el cooperativo (donde sí se muere)?
- **Concha trampa**: atrapa también a quien la puso. ¿Debe eximirla?
- **Bola o piedra**: la fila se llama «bola» y su malla es una piedra (`Piedra1`); el icono sigue a la malla. ¿Nombre o malla?
- **Fila `Score`**: no la saca ningún modo. ¿Se borra de `DT_Items` (requiere tocar el `.uasset`)?
- **Sonidos de las filas**: `StaminaBoost` trae `PickupSound` = `1kSineTonePing` del motor, pero el código no usa
  `PickupSound` ni `UseSound` de la fila (suena el de la tortuga): no se oye. Limpiarlos al tocar el `.uasset`.

## Relación con #52

Estos iconos están pintados en código, en el mismo estilo que los de la carrera y Todos contra Todos. Cuando llegue la plantilla
«pegatina» de arte (#52), los iconos renderizados pueden sustituir a estos en `TNCatalogItemArt::GetIcon` sin tocar nada más.
