/*
 * ESP32-H2 BLE Potentiometer Reader
 * Versión corregida para evitar errores de sincronización
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"
#include "esp_gattc_api.h"
#include "freertos/semphr.h"

// --- INICIO DE MODIFICACIÓN: Nueva librería para el ADC ---
#include "esp_adc/adc_oneshot.h"
// --- FIN DE MODIFICACIÓN ---

#define GATTS_TAG "ESP32H2_POT_BLE"

// Configuración ADC para ESP32-H2
#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHANNEL     ADC_CHANNEL_0  // GPIO1
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_WIDTH       ADC_BITWIDTH_12

// Configuración BLE
#define PROFILE_NUM                 1
#define PROFILE_A_APP_ID            0
#define DEVICE_NAME                 "ESP32H2_Potenciometro"
#define GATTS_SERVICE_UUID_A        0x00FF
#define GATTS_CHAR_UUID_A           0xFF01
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 4

// Variables globales
static const char device_name[] = DEVICE_NAME;
static bool client_connected = false;
static bool notifications_enabled = false;
static SemaphoreHandle_t ble_ready_sem = NULL;
static adc_oneshot_unit_handle_t adc1_handle = NULL;

// Estructura del perfil GATT
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

// Declaraciones de funciones
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

// Instancia del perfil
static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = gatts_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

// Parámetros de advertising
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Inicialización del ADC
static esp_err_t init_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "Error inicializando ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_WIDTH,
        .atten = ADC_ATTEN,
    };
    
    ret = adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "Error configurando canal ADC: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(GATTS_TAG, "ADC inicializado correctamente");
    return ESP_OK;
}

// Tarea de lectura del ADC
static void adc_reader_task(void *arg)
{
    ESP_LOGI(GATTS_TAG, "Tarea ADC esperando inicialización BLE...");
    
    // Esperar a que BLE esté listo
    if (xSemaphoreTake(ble_ready_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(GATTS_TAG, "Timeout esperando inicialización BLE");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(GATTS_TAG, "BLE listo. Iniciando lecturas del potenciómetro...");

    int pot_value_raw = 0;
    //uint32_t pot_value = 0;
    char voltage_str[16]; // Buffer para guardar el string del voltaje. "X.XX V" + nulo.

    while (1) {
        // Leer valor del ADC
        esp_err_t ret = adc_oneshot_read(adc1_handle, ADC_CHANNEL, &pot_value_raw);
        if (ret == ESP_OK) {
            // 1. Convertir el valor crudo a milivoltios.
            //    Usamos 3300mV como referencia para la atenuación de 12dB.
            uint32_t voltage_mv = (uint32_t)(pot_value_raw * 3300) / 4095;
            
            // 2. Formatear los milivoltios a un string con dos decimales (ej. "3.30 V").
            //    snprintf es seguro y evita desbordamientos de buffer.
            snprintf(voltage_str, sizeof(voltage_str), "%.2f V", voltage_mv / 1000.0f);
            
            // Log para debug
            ESP_LOGI(GATTS_TAG, "Potenciómetro: %d | Voltaje: %s | Cliente: %s | Notif: %s", 
                     pot_value_raw,
                     voltage_str,
                     client_connected ? "Conectado" : "Desconectado",
                     notifications_enabled ? "ON" : "OFF");

            // Enviar notificación si hay cliente conectado
            if (client_connected && notifications_enabled) {
                // Actualizar valor en la base de datos GATT
                esp_ble_gatts_set_attr_value(gl_profile_tab[PROFILE_A_APP_ID].char_handle, 
                                           strlen(voltage_str), 
                                           (const uint8_t*)voltage_str);
                
                // Enviar notificación
                esp_err_t notify_ret = esp_ble_gatts_send_indicate(
                    gl_profile_tab[PROFILE_A_APP_ID].gatts_if,
                    gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                    gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                    strlen(voltage_str), 
                    (uint8_t*)voltage_str, 
                    false);
                
                if (notify_ret != ESP_OK) {
                    ESP_LOGW(GATTS_TAG, "Error enviando notificación: %s", esp_err_to_name(notify_ret));
                }
            }
        } else {
            ESP_LOGW(GATTS_TAG, "Error leyendo ADC: %s", esp_err_to_name(ret));
        }

        // Esperar 1 segundo antes de la siguiente lectura
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Callback GAP
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
        
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Error iniciando advertising");
        } else {
            ESP_LOGI(GATTS_TAG, "Advertising iniciado correctamente");
        }
        break;
        
    // --- INICIO DE LA MODIFICACIÓN DE SEGURIDAD ---

    case ESP_GAP_BLE_SEC_REQ_EVT:
        // El cliente solicita iniciar el emparejamiento/cifrado. Aceptamos.
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;

    case ESP_GAP_BLE_NC_REQ_EVT:
        // El stack pide confirmar el "Numeric Comparison". 
        // Como nuestro dispositivo no tiene pantalla, simplemente confirmamos que sí.
        ESP_LOGI(GATTS_TAG, "Numeric Comparison request, passkey: %"PRIu32, param->ble_security.key_notif.passkey);
        esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
        break;

    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
        // El proceso de autenticación ha terminado.
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(GATTS_TAG, "Auth complete, address: "ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bd_addr));
        if (param->ble_security.auth_cmpl.success) {
            ESP_LOGI(GATTS_TAG, "Bonding successful!");
        } else {
            ESP_LOGE(GATTS_TAG, "Bonding failed, reason: 0x%x", param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    }
    // ... otros casos de GAP
    default:
        break;
    }
}

// Callback GATTS
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATTS_TAG, "GATT server registrado, status %d, app_id %d", 
                 param->reg.status, param->reg.app_id);
        
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[PROFILE_A_APP_ID].gatts_if = gatts_if;
            
            // Configurar nombre del dispositivo
            esp_ble_gap_set_device_name(device_name);
            
            // Configurar datos de advertising
            esp_ble_adv_data_t adv_data = {
                .set_scan_rsp = false,
                .include_name = true,
                .include_txpower = true,
                .min_interval = 0x0006,
                .max_interval = 0x0010,
                .appearance = 0x00,
                .manufacturer_len = 0,
                .p_manufacturer_data = NULL,
                .service_data_len = 0,
                .p_service_data = NULL,
                .service_uuid_len = 0,
                .p_service_uuid = NULL,
                .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
            };
            esp_ble_gap_config_adv_data(&adv_data);
            
            // Crear servicio
            gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_A;
            
            esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, 4);
        }
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(GATTS_TAG, "Servicio creado, status %d, service_handle %d", 
                 param->create.status, param->create.service_handle);
        
        if (param->create.status == ESP_GATT_OK) {
            gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
            
            // Iniciar servicio
            esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
            
            // Configurar característica
            gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_A;
            
            // --- INICIO DE LA CORRECCIÓN CLAVE ---

            // 1. Preparamos el valor inicial (puede ser un string vacío).
            char initial_value_str[] = "0.00 V";
            esp_attr_value_t char_value = {
                .attr_max_len = 16, // Tamaño máximo del string, p. ej. 16 bytes
                .attr_len     = strlen(initial_value_str),
                .attr_value   = (uint8_t *)initial_value_str,
            };

            // 2. Preparamos la estructura de control para que la respuesta
            //    sea automática por parte del stack.
            esp_attr_control_t attr_control = {.auto_rsp = ESP_GATT_AUTO_RSP};
        
            // 3. Añadimos la característica, pasando ahora también la estructura de control.
            esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle,
                                &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
                                ESP_GATT_PERM_READ,
                                ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                &char_value, 
                                &attr_control); // <-- Pasamos el puntero a la estructura de control

            // --- FIN DE LA CORRECCIÓN CLAVE --
        }
        break;

    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(GATTS_TAG, "Característica añadida, status %d, attr_handle %d", 
                 param->add_char.status, param->add_char.attr_handle);
        
        if (param->add_char.status == ESP_GATT_OK) {
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
            
            // Añadir descriptor CCCD para notificaciones
            esp_bt_uuid_t descr_uuid;
            descr_uuid.len = ESP_UUID_LEN_16;
            descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            
            esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle,
                                       &descr_uuid,
                                       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                       NULL, NULL);
        }
        break;

    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        ESP_LOGI(GATTS_TAG, "Descriptor añadido, status %d, attr_handle %d", 
                 param->add_char_descr.status, param->add_char_descr.attr_handle);
        
        if (param->add_char_descr.status == ESP_GATT_OK) {
            gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
            
            // BLE está listo, liberar semáforo
            ESP_LOGI(GATTS_TAG, "Servicio BLE completamente configurado");
            xSemaphoreGive(ble_ready_sem);
        }
        break;

    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "Cliente conectado, conn_id %u", param->connect.conn_id);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        client_connected = true;
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TAG, "Cliente desconectado");
        client_connected = false;
        notifications_enabled = false;
        esp_ble_gap_start_advertising(&adv_params);
        break;

    //case ESP_GATTS_READ_EVT:
    //    ESP_LOGI(GATTS_TAG, "Solicitud de lectura, handle %d", param->read.handle);
    //    
    //    if (param->read.handle == gl_profile_tab[PROFILE_A_APP_ID].char_handle) {
    //        esp_gatt_rsp_t rsp;
    //        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
    //        rsp.attr_value.handle = param->read.handle;
    //        
    //        uint16_t length = 0;
    //        const uint8_t *value_ptr = NULL;
    //        esp_gatt_status_t status = esp_ble_gatts_get_attr_value(param->read.handle, &length, &value_ptr);
    //        
    //        if (status == ESP_GATT_OK && value_ptr != NULL) {
    //            rsp.attr_value.len = length;
    //            memcpy(rsp.attr_value.value, value_ptr, length);
    //            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
    //        } else {
    //            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_READ_NOT_PERMIT, NULL);
    //        }
    //    }
    //    break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(GATTS_TAG, "Solicitud de escritura, handle %d, len %d", param->write.handle, param->write.len);
        
        if (param->write.handle == gl_profile_tab[PROFILE_A_APP_ID].descr_handle && param->write.len == 2) {
            uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
            
            if (descr_value == 0x0001) {
                ESP_LOGI(GATTS_TAG, "Notificaciones habilitadas");
                notifications_enabled = true;
            } else if (descr_value == 0x0000) {
                ESP_LOGI(GATTS_TAG, "Notificaciones deshabilitadas");
                notifications_enabled = false;
            }
        }
        
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;

    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t ret;

    // Inicializar NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Crear semáforo para sincronización
    ble_ready_sem = xSemaphoreCreateBinary();
    if (ble_ready_sem == NULL) {
        ESP_LOGE(GATTS_TAG, "Error creando semáforo");
        return;
    }

    // Inicializar ADC
    if (init_adc() != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "Error inicializando ADC");
        return;
    }

    // Inicializar Bluetooth
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error inicializando controlador BT: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error habilitando controlador BT: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error inicializando bluedroid: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error habilitando bluedroid: %s", esp_err_to_name(ret));
        return;
    }

    // Registrar callbacks
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error registrando callback GAP: %x", ret);
        return;
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error registrando callback GATTS: %x", ret);
        return;
    }

    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error registrando app GATTS: %x", ret);
        return;
    }
    // --- INICIO DE LA MODIFICACIÓN DE SEGURIDAD ---

    // 1. Configurar la capacidad de Entrada/Salida del dispositivo. 
    //    ESP_IO_CAP_NONE significa que no tiene ni pantalla ni teclado.
    //    Esto fuerza el emparejamiento "Just Works" o "Numeric Comparison" si el otro dispositivo tiene pantalla.
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));

    // 2. Establecer el nivel de autenticación requerido. 
    //    ESP_LE_AUTH_REQ_SC_BOND pide Conexiones Seguras (SC) con emparejamiento (bonding).
    //    NO estamos pidiendo MITM aquí.
    uint8_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));

    // 3. Configurar las claves que se van a generar e intercambiar.
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));

    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    ret = esp_ble_gatt_set_local_mtu(500);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "Error configurando MTU: %x", ret);
    }

    // Crear tarea de lectura del ADC
    xTaskCreate(adc_reader_task, "adc_reader_task", 4096, NULL, 5, NULL);

    ESP_LOGI(GATTS_TAG, "Aplicación iniciada correctamente");
}