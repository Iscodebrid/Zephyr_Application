#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_event.h>
#include <errno.h>
#include <zephyr/net/wifi_mgmt.h>



//custom libraried
#include "wifi.h"
#include "ping.h"
#include "http_get.h"

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static K_SEM_DEFINE(sem_wifi, 0, 1);
static K_SEM_DEFINE(sem_ipv4, 0, 1);


//wifi settings
#define SSID "H70X"
#define PSK "free1239"

// called when the wifi is connected
static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *) cb->info;

    if(status->status)
    {
        printk("connect request failed (%d)\n", status->status);
    }
    else
    {
        printk("Connected\n");
        k_sem_give(&sem_wifi);
    }
}

// deal disconnect
static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *) cb->info;

    if(status->status)
    {
        printk("Disconnnect request (%d)", status->status);
    }
    else
    {
        printk("Disconnectd\n");
        k_sem_take(&sem_wifi, K_NO_WAIT);
    }
    
}

//handle ipv4
//
static void handle_ipv4_result(struct net_if *iface)
{
    int i=0;
    for(i = 0; i< NET_IF_MAX_IPV4_ADDR; i++)
    {
        char buf[NET_IPV4_ADDR_LEN];

        if(iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP)
        {
            continue;
        }
        printk("IPv4 address: %s\n",
                net_addr_ntop(AF_INET,
                                &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
                                buf, sizeof(buf)));
        printk("Subnet: %s\n",
                net_addr_ntop(AF_INET,
                                &iface->config.ip.ipv4->unicast[i].netmask,
                                buf, sizeof(buf)));
        printk("Router: %s\n",
                net_addr_ntop(AF_INET,
                                &iface->config.ip.ipv4->gw,
                                buf, sizeof(buf)));
    }
    k_sem_give(&sem_ipv4);
}


static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event)
    {

        case NET_EVENT_WIFI_CONNECT_RESULT:
            handle_wifi_connect_result(cb);
            break;

        case NET_EVENT_WIFI_DISCONNECT_RESULT:
            handle_wifi_disconnect_result(cb);
            break;

        case NET_EVENT_IPV4_ADDR_ADD:
            handle_ipv4_result(iface);
            break;

        default:
            break;
    }
}



int main(void)
{
    int sock;

    printk("WiFi Example\nBoard: %s\n", CONFIG_BOARD);

    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);

    net_mgmt_init_event_callback(&ipv4_cb, wifi_mgmt_event_handler, NET_EVENT_IPV4_ADDR_ADD);

    net_mgmt_add_event_callback(&wifi_cb);
    net_mgmt_add_event_callback(&ipv4_cb);

    wifi_connect(SSID, PSK);
    k_sem_take(&sem_wifi, K_FOREVER);
    wifi_status();
    k_sem_take(&sem_ipv4, K_FOREVER);
    printk("Ready...\n\n");

    // Ping Google DNS 4 times
    ping("8.8.8.8", 4);

    printk("\nLooking up IP addresses:\n");
    struct zsock_addrinfo *res;
    nslookup("iot.beyondlogic.org", &res);
    print_addrinfo_results(&res);

    printk("\nConnecting to HTTP Server:\n");
    sock = connect_socket(&res, 80);
    http_get(sock, "httpbin.org", "/get");
    zsock_close(sock);
    
    // Stay connected for 30 seconds, then disconnect.
    //k_sleep(K_SECONDS(30));    
    //wifi_disconnect();

    return(0);
    
}
