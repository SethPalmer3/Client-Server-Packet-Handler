/*
 * client for the DTS service
 *
 * usage: ./dts-client
 *
 * reads each line from standard input, sends it to DTS service on localhost,
 * receives the echo'd line, and writes it to standard output
 */

#include "BXP/bxp.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#define UNUSED __attribute__((unused))
#define HOST "localhost"
#define PORT 19999
#define SERVICE "DTS"

void onint(UNUSED int sig) {
    fprintf(stderr, "\nTerminated by ^C\n");
    exit(EXIT_SUCCESS);
}

int extractWords(char *buf, char *sep, char *words[]) {
    int i;
    char *p;

    for (p = strtok(buf, sep), i = 0; p != NULL; p = strtok(NULL, sep),i++)
        words[i] = p;
    words[i] = NULL;
    return i;
}

void *svcRoutine(void *args) {
    BXPService *bxps = (BXPService *)args;
    BXPEndpoint bxpep;
    char query[1024], *resp = "1";
    unsigned qlen;

    while ((qlen = bxp_query(bxps, &bxpep, query, 1024)) > 0) {
        printf("Timer event %s received\n", query);
	bxp_response(bxps, &bxpep, resp, 2);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    BXPConnection bxpc;
    char buf[250];
    Q_Decl(query,256);
    char resp[251];
    int n;
    unsigned len;
    char *host;
    char *service;
    unsigned short port;
    int opt;
    int ifEncrypt = 1;
    static char usage[] = "usage: %s [-e] [-h host] [-p port] [-s service]\n";
    struct timeval tv;
    char ipaddr[16];
    unsigned short svcPort;
    BXPService *bxps;
    pthread_t svcThread;
    int id=1000;

    host = HOST;
    service = SERVICE;
    port = PORT;
    opterr = 0; /* tells getopt to NOT print an error message */
    while ((opt = getopt(argc, argv, "eh:p:s:")) != -1)
        switch (opt) {
        case 'e':
            ifEncrypt = 1;
            break;
        case 'h':
            host = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 's':
            service = optarg;
            break;
        default:
            fprintf(stderr, usage, argv[0]);
            return EXIT_FAILURE;
        }
    if (signal(SIGINT, onint) == SIG_ERR) {
        fprintf(stderr, "Unable to establish SIGINT handler\n");
        return EXIT_FAILURE;
    }
    assert(bxp_init(0, ifEncrypt));
    bxp_details(ipaddr, &svcPort);
    /* create service to receive timer events */
    if ((bxps = bxp_offer("DTS-ALARM")) == NULL) {
        fprintf(stderr, "Unable to create BXP service to receive events\n");
	exit(EXIT_FAILURE);
    }
    if (pthread_create(&svcThread, NULL, svcRoutine, (void *)bxps)) {
        fprintf(stderr, "Unable to create thread to receive events\n");
	exit(EXIT_FAILURE);
    }
    if ((bxpc = bxp_connect(host, port, service, 0, ifEncrypt)) == NULL) {
        fprintf(stderr, "Failure to connect to %s at %s:%05u\n",
                service, host, port);
        exit(EXIT_FAILURE);
    }
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        char *w[25];
        char *sp = strrchr(buf, '\n');
	int N;
	if (sp != NULL)
            *sp = '\0';
	id += 10;
	N = extractWords(buf, " ", w);
	if (strcmp(w[0], "OneShot") == 0) {
            long secs = 5L;
            gettimeofday(&tv, NULL);
	    if (N > 1)
                sscanf(w[1], "%ld", &secs);
	    tv.tv_sec+= secs;
	    sprintf(query, "OneShot|%d|%ld|%ld|%s|DTS-ALARM|%u", id,
			    tv.tv_sec, tv.tv_usec, ipaddr, (unsigned)svcPort);
        } else if (strcmp(w[0], "Repeat") == 0) {
            long msecs = 1000L;
	    long repeats = 5L;
	    if (N > 1) {
                sscanf(w[1], "%ld", &msecs);
	        if (N > 2)
                    sscanf(w[2], "%ld", &repeats);
	    }
            sprintf(query, "Repeat|%d|%ld|%ld|%s|DTS-ALARM|%u", id, msecs,
			    repeats, ipaddr, (unsigned)svcPort);
        } else if (strcmp(w[0], "Cancel") == 0) {
            if (N != 2) {
                fprintf(stderr, "%s: Cancel requires a event id\n", argv[0]);
		continue;
	    }
	    sprintf(query, "Cancel|%s", w[1]);
	} else {
            fprintf(stderr, "%s: illegal command - %s\n", argv[0], buf);
	    continue;
	}
        n = strlen(query) + 1;
        if (! bxp_call(bxpc, Q_Arg(query), n, resp, sizeof(resp), &len)) {
            fprintf(stderr, "bxp_call() failed\n");
            break;
        }
        if (resp[0] != '1') {
            fprintf(stderr, "DTS server returned ERR\n");
            continue;
        }
	printf("local id %d registered as server id %s\n", id, resp+1);
    }
    bxp_disconnect(bxpc);
    pthread_join(svcThread, NULL);
    return EXIT_SUCCESS;
}
