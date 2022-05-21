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
#include "valgrind/valgrind.h"


#define UNUSED __attribute__((unused))

#define PORT 19999

#define SERVICE "DTS"

const int max_vals = 6;

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
		if (vals_start[spot++]) {num_vals++;}
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

void *serverthread(UNUSED void *arg){
	BXPEndpoint client;
	BXPService bxps;
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
		if (!validrequest(qry)) {
			sprintf(resp, "0");
			bxp_response(service, &client, resp, strlen(resp) + 1);
		}else{
			sprintf(resp, "1%s", qry);
			bxp_response(service, &client, resp, strlen(resp) + 1);
		}
		VALGRIND_MONITOR_COMMAND("leak_check summary");
	}
	
	free(qry);
	free(resp);
	return NULL;
}

int main(UNUSED int argc, UNUSED char **argv){
	pthread_t s;	
	pthread_create(&s, NULL, serverthread, NULL);
	pthread_join(s, NULL);
	
	return 0;
}
