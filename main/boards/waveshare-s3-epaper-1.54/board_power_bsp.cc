#include <stdio.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include "board.h"
#include "display.h"
#include "board_power_bsp.h"
#include <esp_log.h>
#include "esp_heap_caps.h"
#include <random>

void BoardPowerBsp::PowerLedTask(void *arg) {
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = (0x1ULL << GPIO_NUM_3);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    bool isPoemRead  = false;
    bool isFirstRun  = true;
    FILE *file              = nullptr;
    char* poems = (char*)heap_caps_malloc(12000, MALLOC_CAP_SPIRAM);
    srand(time(NULL));
    int counter = 0;

    for (;;) {
        gpio_set_level(GPIO_NUM_3, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(GPIO_NUM_3, 1);
        // 不需要频繁闪烁，间隔指示效果更好
        vTaskDelay(pdMS_TO_TICKS(4800));
        // 判断文件/sdcard/poem.txt是否存在
        if (!isPoemRead && poems) {
            file = fopen("/sdcard/poem.txt", "rb");
            if (file != nullptr) {
                int lineNumber = 0;
                while (lineNumber < 100) {
                    char *buffer = poems + lineNumber * 120;
                    if (!fgets(buffer, 120, file)) {
                        break; // 文件结束或错误
                    }

                    // ESP_LOGI("右右", "read: %s", buffer);
                    lineNumber++;
                }
            }
            isPoemRead = true;
            file = nullptr;
        }

        if ((counter >= 5) && ((true == Board::GetInstance().isChangePoem) || ((counter % 20) == 0))) {
            int random_number = rand() % 100;
            display->SetChatMessage("system", poems + random_number * 120);
            ESP_LOGI("右右", "SetChatMessage: %s", poems + random_number * 120);
            if (Board::GetInstance().isChangePoem) {
                counter = 20; // 重置计数器，避免连续触发
            }
            Board::GetInstance().isChangePoem = false;
        }

        if ((isFirstRun) && (counter >= 10)) {
            isFirstRun = false;
            Board::GetInstance().example_start_file_server();
            ESP_LOGI("右右", "example_start_file_server.");
        }

        counter++;
        ESP_LOGI("右右", "counter: %d", counter);
    }
}

BoardPowerBsp::BoardPowerBsp(int epdPowerPin, int audioPowerPin, int vbatPowerPin) : epdPowerPin_(epdPowerPin), audioPowerPin_(audioPowerPin), vbatPowerPin_(vbatPowerPin) {
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = (0x1ULL << epdPowerPin_) | (0x1ULL << audioPowerPin_) | (0x1ULL << vbatPowerPin_);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    xTaskCreatePinnedToCore(PowerLedTask, "PowerLedTask", 3 * 1024, NULL, 2, NULL, 0);
}

BoardPowerBsp::~BoardPowerBsp() {
}

void BoardPowerBsp::PowerEpdOn() {
    gpio_set_level((gpio_num_t) epdPowerPin_, 0);
}

void BoardPowerBsp::PowerEpdOff() {
    gpio_set_level((gpio_num_t) epdPowerPin_, 1);
}

void BoardPowerBsp::PowerAudioOn() {
    gpio_set_level((gpio_num_t) audioPowerPin_, 0);
}

void BoardPowerBsp::PowerAudioOff() {
    gpio_set_level((gpio_num_t) audioPowerPin_, 1);
}

void BoardPowerBsp::VbatPowerOn() {
    gpio_set_level((gpio_num_t) vbatPowerPin_, 1);
}

void BoardPowerBsp::VbatPowerOff() {
    gpio_set_level((gpio_num_t) vbatPowerPin_, 0);
}