#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb_device_uac.h"
#include "driver/i2s_std.h" 
#include "driver/gpio.h"

static const char *TAG = "UAC_INMP441_MIC";

// 引脚定义
#define I2S_MIC_BCLK     GPIO_NUM_4
#define I2S_MIC_WS       GPIO_NUM_5
#define I2S_MIC_DIN      GPIO_NUM_6
#define MUTE_BUTTON_PIN  GPIO_NUM_1
#define MUTE_LED_PIN     GPIO_NUM_2

#define SAMPLE_RATE       48000 // 48kHz 采样率

//每 1 个声音采样点，在内存里都要占用 2 个字节的格子（16-bit PCM），这是 Windows UAC 设备要求的标准格式
//而底层 I2S 硬件采集的原始数据是以 32-bit 的格式存在 DMA 链表中的，因此每个采样点在 I2S DMA 中占用 4 个字节。
#define UAC_BYTES_PER_SAMPLE 2
#define I2S_BYTES_PER_SAMPLE 4

#define MAX_SAMPLES_PER_REQUEST 1024

static i2s_chan_handle_t rx_handle = NULL;

static bool g_hardware_mute = 0;

static bool last_button_state = 1; // 假设初始状态是未按下（高电平）

static void uac_device_set_mute_cb(uint32_t mute, void *arg) 
{
    // 留空
}

static void uac_device_set_volume_cb(uint32_t volume_percent, void *arg) 
{
    // 留空
}
static void init_i2s_inmp441(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    

    //48000 / 1000 = 48，48 * 5 = 240，设置 DMA 容器大小为 240 ，确保在高速采集时不会丢帧
    chan_cfg.dma_frame_num = 240;
    // 5 + 1 = 6，设置 DMA 容器数量为 6 ，确保在高速采集时不会丢帧
    chan_cfg.dma_desc_num = 6;
    
    // 图解: 
    // ┌───>[ 仓库 1 ]───>[ 仓库 2 ]───>[ 仓库 3 ]──┐
    // │                                           │
    // └───[ 仓库 6 ]<───[ 仓库 5 ]<───[ 仓库 4 ]<──┘


    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .bclk = I2S_MIC_BCLK,
            .ws = I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED, 
            .din = I2S_MIC_DIN,
        },
    };
    
    // 强制指定物理槽宽为 32-bit，且数据在左声道（INMP441 L/R 接地时）
    std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_32BIT; 
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; 
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S rx channel fixed for INMP441 (Pure 32-bit slot bypass mode).");
}

// USB UAC 1:1 数据转换投喂回调函数
static esp_err_t uac_microphone_input_cb(uint8_t *buf, size_t uac_bytes_req, size_t *out_len, void *arg)
{
    // Windows 请求 uac_bytes_req 字节的 16-bit 音频数据，我们需要计算出对应的采样点数

    size_t uac_samples_req = uac_bytes_req / UAC_BYTES_PER_SAMPLE; // 计算出 Windows 需要的采样点数

    size_t i2s_samples_to_read = uac_samples_req;
    if (uac_samples_req > MAX_SAMPLES_PER_REQUEST) {
        i2s_samples_to_read = MAX_SAMPLES_PER_REQUEST; // 限制最大采样点数，防止溢出
    }
    
    size_t i2s_bytes_to_read = i2s_samples_to_read * I2S_BYTES_PER_SAMPLE; // 从I2S读取的原始字节数，稍后在内存中进行高位截断提取
    
    static int32_t raw_buf[MAX_SAMPLES_PER_REQUEST]; // 创建一个静态本地缓冲区用于存储 32-bit 的原始硬件数据（避免在回调中频繁申请内存）

    size_t bytes_read = 0;// 实际从 I2S 硬件 DMA 链表中读取的字节数
    // 从 I2S 硬件 DMA 链表中读取原始 32-bit 的全数字音频，给予 4ms 的超时容错
    esp_err_t ret = i2s_channel_read(rx_handle, raw_buf, i2s_bytes_to_read, &bytes_read, pdMS_TO_TICKS(4));

    
    if ((ret == ESP_OK || ret == ESP_ERR_TIMEOUT) && bytes_read > 0) {
        size_t samples_read = bytes_read / I2S_BYTES_PER_SAMPLE; // 计算出实际读取的采样点数
        int16_t *dst = (int16_t *)buf;

        // 【高位截断提取】：INMP441 采集上来的有效音频存在 32 位槽的高 24 位中
        // 我们将其直接右移 16 位，截取高 16 位数据，即可转换为标准且无损的 16-bit 有符号整数喂给 Windows

        // 图解:
        // ┌─── 高16位数据 ─────┐ ┌─ 低位数据 ─┐ ┌─ 补零 ─┐
        // [ X X X X X X X X X X X X X X X X ]  [ Y Y Y Y Y Y Y Y ] [ 0 0 0 0 0 0 0 0 ]
        // └─── 高16位 ───────────────────────┘ └─────── 低16位 ───────────────────────┘


        if (g_hardware_mute) {
            for (size_t i = 0; i < samples_read; i++) {
            dst[i] = 0;
            }
        } else {
            for (size_t i = 0; i < samples_read; i++) {
            dst[i] = (int16_t)((uint32_t)raw_buf[i] >> 16);
            }
            
        }

        

        // 如果底层硬件在当前时间片内没有攒够预期数量的数据，用 0 补齐，防止噪音和断连
        if (samples_read < uac_samples_req) {
            memset(buf + (samples_read * UAC_BYTES_PER_SAMPLE), 0, uac_bytes_req - (samples_read * UAC_BYTES_PER_SAMPLE));
        }
    } else {
        // 如果遇到彻底的读取失败或硬件未就绪，快速向 Windows 补零，维持 USB 状态机不掉线
        memset(buf, 0, uac_bytes_req);
    }
    
    *out_len = uac_bytes_req;
    return ESP_OK; 
}

void mute_task(void *pvParameters)
{

    while (1) {
        bool current_state = gpio_get_level(MUTE_BUTTON_PIN);
        
        // 检测到按键按下（从 1 变 0 瞬间）
        if (last_button_state == 1 && current_state == 0) {
            vTaskDelay(pdMS_TO_TICKS(20)); // 软件消抖
            if (gpio_get_level(MUTE_BUTTON_PIN) == 0) {
                
                // 💡 状态翻转
                g_hardware_mute = !g_hardware_mute;
                
                // 💡 状态灯联动（如果是静音就切红灯，解除静音就切绿灯）
                if (g_hardware_mute) {
                    gpio_set_level(MUTE_LED_PIN, 0); // 灯灭表示静音
                    // set_ws2812_color(255, 0, 0); // 伪代码：切红灯
                } else {
                    gpio_set_level(MUTE_LED_PIN, 1); // 灯亮表示未静音
                    // set_ws2812_color(0, 255, 0); // 伪代码：切绿灯
                }
            }
        }
        last_button_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(20)); // 降低轮询频率
    }
}

void mute_init(void)
{
    gpio_reset_pin(MUTE_BUTTON_PIN);
    gpio_set_direction(MUTE_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(MUTE_BUTTON_PIN, GPIO_PULLUP_ONLY);

    gpio_reset_pin(MUTE_LED_PIN);
    gpio_set_direction(MUTE_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(MUTE_LED_PIN, 1); // 初始状态灯亮（假设初始状态是未静音）
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing UAC Microphone with INMP441 hardware...");
    init_i2s_inmp441();

    uac_device_config_t uac_config = {
        .output_cb = NULL,
        .input_cb = uac_microphone_input_cb,
        .set_mute_cb = uac_device_set_mute_cb,
        .set_volume_cb = uac_device_set_volume_cb,
        .cb_ctx = NULL,
    };

    // 初始化组件，让底层 TinyUSB 音频栈跑起来
    ESP_ERROR_CHECK(uac_device_init(&uac_config));
    ESP_LOGI(TAG, "USB UAC Stack initialized successfully!");

    mute_init();

    xTaskCreate(mute_task, "button_task", 2048, NULL, 5, NULL);
    
}

