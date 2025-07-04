#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#include "helper.h"   // Librería propia para generación de PWM 
#include "lcd.h"      // Librería para controlar el LCD por I2C
#include "bmp280.h"   // Librería para el sensor BMP280

// ======================= Configuración de I2C ==========================

#define I2C         i2c0        // Se elige el canal i2c0 de la Raspberry Pi Pico
#define SDA_GPIO    8           // Pin GPIO 8 para la línea de datos (SDA)
#define SCL_GPIO    9           // Pin GPIO 9 para la línea de reloj (SCL)
#define ADDR_LCD    0x27        // Dirección I2C del display LCD (7 bits)

// ======================= Declaraciones de recursos ======================

// Handles de tareas
TaskHandle_t handle_Task_BMP280 = NULL;
TaskHandle_t handle_Task_LCD = NULL;

// Semáforo mutex para acceso exclusivo al bus I2C
SemaphoreHandle_t xMutex_I2C;

// Cola para compartir datos entre tareas
QueueHandle_t queue_datos;

// Estructura para encapsular los datos del sensor
typedef struct {
    float temp;
    float presion;
} sensor_data_t;

sensor_data_t struct_datos;  // Variable global temporal (aunque sería mejor usar local en la tarea)

// ======================= TAREA: Lectura del sensor BMP280 ======================

void task_BMP280(void *params) {

    // Estructura con los parámetros de calibración interna del sensor
    struct bmp280_calib_param struct_calib_params;

    // Lectura protegida del bus I2C para obtener los parámetros de calibración
    xSemaphoreTake(xMutex_I2C ,portMAX_DELAY);
    bmp280_get_calib_params(&struct_calib_params);
    xSemaphoreGive(xMutex_I2C);

    while (1) {
        int32_t raw_temp, raw_presion;

        // Se toma el semáforo antes de acceder al bus I2C
        xSemaphoreTake(xMutex_I2C ,portMAX_DELAY);
        bmp280_read_raw(&raw_temp, &raw_presion);  // Lectura de datos crudos del sensor
        xSemaphoreGive(xMutex_I2C);                // Liberación del semáforo

        // Conversión a valores reales usando la calibración
        struct_datos.temp = bmp280_convert_temp(raw_temp, &struct_calib_params);
        struct_datos.presion = bmp280_convert_pressure(raw_presion, raw_temp, &struct_calib_params) / 100.0;

        // Impresión por consola para depuración
        printf("Temperatura: %.2f °C \nPresion: %.2f hPa\n", struct_datos.temp, struct_datos.presion);

        // Envío de los datos a la cola para que otra tarea (LCD) los lea
        xQueueOverwrite(queue_datos, &struct_datos);

        // Retardo de 100 ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ======================= TAREA: Mostrar datos en el LCD ========================

void task_LCD(void *params){
    
    sensor_data_t datos_LCD;   // Variable local para almacenar los datos recibidos de la cola
    char str[16];              // Buffer para mostrar los mensajes en el LCD

    while (1) {
        // Lee el último dato disponible de la cola sin removerlo
        xQueuePeek(queue_datos, &datos_LCD, portMAX_DELAY);

        // Acceso exclusivo al bus I2C para actualizar el display
        xSemaphoreTake(xMutex_I2C ,portMAX_DELAY);

        // Mostrar temperatura en la primera línea del LCD
        lcd_set_cursor(0, 0);
        sprintf(str, "Temp: %.2f C", datos_LCD.temp);
        lcd_string(str);

        // Mostrar presión en la segunda línea del LCD
        lcd_set_cursor(1, 0);
        sprintf(str,"Presion: %.2f hPa", datos_LCD.presion);
        lcd_string(str);

        xSemaphoreGive(xMutex_I2C);  // Libera el semáforo para que otra tarea pueda usar el I2C

        // Retardo de 100 ms entre actualizaciones
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ======================== FUNCIÓN PRINCIPAL (main) ============================

int main(void) {
    stdio_init_all();  // Inicializa la salida estándar (puerto serie por USB)

    // ---------- Creación de recursos compartidos ----------
    queue_datos = xQueueCreate(1, sizeof(sensor_data_t));      // Cola de 1 solo elemento (overwrite)
    xMutex_I2C = xSemaphoreCreateMutex();                      // Mutex para proteger I2C

    // ---------- Inicialización del hardware ----------
    i2c_init(I2C, 100000);                                     // Inicializa I2C a 100 kHz
    gpio_set_function(SDA_GPIO, GPIO_FUNC_I2C);                // Configura GPIO como I2C SDA
    gpio_set_function(SCL_GPIO, GPIO_FUNC_I2C);                // Configura GPIO como I2C SCL
    gpio_pull_up(SDA_GPIO);                                    // Pull-up interno en SDA
    gpio_pull_up(SCL_GPIO);                                    // Pull-up interno en SCL

    lcd_init(I2C, ADDR_LCD);                                   // Inicializa el LCD en la dirección 0x27
    lcd_clear();                                               // Limpia la pantalla

    bmp280_init(I2C);                                          // Inicializa el sensor BMP280

    // ---------- Creación de tareas ----------
    xTaskCreate(task_BMP280, "Task_BMP280", 2*configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_LCD, "Task_LCD", 2*configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // ---------- Inicio del scheduler ----------
    vTaskStartScheduler();  // Inicia FreeRTOS: las tareas comienzan a ejecutarse

    // Si el scheduler falla o se detiene (no debería), se entra en bucle infinito
    while(1);
}
