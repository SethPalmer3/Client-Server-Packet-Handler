#include <bits/types/sigevent_t.h>
#include <bits/types/timer_t.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/signal.h>
#include <pthread.h>
#include <stdio.h>

void alarmhndlr(int sig){
//	signal(SIGALRM, alarmhndlr);
	printf("Hello\n");
}

void paused_t(union sigval arg){
	while (1) {
		pause();	
		printf("Stepped\n");
		signal(SIGALRM, alarmhndlr);
	}
}

void *sister_thread(void *arg){
	while (1) {
		pause();
		printf("Sister stepped\n");
	}
}

int main(int argc, char **argvP){
	
	timer_t t_id;
	struct itimerspec ts;
	struct sigevent se;
	se.sigev_notify = SIGEV_THREAD;
	se.sigev_value.sival_ptr = &t_id;
	se.sigev_notify_function = paused_t;

	ts.it_interval.tv_sec = 1;
	ts.it_interval.tv_nsec = 0;
	ts.it_value = ts.it_interval;
	timer_create(CLOCK_REALTIME, &se, &t_id);
	timer_settime(t_id, 0, &ts, NULL);

	while (1) {
		pause();
		printf("Main stepped\n");
	}
}
