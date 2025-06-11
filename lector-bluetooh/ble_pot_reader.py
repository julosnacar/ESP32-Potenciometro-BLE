import asyncio
import platform
import datetime
from bleak import BleakClient

# --- CONFIGURACIÓN ---
# Reemplaza esta dirección con la de TU ESP32.
# La puedes encontrar en los logs de inicio del ESP32 ("Bluetooth MAC: XX:XX:XX:XX:XX:XX")
# o escaneando con una app móvil.
ADDRESS = "74:4D:BD:61:E5:30"

# UUIDs de tu servicio y característica. Deben coincidir con los de tu firmware.
# Bleak espera el formato estándar con guiones.
POTENTIOMETER_SERVICE_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
# Este es el UUID de la característica que envía los datos del potenciómetro.
POTENTIOMETER_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
# --- FIN DE CONFIGURACIÓN ---
# Asegúrate de que Bleak está instalado:
# pip install bleak
# Esta es la función "callback" que se ejecutará cada vez que llegue una notificación.
def notification_handler(sender, data):
    """Maneja las notificaciones de datos recibidas."""
    # 'data' es un bytearray. Lo decodificamos a un string UTF-8.
    voltage_str = data.decode('utf-8')
    print(f"Recibido: {datetime.datetime.now()} {voltage_str}")
    #
    # --- PROCESAR LOS DATOS ---
    # Por ejemplo, guardar en un archivo CSV:
    with open("pot_data.csv", "a") as f:
        f.write(f"{datetime.datetime.now()} {voltage_str}\n")
    # O podrías añadirlo a una lista para graficarlo después, etc.


async def main():
    print(f"Intentando conectar a {ADDRESS}...")
    
    # Usamos un 'async with' para asegurarnos de que el cliente se desconecte
    # correctamente, incluso si hay errores.
    async with BleakClient(ADDRESS) as client:
        if client.is_connected:
            print(f"Conectado a {ADDRESS}")
        else:
            print(f"No se pudo conectar a {ADDRESS}")
            return

        print("Habilitando notificaciones...")
        try:
            # start_notify() es la magia de Bleak.
            # Le pasas el UUID de la característica y la función que manejará los datos.
            # Bleak se encarga de encontrar el descriptor CCCD y escribir en él.
            await client.start_notify(POTENTIOMETER_CHAR_UUID, notification_handler)
            
            print("Notificaciones habilitadas. Esperando datos...")
            print("Presiona Ctrl+C para detener.")
            
            # Mantenemos el script corriendo para recibir notificaciones.
            # El ESP32 envía datos cada 5 segundos, así que esperamos un poco más.
            while client.is_connected:
                await asyncio.sleep(1)

        except Exception as e:
            print(f"Ocurrió un error: {e}")
        finally:
            print("Deshabilitando notificaciones y desconectando...")
            await client.stop_notify(POTENTIOMETER_CHAR_UUID)

if __name__ == "__main__":
    try:
        # Ejecuta el bucle de eventos asíncrono.
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nPrograma detenido por el usuario.")