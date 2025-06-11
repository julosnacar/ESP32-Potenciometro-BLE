Proyecto de Potenciómetro BLE con ESP32-H2 y Cliente Python
Este repositorio es un proyecto de portafolio que demuestra una solución completa de Internet de las Cosas (IoT), integrando hardware embebido (ESP32-H2) con un cliente de software (Python) a través de Bluetooth Low Energy (BLE).
El sistema permite leer el valor analógico de un potenciómetro conectado a un ESP32-H2 y transmitirlo en tiempo real a una aplicación cliente para su visualización, almacenamiento y posterior análisis.
Autor: Juan Carlos Gutierrez Arcila
Contacto: [Tu Email o LinkedIn Aquí]
GitHub: julosnacar
(Recomendación: Crea un diagrama simple en herramientas como diagrams.net o Lucidchart y sube la imagen al repositorio o a un hosting como Imgur para enlazarla aquí)
📋 Tabla de Contenidos
🚀 Características Principales
🛠️ Tecnologías y Componentes Utilizados
📂 Estructura del Repositorio
⚙️ Funcionamiento Detallado
1. Firmware del ESP32-H2 (Bluedroid_Connection)
2. Cliente Python (lector-bluetooh)
3. Interacción y Flujo de Datos
🔧 Guía de Instalación y Uso
Hardware Requerido
Configuración del Firmware (ESP-IDF)
Configuración del Cliente Python
💡 Aplicaciones Prácticas
🌟 Próximos Pasos
🚀 Características Principales
Lectura Analógica en Tiempo Real: El firmware del ESP32-H2 utiliza el ADC (adc_oneshot API) para leer de forma continua el valor de un potenciómetro.
Comunicación Inalámbrica Eficiente: Se utiliza Bluetooth Low Energy (BLE) para una transmisión de datos de bajo consumo, ideal para dispositivos IoT.
Servidor GATT Personalizado: El ESP32 implementa un servidor GATT con un servicio y una característica personalizados para exponer el valor del potenciómetro.
Notificaciones BLE: El valor del sensor se envía proactivamente al cliente mediante notificaciones BLE, garantizando actualizaciones en tiempo real sin que el cliente necesite preguntar (polling).
Seguridad BLE: Implementa un emparejamiento seguro (Bonding con Secure Connections) para proteger la comunicación.
Cliente Python Asíncrono: El script en Python utiliza la librería Bleak y asyncio para conectarse al ESP32, recibir los datos y procesarlos, demostrando una implementación de software robusta y moderna.
Portabilidad: El cliente Python es multiplataforma (Windows, macOS, Linux).
🛠️ Tecnologías y Componentes Utilizados
Hardware:
Placa de desarrollo ESP32-H2.
Potenciómetro (10kΩ recomendado).
Protoboard y cables de conexión.
Firmware (Embebido):
Lenguaje: C
Framework: ESP-IDF v5.4.1
Componentes clave: FreeRTOS (para tareas y semáforos), Bluedroid (Stack BLE), Driver adc_oneshot.
Software (Cliente):
Lenguaje: Python 3
Librerías clave: Bleak (para comunicación BLE asíncrona), asyncio (para concurrencia).
📂 Estructura del Repositorio
.
├── Bluedroid_Connection/      # Proyecto ESP-IDF para el firmware del ESP32-H2
│   ├── main/
│   │   └── main.c             # Lógica principal del servidor BLE y lectura del ADC
│   └── ...                    # Otros archivos de configuración del proyecto
│
├── lector-bluetooh/           # Script del cliente en Python
│   └── ble_pot_reader.py      # Script para conectar, recibir y mostrar los datos
│
└── README.md                  # Este archivo
Use code with caution.
⚙️ Funcionamiento Detallado
1. Firmware del ESP32-H2 (Bluedroid_Connection)
El firmware convierte al ESP32-H2 en un periférico BLE que mide y reporta un valor.
Inicialización: Se inicializan los componentes básicos: NVS, el controlador Bluetooth en modo BLE y el stack Bluedroid. Se libera la memoria de Bluetooth Clásico para optimizar recursos.
Servidor GATT:
Se registra una aplicación GATT y se define un Servicio personalizado (UUID: ...ff0000).
Dentro de este servicio, se crea una Característica personalizada (UUID: ...01ff0000) para el valor del potenciómetro.
Esta característica tiene las propiedades READ (permite al cliente leer el valor a demanda) y NOTIFY (permite al servidor enviar actualizaciones automáticas).
Se añade un Descriptor CCCD (Client Characteristic Configuration Descriptor), que es el mecanismo estándar que un cliente utiliza para suscribirse a las notificaciones.
Tarea del ADC (adc_reader_task):
Esta tarea se ejecuta en paralelo (gracias a FreeRTOS) y es el motor del sistema.
Utiliza la API adc_oneshot para configurar y leer el GPIO1 (conectado al potenciómetro).
Convierte el valor digital crudo (0-4095) a un string de voltaje (ej. "2.76 V").
Cada 5 segundos, si un cliente está conectado y ha habilitado las notificaciones, el firmware:
Actualiza el valor de la característica en la base de datos local con esp_ble_gatts_set_attr_value().
Envía el nuevo string de voltaje al cliente con esp_ble_gatts_send_indicate().
Sincronización: Se utiliza un semáforo de FreeRTOS (ble_ready_sem) para asegurar que la tarea del ADC no intente enviar datos por BLE antes de que toda la configuración del servicio GATT haya finalizado, evitando así condiciones de carrera.
Gestión de Conexión y Seguridad: El firmware maneja los eventos de conexión, desconexión y emparejamiento seguro, permitiendo que el dispositivo se vuelva a anunciar si se desconecta.
2. Cliente Python (lector-bluetooh)
El script ble_pot_reader.py actúa como un cliente BLE central.
Conexión: Utiliza la librería Bleak para escanear y conectarse al ESP32-H2 a través de su dirección MAC.
Suscripción a Notificaciones: Una vez conectado, el script llama a client.start_notify(). Esta función de alto nivel se encarga automáticamente de:
Descubrir los servicios y características.
Encontrar el descriptor CCCD de la característica del potenciómetro.
Escribir el valor 0x0001 en el descriptor para activar las notificaciones en el ESP32.
Manejo de Datos (notification_handler):
Se define una función callback que es invocada por Bleak cada vez que se recibe una notificación.
Dentro de esta función, los datos recibidos (que son un array de bytes) se decodifican a un string UTF-8.
Finalmente, el string del voltaje se imprime en la consola. Este es el punto de entrada para cualquier procesamiento posterior (almacenamiento, graficación, etc.).
3. Interacción y Flujo de Datos
El ESP32-H2 se anuncia como ESP32H2_Potenciometro.
El script de Python se conecta al ESP32.
El script de Python se suscribe a las notificaciones escribiendo en el descriptor CCCD.
El ESP32-H2, en su gatts_event_handler, detecta esta escritura y activa la bandera notifications_enabled.
La tarea del ADC en el ESP32, que se ejecuta cada 5 segundos, lee el potenciómetro. Al ver que las notificaciones están activadas, envía el valor formateado como string.
El script de Python recibe la notificación, y su notification_handler se dispara, imprimiendo el valor en la consola.
Este ciclo se repite hasta que el cliente se desconecta o el programa se detiene.
🔧 Guía de Instalación y Uso
Hardware Requerido
1 x Placa de desarrollo ESP32-H2.
1 x Potenciómetro (ej. 10kΩ).
1 x Protoboard y cables.
Conexión: Conecta el pin central del potenciómetro al GPIO1 de la placa. Los otros dos pines a 3V3 y GND.
Configuración del Firmware (ESP-IDF)
Clona este repositorio.
Abre el proyecto Bluedroid_Connection en tu entorno de desarrollo ESP-IDF (se recomienda VS Code con la extensión de Espressif).
Conecta tu ESP32-H2 y asegúrate de que el puerto COM correcto está seleccionado.
Ejecuta un fullclean para asegurar una compilación limpia: idf.py fullclean.
Compila y flashea el proyecto: idf.py flash monitor.
Configuración del Cliente Python
Navega a la carpeta lector-bluetooh:
cd lector-bluetooh
Use code with caution.
Bash
Crea un entorno virtual (recomendado). Lo has llamado pot_reader:
python -m venv pot_reader
Use code with caution.
Bash
Activa el entorno virtual:
En Windows: .\pot_reader\Scripts\activate
En macOS/Linux: source pot_reader/bin/activate
Instala las dependencias:
pip install bleak
Use code with caution.
Bash
Edita el script: Abre ble_pot_reader.py y modifica la variable ADDRESS con la dirección MAC de tu ESP32-H2 (la verás en los logs del monitor serie al iniciar).
Ejecuta el script:
python ble_pot_reader.py
Use code with caution.
Bash
💡 Aplicaciones Prácticas
Este proyecto, aunque simple, es la base para una multitud de aplicaciones del mundo real:
Monitoreo Remoto de Sensores: Reemplazando el potenciómetro por sensores de temperatura, humedad, luz o calidad del aire para crear una estación meteorológica inalámbrica.
Interfaces de Control Humano (HCI): Usar potenciómentros o joysticks para controlar remotamente robots, drones o actuadores.
Dispositivos de Asistencia: Crear interfaces adaptativas donde un usuario puede ajustar configuraciones (ej. volumen de un audífono, velocidad de una silla de ruedas) de forma inalámbrica.
Arte Interactivo y Música: Usar sensores analógicos para controlar parámetros de una instalación artística o un sintetizador de música que se ejecuta en un ordenador.
Recolección de Datos para Agricultura Inteligente: Medir la humedad del suelo y reportarla a un sistema central para análisis y riego automatizado.
🌟 Próximos Pasos
Este proyecto puede ser expandido de muchas maneras para demostrar aún más habilidades:
Integración con Frontend Web: Reemplazar el cliente Python por una aplicación web creada con Nuxt 3 y la Web Bluetooth API para visualizar los datos directamente en un navegador.
Almacenamiento en la Nube: Modificar el cliente para que envíe los datos recibidos a una plataforma en la nube como AWS IoT, Google Cloud IoT o Thingspeak.
Múltiples Sensores: Añadir más características BLE para reportar datos de múltiples sensores simultáneamente.
Control Bidireccional: Añadir una característica WRITE para que el cliente pueda enviar comandos al ESP32 (ej. encender o apagar un LED).
