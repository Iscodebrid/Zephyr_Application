#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/net/net_ip.h>

/* ===== WiFi AP 配置 ===== */
#define AP_SSID   "ESP32_LED"
#define AP_PASS   "12345678"
#define PORT      80

/* ===== LED（来自 DTS alias: my_led）===== */
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
        .ssid        = AP_SSID,
        .ssid_length = strlen(AP_SSID),
        .psk         = AP_PASS,
        .psk_length  = strlen(AP_PASS),
        .channel     = 6,
        .security    = WIFI_SECURITY_TYPE_PSK,
    };

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE,
                       iface,
                       &ap_config,
                       sizeof(ap_config));

    if (ret) {
        printk("AP enable request failed: %d\n", ret);
    } else {
        printk("Starting AP...\n");
    }
}

/* ===== 发送完整 HTTP 响应（处理部分发送）===== */
static void send_all(int fd, const char *data, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        int n = zsock_send(fd, data + sent, len - sent, 0);
        if (n <= 0) {
            break;
        }
        sent += n;
    }
}

/* ===== HTTP Server ===== */
static void start_http_server(void)
{
    int server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (server_fd < 0) {
        printk("socket failed: %d\n", server_fd);
        return;
    }

    /* 允许端口快速复用，避免重启时 bind 失败 */
    int opt = 1;
    zsock_setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };

    if (zsock_bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printk("bind failed\n");
        zsock_close(server_fd);   /* 修复：bind 失败也要关闭 fd */
        return;
    }

    if (zsock_listen(server_fd, 5) < 0) {
        printk("listen failed\n");
        zsock_close(server_fd);   /* 修复：listen 失败也要关闭 fd */
        return;
    }

    printk("HTTP Server listening on http://192.168.4.1:%d\n", PORT);

    while (1) {
        printk("Waiting for client...\n");
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

        /* ===== 控制 LED =====
         * gpio_pin_set_dt: 1 = 有效电平（亮），0 = 无效电平（灭）
         * 修复：原代码传值反了
         * 修复：用带结束符的精确匹配，避免 /online 误触 /on
         */
        if (strstr(buf, "GET /on ") || strstr(buf, "GET /on\r")) {
            gpio_pin_set_dt(&led, 1);
            printk("LED ON\n");
        } else if (strstr(buf, "GET /off ") || strstr(buf, "GET /off\r")) {
            gpio_pin_set_dt(&led, 0);
            printk("LED OFF\n");
        }

        /* ===== 返回网页 =====
         * 修复：加上 Content-Length 和 Connection: close，
         * 防止浏览器等待更多数据导致页面转圈
         */
        const char *body =
            "<html><body>"
            "<h1>ESP32 LED Control</h1>"
            "<a href=\"/on\">ON</a><br>"
            "<a href=\"/off\">OFF</a>"
            "</body></html>";

        char header[128];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            (int)strlen(body));

        send_all(client, header, header_len);
        send_all(client, body, strlen(body));

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
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    /* 注册 WiFi 事件 */
    net_mgmt_init_event_callback(&wifi_cb,
        wifi_event_handler,
        NET_EVENT_WIFI_AP_ENABLE_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    /* 启动 AP */
    start_ap();

    /* 等待 AP 就绪 */
    printk("Waiting for AP ready...\n");
    if (k_sem_take(&sem_ap, K_SECONDS(10)) != 0) {
        printk("AP start timeout!\n");
        return 0;
    }
    printk("AP is ready!\n");

    /* ===== AP 就绪后再配置 IP =====
     * 修复：原代码在 sem_take 之前设置 IP，AP 启动完成可能覆盖配置
     */
    struct net_if *iface = net_if_get_default();

    struct in_addr gw_addr;
    gw_addr.s_addr = htonl(0xC0A80401); /* 192.168.4.1 */
    net_if_ipv4_addr_add(iface, &gw_addr, NET_ADDR_MANUAL, 0);

    struct in_addr netmask;
    net_addr_pton(AF_INET, "255.255.255.0", &netmask);
    net_if_ipv4_set_netmask_by_addr(iface, &gw_addr, &netmask);

    printk("Gateway IP: 192.168.4.1\n");

    /* ===== 启动 DHCP Server =====
     * 修复：原代码完全缺少这一步，导致手机连上 AP 后拿不到 IP
     * 从 192.168.4.2 开始分配给客户端
     */
    struct in_addr pool_start;
    net_addr_pton(AF_INET, "192.168.4.2", &pool_start);

    int ret = net_dhcpv4_server_start(iface, &pool_start);
    if (ret) {
        printk("DHCP server start failed: %d\n", ret);
    } else {
        printk("DHCP server started (pool: 192.168.4.2+)\n");
    }

    /* 启动 HTTP Server（阻塞） */
    start_http_server();

    return 0;
}
