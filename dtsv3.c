#include <BXP/bxp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include "bxp/bxp.h"
#include "ADTs/prioqueue.h"
#include "ADTs/cskmap.h"


#define UNUSED __attribute__((unused))

#define PORT 19999

#define SERVICE "DTS"

const int max_vals = 6;

static unsigned long epoch;

// list of requests
const PrioQueue *rqts;
pthread_mutex_t pq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pq_cond = PTHREAD_COND_INITIALIZER;

// list of canceld requests
const CSKMap *cncls;
pthread_mutex_t cncls_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cncls_cond = PTHREAD_COND_INITIALIZER;

typedef struct client_timer ClientTimer;

int next_word_index(char *start){
	int spot = 0;
	while (start[spot] != '|' && start[spot] != '\0') {
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

int cli_cmp(void *a, void *b){
	/* compare client a to epoch b */
	char *a_arg = get_request_arg(a, 2);
	int a_len = length_arg(a_arg);
	a_len = range_strtoint(a, a_len);
	char *b_arg = get_request_arg(b, 2);
	int b_len = length_arg(b_arg);
	b_len = range_strtoint(b, b_len);
	if (strncmp(a, "OneShot", 7) == 0) {
		a_len *= 1000000;
	}
	if (strncmp(b, "Repeat", 6) == 0) {
		a_len *= 1000000;
	}
	a_len = epoch - a_len;
	b_len = epoch - b_len;
	return a_len - b_len;
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
	unsigned long time;
	unsigned long add_info_time; // Either number of micro seconds or number of repeats
	unsigned short port;
};

static unsigned long next_server_id = 0;

Request *fill_info(char *req_str){

	char *req_typ = strndup(req_str, length_arg(req_str));
	char *host = strndup(get_request_arg(req_str, 1), length_arg_ind(req_str, 1));
	char *service = strndup(get_request_arg(req_str, 5), length_arg_ind(req_str, 5));

	Request *new_req = (Request *)malloc(sizeof(Request));

	new_req->svid = next_server_id++;
	new_req->request_type = req_typ;
	new_req->service = service;
	new_req->host = host;
	new_req->clid = range_strtounsignlong(get_request_arg(req_str, 1), length_arg_ind(req_str, 1)); 
	new_req->time = range_strtounsignlong(get_request_arg(req_str, 2), length_arg_ind(req_str, 2)); 
	new_req->add_info_time = range_strtounsignlong(get_request_arg(req_str, 3), length_arg_ind(req_str, 3)); 
	return new_req;
}

void cli_free(void *client){
	Request *c = (Request*)client;
	free(c->request_type);
	free(c->service);
	free(c->host);
	free(client);
}

void *serverthread(UNUSED void *arg){
	BXPEndpoint client;
	BXPService bxps;
	epoch = 0;
	unsigned short port = PORT;
	int ifEncrypted = 1;
	char *service = SERVICE;
	unsigned len;
	char *qry = (char*)malloc(BUFSIZ);
	char *resp = (char*)malloc(BUFSIZ);

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
		}else if (strncmp(qry, "Cancel", 6)) {
			pthread_mutex_lock(&cncls_lock);
			cncls->put(cncls, get_request_arg(qry, 1), NULL); // Add id to cancel map
			pthread_cond_broadcast(&cncls_cond);
			pthread_mutex_unlock(&cncls_lock);
		}else{
			Request *tmp = fill_info(qry);

			pthread_mutex_lock(&pq_lock);

			rqts->insert(rqts, (void *)&epoch, tmp); // Put request on request priority

			pthread_cond_broadcast(&pq_cond);
			pthread_mutex_unlock(&pq_lock);
		}
	}
	
	free(qry);
	free(resp);
	return NULL;
}

int main(UNUSED int argc, UNUSED char **argv){
	rqts = PrioQueue_create(cli_cmp, NULL, cli_free);
	cncls = CSKMap_create(cli_free);
	pthread_t s;	
	pthread_create(&s, NULL, serverthread, NULL);
	pthread_join(s, NULL);
	
	return 0;
}
