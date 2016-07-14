
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/wait.h>
#include <time.h>

#include "config.h"
#include "main.h"
#include "networking.h"
#include "daemon_glue.h"

void signal_handler(int sig, siginfo_t * si, void *context)
{
    int status;
    pid_t pid;
    int i;
    int found = 0;
    sigset_t x;


    if (sig == SIGCHLD) {
        sigemptyset (&x);
        sigaddset(&x, SIGCHLD);
        sigprocmask(SIG_BLOCK, &x, NULL);

        if (si->si_code == CLD_EXITED || si->si_code == CLD_KILLED) {
            while (1) {
                pid = waitpid(-1, &status, WNOHANG);
                printf("sigchld %d\n", pid);
                if (pid > 0) {
                    for (i = 0; i < num_daemons; i++) {
                        if (d[i].client_pid == pid) {
                            found = 1;
                            // close connection with client
                            close(d[i].client_fd);
#ifdef CONFIG_DEBUG
                            printf("pid %d exited, closed fd %d.\n", pid, d[i].client_fd);
#endif
                            d[i].client_fd = -1;
                            d[i].client_pid = -1;
                            if (d[i].status == S_BUSY) {
                                d[i].status = S_AVAILABLE;
                                all_busy = 0;
                            }
                            d[i].time = 0;
                        } else if (d[i].daemon_pid == pid) {
                            found = 1;
                            fprintf(stderr, "daemon d[%d] has died\n", i);
                            st.daemon_deaths++;
                            d[i].daemon_pid = -1;
                            d[i].status = S_DEAD;
                            if (d[i].client_pid != -1) {
                                kill(d[i].client_pid, SIGKILL);
                            }
                        }
                    }
                } else if (pid == 0) {
                    sigprocmask(SIG_UNBLOCK, &x, NULL);
                    return;
                }
            }
        }
        if (!found) {
            fprintf(stderr, "unknown child\n");
        }
        sigprocmask(SIG_UNBLOCK, &x, NULL);
    } else if (sig == SIGINT) {
        /*
           for (i=0; i<num_daemons; i++) {
           if (d[0].client_pid != -1) {
           kill(d[0].client_pid, SIGKILL);
           }
           if (d[0].daemon_pid != -1) {
           kill(d[0].daemon_pid, SIGKILL);
           }
           }
         */
        //free(daemon_array);
        //free(d);
    } else if (sig == SIGUSR1) {
        update_status(NULL, NULL);
        fprintf(stdout, "queries total   %lu\n", st.queries_total);
        fprintf(stdout, "queries replied %lu\n", st.queries_replied);
        fprintf(stdout, "queries failed  %lu\n", st.queries_failed);
        fprintf(stdout, "queries delayed %lu\n", st.queries_delayed);
        fprintf(stdout, "daemon spawns   %lu\n", st.daemon_spawns);
        fprintf(stdout, "daemon deaths   %lu\n", st.daemon_deaths);
        fprintf(stdout, "daemon respawns %lu\n", st.daemon_respawns);
        fprintf(stdout, "daemon S_AVAILABLE %d\n", st.d_avail);
        fprintf(stdout, "daemon S_BUSY   %d\n", st.d_busy);
        fprintf(stdout, "daemon S_DEAD   %d\n", st.d_dead);
        fprintf(stdout, "uptime   %lu\n", time(NULL) - st.started);
    } else if (sig == SIGUSR2) {
        for (i = 0; i < num_daemons; i++) {
            fprintf(stdout, "d[%d].status = %d\n",i, d[i].status);
            fprintf(stdout, "d[%d].producer_pipe_fd[PIPE_END_WRITE] = %d\n", i,
                d[i].producer_pipe_fd[PIPE_END_WRITE]);
            fprintf(stdout, "d[%d].consumer_pipe_fd[PIPE_END_READ] = %d\n", i,
                d[i].consumer_pipe_fd[PIPE_END_READ]);
            fprintf(stdout, "d[%d].daemon_pid = %d\n",i, d[i].daemon_pid);
            fprintf(stdout, "d[%d].client_pid = %d\n",i, d[i].client_pid);
            fprintf(stdout, "d[%d].client_fd = %d\n",i, d[i].client_fd);
            fprintf(stdout, "\n");
        }
    }
}

void parse_options(int argc, char **argv)
{
    static const char short_options[] = "hed:i:I:p:m:";
    static const struct option long_options[] = {
        {.name = "help",.val = 'h'},
        {.name = "daemon",.has_arg = 1,.val = 'd'},
        {.name = "ipv4",.has_arg = 1,.val = 'i'},
        {.name = "ipv6",.has_arg = 1,.val = 'I'},
        {.name = "port",.has_arg = 1,.val = 'p'},
        {.name = "num-daemons",.has_arg = 1,.val = 'n'},
        {.name = "debug",.val = 'e'},
        {0, 0, 0, 0}
    };
    int option;

    // default values
    daemon_str = "squidGuard -c sg/adblock.conf";
    ip4 = "127.0.0.1";
    ip6 = "";
    port = 9991;
    num_daemons = 4;
    debug = 0;

    while ((option = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
        switch (option) {
        case 'h':
            fprintf(stdout, "Usage: url_checkd [OPTION]\n\n");
            fprintf(stdout,
                    "Mandatory arguments to long options are mandatory for short options too.\n");
            fprintf(stdout,
                    "  -h, --help              this help\n"
                    "  -d, --daemon=NAME       daemon to be run\n"
                    "                               (default '%s')\n"
                    "  -i, --ipv4=IP           IPv4 used for listening for connections\n"
                    "                               (default '%s')\n"
                    "  -I, --ipv6=IP           IPv6 used for listening for connections\n"
                    "                               (default '%s')\n"
                    "  -p, --port=NUM          port used\n"
                    "                               (default '%d')\n"
                    "  -n, --num-daemons=NUM   number of daemons accepted\n"
                    "                               (default '%d') - not implemented\n",
                    daemon_str, ip4, ip6, port, num_daemons);
            exit(EXIT_SUCCESS);
            break;
        case 'd':
            daemon_str = optarg;
            break;
        case 'i':
            ip4 = optarg;
            break;
        case 'I':
            ip6 = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            if (port < 1 || port > 65535) {
                fprintf(stderr, "invalid port value\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'n':
            num_daemons = atoi(optarg);
            if (num_daemons < 1 || num_daemons > MAX_DAEMONS) {
                fprintf(stderr, "invalid num_daemons value\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'b':
            break;
        case 't':
            break;
        case 'e':
            // not implemented
            break;
        default:
            fprintf(stderr, "unknown option: %c\n", option);
            exit(EXIT_FAILURE);
        }
    }

}

int main(int argc, char **argv)
{
    int i;

    char *buf;
    char *p;
    int elem = 0;
    struct sigaction sa;

    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(&st, 0, sizeof(st));

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigaction(SIGCHLD, &sa, NULL);
    //sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    st.started = time(NULL);

    parse_options(argc, argv);

    buf = strdup(daemon_str);
    p = strtok(buf, " ");
    daemon_array = (char **)malloc(strlen(daemon_str) + 2 * sizeof(char));
    while (p) {
        daemon_array[elem] = p;
        p = strtok(NULL, " ");
        elem++;
    }
    daemon_array[elem] = 0;

    d = (daemon_t *) malloc(num_daemons * sizeof(daemon_t));
    for (i = 0; i < num_daemons; i++) {
        d[i].status = S_NULL;
        d[i].daemon_pid = -1;
        d[i].client_pid = -1;
        d[i].client_fd = -1;
        d[i].time = 0;
    }

    for (i = 0; i < num_daemons; i++) {
        spawn(&d[i]);
    }

    srand(time(NULL));
    usleep(200000);

    // networking loop
    network_glue();

    //wait(NULL);

    free(buf);
    free(daemon_array);
    free(d);

    return EXIT_SUCCESS;
}
