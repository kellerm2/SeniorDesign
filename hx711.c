/*
 * hx711.c
 *
 *  Created on: Dec 6, 2025
 *      Author: Owner
 */


#include "hx711.h"
#include <math.h>
#define DEADBAND_THRESHOLD 50.0f  // Ignore anything < 50mg as noise
#define PILL_WEIGHT_EXPECTED 200.0f
#define PILL_DETECTION_TOLERANCE 80.0f // 200mg +/- 80mg

void HX711_Init(HX711* data, GPIO_TypeDef* clk_gpio, uint16_t clk_pin, GPIO_TypeDef* dat_gpio, uint16_t dat_pin) {
    data->clk_gpio = clk_gpio;
    data->clk_pin = clk_pin;
    data->dat_gpio = dat_gpio;
    data->dat_pin = dat_pin;
    data->coefficient = 1.0f; // Default scale

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Ensure clock is low initially
    HAL_GPIO_WritePin(data->clk_gpio, data->clk_pin, GPIO_PIN_RESET);
}

int32_t HX711_Read(HX711* data) {
    uint32_t count = 0;

    // Wait for the chip to become ready
    // Note: In production code, add a timeout to avoid hanging here!
    while (HAL_GPIO_ReadPin(data->dat_gpio, data->dat_pin) == GPIO_PIN_SET);

    // Pulse the clock 24 times to read the data
    for (int i = 0; i < 24; i++) {
        HAL_GPIO_WritePin(data->clk_gpio, data->clk_pin, GPIO_PIN_SET);
        count = count << 1;
        HAL_GPIO_WritePin(data->clk_gpio, data->clk_pin, GPIO_PIN_RESET);
        if (HAL_GPIO_ReadPin(data->dat_gpio, data->dat_pin) == GPIO_PIN_SET) {
            count++;
        }
    }

    // Pulse the clock one more time (25th pulse) to set gain to 128 (default)
    HAL_GPIO_WritePin(data->clk_gpio, data->clk_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(data->clk_gpio, data->clk_pin, GPIO_PIN_RESET);

    // Convert 24-bit two's complement to 32-bit signed integer
    if (count & 0x800000) {
        count |= 0xFF000000;
    }

    return (int32_t)count;
}

int32_t HX711_ReadAverage(HX711* data, uint8_t times) {
    int32_t sum = 0;
    for (int i = 0; i < times; i++) {
        sum += HX711_Read(data);
    }
    return sum / times;
}

void HX711_Tare(HX711* data, uint8_t times) {
    int32_t sum = HX711_ReadAverage(data, times);
    data->offset = sum;
}

float HX711_GetWeight(HX711* data, uint8_t times) {
    int32_t val = HX711_ReadAverage(data, times);
    return (float)(val - data->offset) / data->coefficient;
}

void HX711_SetupPageHinkley(HX711* data, float delta, float threshold) {
    data->ph_delta = delta;
    data->ph_threshold = threshold;
    data->ph_sum = 0.0f;
    data->ph_min_sum = 0.0f;
    data->ph_avg_mean = 0.0f; // Will be set on first read
    data->ph_is_active = 0;   // Start inactive until first read
}

uint8_t HX711_CheckChange(HX711* data, float current_weight) {

    // 1. THE SHIELD: Ignore bouncy weight while cooling down from a drop
    if (data->cooldown_timer > 0) {
        data->cooldown_timer--;
        data->baseline = current_weight; // Track the bouncing weight to settle accurately
        for(int i=0; i<8; i++) data->diff_history[i] = 0;
        return 0;
    }

    float instant_diff = current_weight - data->baseline;

    // 2. Add to rolling buffer
    data->diff_history[data->diff_idx] = instant_diff;
    data->diff_idx = (data->diff_idx + 1) % 8;

    // 3. Calculate the Average Difference
    float avg_diff = 0;
    for(int i = 0; i < 8; i++) {
        avg_diff += data->diff_history[i];
    }
    avg_diff /= 8;

    // Absorbs motor hum and tiny vibrations up to 50mg
    if (fabsf(avg_diff) <= 40.0f) {
        data->baseline += 0.01f * avg_diff;
        return 0;
    }

    // CHECK 1: DISPENSED (Positive weight change only)
    if (avg_diff > 50.0f) {
        data->baseline = current_weight;
        for(int i=0; i<8; i++) data->diff_history[i] = 0;

        data->cooldown_timer = 10; // Start cooldown to ignore the physical bounce!
        return 1;  // Placed
    }

    // Set to -90.0f to require a heavier pull, ignoring standard downward vibration
    else if (avg_diff < -90.0f) {
        data->baseline = current_weight;
        for(int i=0; i<8; i++) data->diff_history[i] = 0;
        return 2;  // Removed
    }

    return 0;
}
