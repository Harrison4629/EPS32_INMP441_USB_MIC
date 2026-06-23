#include "audio_effects.h"
#include <math.h>
#include <stdlib.h>


#define DELAY_BUF_SIZE 2048

static int16_t hp_prev = 0;
static int16_t agc_gain = 2;

// 内部函数 1：炸麦
static void apply_whitenoise_effect(int16_t *buffer, size_t samples_count) {
    for (size_t i = 0; i < samples_count; i++) {
        buffer[i] = (int16_t)((rand() % 65536) - 32768) * 0.05; // 生成随机噪声并降低音量
    }
}

// 内部函数 2：对讲机
static void apply_radio_effect(int16_t *buffer, size_t samples_count)
{
    static int32_t hp_prev_input = 0;
    static int32_t hp_prev_output = 0;

    static int32_t lp_prev = 0;

    for (size_t i = 0; i < samples_count; i++)
    {
        int32_t x = buffer[i];

        // ==========================
        // 高通（去低频）
        // 约 300Hz
        // ==========================

        int32_t hp =
            x - hp_prev_input +
            ((hp_prev_output * 31) >> 5);

        hp_prev_input = x;
        hp_prev_output = hp;

        x = hp;

        // ==========================
        // 低通（砍高频）
        // 约 3~4kHz
        // ==========================

        lp_prev += (x - lp_prev) >> 3;

        x = lp_prev;

        // ==========================
        // 噪声门
        // ==========================

        if (abs(x) < 300)
        {
            x = 0;
        }

        // ==========================
        // 压缩器
        // 小声放大
        // 大声压缩
        // ==========================

        x *= 4;

        if (x > 12000)
        {
            x = 12000 + ((x - 12000) >> 3);
        }

        if (x < -12000)
        {
            x = -12000 + ((x + 12000) >> 3);
        }

        // ==========================
        // 无线电削波
        // ==========================

        if (x > 18000)
            x = 18000;

        if (x < -18000)
            x = -18000;

        // ==========================
        // 轻微底噪
        // ==========================

        if (x != 0)
        {
            x += (rand() & 7) - 3;
        }

        buffer[i] = (int16_t)x;
    }
}

// 内部函数 3：回声音效
static void apply_mc_mic_effect(int16_t *buffer, size_t samples_count)
{
    for (size_t i = 0; i < samples_count; i++)
    {
        int32_t x = buffer[i];

        // =========================
        // 1. 高通（去闷音 / 近讲轰鸣）
        // =========================
        int32_t hp = x - hp_prev;
        hp_prev = x;
        x = hp;

        // =========================
        // 2. 中频增强（主播“贴脸感”关键）
        // =========================
        x = x + (x / 2);  // +6dB左右中频感

        // =========================
        // 3. 自动增益（AGC）
        // =========================
        int32_t abs_x = (x < 0) ? -x : x;

        if (abs_x > 12000)
            agc_gain--;

        else if (abs_x < 3000)
            agc_gain++;

        if (agc_gain < 1) agc_gain = 1;
        if (agc_gain > 6) agc_gain = 6;

        x *= agc_gain;

        // =========================
        // 4. 压缩（MC喊麦核心）
        // =========================
        if (x > 14000)
            x = 14000 + (x - 14000) / 4;

        if (x < -14000)
            x = -14000 + (x + 14000) / 4;

        // =========================
        // 5. 防爆音削波
        // =========================
        if (x > 16000) x = 16000;
        if (x < -16000) x = -16000;

        // =========================
        // 6. 极轻微空气感（不是回声！）
        // =========================
        x += (rand() % 3) - 1;

        buffer[i] = (int16_t)x;
    }
}

// ✨ 暴漏给外部的唯一公共接口
void apply_audio_effect(MicMode mode, int16_t *buffer, size_t samples_count) {
    switch (mode) {
        case MODE_WHITENOISE:
            apply_whitenoise_effect(buffer, samples_count);
            break;
        case MODE_RADIO:
            apply_radio_effect(buffer, samples_count);
            break;
        case MODE_ECHO_VOICE:
            apply_mc_mic_effect(buffer, samples_count);
            break;
        case MODE_NORMAL:
        default:
            break;
    }
}