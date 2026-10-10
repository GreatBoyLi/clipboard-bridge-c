#ifndef CB_SCROLL_RESTORE_H
#define CB_SCROLL_RESTORE_H
/* 守护进程使用的恢复状态。原值可为 0 或 UINT_MAX（整页滚动），必须原样保留。
 * 后端失败时不更新 applied，后续退出仍能重试恢复。
 */
typedef struct { unsigned original, applied; } cb_scroll_owner;
static inline int cb_scroll_apply(cb_scroll_owner *s,unsigned requested,
    int (*set)(unsigned,void *),void *context) {
    if(requested>5) return -1;
    unsigned target=requested ? requested : s->original;
    if(target==s->applied) return 0;
    if(set(target,context)!=0) return -1;
    s->applied=target;return 0;
}
#endif
