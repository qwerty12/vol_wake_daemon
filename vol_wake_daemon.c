#define _GNU_SOURCE 1

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>

#include <asm/bitsperlong.h>

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>

#include <linux/input.h>
#include <linux/ioprio.h>
#include <linux/uinput.h>
#include <linux/sched/types.h>

#include "BinderGlue.h"
#include "IsInteractive.h"

#define SINGLETON_NAME "vol_wake_daemon#6CDB7CC6-4DAC-4fcf-B81B-48BCDAD85DED"

static int g_verbose   = 0;
static int g_foreground = 0;

static char g_vol_dev[PATH_MAX];
static volatile sig_atomic_t g_running = 1;

#define inline_force __attribute__((always_inline)) inline

#define log_msg(...) do { if (__predict_false(g_foreground)) __log_msg(__VA_ARGS__); } while (0)
#define log_verbose(...) do { if (__predict_false(g_verbose && g_foreground)) __log_msg(__VA_ARGS__); } while (0)

static __attribute__((noinline)) void __log_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static void on_signal(__unused const int sig)
{
    g_running = 0;
}

#define KEYBITS_WORDS howmany(KEY_MAX + 1, __BITS_PER_LONG)

static inline_force int count_bits_set(const unsigned long *bits, const size_t nwords)
{
    int count = 0;
    for (size_t i = 0; i < nwords; ++i)
        count += __builtin_popcountl(bits[i]);
    return count;
}

static int open_volume_key_device(const char *vol_name)
{
    const char *device_path = "/dev/input";

    char *filename;
    DIR *dir;
    struct dirent *de;

    char name[80];
    int best_fd = -1, best_count = -1;
    unsigned long keybits[KEYBITS_WORDS];
    char best_path[sizeof(g_vol_dev)];

    if (!(dir = opendir(device_path)))
        return -1;

    if (vol_name)
        name[sizeof(name) - 1] = '\0';

    strlcpy(g_vol_dev, device_path, sizeof(g_vol_dev));
    filename = g_vol_dev + strlen(device_path);
    *filename++ = '/';
    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.' &&
           (de->d_name[1] == '\0' ||
            (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        
        if (__predict_false(strncmp(de->d_name, "event", 5)))
            continue;

        strlcpy(filename, de->d_name, sizeof(g_vol_dev) - strlen(device_path) - 1);

        const int fd = open(g_vol_dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            log_verbose("open(%s) for reading failed: %s", g_vol_dev, strerror(errno));
            continue;
        }

        if (vol_name) {
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 1 && strcmp(name, vol_name) == 0) {
                best_fd = fd;
                break;
            }
        } else {
            memset(keybits, 0, sizeof(keybits));
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
                const int has_volup = (keybits[KEY_VOLUMEUP / __BITS_PER_LONG] >>
                                        (KEY_VOLUMEUP % __BITS_PER_LONG)) & 1;
                if (has_volup) {
                    const int n = count_bits_set(keybits, KEYBITS_WORDS);
                    log_verbose("%s supports KEY_VOLUMEUP, %d total keys", g_vol_dev, n);
                    if (best_count < 0 || n < best_count) {
                        best_count = n;
                        if (best_fd != -1) close(best_fd);
                        best_fd = fd;
                        strlcpy(best_path, g_vol_dev, sizeof(best_path));
                        continue;
                    }
                }
            }
        }

        close(fd);
    }

    if (!vol_name && best_fd != -1)
        strlcpy(g_vol_dev, best_path, sizeof(g_vol_dev));

    closedir(dir);
    return best_fd;
}

static void parse_cpuset_cpus(char *cpus, cpu_set_t *cpu_set)
{
    /* Copyright 2006, The Android Open Source Project
     * Licensed under the Apache License, Version 2.0 */
    char *saveptr;
    char *cpu_range = strtok_r(cpus, ",", &saveptr);

    while (cpu_range) {
        unsigned int start = 0, end = 0;
        const int matched = sscanf(cpu_range, "%u-%u", &start, &end);
        cpu_range = strtok_r(NULL, ",", &saveptr);

        if (start >= CPU_SETSIZE) {
            log_verbose("parse_cpuset_cpus: ignoring CPU number larger than %d", CPU_SETSIZE);
            continue;
        }
        if (end >= CPU_SETSIZE)
            end = CPU_SETSIZE - 1;

        if (matched == 1) {
            CPU_SET(start, cpu_set);
        } else if (matched == 2) {
            for (unsigned int i = start; i <= end; ++i)
                CPU_SET(i, cpu_set);
        } else {
            log_verbose("parse_cpuset_cpus: failed to match \"%s\"", cpu_range);
        }
    }
}

static void set_background_affinity(cpu_set_t *cpu_set)
{
    CPU_ZERO(cpu_set);

    FILE *file = fopen("/dev/cpuset/background/cpus", "re");
    if (file) {
        char line[128];
        if (fgets(line, sizeof(line), file)) {
            const size_t len = strlen(line);
            if ((len > 0 && line[len - 1] == '\n') || fgetc(file) == EOF)
                parse_cpuset_cpus(line, cpu_set);
            else
                log_verbose("background cpuset line too long, ignoring");
        } else {
            log_verbose("failed to read background cpuset");
        }
        fclose(file);
    }

    if (CPU_COUNT(cpu_set) < 2) {
        CPU_ZERO(cpu_set);
        CPU_SET(0, cpu_set);
        CPU_SET(1, cpu_set);
    }
}

static void apply_low_priority(void)
{
    cpu_set_t cpu_set;
    set_background_affinity(&cpu_set);

    if (setpriority(PRIO_PROCESS, 0, 19) < 0)
        log_verbose("setpriority failed: %s", strerror(errno));

    struct sched_param sp = { 0 };
    if (__predict_false(sched_setscheduler(0, SCHED_IDLE, &sp) < 0)) {
        log_verbose("sched_setscheduler(SCHED_IDLE) failed, trying SCHED_BATCH: %s", strerror(errno));
        sched_setscheduler(0, SCHED_BATCH, &sp);
    }

    if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0)
        log_verbose("sched_setaffinity failed: %s", strerror(errno));

    if (__predict_false(syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)) < 0)) {
        log_verbose("ioprio_set failed, trying best effort: %s", strerror(errno));
        syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 7));
    }

    if (prctl(PR_SET_TIMERSLACK, 40000000UL, 0, 0, 0) < 0)
        log_verbose("prctl(PR_SET_TIMERSLACK) failed: %s", strerror(errno));

#if 0
    if (access("/proc/sys/kernel/sched_util_clamp_min", F_OK) == 0) {
        struct sched_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size = sizeof(attr);
        attr.sched_flags = SCHED_FLAG_UTIL_CLAMP | SCHED_FLAG_KEEP_ALL;
        attr.sched_util_min = 0;   // boost = 0
        attr.sched_util_max = 307; // ~30% of 1024, matches PerfClamp
        if (syscall(SYS_sched_setattr, 0 /* self */, &attr, 0) < 0)
            log_verbose("sched_setattr(uclamp.max) unavailable: %s", strerror(errno));
    }
#endif
}

static int acquire_singleton_lock(void)
{
    const size_t name_len = sizeof(SINGLETON_NAME) - 1;
    struct sockaddr_un addr;

    _Static_assert(name_len <= sizeof(((struct sockaddr_un *)0)->sun_path) - 1, "singleton lock name too long");

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        __log_msg("socket() for singleton lock failed: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, SINGLETON_NAME, name_len);
    const socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);

    if (bind(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        if (errno == EADDRINUSE)
            __log_msg("another instance is already running");
        else
            __log_msg("bind() for singleton lock failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void daemonise(const int keep_fd)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        const rlim_t max_fd = (rl.rlim_max == RLIM_INFINITY) ? 1024 : rl.rlim_max;
        for (rlim_t i = STDERR_FILENO + 1; i < max_fd; ++i) {
            if (__predict_false((int)i == keep_fd))
                continue;
            close((int)i);
        }
    }

    for (int i = 1; i < _NSIG; ++i)
        signal(i, SIG_DFL);
    sigset_t empty_set;
    sigemptyset(&empty_set);
    sigprocmask(SIG_SETMASK, &empty_set, NULL);

    pid_t pid = fork();
    if (pid < 0) { __log_msg("fork: %m"); exit(EXIT_FAILURE); }
    if (pid > 0) _exit(EXIT_SUCCESS);

    if (setsid() < 0) { __log_msg("setsid: %m"); exit(EXIT_FAILURE); }
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) _exit(EXIT_SUCCESS);

    umask(0);

    const int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) exit(EXIT_FAILURE);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (__predict_false(devnull > STDERR_FILENO)) close(devnull);

    if (__predict_false(chdir("/") < 0)) exit(EXIT_FAILURE);
}

static inline_force int uinput_emit(struct input_event *ev, const int fd, const int type, const int code, const int val)
{
    ev->type = type;
    ev->code = code;
    ev->value = val;
    /* timestamp values below are ignored */
    ev->input_event_sec = ev->input_event_usec = 0;
    return __predict_true(write(fd, ev, sizeof(struct input_event)) == sizeof(struct input_event)) ? 1 : 0;
}

static int uinput_init(const int allowed_keycode)
{
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (__predict_false(fd < 0)) {
        log_msg("open /dev/uinput: %m");
        return -1;
    }

    if (__predict_false(
        ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, allowed_keycode) < 0
    )) {
        log_verbose("UI_SET_{EV,KEY}BIT: %m");
        close(fd);
        return -1;
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor = 0x7239;
    usetup.id.product = 0x3666;
    strlcpy(usetup.name, "vol_wake_daemon", sizeof(usetup.name));

    if (__predict_false(ioctl(fd, UI_DEV_SETUP, &usetup) < 0)) {
        log_msg("UI_DEV_SETUP: %m");
        close(fd);
        return -1;
    }

    if (__predict_false(ioctl(fd, UI_DEV_CREATE) < 0)) {
        log_msg("UI_DEV_CREATE: %m");
        close(fd);
        return -1;
    }

    usleep(150 * 1000);

    return fd;
}

static inline_force void wakeup_screen(const int uinput_fd)
{
    static struct input_event ev;
    if (__predict_true(uinput_emit(&ev, uinput_fd, EV_KEY, KEY_WAKEUP, 1)))
        (void)uinput_emit(&ev, uinput_fd, EV_SYN, SYN_REPORT, 0);

    (void)uinput_emit(&ev, uinput_fd, EV_KEY, KEY_WAKEUP, 0);
    (void)uinput_emit(&ev, uinput_fd, EV_SYN, SYN_REPORT, 0);
}

static inline_force int is_screen_on(void)
{
    const int interactive = IsInteractive();
    if (__predict_true(interactive != -1))
        return interactive;

    log_verbose("IsInteractive() unavailable; assuming screen may be off");
    return 0;
}

static void usage(const char *argv0)
{
    __log_msg(
        "usage: %s [-f] [-v] [--vol-name NAME]\n"
        "  -f              stay in foreground, log to stderr (default: daemonise)\n"
        "  -v              verbose logging\n"
        "  --vol-name      evdev name (not path) for the volume keys (default: auto-detect by\n"
        "                  finding the KEY_VOLUMEUP-supporting device with the fewest keys)",
        __predict_true(argv0) ? basename(argv0) : "");
}

int main(int argc, char **argv)
{
    char *vol_name = NULL;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-f") == 0) {
            g_foreground = 1;
        } else if (strcmp(arg, "-v") == 0) {
            g_verbose = 1;
        } else if (strcmp(arg, "--vol-name") == 0) {
            if (++i >= argc) { usage(argv[0]); return 1; }
            vol_name = argv[i];
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    const int singleton_fd = acquire_singleton_lock();
    if (singleton_fd < 0)
        return EXIT_FAILURE;

    if (__predict_true(!g_foreground)) {
        apply_low_priority();
        daemonise(singleton_fd);
    }

    int ret = EXIT_SUCCESS;

    const int vol_fd = open_volume_key_device(vol_name);
    const int binder_fd = vol_fd > -1 ? SetupBinder() : -1;
    int uinput_fd = -1;
    if (vol_fd < 0) {
        if (!vol_name)
            log_msg("no evdev device advertises KEY_VOLUMEUP support; pass --vol-name explicitly");
        else
            log_msg("could not find an input device matching \"%s\"", vol_name);
        ret = EXIT_FAILURE;
        goto end;
    }
    if (__predict_false(binder_fd < 0)) {
        if (__predict_true(binder_fd != -1))
            log_msg("error setting up Binder polling: %s", strerror(-binder_fd));
        else
            log_msg("invalid Binder FD (or maybe EPERM)");
        ret = EXIT_FAILURE;
        goto end;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (__predict_false(!ConnectPowerService())) {
        log_msg("failed to connect to the power service via Binder");
        ret = EXIT_FAILURE;
        goto end;
    }

    if (__predict_false((uinput_fd = uinput_init(KEY_WAKEUP)) < 0)) {
        ret = EXIT_FAILURE;
        goto end;
    }

    log_verbose("vol=%s pid=%ld", g_vol_dev, (long)getpid());

    struct pollfd pfds[2];
    pfds[0].fd = vol_fd;    pfds[0].events = POLLIN; pfds[0].revents = 0;
    pfds[1].fd = binder_fd; pfds[1].events = POLLIN; pfds[1].revents = 0;

    while (g_running) {
        const int nready = poll(pfds, 2, -1);

        if (__predict_false(nready < 0)) {
            if (errno == EINTR) continue;
            log_msg("poll failed, exiting: %s", strerror(errno));
            ret = EXIT_FAILURE;
            goto end;
        }

        if (__predict_false(pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            log_msg("vol_fd error (revents=0x%x), exiting", pfds[0].revents);
            ret = EXIT_FAILURE;
            goto end;
        }

        if (__predict_false(pfds[1].revents & POLLIN))
            OnBinderReadReady();

        if (__predict_true(pfds[0].revents & POLLIN)) {
            struct input_event ev;
            ssize_t n;
            while (__predict_true((n = read(vol_fd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev))) {
                if (ev.type == EV_KEY && ev.code == KEY_VOLUMEUP && ev.value == 1) {
                    if (is_screen_on()) {
                        log_verbose("volume-up down: screen already on, skipping wake");
                    } else {
                        wakeup_screen(uinput_fd);
                        log_msg("volume-up down: waking screen");
                    }
                }
            }

            if (__predict_false(n == 0)) {
                log_msg("vol_fd hit EOF, exiting");
                ret = EXIT_FAILURE;
                goto end;
            }

            if (__predict_false(n < 0 && errno != EAGAIN)) {
                log_msg("read failed, exiting: %s", strerror(errno));
                ret = EXIT_FAILURE;
                goto end;
            }
        }
    }

    log_verbose("exiting");
end:
    if (singleton_fd != -1)
        close(singleton_fd);
    if (uinput_fd != -1) {
        ioctl(uinput_fd, UI_DEV_DESTROY);
        close(uinput_fd);
    }
    if (binder_fd != -1)
        close(binder_fd);
    if (vol_fd != -1)
        close(vol_fd);
    return ret;
}
