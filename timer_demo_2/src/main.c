#include "zephyr/device.h"
#include "zephyr/sys/printk.h"
#include <stdint.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/counter.h>

#define COUNTER_DELAY_US 1000000
#define ALARM_CH_ID 0


void counter_isr (const struct device *dev,
                  uint8_t chan_id,
                  uint32_t ticks,
                  void *user_data)
{
  struct counter_alarm_cfg *alarm_cfg = user_data;

  alarm_cfg->ticks = counter_us_to_ticks(dev, COUNTER_DELAY_US);
  counter_set_channel_alarm(dev,ALARM_CH_ID, alarm_cfg);

  //Do something
  printk("counter!\r\n");
}

int main(void)
{
  int ret;
  const struct device *counter_dev = DEVICE_DT_GET(DT_ALIAS(my_timer));

  //configure the alarm
  struct counter_alarm_cfg alarm_cfg = {
    .callback = counter_isr,
    .ticks = counter_us_to_ticks(counter_dev, COUNTER_DELAY_US),
    .user_data = &alarm_cfg,
    .flags = 0
  };

  // start the counter
  ret = counter_start(counter_dev);
  if(ret<0)
  {
    printk("Error (%d): Failed to start counter\r\n", ret);
    return 0;
  }

  //Set alarm
  ret = counter_set_channel_alarm(counter_dev, ALARM_CH_ID, &alarm_cfg);
  if(ret<0)
  {
    printk("ERROR (%d): Failed to configure timer\r\n", ret);
    return 0;
  }
  printk("Counter alarm set to %u ticks\r\n", alarm_cfg.ticks);

  //Do nothing
  while(1)
  {
    k_sleep(K_FOREVER);
  }
  
  return 0;
}


