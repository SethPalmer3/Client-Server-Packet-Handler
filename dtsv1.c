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

int main(UNUSED int argc, UNUSED char **argv){

	BXPEndpoint client;
	BXPService bxps;
	unsigned short port = PORT;
	int ifEncrypted = 1;
	char *service = SERVICE;
	unsigned len;
	char *qry = (char*)malloc(BUFSIZ);
	char *resp = (char*)malloc(BUFSIZ);
	char *rest,*cmd;

	assert(bxp_init(port, 1));
	bxps = bxp_offer(service);
	if(bxps==NULL){
		printf("Could not initialize service\n");
		free(qry);
		free(resp);
		exit(EXIT_FAILURE);
	}
	while ((len = bxp_query(bxps, &client, qry, BUFSIZ)) > 0) {
		/*cmd = qry;
		rest = strchr(qry, ':');
		*rest++ = '\0';
		if(strcmp(cmd, "ECHO") == 0){
			resp[0] = '1';
			strcpy(resp+1, rest);
		}else if(strcmp(cmd, "SINK") == 0){
			sprintf(resp, "1");
		}else{
			sprintf(resp, "0Illegal Command %s", cmd);
		}*/
		sprintf(resp, "1%s", qry);
		bxp_response(service, &client, resp, strlen(resp) + 1);
	}
	
	free(qry);
	free(resp);
	return 0;
}
