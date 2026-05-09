import paho.mqtt.client as mqtt
import sqlite3
import requests
import json
import time
import threading

# ==========================================
# CONFIGURACION GENERAL
# ==========================================
UBIDOTS_TOKEN  = "BBUS-UuTS6zOUdWlBcxsJRWJJQUNs8iB5ON"
GEMINI_API_KEY = "AIzaSyAase7SwNUEgbVNVv_NCxX7oIvak8g9bvk"

DEVICE_LABEL  = "nodo_quetzal"
BROKER_LOCAL  = "localhost"
TOPIC_ESCUCHA = "sensor/datos"
DB_NAME       = "historial_aire.db"

# Variables Ubidots para el módulo IA
VAR_SOLICITAR_IA   = "solicitar_ia"    # Switch en el dashboard (bool)
VAR_DIAGNOSTICO_IA = "diagnostico_ia"  # Text widget en el dashboard

# Cada cuántas lecturas se dispara el análisis IA automático
IA_CADA_N = 20
contador_lecturas = 0
ia_en_proceso     = False  # evita llamadas simultáneas

# ==========================================
# 1. BASE DE DATOS LOCAL (SQLite)
# ==========================================
def setup_db():
    conn = sqlite3.connect(DB_NAME)
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS lecturas
                 (id     INTEGER PRIMARY KEY AUTOINCREMENT,
                  fecha  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                  pm25   REAL,
                  gas    REAL,
                  temp   REAL,
                  hum    REAL,
                  pres   REAL,
                  estado TEXT)''')
    conn.commit()
    conn.close()

def guardar_en_bd(pm25, gas, temp, hum, pres, estado):
    conn = sqlite3.connect(DB_NAME)
    c = conn.cursor()
    c.execute(
        "INSERT INTO lecturas (pm25, gas, temp, hum, pres, estado) VALUES (?, ?, ?, ?, ?, ?)",
        (pm25, gas, temp, hum, pres, estado)
    )
    conn.commit()
    conn.close()
    print("[DB]  Registro guardado en SQLite.")

# ==========================================
# 2. UBIDOTS VIA MQTT
# ==========================================
ubidots_client = mqtt.Client(client_id="Raspberry_Gateway_Quetzal")
ubidots_client.username_pw_set(UBIDOTS_TOKEN, "")

def _ubidots_on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("[OK]  (Re)conectado a Ubidots MQTT.")
        # Suscribirse al switch para recibir solicitudes IA en tiempo real
        topic_switch = f"/v1.6/devices/{DEVICE_LABEL}/{VAR_SOLICITAR_IA}/lv"
        client.subscribe(topic_switch)
        print(f"[OK]  Suscrito al switch IA: '{topic_switch}'")
    else:
        print(f"[ERR] Ubidots conexion fallida rc={rc}.")

def _ubidots_on_disconnect(client, userdata, rc):
    print(f"[--]  Ubidots desconectado (rc={rc}). Reintentando en 5 s...")
    time.sleep(5)
    try:
        client.reconnect()
    except Exception as e:
        print(f"[ERR] Reintento Ubidots fallido: {e}")

def _ubidots_on_message(client, userdata, msg):
    """Recibe el cambio del Switch 'solicitar_ia' desde el dashboard."""
    global ia_en_proceso
    try:
        valor = float(msg.payload.decode("utf-8"))
        print(f"[UBI] Switch IA recibido: {valor}")
        if valor == 1.0 and not ia_en_proceso:
            print("[UBI] Solicitud manual de IA desde el dashboard.")
            threading.Thread(target=analizar_con_ia, args=(True,), daemon=True).start()
    except Exception as e:
        print(f"[ERR] Error procesando mensaje Ubidots: {e}")

ubidots_client.on_connect    = _ubidots_on_connect
ubidots_client.on_disconnect = _ubidots_on_disconnect
ubidots_client.on_message    = _ubidots_on_message

def conectar_ubidots():
    try:
        ubidots_client.connect("industrial.api.ubidots.com", 1883, 60)
        ubidots_client.loop_start()
        print("[OK]  Conectado al broker MQTT de Ubidots.")
    except Exception as e:
        print(f"[ERR] No se pudo conectar a Ubidots: {e}")

def enviar_ubidots_mqtt(payload):
    """Publica las 5 variables numéricas en Ubidots vía MQTT."""
    payload_num = {
        "pm25": payload.get("pm25", 0),
        "gas":  payload.get("gas",  0),
        "temp": payload.get("temp", 0),
        "hum":  payload.get("hum",  0),
        "pres": payload.get("pres", 0),
    }
    topic  = f"/v1.6/devices/{DEVICE_LABEL}"
    result = ubidots_client.publish(topic, json.dumps(payload_num))
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print("[OK]  Datos publicados en Ubidots via MQTT.")
    else:
        print(f"[ERR] Fallo al publicar en Ubidots (rc={result.rc}).")

def publicar_diagnostico_ubidots(texto):
    """
    Publica el diagnóstico de Gemini como variable de texto en Ubidots.
    Ubidots acepta variables de texto con el campo 'context' o usando
    el valor numérico 0 y el texto en 'context.status'.
    """
    payload = {
        VAR_DIAGNOSTICO_IA: [{"value": 0, "context": {"status": texto[:200]}}]
    }
    topic  = f"/v1.6/devices/{DEVICE_LABEL}"
    result = ubidots_client.publish(topic, json.dumps(payload))
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print("[OK]  Diagnostico publicado en Ubidots.")
    else:
        print(f"[ERR] Fallo al publicar diagnostico (rc={result.rc}).")

def resetear_switch_ia():
    """Resetea el switch a 0 después de procesar la solicitud."""
    payload = {VAR_SOLICITAR_IA: 0}
    topic   = f"/v1.6/devices/{DEVICE_LABEL}"
    ubidots_client.publish(topic, json.dumps(payload))
    print("[UBI] Switch IA reseteado a 0.")

# ==========================================
# 3. MODULO DE INTELIGENCIA ARTIFICIAL (Gemini)
# ==========================================
def analizar_con_ia(desde_dashboard=False):
    """
    Consulta Gemini con los últimos 5 registros de SQLite.
    - desde_dashboard=True: solicitud manual via switch Ubidots
    - desde_dashboard=False: disparo automático cada IA_CADA_N lecturas
    Publica el resultado en el widget de texto del dashboard de Ubidots.
    """
    global ia_en_proceso
    ia_en_proceso = True
    origen = "manual (dashboard)" if desde_dashboard else "automatico"
    print(f"[IA]  Iniciando analisis Gemini [{origen}]...")

    try:
        conn = sqlite3.connect(DB_NAME)
        c = conn.cursor()
        c.execute(
            "SELECT pm25, gas, temp, hum, pres, estado FROM lecturas ORDER BY id DESC LIMIT 5"
        )
        filas = c.fetchall()
        conn.close()

        if not filas:
            msg = "Sin datos suficientes para analizar."
            print(f"[IA]  {msg}")
            publicar_diagnostico_ubidots(msg)
            return

        contexto = (
            "Actua como un experto en calidad del aire. "
            "Los siguientes son los ultimos 5 registros de un sensor IoT ubicado en "
            "la region Sabana Centro, Cundinamarca, Colombia: "
            f"(PM2.5 ug/m3, Gas VOC ppm, Temp C, Humedad %, Presion hPa, Estado): {filas}. "
            "Genera un diagnostico conciso en maximo 3 oraciones: "
            "1) Estado actual de la calidad del aire. "
            "2) Si existe riesgo de polucion o inversion termica. "
            "3) Una recomendacion accionable para las autoridades locales."
        )

        url = (
            "https://generativelanguage.googleapis.com/v1beta/"
            f"models/gemini-2.0-flash:generateContent?key={GEMINI_API_KEY}"
        )
        headers = {"Content-Type": "application/json"}
        data    = {"contents": [{"parts": [{"text": contexto}]}]}

        resp = requests.post(url, headers=headers, json=data, timeout=15)
        resp.raise_for_status()
        texto = resp.json()["candidates"][0]["content"]["parts"][0]["text"].strip()
        print(f"[IA]  Diagnostico Gemini:\n{texto}\n")

        # Publicar en el widget de texto del dashboard
        publicar_diagnostico_ubidots(texto)

        # Log local
        with open("diagnosticos_ia.txt", "a", encoding="utf-8") as f:
            from datetime import datetime
            f.write(f"\n[{datetime.now()}] [{origen}]\n{texto}\n{'='*60}\n")

    except requests.exceptions.Timeout:
        err = "Gemini no respondio a tiempo."
        print(f"[ERR] {err}")
        publicar_diagnostico_ubidots(f"Error: {err}")
    except Exception as e:
        print(f"[ERR] Error en modulo IA: {e}")
        publicar_diagnostico_ubidots(f"Error IA: {str(e)[:100]}")
    finally:
        if desde_dashboard:
            resetear_switch_ia()
        ia_en_proceso = False

# ==========================================
# 4. BROKER LOCAL (Escucha al ESP32)
# ==========================================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("[OK]  Conectado a Mosquitto local.")
        client.subscribe(TOPIC_ESCUCHA)
        print(f"[OK]  Suscrito al topico: '{TOPIC_ESCUCHA}'")
    else:
        print(f"[ERR] Error de conexion MQTT local, codigo: {rc}")

def on_disconnect(client, userdata, rc):
    print("[--]  Desconectado de Mosquitto. Reintentando en el ciclo principal...")

def on_message(client, userdata, msg):
    global contador_lecturas
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
        print(f"[IN]  Recibido desde ESP32: {payload}")

        guardar_en_bd(
            payload.get("pm25",   0),
            payload.get("gas",    0),
            payload.get("temp",   0),
            payload.get("hum",    0),
            payload.get("pres",   0),
            payload.get("estado", "")
        )

        enviar_ubidots_mqtt(payload)

        contador_lecturas += 1
        print(f"[CTR] Lecturas acumuladas: {contador_lecturas}/{IA_CADA_N}")
        if contador_lecturas >= IA_CADA_N and not ia_en_proceso:
            threading.Thread(target=analizar_con_ia, args=(False,), daemon=True).start()
            contador_lecturas = 0

    except json.JSONDecodeError:
        print("[ERR] Mensaje recibido no es JSON valido.")
    except Exception as e:
        print(f"[ERR] Error procesando mensaje: {e}")

# ==========================================
# MOTOR PRINCIPAL
# ==========================================
if __name__ == "__main__":
    setup_db()
    conectar_ubidots()

    local_client = mqtt.Client(client_id="Raspberry_Local_Listener")
    local_client.on_connect    = on_connect
    local_client.on_disconnect = on_disconnect
    local_client.on_message    = on_message

    print("[--]  Iniciando Gateway IoT Nodo Quetzal...")

    while True:
        try:
            local_client.connect(BROKER_LOCAL, 1883, 60)
            local_client.loop_forever()
        except Exception as e:
            print(f"[--]  Mosquitto no disponible. Reintentando en 5s... ({e})")
            time.sleep(5)