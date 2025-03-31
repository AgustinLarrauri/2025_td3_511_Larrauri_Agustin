#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "semphr.h"

// GPIO de boton
#define SEMPHR_BTN  20

// Semaforo para desbloquear tarea
SemaphoreHandle_t semphr;

/**
 * @brief Callback para interrupcion de GPIO
 */
void irq_callback(uint gpio, uint32_t event_mask) {
    // Doy el semaforo para desbloquar
    BaseType_t to_higher_priority_task = false;
    xSemaphoreGiveFromISR(semphr, &to_higher_priority_task);
    // Reviso si es necesario el cambio a otra tarea
    portYIELD_FROM_ISR(to_higher_priority_task);
}

/**
 * @brief Tarea de inicializacion
 */
void task_init(void *params) {
    // Inicializacion de GPIO para pulsador
    gpio_init(SEMPHR_BTN);
    gpio_set_dir(SEMPHR_BTN, false);
    gpio_pull_up(SEMPHR_BTN);
    // Inicializo interrupcion para GPIO por flanco descendente
    gpio_set_irq_enabled_with_callback(SEMPHR_BTN, GPIO_IRQ_EDGE_FALL, true, irq_callback);
    // Inicializacion de GPIO para LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    // Inicializacion de semaforo, tomo para bloquar
    vSemaphoreCreateBinary(semphr);
    xSemaphoreTake(semphr, portMAX_DELAY);
    // Elimino tarea para liberar recursos
    vTaskDelete(NULL);
}

/**
 * @brief Tarea que maneja el LED
 */
void task_led(void *params) {

    while(1) {
        // Intenta tomar el semaforo, se bloquea si no esta disponible
        xSemaphoreTake(semphr, portMAX_DELAY);
        // Conmuta el LED
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    }
}

/**
 * @brief Programa principal
 */
int main(void) {
    stdio_init_all();

    // Creacion de tareas
    xTaskCreate(task_init, "Init", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_led, "LED", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Inicia el sistema operativo
    vTaskStartScheduler();
    while (true);
}