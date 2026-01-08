Altimetro digital:
El proyecto tiene como objetivo la fabricacion de un altimetro digital para paracaidismo deportivo como el ares 2 o mejor. el desarrollo del proytecto en cuanto a estrucuta debe ser escalable y modular para asegurar que el proyecto pueda crecer con el tiempo
-hardware:
esp32 s3
bateria litio 3,7 253450 400mha
modulo cargador tp4057. entrada +5v a divisor de voltaje a pin 11 para detectar presencia de carga. salida de bateria a divisor de voltaje a pin 1 para medir voltaje de carga de la bateria. pines adc
bmp390L i2c a pines 2 y 3 con api de bosch:
Se reconfigura dinámicamente según modo de sensor
Ahorro: oversampling alto, filtro IIR fuerte, ODR baja (25 Hz) y I²C a 100 kHz.
Ultra preciso: oversampling medio, filtro IIR medio, ODR 50 Hz e I²C a 400 kHz.
Freefall: oversampling mínimo, sin filtro IIR, pensado para alta frecuencia
modulo lcd transflectivo por spi unico canal para la ui= LCD 12864 SPI 128X64 SPI ST7567A COG con constructor U8G2_ST7567_JLX12864_F_4W_SW_SPI y libreria u8g2 y pines 4, 5, 8, 6, 7, 9 para LCD_SCK, LCD_MOSI, LCD_CS, LCD_DC, LCD_RST y LCD_LED respectivamente.
3 pulsadores a 3.3v. para navegar por la ui y hacer wake de deep sleep y light sleep
motor  vibrador a pin 15 por mosfet a bateria
RTC DS3231 i2c al mismo sda/scl que sensor bmp. autoridad de tiempo
-desarrollado en platformio: 
platform = espressif32 
board = esp32-s3-fh4r2
framework = arduino
monitor_speed = 115200
Funcionamiento en cuanto a consumo de bateria:
Ya que en paracaidismo deportivo un altimetro pasa la mayor cantidad de tiempo en suelo lo que buscamos es un funcionamiento orientado al bajo consumo cuando estoy en el suelo. Para esto definimos distinto estados en los que puede estar un altimetro (suelo/ahorro, en vuelo, en caida libre y bajo campana) y definimos lo siguiente:
1. el procesador funciona en frecuencias variables de cpu: suelo 40mhz, en vuelo y bajo campana 80mhz y caida libre en 160mhz
2. En modo suelo se aplica light sleep entre lecturas, maximizando el tiempo dormido, con un duty cycle orientado a ahorrar batería (en realidad debe ser lo maximo posible que permita prolongar bateria sin romper funcionalidad)
3. en light sleep los botones deben seguir "atentos" para activar sus funciones en pantalla principal (detalladas mas abajo) y deben tener un tiempo de gracia de no sleep para asegurar la fluidez de la ui.
4. el menu de la ui será la excepcion de light sleep, si estamos en el no activaremos light sleep y subiremos frecuencia de procesador a 80mhz
5. para el deep sleep tendremos tiempos de ahorro configurables, los periodos configurados aplicaran el deep sleep cuando se cumpla el tiempo por inactividad. reiniciaran el contador de inactividad cualquier pulsacion de boton, entrar en modo vuelo o cualquier interaccion con el altimetro. la funcion de suspension se debe implementar de forma que por ningun motivo pueda haber un apagado mientras no esté en suelo. Tambien haremos un deep sleep cuando el voltaje de bateria sea de 3.36v. en caso de conectar cargador haremos wake y no dormiremos por bateria baja para evitar reinicios pero si seguiremos durmiendo por inactividad.
6. modo vuelo tambien tendrá un light sleep pero mas pequeño. debe dormir lo suficiente para ahorrar energia pero no demasiado para interferir con las lecturas ni interferir con la deteccion de cambio de vuelo a free fall
7. en la pantalla principal (solo en la principal no en menu) y solo cuando estemos en ahorro usaremos el ahorro del lcd, mantendremos la pantalla "congelada" y repintaremos toda la ui cuando:
-detectemos un cambio de altura (fuera del deadband explicado mas abajo)
-cambio de minuto en hora
-se pulse cualquier boton
-activemos el lock
-cambie el porcentaje de bateria
-conectemos cargador
-volvamos a pantalla principal desde el menu
Funcionamiento de los botones:
en pantalla principal
-boton arriba enciende el led de la pantalla
-boton medio entra al menu
-boton abajo activa un lock al presionar 3 segundos y lo desactiva al presionar 6 segundos. el lock dibuja un candado en la ui, la funcion está pensada para ser activada en zona de embarque, debe reajustar altitud en cero en caso de que hayan variaciones de presion durante el dia, bloquear la ui y entrar en modo vuelo. una vez activado el lock se desactivara en suelo estable de 1 minuto. rompe modo ahorro y por lo mismo no permite deep sleep
en menú (menu tipo lista hacia abajo y con varias paginas):
-boton arriba y abajo son para navegar
-boton medio es para seleccionar opciones
Pantalla principal:
altimetro enciende, muestra cuenta regresiva mientras por detras inicia los distintos modulos y setea la altura en cero luego se muestra la pantalla principal que contiene: altura seteada en cero, hora, temperatura, unidad de medida de la altura, porcentaje de bateria, numero de saltos, candado en caso de activar lock, flecha arriba cuando se detecta que estamos en vuelo por aumento de altura, flecha abajo cuando se detecta freefall, dibujo de zzz cuando queden 5 minutos para entrar en deep sleep y un icono de carga que aparece al detectar carga.
menu: arriba pone la fecha y abajo numero de pagina
unidad de medida: permite seleccionar entre metros o pies
brillo: permite prender o apagar el backlight del lcd (sin niveles intermedios)
bitacora:abre el menu de bitacora (en backend es binario circular en LittleFS). se permite hacer un reset de bitacora con confirmacion (mantengo arriba y abajo por 3 segundos luego me pide confirmacion y repito la accion para borrar). guardaremos el salto cuando tengamos suelo estable. Guarda por salto:
ID de salto.
Fecha/hora UTC.
Altitud de salida.
Altitud de apertura.
Tiempo en freefall.
Velocidad máxima en ff
Velocidad máxima bajo campana
ahorro: debe permitirme seleccionar un tiempo de ahorro para el deep sleep entre al menos 3 opciones
invertir: debe permitirme invertir el altimetro en 180 grados, es decir, pantalla gira y los botones tambien, abajo es arriba, medio es medio y arriba es abajo)
offset: debe permitirme agregar una diferencia de altura, la diferencia a establecer debe corresponder con la unidad de medida configurada. este offset pasará a ser mi cero interno (la ui debe mostrar el valor real del offset). definimos un deadband en backend y este se debe ajustar al offset (configuraremos un deadband para no mostrar alturas en x intervalo de altura hacia arriba y hacia abajo con el fin de no mostrar variaciones pequeñas de alturas). como el offset será el cero si por ej offset = 40ft al hacer el lock en pantalla principal se debe recalibrar hacia los 40ft.
fecha y hora: abre el menu de ajuste de fecha y hora
idioma: debe permitirme seleccionar entre ingles y español
salir del menu: debe llevarme a la pantalla principal
la fuente de la ui debe ser logisoso y debe estar definida globalmente en config para poder cambiarla en caso de requerirlo
auto ground zero: algoritmo que se encargará de ajustar las variaciones de altura diarias llevando a cero cuando estemos en suelo
altura a mostrar en ui: 
Si |altura| < 999
Se muestra como entero normal:
long v = lroundf(altToShow);
altDisplay = String(v);
Ejemplos:
120 m "120"
850 ft "850"
-35 m "-35"
Si 999 ≤ |altura| < 9999
Se muestra en “miles” con 2 decimales:
float vDisp = roundf((altToShow/1000.0f)*100.0f)/100.0f;
altDisplay = String(vDisp, 2);
Eso es altToShow / 1000 con 2 decimales.
Ejemplos:
2500 m 2.50
4200 m 4.20
13500 ft 13.50
9000 ft 9.00
Si |altura| ≥ 9999
También en miles, pero con 1 decimal:
float vDisp = roundf((altToShow/1000.0f)*10.0f)/10.0f;
altDisplay = String(vDisp, 1);
Ejemplos:
17000 ft 17.0
12000 ft 12.0
10500 m 10.5
Ademas para caida libre, en el tramo que se muestra con dos decimales, las cifras se cuantizan en pasos de 0.05k (ej: 4.55 → 4.50 → 4.45) para que no maree durante la caída.
Persistencia en NVS
Qué cosas se guardan:
unidadMetros
brilloPantalla
ahorroTimeoutOption
alturaOffset
idioma
inverPant
usrActual

el altimetro base tendrá dos versiones una con y una sin bluetooth, corresponderá a una tipica limitacion de software y no de hardware por lo que deberemos poder desactivar el bl con alguna flag

# bluetooth (borrador)
- BLE debe poder apagarse totalmente desde el menú para no gastar batería: cuando está Off no se inicializa el stack ni se hace advertising.
- Identidad única por dispositivo: nombre derivado del MAC (ej. `ALTI-XXXX`) y/o PIN almacenado en NVS, visible en etiqueta/QR para emparejarse con la app móvil correcta.
- Seguridad: exigir pairing o un desafío/PIN antes de permitir lecturas/escrituras (logbook, ajustes).
- Advertising lento y de baja potencia; opcionalmente sólo on-demand. Tras desconexión, se puede apagar si no hay actividad.
- Servicio GATT propio con UUID base: control de comandos, lectura de bitácora en bloques, estado básico y ajustes. Documentar el protocolo en un contrato compartido con la app (repo separado).

# ota por bluetooth (borrador)
- Binario actual ~500 kB. Con BLE típico (~10–30 kB/s), estimar 20–60 s de transferencia + verificación.
- Usar particiones OTA existentes: recibir en la partición inactiva, validar hash/versión y hacer switch seguro.
- Requiere autenticación previa (pairing/PIN) y verificación de imagen (hash/firma) antes de aplicar para evitar cargas no autorizadas o corruptas.
- Durante la actualización, deshabilitar sleeps que interrumpan la transferencia; reactivar política de energía al terminar.
- La app móvil debe manejar reconexiones y reintentos, mostrar progreso y no dejar imágenes a medias.
