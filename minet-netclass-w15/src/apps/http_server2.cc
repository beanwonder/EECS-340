#include "minet_socket.h"
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>

#define BUFSIZE 1024
#define FILENAMESIZE 100

int handle_connection(int);
int writenbytes(int,char *,int);
int readnbytes(int,char *,int);

int main(int argc,char *argv[])
{
  int server_port;
  int listener,sock2;
  struct sockaddr_in sa,sa2;
  int rc,i;
  fd_set readlist;
  fd_set connections;
  int maxfd;

  /* parse command line args */
  if (argc != 3)
  {
    fprintf(stderr, "usage: http_server1 k|u port\n");
    exit(-1);
  }
  server_port = atoi(argv[2]);
  if (server_port < 1500)
  {
    fprintf(stderr,"INVALID PORT NUMBER: %d; can't be < 1500\n",server_port);
    exit(-1);
  }

  /* initialize and make socket */
  if (toupper(*(argv[1])) == 'K') {
    minet_init(MINET_KERNEL);
  } else if (toupper(*(argv[1])) == 'U') {
    minet_init(MINET_USER);
  } else {
    minet_perror("usage: http_server k|u port\n");
    exit(-1);
  }

  listener = minet_socket(SOCK_STREAM);

  if (listener < 0) {
    minet_perror("failed to make socket\n");
    exit(-1);
  }

  /* set server address*/
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  sa.sin_port = htons(server_port);

  /* bind listening socket */
  if (minet_bind(listener, &sa) < 0) {
    minet_close(listener);
    minet_perror("failed to binding\n");
    exit(-1);
  }
  /* start listening */
  int backlog = 10;
  if (minet_listen(listener, backlog) < 0) {
    minet_close(listener);
    minet_perror("failed to listen\n");
    exit(-1);
  } else {
    fprintf(stdout, "server started to linsten at port %d ...\n", server_port);
  }

  // add listener to readlist
  fd_set master;
  FD_SET(listener, &master);
  maxfd = listener;

  /* connection handling loop */
  while(1)
  {
    /* create read list */
    readlist = master;

    /* do a select */
    if (select(maxfd+1, &readlist, NULL, NULL, NULL) < 0) {
        minet_close(listener);
        minet_perror("failed to select\n");
        exit(-1);
    }

    /* process sockets that are ready */
    for (int i=0; i <= maxfd; i++) {
      /* for the accept socket, add accepted connection to connections */
        if (FD_ISSET(i, &readlist)) {
            if (i == listener) {
                if ((sock2 = minet_accept(listener, &sa2)) < 0) {
                    minet_perror("failed to accept\n");
                } else {
                    FD_SET(sock2, &master);
                    if (sock2 > maxfd) {
                        maxfd = sock2;
                    }
                    fprintf(stdout, "accept a socket \n");
                }
            } else {  /* for a connection socket, handle the connection */
                // rc = handle_connection(i);
                fprintf(stdout, "handle a socket connect\n");
                FD_CLR(i, &master);
            }
        }

    }

  }
}

int handle_connection(int sock2)
{
  char filename[FILENAMESIZE+1];
  int rc;
  int fd;
  struct stat filestat;
  char buf[BUFSIZE+1];
  char *headers;
  char *endheaders;
  char *bptr;
  int datalen=0;
  char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                      "Content-type: text/plain\r\n"\
                      "Content-length: %d \r\n\r\n";
  char ok_response[100];
  char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                         "Content-type: text/html\r\n\r\n"\
                         "<html><body bgColor=black text=white>\n"\
                         "<h2>404 FILE NOT FOUND</h2>\n"\
                         "</body></html>\n";
  bool ok=true;

  /* first read loop -- get request and headers*/

  /* parse request to get file name */
  /* Assumption: this is a GET request and filename contains no spaces*/

    /* try opening the file */

  /* send response */
  if (ok)
  {
    /* send headers */

    /* send file */
  }
  else	// send error response
  {
  }

  /* close socket and free space */

  if (ok)
    return 0;
  else
    return -1;
}

int readnbytes(int fd,char *buf,int size)
{
  int rc = 0;
  int totalread = 0;
  while ((rc = minet_read(fd,buf+totalread,size-totalread)) > 0)
    totalread += rc;

  if (rc < 0)
  {
    return -1;
  }
  else
    return totalread;
}

int writenbytes(int fd,char *str,int size)
{
  int rc = 0;
  int totalwritten =0;
  while ((rc = minet_write(fd,str+totalwritten,size-totalwritten)) > 0)
    totalwritten += rc;

  if (rc < 0)
    return -1;
  else
    return totalwritten;
}

