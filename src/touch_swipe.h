#ifndef CB_TOUCH_SWIPE_H
#define CB_TOUCH_SWIPE_H
#include <math.h>
#include <string.h>
/* 识别器仅接收抽象触点，可用普通 C 测试，不依赖私有系统接口。 */
typedef struct { int id; double x, y; } cb_touch_point;
typedef struct {
    int phase, samples, ids[3]; /* 0=未开始，1=三指跟踪，2=取消，3=已触发 */
    unsigned pid;
    double start_x, start_y, start_time, last_time;
} cb_touch_state;
typedef struct { unsigned short key; unsigned pid; } cb_touch_result;
static inline cb_touch_result cb_touch_step(cb_touch_state *s, const cb_touch_point *p,
                                           int n, double t, unsigned pid)
{
    cb_touch_result none = {0, 0};
    if (n == 0) { memset(s, 0, sizeof(*s)); return none; }
    if (s->phase >= 2) return none; /* 必须全部抬手后，才能开始下一次手势 */
    if (n != 3) { if (s->phase == 1 || n >= 4) s->phase = 2; return none; }
    if (!pid || !isfinite(t)) { s->phase = 2; return none; }
    double x = 0, y = 0;
    int ids[3];
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(p[i].x) || !isfinite(p[i].y) || p[i].x < 0 || p[i].x > 1 || p[i].y < 0 || p[i].y > 1) {
            s->phase = 2; return none;
        }
        ids[i] = p[i].id; x += p[i].x / 3.0; y += p[i].y / 3.0;
    }
    for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j)
        if (ids[i] > ids[j]) { int tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp; }
    if (ids[0] == ids[1] || ids[1] == ids[2]) { s->phase = 2; return none; }
    if (!s->phase) {
        s->phase = 1; s->pid = pid; s->samples = 1;
        memcpy(s->ids, ids, sizeof(ids));
        s->start_x = x; s->start_y = y; s->start_time = s->last_time = t;
        return none;
    }
    if (pid != s->pid || memcmp(ids, s->ids, sizeof(ids)) || t < s->last_time || t - s->last_time > 0.25) {
        s->phase = 2; return none;
    }
    s->last_time = t; ++s->samples;
    double dx = x - s->start_x, dy = y - s->start_y;
    if (fabs(dy) > 0.08 && fabs(dy) >= fabs(dx)) { s->phase = 2; return none; }
    /* 归一化位移约触控板宽度的 12%；至少三个采样和 40ms，过滤落指抖动。 */
    if (s->samples >= 3 && t - s->start_time >= 0.04 && fabs(dx) >= 0.12 && fabs(dx) >= fabs(dy) * 1.8) {
        s->phase = 3;
        /* 实机确认此设备的 x 轴与最初假设相反：正位移为左滑前进，
         * 负位移为右滑后退。仅交换导航方向，不改变识别阈值。
         */
        return (cb_touch_result){dx > 0 ? 124 : 123, pid};
    }
    return none;
}
#endif
