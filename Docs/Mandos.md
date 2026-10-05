# Mandos: Xbox, PlayStation, Switch y genéricos (#743)

El juego y el editor leen cualquier mando de Windows: los de Xbox por XInput (el plugin de serie `XInputDevice`) y el resto
por DirectInput con un lector propio del módulo del juego (`Source/Tortunabo/Private/Settings/TN_GamepadDevice.cpp`). Cada mando que
no es de Xbox se traduce a las teclas `Gamepad_*` de serie con un perfil de `Config/DefaultInput.ini`, así valen los `IMC_*`
sin tocarlos.

## Qué mandos funcionan

| Mando | VID:PID | Editor (PIE) y juego sin Steam Input | Juego lanzado por Steam con Steam Input |
|---|---|---|---|
| Xbox 360, One, Series y cualquiera en modo XInput (8BitDo en X...) | — | XInput, por cable y Bluetooth | Steam Input o XInput (los traduce Steam si así está en Steam) |
| DualShock 4 (y su adaptador inalámbrico) | 054C:05C4, 054C:09CC, 054C:0BA0 | DirectInput, por cable y Bluetooth | Steam Input; el lector se lo calla |
| DualSense | 054C:0CE6 | DirectInput, por cable y Bluetooth | Steam Input; el lector se lo calla |
| DualSense Edge | 054C:0DF2 | DirectInput (los botones Fn y las palancas traseras no hacen nada) | Steam Input; el lector se lo calla |
| Switch Pro | 057E:2009 | DirectInput **por Bluetooth**. Por cable el mando no manda nada hasta que algo lo inicializa (Steam, BetterJoy) | Steam Input; el lector se lo calla |
| Mando HID genérico (DirectInput) | cualquiera | DirectInput con el perfil genérico (ver abajo); si los botones no cuadran, se hace un perfil | Steam Input si está activado para genéricos |
| Joy-Con sueltos o en pareja | 057E:2006/2007 | Entran como genéricos y los botones no cuadran: mejor con Steam Input | Steam Input |

No hay vibración, luz, giroscopio ni panel táctil (solo su clic) en los mandos de DirectInput: DirectInput no los da sin el
protocolo propio de cada mando. Con Steam Input, Steam sí los da.

## Cómo funciona

- **XInput** (`XInputDevice`, activo de serie): mandos de Xbox. No se toca.
- **Lector DirectInput** (`TNGamepadDevice`, se da de alta al arrancar el módulo del juego como `IInputDeviceModule`): busca
  los mandos al empezar y cada vez que cambia la lista de aparatos de Windows (enchufar o desenchufar, cable o Bluetooth).
  - Se salta los mandos que también son XInput (su interfaz HID lleva `IG_` en el nombre) y los de Valve (VID 28DE): así no
    hay pulsaciones dobles con XInput.
  - Cada mando recibe un `FInputDeviceId` y un usuario como uno de XInput (`IPlatformInputDeviceMapper`), así cuenta también en
    la partida local a pantalla partida (`Docs/Modo_Local.md`).
  - Lee el estado en cada fotograma (como XInput), con la misma repetición de botones (`InitialButtonRepeatDelay`,
    `ButtonRepeatDelay`) y los mismos umbrales para los gatillos y para los sticks como botones.
- **Perfiles**: `[/Script/Tortunabo.TN_GamepadSettings]` en `Config/DefaultInput.ini` (o Ajustes del proyecto > Tortunavy -
  Mandos). Gana el primero que encaja por VendorID/ProductID; el último, sin ID, vale para cualquier mando. Si se borran todos,
  el lector usa el genérico de serie (`TNGamepadRules::MakeGenericProfile`).
- **Avisos de botones** (#347): cada perfil lleva un `HardwareId` (`DualShock4`, `DualSense`, `DualSenseEdge`, `SwitchPro`,
  `GenericGamepad`) que va como aparato del motor (`FInputDeviceScope` «TNGamepad» y `+HardwareDevices` en
  `[InputPlatformSettings_Windows InputPlatformSettings]`). `UTN_InputDeviceSubsystem` saca la familia de ese nombre:
  PlayStation (cruz, círculo, cuadrado, triángulo; L1, R2...), Switch (B abajo, A derecha, Y izquierda, X arriba; L, R, ZL, ZR;
  + y −) y Xbox para el resto. Con Steam, la familia sale de Steam Input como antes (ahora también la de Switch).

### Distribución de los perfiles de serie

Los botones van por su número de DirectInput (empieza en 0); la cruceta es el primer «hat».

| Perfil | Botones | Sticks | Gatillos |
|---|---|---|---|
| DualShock 4, DualSense y Edge | 0 cuadrado, 1 cruz, 2 círculo, 3 triángulo, 4 L1, 5 R1, 8 Share/Create y 13 panel táctil (vista), 9 Options (menú), 10 L3, 11 R3 | X, −Y / Z, −RZ | RX y RY (analógicos) |
| Switch Pro | 0 B, 1 A, 2 Y, 3 X, 4 L, 5 R, 6 ZL, 7 ZR, 8 −, 9 +, 10 y 11 clic de los sticks | X, −Y / RX, −RY | ZL y ZR como botón (el eje vale 0 o 1) |
| Genérico | 0 izquierda, 1 abajo, 2 derecha, 3 arriba, 4 LB, 5 RB, 6 LT, 7 RT, 8 vista, 9 menú, 10 y 11 clic de los sticks | X, −Y / Z, −RZ | 6 y 7 como botón |

Los botones de la cara van por **posición** (como en el motor): el de abajo es `Gamepad_FaceButton_Bottom` en todos los mandos
(la A de Xbox, la cruz de PlayStation y la B de Switch), y el aviso dibuja la letra o el símbolo que lleva impreso ese mando.

## Por qué no los plugins del motor

Se estudiaron los tres que trae UE 5.6 para mandos de Windows:

- **GameInput** (`Runtime/GameInput`) y **GameInputWindows** (`Experimental/GameInputWindows`): necesitan el GDK de Microsoft
  (variable `GRDKLatest`) al compilar el **motor**. El motor del lanzador de Epic está compilado sin él (`GAME_INPUT_SUPPORT 0`
  en `Definitions.GameInputBase.h`): activarlos solo deja en el registro «Failed to create a GameInput device!
  GAME_INPUT_SUPPORT is false!» y no leen nada. Sus ajustes (`UGameInputDeveloperSettings`, mapeos por VendorID/ProductID) no
  sirven sin el SDK.
- **RawInput** (`Experimental/RawInput`, `URawInputSettings::DeviceConfigurations`): la cruceta (hat) llega como un eje y no
  se puede asignar a `Gamepad_DPad_*`; el dispositivo de serie registra solo joysticks **o** gamepads (no los dos); no ve los
  mandos enchufados después de arrancar; manda todos los mandos al jugador 1 y lee también la interfaz HID de los mandos de
  Xbox (pulsaciones dobles con XInput).
- **WinDualShock** necesita el SDK de Sony (libScePad) y **SteamController** es el Steam Controller antiguo con la API de Steam.

Por eso el lector es propio y no hay plugins nuevos en `Tortunabo.uproject`.

## Steam Input: sin pulsaciones dobles

Con el juego lanzado desde Steam y Steam Input activo para un mando, Steam le da al juego un mando de Xbox virtual (llega por
XInput) y el mando de verdad sigue visible por DirectInput. El lector decide así, mando a mando (`UpdateMute` en
`TN_GamepadDevice.cpp`, reglas en `TNGamepadRules`):

1. `TN.Input.DirectInput 0`: lector apagado. `2`: lee todo, también lo que traduce Steam (para probar).
2. Si el entorno trae `SDL_GAMECONTROLLER_IGNORE_DEVICES` (o `SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT`), la lista que pone
   Steam al lanzar el juego con los VID/PID que traduce (la convención de SDL, también con `@fichero`), esa lista manda: no se
   leen los que están en ella (con `_EXCEPT`, todos menos los de la lista).
3. Sin esa lista: si Steam está en marcha (solo en `-game` y en el juego empaquetado; en el editor nunca), Steam Input dice
   tener mandos de PlayStation (tipos 5, 12, 13), de Switch (8, 9, 10) o genéricos (4) **y** hay un mando XInput conectado
   (el virtual de Steam), se callan los mandos de DirectInput de esa familia. Si no hay mando XInput, Steam no le está dando
   nada al juego y el mando se sigue leyendo.
4. Si no, se lee.

Un mando callado se suelta del motor (sin usuario): no mueve a nadie ni cuenta en la partida local. Se vuelve a mirar cada
2 s. `bSkipSteamInputControllers=False` en el ini quita los pasos 2 y 3. El registro dice cada cambio («callado, Steam Input lo
traduce...»).

## Añadir un mando nuevo

1. Enchufa el mando y, en PIE, escribe `TN.Input.Pads`: en el registro sale con su nombre, su `VID:PID` y el perfil que le
   ha tocado.
2. `TN.Input.PadDebug 1` y pulsa cada botón, mueve cada stick y cada gatillo y la cruceta: el registro dice «botón N
   apretado», «eje Z = 32767» y «hat = 9000».
3. Añade en `Config/DefaultInput.ini`, **antes del genérico**, una línea `+Profiles=(Name=...,HardwareId=...,VendorId="XXXX",
   ProductIds=("YYYY"),Buttons=(...),LeftX=(Axis=X),LeftY=(Axis=Y,bInvert=True),...)`. En `Buttons`, la tecla de cada número
   (`None` si no hace nada); en los sticks, la Y suele ir al revés (`bInvert=True`); un gatillo sin eje va como botón
   `Gamepad_LeftTrigger`/`Gamepad_RightTrigger`.
4. Si el `HardwareId` es nuevo, añade su `+HardwareDevices=(InputClassName="TNGamepad",HardwareDeviceIdentifier="...",
   PrimaryDeviceType=Gamepad,SupportedFeaturesMask=4)`. La familia de los avisos sale del nombre: con DualShock, DualSense,
   PS4, PS5 o PlayStation, PlayStation; con Switch, Nintendo o JoyCon, Switch; el resto, Xbox.
5. El ini se lee al arrancar: reinicia el editor (o cambia el perfil en Ajustes del proyecto y escribe
   `TN.Input.Pads.Rescan`). `Automation RunTests Tortunabo.Input.Gamepad` comprueba que todas las teclas del ini existen.

## Comandos

| Comando | Qué hace |
|---|---|
| `TN.Input.Pads` | Escribe en el registro los ajustes, los perfiles, los aparatos de mando del motor, los mandos de DirectInput (en uso, callados por Steam, XInput o de Valve) y la lista de Steam. |
| `TN.Input.Pads.Rescan` | Vuelve a buscar los mandos y a elegir su perfil. |
| `TN.Input.PadDebug 0\|1` | Botones, ejes y hat en crudo de los mandos de DirectInput, para hacer un perfil. |
| `TN.Input.DirectInput 0\|1\|2` | Lector apagado, automático (sin lo que traduce Steam Input) o siempre. |
| `TN.Input.PadFamily 0\|1\|2\|3\|4` | Botones que dibujan los avisos: el mando conectado, Xbox, PlayStation, Steam Deck o Switch. |

## Pruebas

`Automation RunTests Tortunabo.Input.Gamepad` (perfiles del ini, traducción de botones, ejes y cruceta, lista de Steam, alta
del lector) y `Tortunabo.UI.InputGlyphs` (familia y dibujo de los botones de Switch).
