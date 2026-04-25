#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <stdio.h>

/* ===== WiFi AP 配置 ===== */
#define AP_SSID "ESP32_LED"
#define AP_PASS "12345678"
#define PORT 80

/* ===== LED（来自 DTS alias: led0）===== */
#define LED_NODE DT_ALIAS(my_led)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* ===== WiFi 事件 & 信号量 ===== */
static struct net_mgmt_event_callback wifi_cb;
K_SEM_DEFINE(sem_ap, 0, 1);

/* ===== WiFi 事件回调 ===== */
static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event,
                               struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
        const struct wifi_status *status =
            (const struct wifi_status *)cb->info;

        if (status->status) {
            printk("AP start failed (%d)\n", status->status);
        } else {
            printk("AP started successfully\n");
            printk("iface: %p\n", net_if_get_default());
            k_sem_give(&sem_ap);
        }
        break;
    }

    default:
        break;
    }
} 

/* ===== 启动 AP ===== */
static void start_ap(void)
{
    struct net_if *iface = net_if_get_default();

    if (!iface) {
        printk("No network interface found!\n");
        return;
    }

    struct wifi_connect_req_params ap_config = {
        .ssid = AP_SSID,
        .ssid_length = strlen(AP_SSID),
        .psk = AP_PASS,
        .psk_length = strlen(AP_PASS),
        .channel = 6,
        .security = WIFI_SECURITY_TYPE_PSK,
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE,
                       iface,
                       &ap_config,
                       sizeof(ap_config));

    if (ret) {
        printk("AP enable failed: %d\n", ret);
    } else {
        printk("Starting AP...\n");
    }
}

/* ===== HTTP Server ===== */
static void start_http_server(void)
{
    int server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (server_fd < 0) {
        printk("socket failed\n");
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (zsock_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printk("bind failed\n");
        return;
    }

    if (zsock_listen(server_fd, 5) < 0) {
        printk("listen failed\n");
        return;
    }

    printk("HTTP Server started on http://192.168.4.1\n");

    while (1) {
        int client = zsock_accept(server_fd, NULL, NULL);

        if (client < 0) {
            continue;
        }

        char buf[512];
        int len = zsock_recv(client, buf, sizeof(buf) - 1, 0);

        if (len <= 0) {
            zsock_close(client);
            continue;
        }

        buf[len] = '\0';
        printk("Request:\n%s\n", buf);

        /* ===== 控制 LED ===== */
        if (strstr(buf, "GET /on")) {
            gpio_pin_set_dt(&led, 0);
            printk("LED ON\n");
        } else if (strstr(buf, "GET /off")) {
            gpio_pin_set_dt(&led, 1);
            printk("LED OFF\n");
        }

        /* ===== 返回网页 ===== */
        const char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<html><body>"
            "<h1>ESP32 LED Control</h1>"
            "<a href=\"/on\">ON</a><br>"
            "<a href=\"/off\">OFF</a>"
            "</body></html>";

        zsock_send(client, response, strlen(response), 0);
        zsock_close(client);
    }
}

/* ===== main ===== */
int main(void)
{
    printk("ESP32-S3 LED Server\n");

    /* LED 初始化 */
    if (!device_is_ready(led.port)) {
        printk("LED device not ready\n");
        return 0;
    }

    gpio_pin_configure_dt(&led, GPIO_OUTPUT);

    /* 注册 WiFi 事件 */
    net_mgmt_init_event_callback(&wifi_cb,
        wifi_event_handler,
        NET_EVENT_WIFI_AP_ENABLE_RESULT);

    net_mgmt_add_event_callback(&wifi_cb);

    /* 启动 AP */
    start_ap();

    printk("Waiting for AP ready...\n");
    if (k_sem_take(&sem_ap, K_SECONDS(10)) != 0) {
        printk("AP start timeout!\n");
        return 0;
    }

    printk("AP is ready!\n");

    /* 启动 HTTP Server */
    start_http_server();

    return 0;
}
