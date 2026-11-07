#ifndef QLCG_INPUT_BIND_H
#define QLCG_INPUT_BIND_H

#include <stdint.h>
#include <stdbool.h>

/* 可绑定动作，对齐base GameAction */
typedef enum {
    ACT_FORWARD = 0,
    ACT_BACK,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_JUMP,
    ACT_SPRINT,
    ACT_CROUCH,
    ACT_VIEW_TOGGLE,
    ACT_DEBUG,
    ACT_UNDO,
    ACT_QUICKSAVE,   /* 新增项必须在末尾，旧索引保持 */
    ACT_COUNT
} GameAction;

/* 修饰键位 */
#define QL_MOD_CTRL   0x01
#define QL_MOD_SHIFT  0x02
#define QL_MOD_ALT    0x04

/* 一个动作的绑定：键/鼠/柄/触四源，-1未绑 */
typedef struct {
    uint16_t key;
    uint8_t  mods;
    int8_t   mouse;
    int8_t   pad;
    int8_t   touch;
} QlBind;

/* 动作显示名 */
const char *qlcg_action_name(int act);

/* 默认绑定表 */
void qlcg_binds_default(QlBind *b);

/* 虚拟键码转可读标签，返回 buf */
const char *qlcg_bind_key_label(uint16_t vk, uint8_t mods, char *buf, int cap);

/* 键码归一：左右修饰键折成标准码 */
uint16_t qlcg_norm_key(uint16_t vk);

/* 查 key+mods 占用，返回冲突动作号或-1 */
int qlcg_bind_conflict(const QlBind *b, int act, uint16_t vk, uint8_t mods);

/* 交换两个动作的键盘绑定（用于解冲突） */
void qlcg_bind_swap_key(QlBind *binds, int act_a, int act_b);

#endif /* QLCG_INPUT_BIND_H */
