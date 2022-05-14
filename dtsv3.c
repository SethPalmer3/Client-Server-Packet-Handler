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

int range_strtoint(char *start, int chars){
	int ret = 0;
	int place = 1;
	for (int s = chars; s >= 0; s--) {
		ret += (int)(start[s] - '0') * place;
		place *= 10;
	}
	return ret;
}


int cli_cmp(void *a, void *b){
	/* compare client a to epoch b */
	char *a_arg = get_request_arg(a, 2);
	int a_len = length_arg(a_arg);
	a_len = range_strtoint(a, a_len);
	char *b_arg = get_request_arg(b_arg, 2);
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

void cli_free(void *client){
	free(client);
}

int validrequest(char *req){
	int num_vals;
	if (strncmp(req, "OneShot", 6) == 0) {
		num_vals = get_param_values(req + 1);
		return num_vals == 6;
	}else if(strncmp(req, "Repeat", 5) == 0){
		num_vals = get_param_values(req + 1);
		return num_vals == 6;
	}else if (strncmp(req, "Cancel", 5) == 0) {
		num_vals = get_param_values(req + 1);
		return num_vals == 1;
	}else{
		return 0;
	}
}

void *serverthread(UNUSED void *arg){
	BXPEndpoint client;
	BXPService bxps;
	unsigned short port = PORT;
	int ifEncrypted = 1;
	int valid_request;
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
	int num_vals;

	while ((len = bxp_query(bxps, &client, qry, BUFSIZ)) > 0) {
		valid_request = 1;
		char *request = strchr(qry, '|');
		// determine if the request is valid at all
		if (request == NULL || !validrequest(qry)) {
			valid_request = 0;
		}
		// determine the request from client
			
		if(!valid_request){
			sprintf(resp, "0");
			bxp_response(service, &client, resp, strlen(resp) + 1);
		}else{
			pthread_mutex_lock(&pq_lock);
			char *tmp = (char *)malloc(BUFSIZ);
			strcpy(tmp, qry);
			rqts->insert(rqts, (void *)&epoch, tmp);
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
