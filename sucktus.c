/* made by profil 2011-12-29.
**
** Compile with:
** gcc -Wall -pedantic -lX11 status.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <X11/Xlib.h>
#include <pulse/pulseaudio.h>

#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

static Display *dpy;
static pa_mainloop_api *mainloop_api = NULL;
static char volume[PA_CVOLUME_SNPRINT_MAX];

void setstatus(char *str) {
    XStoreName(dpy, DefaultRootWindow(dpy), str);
    XSync(dpy, False);
}

static void context_drain_complete(pa_context *c, void *userdata) {
    pa_context_disconnect(c);
}

static void volume_cb(pa_context *c, const pa_sink_info *i, int is_last, void *userdata) {
    pa_operation *o;
    char *v = userdata;

    if (i->mute) {
        strncpy(v, "off ", 4);
    } else {
        char tmp[PA_CVOLUME_SNPRINT_MAX];
        char *pch;

        pa_cvolume_snprint(tmp, sizeof(tmp), &i->volume);
        pch = strpbrk(tmp, ":");
        pch+=2;

        strncpy(v, pch, 4);
    }

    if (!(o = pa_context_drain(c, context_drain_complete, NULL)))
        pa_context_disconnect(c);
    else
        pa_operation_unref(o);
}

static void context_state_callback(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            pa_operation_unref(pa_context_get_sink_info_by_index(c, 0, volume_cb, userdata));
            break;

        case PA_CONTEXT_TERMINATED:
            mainloop_api->quit(mainloop_api, 0);
            break;

        case PA_CONTEXT_FAILED:
            mainloop_api->quit(mainloop_api, -1);
            break;

        default:
            ;
    }
}

char *getvolume() {
    pa_mainloop *m = NULL;
    pa_context *context = NULL;
    int ret = 0;
    
    if (!(m = pa_mainloop_new()))
        goto quit;

    mainloop_api = pa_mainloop_get_api(m);
    pa_signal_init(mainloop_api);

    if (!(context = pa_context_new(mainloop_api, NULL))) 
        goto quit;

    pa_context_set_state_callback(context, context_state_callback, &volume);
    if (pa_context_connect(context, NULL, 0, NULL) < 0)
        goto quit;

    if (pa_mainloop_run(m, &ret) < 0)
        goto quit;

quit:
    if (context)
        pa_context_unref(context);

    if (m) {
        pa_signal_done();
        pa_mainloop_free(m);
    }

    return volume;
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
    if(fd == NULL) {
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

    if((buf = malloc(sizeof(char)*65)) == NULL) {
        fprintf(stderr, "Cannot allocate memory for buf.\n");
        exit(1);
    }
    result = time(NULL);
    resulttm = localtime(&result);
    if(resulttm == NULL) {
        fprintf(stderr, "Error getting localtime.\n");
        exit(1);
    }
    if(!strftime(buf, sizeof(char)*65-1, "%a %b %d %H:%M:%S", resulttm)) {
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
    
    if(perc > 100)
        return 100;
    else
        return perc;
}

int ischarging()
{
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

char *getaddress(char *interface)
{
    struct ifaddrs *ifaddr, *ifa, *ifinal;
    int family;
    char host[INET6_ADDRSTRLEN];
    char *buf;
    enum type connection_type = NONE;

    if ((buf = malloc(200)) == NULL) {
        fprintf(stderr, "Cannot allocate memory for buf.\n");
        exit(1);
    }

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

    if((status = malloc(200)) == NULL)
        exit(1);
    
    for (;;sleep(1)) {
//        getvolume();
        datetime = getdatetime();
        mem = getmeminfo();
        bat0 = getbattery();
        address = getaddress("wlan0");

        if(ischarging())
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
