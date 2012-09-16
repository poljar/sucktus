#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <X11/Xlib.h>

#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

static Display *dpy;

void *xmalloc(size_t size) {
    void *p;

    if (!(p = malloc(size))) {
        fprintf(stderr, "Not enough memory!\n");
        raise(SIGQUIT);
    }

    return p;
}

void setstatus(char *str) {
    XStoreName(dpy, DefaultRootWindow(dpy), str);
    XSync(dpy, False);
}

float getmeminfo() {
    FILE *fd;
    double perc;

    struct {
        long total;
        long free;
        long cached;
        long buffers;
        long used;
    } meminfo;

    fd = fopen("/proc/meminfo", "r");
    if (fd == NULL) {
        printf("canot opent file\n");
        exit(1);
    }

    fscanf(fd, "MemTotal: %ld kB", &meminfo.total);
    fscanf(fd, " MemFree: %ld kB", &meminfo.free);
    fscanf(fd, " Buffers: %ld kB", &meminfo.buffers);
    fscanf(fd, " Cached: %ld kB", &meminfo.cached);
    fclose(fd);

    meminfo.used = meminfo.total - meminfo.free - meminfo.buffers - meminfo.cached;
    perc = (double) meminfo.used / meminfo.total;
    perc *= 100;

    return perc;
}

char *getdatetime() {
    char *buf;
    time_t result;
    struct tm *resulttm;

    buf = xmalloc(70);
    result = time(NULL);
    resulttm = localtime(&result);
    if (resulttm == NULL) {
        fprintf(stderr, "Error getting localtime.\n");
        exit(1);
    }
    if (!strftime(buf, sizeof(char)*70-1, "%a %b %d %H:%M:%S", resulttm)) {
        fprintf(stderr, "strftime is 0.\n");
        exit(1);
    }

    return buf;
}

int getbattery() {
    FILE *fd;
    int energy_now, energy_full, voltage_now, perc;

    fd = fopen("/sys/class/power_supply/BAT0/charge_now", "r");
    if (fd == NULL) {
        fprintf(stderr, "Error opening energy_now.\n");
        return -1;
    }
    fscanf(fd, "%d", &energy_now);
    fclose(fd);

    fd = fopen("/sys/class/power_supply/BAT0/charge_full", "r");
    if (fd == NULL) {
        fprintf(stderr, "Error opening energy_full.\n");
        return -1;
    }
    fscanf(fd, "%d", &energy_full);
    fclose(fd);

    fd = fopen("/sys/class/power_supply/BAT0/voltage_now", "r");
    if (fd == NULL) {
        fprintf(stderr, "Error opening voltage_now.\n");
        return -1;
    }
    fscanf(fd, "%d", &voltage_now);
    fclose(fd);

    perc = ((float)energy_now * 1000 / (float)voltage_now) * 100;
    perc /= ((float)energy_full * 1000 / (float)voltage_now);

    if (perc > 100)
        return 100;
    else
        return perc;
}

int ischarging() {
    int charging;
    FILE *fd;

    fd = fopen("/sys/class/power_supply/AC/online", "r");
    if (fd == NULL) {
        fprintf(stderr, "Error opening energy_now.\n");
        return -1;
    }

    fscanf(fd, "%d", &charging);
    fclose(fd);

    return charging;
}

enum type {
    WIRED,
    WIREDV6,
    WLAN,
    WLANV6,
    NONE
};

char *getaddress() {
    struct ifaddrs *ifaddr, *ifa, *ifinal = NULL;
    int family;
    char host[INET6_ADDRSTRLEN];
    char *buf;
    enum type connection_type = NONE;

    buf = xmalloc(200);

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) {
            if (strcmp(ifa->ifa_name, "eth0") == 0) {
                connection_type = WIRED;
                ifinal = ifa;
            }

            if (strcmp(ifa->ifa_name, "wlan0") == 0)
                if (connection_type != WIRED && connection_type != WLANV6) {
                    connection_type = WLAN;
                    ifinal = ifa;
                }
        }

        if (family == AF_INET6) {
            char prefix[7];
            strncpy(prefix, inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr, host, sizeof(host)), 6);
            if (strcmp(prefix, "fe80::") == 0)
                continue;

            if (strcmp(ifa->ifa_name, "eth0") == 0) {
                connection_type = WIREDV6;
                ifinal = ifa;
                break;
            }

            if (strcmp(ifa->ifa_name, "wlan0") == 0)
                if (connection_type != WIRED && connection_type != WLANV6) {
                    connection_type = WLANV6;
                    ifinal = ifa;
                }
        }
    }

    if (connection_type == NONE)
        snprintf(buf, 200, "lo:::1");

    else {
        switch (ifinal->ifa_addr->sa_family) {
            case AF_INET:
                inet_ntop(AF_INET, &((struct sockaddr_in *)ifinal->ifa_addr)->sin_addr, host, sizeof(host));
                break;

            case AF_INET6:
                inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ifinal->ifa_addr)->sin6_addr, host, sizeof(host));
                break;

            default:
                ;
        }
        snprintf(buf, 200, "%s:%s", ifinal->ifa_name, host);
    }

    freeifaddrs(ifaddr);

    return buf;
}

int main(void) {
    char *status;
    float mem;
    int bat0;
    char *datetime;
    char *address;
    char battext[6];

    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "Cannot open display.\n");
        return 1;
    }

    status = xmalloc(200);

    for (;;sleep(1)) {
        datetime = getdatetime();
        mem = getmeminfo();
        bat0 = getbattery();
        address = getaddress();

        if (ischarging())
            strcpy(battext, "ac:");
        else {
            if (bat0 > 70)
                strcpy(battext, "bat:");
            else if (bat0 < 40)
                strcpy(battext, "bat:");
            else
                strcpy(battext, "bat:");
        }

        snprintf(status, 200,
                "| %s | mem:%0.0f%% | %s%d%% | %s |",
                address, mem, battext, bat0, datetime);

        free(datetime);
        free(address);
        setstatus(status);
    }

    free(status);
    XCloseDisplay(dpy);

    return 0;
}
