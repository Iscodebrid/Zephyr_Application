#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include "lvgl.h"


extern lv_font_t job;
#define LV_USE_UNICODE 1

int main(void)
{
    const struct device *display_dev;

    /* 获取 display 设备 */
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("Display not ready\n");
        return 0;
    }

    printk("Display ready\n");

    /* ⭐ 打开屏幕（非常关键） */
    display_blanking_off(display_dev);

    /* ⭐ 等待 LVGL 初始化完成 */
    k_msleep(300);

    /* 获取当前屏幕 */
    lv_obj_t *scr = lv_scr_act();
    if (!scr) {
        printk("LVGL not ready\n");
        return 0;
    }

    /* ⭐ 设置背景（白色） */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    /* ⭐ 创建标签 */
    lv_obj_t *label = lv_label_create(scr);

    /* ⭐ 设置字体颜色（蓝色） */
    lv_obj_set_style_text_color(label, lv_color_hex(0x0000FF), 0);

    lv_obj_set_style_text_font(label, &job, 0);
    /* ⭐ 设置文字 */
    lv_label_set_text(label, "接毕设\n 电子设计\n 物联网设计\n 智能家居 \n RTOS \nSTM32 \nESP32");

    /* ⭐ 居中 */
    lv_obj_center(label);

    /* ⭐ 主循环 */
    while (1) {
        lv_timer_handler();   // LVGL 刷新
        k_msleep(10);
    }

    return 0;
}



