#include "zephyr/sys/iterable_sections.h"
#include "zephyr/sys/printk.h"
#include "zephyr/sys/util_macro.h"
#include <stdint.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(my_botton), gpios);
static const gpio_callback btn_cb_data;

// gpio callback
void button_isr(const struct device *dev,
                struct gpio_callback *cb,
                uint32_t pins)
{
  // checkis if the correct button was pressed
  if(BIT(btn.pin) & pins){
    printk("Button pressed! Pin: %d\r\n", btn.pin);
    
  }
}


int main(void)
{
  int ret;
  if(!gpio_is_ready_dt(&btn)){
    printk("Error: button not ready\r\n");
    return 0;
  }

  //Set the button as input
  ret = gpio_pin_configure_dt(&btn, GPIO_INPUT);
  if(ret < 0)
  {
    printk("ERROR: could not set button as input \r\n");
    return 0;
  }

  //configure the interrupt
  gpio_init_callback(&btn_cb_data, button_isr, BIT(btn.pin));
  gpio_add_callback(btn.port, &btn_cb_data);
  
  // Do forver
  while(1)
  {
    
    k_sleep(K_FOREVER);
    
  }
  return 0;
  
}







      
