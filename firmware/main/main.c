#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/i2c.h"

// idk if I need these handles later for killing tasks, but leaving them here just in case
TaskHandle_t ecgTaskRef = NULL;
TaskHandle_t imuTaskRef = NULL;

// Task 1: Read the heart rate sensor (AD8232)
// README says sample at 250Hz -> 1000ms / 250 = 4ms delay loop
void readHeartSensor(void *pvParameters) {
    printf("Heart sensor task started!\n");
    
    while(1) {
        // TODO: Figure out the actual ADC raw channel readings for the ESP32-S3 pin
        // For now, just a placeholder delay so the watchdogs don't trigger
        vTaskDelay(pdMS_TO_TICKS(4)); 
    }
}

// Task 2: Read the motion sensor (MPU6050)
// README says sample at 100Hz -> 1000ms / 100 = 10ms delay loop
void readMotionSensor(void *pvParameters) {
    printf("Motion sensor task started!\n");
    
    while(1) {
        // Need to setup the 400kHz I2C wire transmission stuff here later
        // Just delay for 10ms to hit the 100Hz goal
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_main(void) {
    printf("--- STARTING SIDS GUARDIAN CODE RUN ---\n");

    // Setting up the FreeRTOS tasks to run on different cores 
    // Core 0 gets heart monitor (Priority 5, pretty high)
    xTaskCreatePinnedToCore(readHeartSensor, "HeartTask", 4096, NULL, 5, &ecgTaskRef, 0);
    
    // Core 1 gets gyro/accel tracker (Priority 4)
    xTaskCreatePinnedToCore(readMotionSensor, "MotionTask", 4096, NULL, 4, &imuTaskRef, 1);
    
    printf("Tasks spawned successfully. Checking loops...\n");
}
