#include <BXP/bxp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include "bxp/bxp.h"

#define UNUSED __attribute__((unused))

#define PORT 19999

#define SERVICE "DTS"

const int max_vals = 6;

int next_word_index(char *start){
	int spot = 0;
	while (start[spot] != '|' && start[spot++] == '\0') {;}
	return spot + 1;
}

int get_param_values(char *vals_start, char **vals){
	/* obtains all params values and returns the spots and number of values */
	if (vals_start == NULL) {
		return 0;
	}
	int num_vals = 0;
	int spot = 0;
	int last_spot = 0;
	while (vals_start[spot] != '\0') {
		if (num_vals >= max_vals) {
			return 0;
		}
		last_spot = spot;
		spot += next_word_index(vals_start + spot);
		strncpy(vals[num_vals++], vals_start + last_spot, spot - last_spot - 1);
	}
	return num_vals;
}

int validrequest(char *req){
	if (strncmp(req, "OneShot", 6) == 0) {
		return 1;
	}else if(strncmp(req, "Repeat", 5) == 0){
		return 2;
	}else if (strncmp(req, "Cancel", 5) == 0) {
		return 3;
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
	char **values = (char **)malloc(sizeof(char *) * max_vals);
	for (int i = 0; i < max_vals; i++) {
		values[i] = (char *)malloc(sizeof(char) * BUFSIZ);
	}
	int num_vals;

	while ((len = bxp_query(bxps, &client, qry, BUFSIZ)) > 0) {
		char *request = strchr(qry, '|');
		// determine if the request is valid at all
		if (request == NULL ||
			!validrequest(qry) ||
		   	!(num_vals = get_param_values(request + 1, values))) {
			sprintf(resp, "0");
			goto respond;
		}
		// determine the request from client
		switch (validrequest(qry)) {
		case 1:
			// OneShot request
			if(num_vals != 6){
				sprintf(resp, "0");	
				goto respond;
			}
			break;
		case 2:
			// Repeat request
			if(num_vals != 6){
				sprintf(resp, "0");	
				goto respond;
			}
			break;
		case 3:
			// Cancel request
			if(num_vals != 1){
				sprintf(resp, "0");	
				goto respond;
			}
			break;
		case 0:
			sprintf(resp, "0");	
			goto respond;
			break;

		}
			
		sprintf(resp, "1%s", qry);
		respond:
		bxp_response(service, &client, resp, strlen(resp) + 1);
	}
	
	free(qry);
	free(resp);
	for (int i = 0; i < max_vals; i++) {
		free(values[i]);
	}
	free(values);
	return NULL;
}

int main(UNUSED int argc, UNUSED char **argv){
	pthread_t s;	
	pthread_create(&s, NULL, serverthread, NULL);
	pthread_join(s, NULL);
	
	return 0;
}
