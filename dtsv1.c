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
		printf("Could not initialize service\n");
		free(qry);
		free(resp);
		exit(EXIT_FAILURE);
	}
	while ((len = bxp_query(bxps, &client, qry, BUFSIZ)) > 0) {
		sprintf(resp, "1%s", qry);
		bxp_response(service, &client, resp, strlen(resp) + 1);
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
