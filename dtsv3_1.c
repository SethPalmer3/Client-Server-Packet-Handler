#include <BXP/bxp.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include "bxp/bxp.h"
#include "ADTs/prioqueue.h"
#include "ADTs/cskmap.h"


#define UNUSED __attribute__((unused))

#define PORT 19999

#define SERVICE "DTS"

const int max_vals = 6;

time_t last_epoch;

// list of requests
const PrioQueue *rqts;
pthread_mutex_t pq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pq_cond = PTHREAD_COND_INITIALIZER;

// list of canceld requests
const CSKMap *cncls;
pthread_mutex_t cncls_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cncls_cond = PTHREAD_COND_INITIALIZER;

int next_word_index(char *start){
	int spot = 0;
	while (start[spot] != '|' && start[spot] != '\0' && start[spot] != '\n') {
		spot++;
	}
	return spot + 1;
}

char *get_request_arg(char *req, int ind){
	int spot = 0;
	for (int i = 0; i < ind; i++) {
		spot += next_word_index(req + spot);
	}
	return req + spot;
}

int get_param_values(char *vals_start){
	/* obtains all params values and returns the spots and number of values */
	int num_vals = 0;
	int spot = 0;
	while (vals_start[spot] != '\0' && vals_start[spot] != '\n') {
		spot += next_word_index(vals_start + spot);
		num_vals++;
	}
	return num_vals;

}

int length_arg(char *arg_start){
	int spot = 0;
	while (arg_start[spot] != '|' && arg_start[spot] != '\0' && arg_start[spot] != '\n') {
	spot++;
	}
	return spot;
}

int length_arg_ind(char *request, int ind){
	char *p = get_request_arg(request, ind);
	return length_arg(p);
}

int range_strtoint(char *start, int chars){
	int ret = 0;
	int place = 1;
	for (int s = chars; s >= 0; s--) {
		ret += (int)(start[s] - '0') * place;
		place *= 10;
	}
	return ret;
}

unsigned long range_strtounsignlong(char *start, int chars){
	unsigned long ret = 0;
	unsigned long place = 1;
	for (int s = chars; s >= 0; s--) {
		ret += (unsigned long)(start[s] - '0') * place;
		place *= 10;
	}
	return ret;
}


int validrequest(char *req){
	int num_vals;
	if (strncmp(req, "OneShot", 7) == 0) {
		num_vals = get_param_values(req + 8);
		return num_vals == 6;
	}else if(strncmp(req, "Repeat", 6) == 0){
		num_vals = get_param_values(req + 7);
		return num_vals == 6;
	}else if (strncmp(req, "Cancel", 6) == 0) {
		num_vals = get_param_values(req + 7);
		return num_vals == 1;
	}else{
		return 0;
	}
}

typedef struct request_block Request;

struct request_block{
	char *request_type;
	char *host;
	char *service;
	unsigned long svid;
	unsigned long clid;
	double time;
	double add_info_time; // Either number of micro seconds or number of repeats
	unsigned short port;
	time_t add_time;
	int cancel;
};

static unsigned long next_server_id = 0;

Request *fill_info(char *req_str, unsigned long svid){

	char *req_typ = strndup(req_str, length_arg(req_str));
	char *host = strndup(get_request_arg(req_str, 4), length_arg_ind(req_str, 4));
	char *service = strndup(get_request_arg(req_str, 5), length_arg_ind(req_str, 5));

	Request *new_req = (Request *)malloc(sizeof(Request));

	new_req->request_type = req_typ;
	new_req->service = service;
	new_req->host = host;
	new_req->clid = (unsigned long)atol(get_request_arg(req_str, 1));
	new_req->time = (double)atof(get_request_arg(req_str, 2));
	new_req->add_info_time = (double)atof(get_request_arg(req_str, 3));
	new_req->port = (unsigned short)atoi(get_request_arg(req_str, 6));
	new_req->svid = svid;
	new_req->cancel = 0;
	time(&new_req->add_time);

	if (strcmp(req_typ, "Repeat") == 0) {new_req->time /= 1000;} // convert milliseconds to seconds
	if (strcmp(req_typ, "Repeat") == 0 && new_req->add_info_time == 0) {--new_req->add_info_time;} // make repeats -1 to indicate repeat forever
	if (strcmp(req_typ, "OneShot") == 0){new_req->add_info_time /= 10000000;} // conver microseconds to seconds 
	return new_req;
}

int cli_cmp(void *a, void *b){
	/* compare client a to epoch b */
	time_t a_t = (time_t)a;
	time_t b_t = (time_t)b;
	return a_t - b_t;
}

void cli_free(void *client){
	Request *c = (Request*)client;
	free(c->request_type);
	free(c->service);
	free(c->host);
	free(client);
}

void prio_free(void *pr){
	free(pr);
}

int blocking_q_insert(const PrioQueue *q, void* p, void *val, pthread_mutex_t *m, pthread_cond_t *c){
	int res;
	pthread_mutex_lock(m);
	res = q->insert(q, p, val);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
}

int blocking_q_remove(const PrioQueue *q, void **p, void **val, pthread_mutex_t *m, pthread_cond_t *c){
	int res;
	pthread_mutex_lock(m);
	res = q->removeMin(q, p, val);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
}

long blocking_q_size(const PrioQueue *q, pthread_mutex_t *m, pthread_cond_t *c){
	long res;
	pthread_mutex_lock(m);
	res = q->size(q);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
}


int blocking_map_put(const CSKMap *map, char *k, void *val, pthread_mutex_t *m, pthread_cond_t *c){
	int res;
	pthread_mutex_lock(m);
	res = map->put(map, k, val);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
}

int blocking_map_get(const CSKMap *map, char *k, void **val, pthread_mutex_t *m, pthread_cond_t *c){
	int res;
	pthread_mutex_lock(m);
	res = map->get(map, k, val);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
}

int blocking_map_remove(const CSKMap *map, char *k, pthread_mutex_t *m, pthread_cond_t *c){
	int res;
	pthread_mutex_lock(m);
	res = map->remove(map, k);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
}

void changetimer(time_t sec, suseconds_t usec){
	struct itimerval *t = (struct itimerval*)malloc(sizeof(struct itimerval));
	t->it_interval.tv_sec = sec;
	t->it_interval.tv_usec = usec;
	t->it_value = t->it_interval;
	setitimer(ITIMER_REAL, t, NULL); // interval timer
	free(t);
}

void *serverthread(UNUSED void *arg){
	BXPEndpoint client;
	BXPService bxps;
	unsigned short port = PORT;
	int ifEncrypted = 1;
	char *service = SERVICE;
	unsigned len;
	char *qry = (char*)malloc(BUFSIZ);
	char *resp = (char*)malloc(BUFSIZ);
	Request *tmp;
	char *key;

	assert(bxp_init(port, ifEncrypted));
	bxps = bxp_offer(service);
	if(bxps==NULL){
		free(qry);
		free(resp);
		exit(EXIT_FAILURE);
	}

	while ((len = bxp_query(bxps, &client, qry, BUFSIZ)) > 0) {
		// determine if the request is valid at all
		// determine the request from client
		printf("%s", qry);
			
		if(!validrequest(qry)){
			sprintf(resp, "0");
			bxp_response(service, &client, resp, strlen(resp) + 1);
		}else if (strncmp(qry, "Cancel", 6) == 0) {
			key = strdup(get_request_arg(qry, 1));
			key[length_arg_ind(qry, 1)] = '\0';
			if(blocking_map_get(cncls, key, (void**)&tmp, &cncls_lock, &cncls_cond)){
				tmp->cancel = 1;	
				sprintf(resp, "1");
				bxp_response(service, &client, resp, strlen(resp) + 1);
			}else{
				sprintf(resp, "0");
				bxp_response(service, &client, resp, strlen(resp) + 1);
			} 
			free(key);
		}else{
			unsigned long c_svid = next_server_id++;
			char key[BUFSIZ];
			sprintf(key, "%ld", c_svid);
			tmp = fill_info(qry, c_svid);

			double *prio = (double*)malloc(sizeof(double));
			*prio = difftime(clock(), last_epoch);

			rqts->insert(rqts, prio, tmp); // Put request on request priority
			blocking_q_insert(rqts, prio, tmp, &pq_lock, &pq_cond);

			blocking_map_put(cncls, key, (void*)tmp, &cncls_lock, &cncls_cond); // Add Request to map to cancel possibly later

			sprintf(resp, "1%s\n", key);
			bxp_response(service, &client, resp, strlen(resp) + 1);
		}
	}
	
	free(qry);
	free(resp);
	rqts->destroy(rqts);
	cncls->destroy(cncls);
	return NULL;
}

void alarmhandlr(UNUSED int sig){
	signal(SIGALRM, alarmhandlr);
	Request *holder = NULL;
	time_t prio;
	
	if (blocking_q_size(rqts, &pq_lock, &pq_cond) == 0) {
		return;
	}
	time_t current; // current time
	time(&current);
	while (blocking_q_remove(rqts, (void**)&prio, (void**)&holder, &pq_lock, &pq_cond) && difftime(current, holder->add_time) >= holder->time) { // See if theres a request in queue and if their time has elapsed
		if (holder->cancel) {
			char* key = (char*)malloc(sizeof(char)*BUFSIZ);
			sprintf(key, "%ld", holder->svid);
			//blocking_map_remove(cncls, key, &cncls_lock, &cncls_cond);
			cli_free(holder);
			free(key);
			holder = NULL;
			time(&current);
			continue;
		}
		printf("Event fired: %lu|%s|%s|%u\n", holder->clid, holder->host, holder->service, holder->port);
		if (strcmp(holder->request_type, "Repeat") == 0 && --holder->add_info_time != 0) { // if it is a repeat request put back if valid
			prio = difftime(current, last_epoch);
			time(&holder->add_time);
			blocking_q_insert(rqts, (void*)prio, holder, &pq_lock, &pq_cond);
		}else{
			char* key = (char*)malloc(sizeof(char)*BUFSIZ);
			sprintf(key, "%ld", holder->svid);
			//blocking_map_remove(cncls, key, &cncls_lock, &cncls_cond);
			cli_free(holder);
			free(key);
			holder = NULL;
		}
		time(&current);
	}

	if (holder != NULL) {
		prio = difftime(current, last_epoch);
		blocking_q_insert(rqts, (void*)prio, (void*)holder, &pq_lock, &pq_cond);
	}
	time(&last_epoch);
}

void *timer_thread(UNUSED void*a){
	struct itimerval *t = (struct itimerval*)malloc(sizeof(struct itimerval));
	t->it_interval.tv_sec = 0;
	t->it_interval.tv_usec = 500;
	t->it_value = t->it_interval;
	signal(SIGALRM, alarmhandlr); // step catcher 
	setitimer(ITIMER_REAL, t, NULL);

//	changetimer(0, 500);

	time(&last_epoch);
	while (1) { // Keep catching 
		pause();
	}
	return NULL;
}

int main(UNUSED int argc, UNUSED char **argv){
	signal(SIGALRM, SIG_IGN);
	rqts = PrioQueue_create(cli_cmp, prio_free, cli_free);
	cncls = CSKMap_create(cli_free);
	pthread_t s;	
	pthread_t t;
	pthread_create(&s, NULL, serverthread, NULL);
	pthread_create(&t, NULL, timer_thread, NULL);
	pthread_join(s, NULL);
	pthread_join(t, NULL);
	
	return 0;
}
