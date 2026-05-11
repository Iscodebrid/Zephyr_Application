#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(servo_demo, LOG_LEVEL_INF);

/* ── 从 DTS overlay 获取舵机 PWM 节点 ─────────────────────── */
static const struct pwm_dt_spec servo = PWM_DT_SPEC_GET(DT_ALIAS(servo0));

/* ── 舵机参数（单位：纳秒） ──────────────────────────────── */
#define SERVO_PERIOD_NS    20000000U   /* 20 ms = 50 Hz                 */
#define SERVO_MIN_PULSE_NS   500000U   /* 0.5 ms → 约  0°               */
#define SERVO_MAX_PULSE_NS  2500000U   /* 2.5 ms → 约 180°              */

/* ── 角度转脉冲宽度 ──────────────────────────────────────── */
static uint32_t angle_to_pulse_ns(uint8_t angle_deg)
{
    /* 线性插值: 0° → 500000ns, 180° → 2500000ns */
    return SERVO_MIN_PULSE_NS +
           (uint32_t)((SERVO_MAX_PULSE_NS - SERVO_MIN_PULSE_NS) *
                      (uint32_t)angle_deg / 180U);
}

/* ── 转到指定角度 ────────────────────────────────────────── */
static int servo_set_angle(uint8_t angle_deg)
{
    if (angle_deg > 180) {
        LOG_WRN("角度超出范围，截断到 180°");
        angle_deg = 180;
    }

    uint32_t pulse_ns = angle_to_pulse_ns(angle_deg);
    int ret = pwm_set_dt(&servo, SERVO_PERIOD_NS, pulse_ns);
    if (ret < 0) {
        LOG_ERR("PWM 设置失败: %d", ret);
    } else {
        LOG_INF("舵机 → %3d°  (脉冲: %u ns)", angle_deg, pulse_ns);
    }
    return ret;
}

/* ── 平滑扫描: 从 from° 扫到 to°，步进 step°，每步延迟 ms ── */
static void servo_sweep(uint8_t from, uint8_t to, uint8_t step_deg, uint32_t delay_ms)
{
    if (from <= to) {
        for (uint8_t a = from; a <= to; a += step_deg) {
            servo_set_angle(a);
            k_msleep(delay_ms);
        }
    } else {
        /* 避免 uint8_t 下溢：用 int16_t 做循环 */
        for (int16_t a = from; a >= to; a -= step_deg) {
            servo_set_angle((uint8_t)a);
            k_msleep(delay_ms);
        }
    }
}

/* ── 主程序 ──────────────────────────────────────────────── */
int main(void)
{
    LOG_INF("=== ESP32-S3 Zephyr 舵机示例启动 ===");

    /* 检查 PWM 设备是否就绪 */
    if (!pwm_is_ready_dt(&servo)) {
        LOG_ERR("PWM 设备未就绪，请检查 overlay 配置");
        return -ENODEV;
    }

    LOG_INF("PWM 设备就绪，开始控制舵机...");

    while (1) {
        /* ① 转到 0° */
        LOG_INF("── 定位到 0°");
        servo_set_angle(0);
        k_msleep(1000);

        /* ② 慢速扫描 0° → 180° */
        LOG_INF("── 慢速扫描: 0° → 180°");
        servo_sweep(0, 180, 2, 20);   /* 每 2° 一步，每步 20ms */
        k_msleep(500);

        /* ③ 快速扫描 180° → 0° */
        LOG_INF("── 快速扫描: 180° → 0°");
        servo_sweep(180, 0, 5, 10);   /* 每 5° 一步，每步 10ms */
        k_msleep(500);

        /* ④ 定点演示：0° → 90° → 45° → 135° → 180° */
        LOG_INF("── 定点序列");
        uint8_t positions[] = {0, 90, 45, 135, 180};
        for (int i = 0; i < ARRAY_SIZE(positions); i++) {
            servo_set_angle(positions[i]);
            k_msleep(800);
        }

        k_msleep(1000);
    }

    return 0;
}
