#ifndef AUDIO_EFFECTS_H
#define AUDIO_EFFECTS_H

#include <stdint.h>
#include <stddef.h>


// 定义音频模式枚举
typedef enum {
    MODE_NORMAL = 0,    // 正常模式
    MODE_WHITENOISE,          // 炸麦模式
    MODE_RADIO,         // 对讲机音效
    MODE_ECHO_VOICE,         // 回声音效
    MODE_COUNT          // 模式总数
} MicMode;

// 统一的变声处理接口
void apply_audio_effect(MicMode mode, int16_t *buffer, size_t samples_count);

#endif // AUDIO_EFFECTS_H