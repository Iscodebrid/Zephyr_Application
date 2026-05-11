#include <stdio.h>
#include <zephyr/kernel.h>

#define TIMER_MS 1000

static struct k_timer my_timer;

void timer_callback(struct k_timer *timer)
{
  //check to make sure the correct timer triggered
  if (timer == &my_timer)
  {
    printk("Timer!\r\n");
  }
}


int main()
{
  // initialize the timer
  k_timer_init(&my_timer, timer_callback, NULL);

  k_timer_start(&my_timer, K_MSEC(TIMER_MS), K_MSEC(TIMER_MS));

  while (1) {
    k_sleep(K_FOREVER);
  
  }
  return 0;
}



