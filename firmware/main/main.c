#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/i2c.h"

// Hardware configuration matching my schematic layout
#define I2C_MASTER_SDA_IO           4    
#define I2C_MASTER_SCL_IO           5    
#define I2C_MASTER_FREQ_HZ          400000
#define I2C_MASTER_NUM              I2C_NUM_0

#define MPU6050_ADDR                0x68
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_ACCEL_ZOUT_H    0x3F

#define HEART_ADC_CHANNEL           ADC1_CHANNEL_0 // GPIO1 on the ESP32-S3

// idk if I need these handles later for killing tasks, but leaving them here just in case
TaskHandle_t ecgTaskRef = NULL;
TaskHandle_t imuTaskRef = NULL;

// Global variables so Core 0 and Core 1 can share sensor states
volatile int current_bpm = 80;
volatile int is_on_stomach = 0;

// Boilerplate I2C initialization
static esp_err_t init_i2c_master(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// MPU6050 wakes up in sleep mode by default, need to clear this register first
static esp_err_t init_mpu6050(void) {
    uint8_t write_buf[2] = {MPU6050_REG_PWR_MGMT_1, 0x00}; 
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, write_buf, sizeof(write_buf), pdMS_TO_TICKS(1000));
}

// Task 1: Read the heart rate sensor (AD8232)
// README says sample at 250Hz -> 1000ms / 250 = 4ms delay loop
void readHeartSensor(void *pvParameters) {
    printf("Heart sensor task started!\n");
    
    // 12-bit configuration means readings map from 0 to 4095
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(HEART_ADC_CHANNEL, ADC_ATTEN_DB_11);

    while(1) {
        int raw_adc = adc1_get_raw(HEART_ADC_CHANNEL);
        
        // Simple low-pass filter formula to smooth out analog line jumpiness
        static int running_sum = 0;
        running_sum = (running_sum * 0.9) + (raw_adc * 0.1);

        // Need to write the peak-detection interval math here later
        // Hardcoding a stable 72 just to make sure the loop runs fine
        current_bpm = 72; 

        vTaskDelay(pdMS_TO_TICKS(4)); 
    }
}

// Task 2: Read the motion sensor (MPU6050)
// README says sample at 100Hz -> 1000ms / 100 = 10ms delay loop
void readMotionSensor(void *pvParameters) {
    printf("Motion sensor task started!\n");
    
    uint8_t data_rd[2];
    int16_t raw_accel_z;

    while(1) {
        // Read 2 bytes from the Z-axis high data register
        uint8_t reg_addr = MPU6050_REG_ACCEL_ZOUT_H;
        esp_err_t err = i2c_master_write_read_device(I2C_MASTER_NUM, MPU6050_ADDR, &reg_addr, 1, data_rd, 2, pdMS_TO_TICKS(50));

        if (err == ESP_OK) {
            // Combine high and low register bytes into a signed short
            raw_accel_z = (int16_t)((data_rd[0] << 8) | data_rd[1]);
            
            // Map raw value to G-forces (+/-2g scale means 16384 LSB/g)
            float accel_z_g = (float)raw_accel_z / 16384.0;

            // Gravity vector flips negative if the baby rolls completely face down
            if (accel_z_g < -0.70) {
                is_on_stomach = 1; 
            } else {
                is_on_stomach = 0;
            }
        }

        // Safety verification: trigger alert only if stomach roll maps with low heart rate
        if (is_on_stomach == 1 && current_bpm < 60) {
            printf("[ALERT] SIDS Risk: Prone position + low heart rate (%d BPM)!\n", current_bpm);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_main(void) {
    printf("--- STARTING SIDS GUARDIAN CODE RUN ---\n");

    if (init_i2c_master() == ESP_OK && init_mpu6050() == ESP_OK) {
        printf("Hardware peripherals initialized successfully.\n");
    } else {
        printf("Hardware init failed. Check schematic wiring.\n");
    }

    // Setting up the FreeRTOS tasks to run on different cores 
    // Core 0 gets heart monitor (Priority 5, pretty high)
    xTaskCreatePinnedToCore(readHeartSensor, "HeartTask", 4096, NULL, 5, &ecgTaskRef, 0);
    
    // Core 1 gets gyro/accel tracker (Priority 4)
    xTaskCreatePinnedToCore(readMotionSensor, "MotionTask", 4096, NULL, 4, &imuTaskRef, 1);
    
    printf("Tasks spawned successfully. Checking loops...\n");
}
