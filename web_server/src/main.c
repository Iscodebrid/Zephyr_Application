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

/* ===== HTML 页面（UTF-8 中文用转义避免编译器编码问题）===== */
static const char html_body[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ESP32 LED</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,sans-serif;background:#f5f5f0;"
         "display:flex;align-items:center;justify-content:center;min-height:100vh}"
    ".card{background:#fff;border-radius:16px;border:1px solid #e8e8e4;"
          "padding:2rem;width:300px}"
    ".chip{display:inline-flex;align-items:center;font-size:12px;"
          "color:#3B6D11;background:#EAF3DE;border-radius:99px;"
          "padding:3px 10px;margin-bottom:1.5rem}"
    ".dot{width:7px;height:7px;border-radius:50%;background:#3B6D11;"
         "display:inline-block;margin-right:6px}"
    "h1{font-size:18px;font-weight:500;color:#1a1a18;margin-bottom:4px}"
    ".sub{font-size:13px;color:#888780;margin-bottom:1.75rem}"
    ".tblock{background:#f5f5f0;border-radius:10px;padding:14px 16px;margin-bottom:1.75rem}"
    ".tlabel{font-size:11px;color:#b4b2a9;margin-bottom:2px}"
    ".tval{font-size:28px;font-weight:500;color:#1a1a18;"
          "font-variant-numeric:tabular-nums;letter-spacing:1px}"
    ".dval{font-size:12px;color:#888780;margin-top:2px}"
    ".lrow{display:flex;align-items:center;gap:10px;margin-bottom:1.25rem}"
    ".ldot{width:11px;height:11px;border-radius:50%;background:#d3d1c7;"
          "transition:background .3s,box-shadow .3s}"
    ".ldot.on{background:#EF9F27;box-shadow:0 0 0 4px #FAEEDA}"
    ".llabel{font-size:13px;color:#888780}"
    ".lstate{font-size:13px;font-weight:500;color:#1a1a18;margin-left:auto}"
    ".brow{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:1.25rem}"
    ".btn{display:block;padding:13px 0;border-radius:10px;font-size:14px;"
         "font-weight:500;cursor:pointer;border:1px solid #d3d1c7;background:#fff;"
         "color:#444441;text-align:center;text-decoration:none;"
         "-webkit-tap-highlight-color:transparent;transition:opacity .15s}"
    ".btn:active{opacity:.75}"
    ".bon{background:#EAF3DE;border-color:#97C459;color:#3B6D11}"
    ".boff{background:#f5f5f0;border-color:#d3d1c7;color:#888780}"
    ".foot{font-size:11px;color:#b4b2a9;text-align:center;"
          "border-top:1px solid #e8e8e4;padding-top:1rem}"
    "</style></head><body>"
    "<div class=\"card\">"
      "<div class=\"chip\"><span class=\"dot\"></span>AP \xe5\xb7\xb2\xe8\xbf\x9e\xe6\x8e\xa5</div>"
      "<h1>LED \xe6\x8e\xa7\xe5\x88\xb6</h1>"
      "<p class=\"sub\">ESP32-S3 &middot; 192.168.4.1</p>"
      "<div class=\"tblock\">"
        "<div class=\"tlabel\">\xe5\x8c\x97\xe4\xba\xac\xe6\x97\xb6\xe9\x97\xb4</div>"
        "<div class=\"tval\" id=\"clk\">--:--:--</div>"
        "<div class=\"dval\" id=\"dat\"></div>"
      "</div>"
      "<div class=\"lrow\">"
        "<div class=\"ldot\" id=\"ldot\"></div>"
        "<span class=\"llabel\">LED \xe7\x8a\xb6\xe6\x80\x81</span>"
        "<span class=\"lstate\" id=\"lst\">\xe5\x85\xb3\xe9\x97\xad</span>"
      "</div>"
      "<div class=\"brow\">"
        "<a href=\"/on\" class=\"btn bon\">\xe5\xbc\x80\xe5\x90\xaf</a>"
        "<a href=\"/off\" class=\"btn boff\">\xe5\x85\xb3\xe9\x97\xad</a>"
      "</div>"
      "<div class=\"foot\">ESP32-S3 LED Server</div>"
    "</div>"
    "<script>"
    /* 北京时间（手机浏览器本地计算，无需网络） */
    "function p(n){return String(n).padStart(2,'0');}"
    "function tick(){"
      "var bj=new Date(new Date().toLocaleString('en-US',{timeZone:'Asia/Shanghai'}));"
      "document.getElementById('clk').textContent="
        "p(bj.getHours())+':'+p(bj.getMinutes())+':'+p(bj.getSeconds());"
      "document.getElementById('dat').textContent="
        "bj.getFullYear()+'/'+ p(bj.getMonth()+1)+'/'+p(bj.getDate());"
    "}"
    "tick();setInterval(tick,1000);"
    /* 根据当前路径同步 LED 状态显示 */
    "var on=(location.pathname==='/on');"
    "if(on){"
      "document.getElementById('ldot').className='ldot on';"
      "document.getElementById('lst').textContent='\xe5\xbc\x80\xe5\x90\xaf';"
    "}"
    "</script>"
    "</body></html>";

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
        int body_len = (int)(sizeof(html_body)-1);
        char header[160];
        int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.0 200 OK\r\n"
                              "Content-Type:text/html; charset=utf-8\r\n"
                              "Content-Length:%d\r\n"
                              "Connection:close\r\n"
                              "\r\n",
                              body_len);
        
        send_all(client, header, header_len);
        send_all(client, html_body, body_len);

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
