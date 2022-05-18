#include <BXP/bxp.h>
#include <bits/types/struct_timeval.h>
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
#include "ADTs/queue.h"
#include "ADTs/cskmap.h"


#define UNUSED __attribute__((unused))

#define PORT 19999

#define USECS 1000

#define SERVICE "DTS"

const int max_vals = 6;

long time_width = 10;

// list of waiting requests
const PrioQueue *rqts;
pthread_mutex_t pq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pq_cond = PTHREAD_COND_INITIALIZER;

// list of canceld requests
const CSKMap *cncls;
pthread_mutex_t cncls_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cncls_cond = PTHREAD_COND_INITIALIZER;

// list of dispatching requests
const Queue *disp;
pthread_mutex_t disp_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t disp_cond = PTHREAD_COND_INITIALIZER;

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

int get_num_params(char *vals_start){
	/* obtains all params values and returns the spots and number of values */
	int num_vals = 0;
	int spot = 0;
	while (vals_start[spot] != '\0' && vals_start[spot] != '\n') {
		if (vals_start[spot++] == '|') {num_vals ++;}
	}
	return num_vals+1;

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
		num_vals = get_num_params(req + 8);
		return num_vals == 6;
	}else if(strncmp(req, "Repeat", 6) == 0){
		num_vals = get_num_params(req + 7);
		return num_vals == 6;
	}else if (strncmp(req, "Cancel", 6) == 0) {
		num_vals = get_num_params(req + 7);
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
	struct timeval time;
	unsigned long repeats; // Either number of micro seconds or number of repeats
	unsigned long interval;
	unsigned short port;
	int cancel;
	BXPEndpoint ep;
};

static unsigned long next_server_id = 0;

Request *fill_info(char *req_str, unsigned long svid, BXPEndpoint ep){

	char *req_typ = strndup(req_str, length_arg(req_str));
	char *host = strndup(get_request_arg(req_str, 4), length_arg_ind(req_str, 4));
	char *service = strndup(get_request_arg(req_str, 5), length_arg_ind(req_str, 5));

	Request *new_req = (Request *)malloc(sizeof(Request));

	new_req->request_type = req_typ;
	new_req->service = service;
	new_req->host = host;
	
	new_req->clid = (unsigned long)atol(get_request_arg(req_str, 1));
	new_req->svid = svid;
	if (strcmp(req_typ, "OneShot") == 0) {
		new_req->time.tv_sec = (unsigned long)atol(get_request_arg(req_str, 2));
		new_req->time.tv_usec = atol(get_request_arg(req_str, 3));
		new_req->repeats = 1;
	}else{
		gettimeofday(&new_req->time, NULL);
		new_req->interval = atol(get_request_arg(req_str, 2)) * 1000; // turns milliseconds interval to microseconds
		new_req->time.tv_usec += new_req->interval;
		new_req->repeats = atol(get_request_arg(req_str, 3));
		if (new_req->repeats == 0) {
			new_req->repeats--;
		}
	}

	new_req->port = (unsigned short)atoi(get_request_arg(req_str, 6));
	new_req->cancel = 0;
	new_req->ep = ep;

	return new_req;
}

int cli_cmp(void *a, void *b){
	/* compare client a to epoch b */
	struct timeval *a_c = (struct timeval*)a;
	struct timeval *b_c = (struct timeval*)b;
	if (timercmp(a_c, b_c, >)) {
		return 1;
	}else if (timercmp(a_c, b_c, <)) {
		return -1;
	}else {
		return 0;
	}
	
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

int blocking_que_enq(const Queue *q, void *elm, pthread_mutex_t *m, pthread_cond_t *c){
	int res;
	pthread_mutex_lock(m);
	res = q->enqueue(q, elm);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
}

int blocking_que_deq(const Queue *q, void **elm, pthread_mutex_t *m, pthread_cond_t *c){
	int res;
	pthread_mutex_lock(m);
	res = q->dequeue(q, elm);
	pthread_cond_broadcast(c);
	pthread_mutex_unlock(m);
	return res;
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
			
		if(!validrequest(qry)){
			sprintf(resp, "0");
			bxp_response(service, &client, resp, strlen(resp) + 1);
		}else if (strncmp(qry, "Cancel", 6) == 0) {

			key = strdup(get_request_arg(qry, 1));
			key[length_arg_ind(qry, 1)] = '\0';
			if(blocking_map_get(cncls, key, (void**)&tmp, &cncls_lock, &cncls_cond)){
				tmp->cancel = 1;	
				sprintf(resp, "1%08lu", tmp->svid);
				bxp_response(service, &client, resp, strlen(resp) + 1);
			}else{
				sprintf(resp, "0");
				bxp_response(service, &client, resp, strlen(resp) + 1);
			} 
			free(key);
		}else{
			unsigned long c_svid = next_server_id++;
			char key[BUFSIZ];
			sprintf(key, "%08ld", c_svid);
			tmp = fill_info(qry, c_svid, client);

			struct timeval *prio = (struct timeval*)malloc(sizeof(struct timeval));
			gettimeofday(prio, NULL);

			blocking_q_insert(rqts, prio, tmp, &pq_lock, &pq_cond);

			blocking_map_put(cncls, key, (void*)tmp, &cncls_lock, &cncls_cond); // Add Request to map to cancel possibly later

			sprintf(resp, "1%08lu", c_svid);
			bxp_response(service, &client, resp, strlen(resp) + 1);
		}
	}
	
	free(qry);
	free(resp);
	return NULL;
}

void process_events(void){
	Request *holder = NULL;
	struct timeval *prio;
	struct timeval current;

	if (blocking_q_size(rqts, &pq_lock, &pq_cond) == 0) {
		return;
	}

	gettimeofday(&current, NULL);
	while (blocking_q_remove(rqts, (void**)&prio, (void**)&holder, &pq_lock, &pq_cond) && timercmp(&current, &holder->time, >)) { // See if theres a request in queue and if their time has elapsed
		blocking_que_enq(disp, (void*)holder, &disp_lock, &disp_cond);
		free(prio);
		holder = NULL;
	}

	if (holder != NULL) {
		*prio = current;
		blocking_q_insert(rqts, (void*)prio, (void*)holder, &pq_lock, &pq_cond);
	}
}

void *timer_thread(UNUSED void*a){
	unsigned long long counter = 0;
	struct timeval current;

	for(;;){
		usleep(USECS);
		gettimeofday(&current, NULL);
		counter++;
		if ((counter % time_width) == 0) {
			process_events();
		}
	}
	return NULL;
}

void *dispatching_thread(UNUSED void*a){
	Request *tmp;
	struct timeval *prio;
	char* key = (char*)malloc(sizeof(char)*BUFSIZ);
	while (1) {
		pthread_mutex_lock(&disp_lock);
		while (disp->size(disp) == 0) {
			pthread_cond_wait(&disp_cond, &disp_lock);
		}
		pthread_cond_broadcast(&disp_cond);
		pthread_mutex_unlock(&disp_lock);
		struct timeval current; // current time
		gettimeofday(&current, NULL);
		while (blocking_que_deq(disp, (void**)&tmp, &disp_lock, &disp_cond)) {
			if (tmp->cancel) {
				sprintf(key, "%ld", tmp->svid);
				blocking_map_remove(cncls, key, &cncls_lock, &cncls_cond);
				tmp = NULL;
				continue;
			}

			printf("Event fired: %lu|%s|%s|%u\n", tmp->clid, tmp->host, tmp->service, tmp->port);
			if (--tmp->repeats != 0) { 
				prio = (struct timeval *)malloc(sizeof(struct timeval));
				gettimeofday(prio, NULL);
				tmp->time = *prio;
				tmp->time.tv_usec += tmp->interval;
				blocking_q_insert(rqts, (void*)prio, (void*)tmp, &pq_lock, &pq_cond);
			}else{
				sprintf(key, "%ld", tmp->svid);
				blocking_map_remove(cncls, key, &cncls_lock, &cncls_cond);
				tmp = NULL;
			}
			gettimeofday(&current, NULL);
		}	
	}
	free(key);
	return NULL;
}

/*
 * TODO: Check memory status with valgrind
 */

int main(UNUSED int argc, UNUSED char **argv){
	rqts = PrioQueue_create(cli_cmp, prio_free, cli_free);
	cncls = CSKMap_create(cli_free);
	disp = Queue_create(cli_free);
	pthread_t s;	
	pthread_t t;
	pthread_t d;
	pthread_create(&s, NULL, serverthread, NULL);
	pthread_create(&t, NULL, timer_thread, NULL);
	pthread_create(&d, NULL, dispatching_thread, NULL);
	pthread_join(s, NULL);
	pthread_join(t, NULL);
	pthread_join(d, NULL);
	rqts->destroy(rqts);
	cncls->destroy(cncls);
	disp->destroy(disp);
	
	return 0;
}
