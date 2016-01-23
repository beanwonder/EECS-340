#include "minet_socket.h"
#include <stdlib.h>
#include <ctype.h>
#include <string>

using namespace std;

#define BUFSIZE 1024

int write_n_bytes(int fd, char * buf, int count);

int main(int argc, char * argv[]) {
    char * server_name = NULL;
    //int server_port = 0;
    char * server_port = NULL;
    char * server_path = NULL;

    int sock = 0;
    int rc = -1;
    int datalen = 0;
    bool ok = true;
    //struct sockaddr_in sa;
    FILE * wheretoprint = stdout;
    //struct hostent * site = NULL;
    struct addrinfo hints;
	struct addrinfo * servinfo;
    char * req = NULL;

    char buf[BUFSIZE + 1];
    char * bptr = NULL;
    char * bptr2 = NULL;
    char * endheaders = NULL;

    struct timeval timeout;
    fd_set set;

    /*parse args */
    if (argc != 5) {
	    fprintf(stderr, "usage: http_client k|u server port path\n");
	    exit(-1);
    }

    server_name = argv[2];
    //server_port = atoi(argv[3]);
    server_port = argv[3];
    server_path = argv[4];



    /* initialize minet */
    if (toupper(*(argv[1])) == 'K') {
	    minet_init(MINET_KERNEL);
    } else if (toupper(*(argv[1])) == 'U') {
	    minet_init(MINET_USER);
    } else {
	    fprintf(stderr, "First argument must be k or u\n");
	    exit(-1);
    }

	/* setup addrinfo struct*/
	//site = gethostbyname(server_name);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(server_name, server_port, &hints, &servinfo) != 0) {
		minet_perror("clinet getaddrinfo");
		exit(EXIT_FAILURE);
	}

    /* create socket */
	sock = minet_socket(SOCK_STREAM);
	if (sock < 0) {
		minet_perror("client socket");
		exit(EXIT_FAILURE);
	}

    /* set address */

    /* connect socket */
	if (minet_connect(sock, (sockaddr_in *)servinfo->ai_addr) < 0) {
		minet_close(sock);
		minet_perror("clinet connect");
		exit(EXIT_FAILURE);
	}

    const string CRLF = "\r\n";
    /* send request */
    string method("GET");
    string url(server_path);
    string version("HTTP/1.0");
    string reqStr = method + " " + url + " " + version + CRLF + CRLF;

    req = new char[reqStr.length()];
	sprintf(req, reqStr.c_str());
	if (write_n_bytes(sock, req, strlen(req)) < 0) {
        delete req;
		minet_perror("clinet send 1");
		exit(EXIT_FAILURE);
	}
    delete req;

    /* wait till socket can be read */
	timeout.tv_sec = 2;
	timeout.tv_usec = 500000;
	FD_ZERO(&set);
	FD_SET(sock, &set);

	if (minet_select(sock + 1, &set, NULL, NULL, &timeout) < 0) {
		minet_perror("clinet select timeout");
		exit(EXIT_FAILURE);
	}

	if(FD_ISSET(sock, &set)) {
		if(minet_read(sock, buf, BUFSIZE) < 0) {
			minet_perror("client received 1");
			exit(EXIT_FAILURE);
		}
	} else {
		minet_perror("Connect to remote sever failed");
		exit(EXIT_FAILURE);
	}

//	fprintf(wheretoprint, "%s", buf);

    /* first read loop -- read headers */
	bptr = buf;
    //Skip "HTTP/1.0"
	while(*bptr != ' ') {
		++bptr;
    }
	++bptr;

	char code[4];
	strncpy(code, bptr, 3);
	code[3] = '\0';
	rc = atoi(code);

	fprintf(wheretoprint, "HTTP response code: %d\n\n", rc);
	if(rc != 200) {
		wheretoprint = stderr;
		ok = false;
	}

	bptr2 = bptr + 2;
	while(!(*bptr == '\n' && *bptr2 == '\n')) {
		if(*(bptr2 + 1) == '\0')
			break;
		++bptr;
		++bptr2;
	}
	++bptr2;

    /* print first part of response */
	fprintf(wheretoprint, "HTTP response body:\n");
	fprintf(wheretoprint, "%s", bptr2);

    /* second read loop -- print out the rest of the response */
	while((datalen = minet_read(sock, buf, BUFSIZE)) > 0) {
		buf[datalen] = '\0';
		fprintf(wheretoprint, "%s", buf);
	}

    /*close socket and deinitialize */
	freeaddrinfo(servinfo);
	if(minet_deinit() < 0) {
		minet_perror("deinit");
		exit(EXIT_FAILURE);
	}

    if (ok) {
	return 0;
    } else {
	return -1;
    }
}

int write_n_bytes(int fd, char * buf, int count) {
    int rc = 0;
    int totalwritten = 0;

    while ((rc = minet_write(fd, buf + totalwritten, count - totalwritten)) > 0) {
	totalwritten += rc;
    }

    if (rc < 0) {
	return -1;
    } else {
	return totalwritten;
    }
}


