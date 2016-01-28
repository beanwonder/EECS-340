#include "minet_socket.h"
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <assert.h>


#define FILENAMESIZE 100
#define BUFSIZE 1024
#define BACKLOG 20

typedef enum \
{NEW,READING_HEADERS,WRITING_RESPONSE,READING_FILE,WRITING_FILE,CLOSED} states;

typedef struct connection_s connection;
typedef struct connection_list_s connection_list;

struct connection_s
{
  int sock;
  int fd;
  char filename[FILENAMESIZE+1];
  char buf[BUFSIZE+1];
  char *endheaders;
  bool ok;
  long filelen;
  states state;
  int headers_read; //how much left to read
  int response_written;
  int file_read;
  int file_written;

  connection *next;
};

struct connection_list_s
{
  connection *first,*last;
};

void add_connection(int,connection_list *);
void insert_connection(int,connection_list *);
void init_connection(connection *con);

int writenbytes(int,char *,int);
int readnbytes(int,char *,int);
void read_headers(connection *);
void write_response(connection *);
void read_file(connection *);
void write_file(connection *);

int main(int argc,char *argv[])
{
  int server_port;
  int listener, accepter;
  struct sockaddr_in sa,sa2;
  int rc;
  fd_set readlist,writelist;
  connection_list connections;
  connection *i;
  int maxfd;
  struct addrinfo hints;
  struct addrinfo * servinfo;
  int sock;

  /* parse command line args */
  if (argc != 3)
  {
    fprintf(stderr, "usage: http_server3 k|u port\n");
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

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if(getaddrinfo(NULL, argv[2], &hints, &servinfo) != 0) {
		perror("clinet getaddrinfo");
		exit(EXIT_FAILURE);
	}

	listener = minet_socket(SOCK_STREAM);
	if(listener < 0) {
		minet_perror("make socket error:");
		exit(EXIT_FAILURE);
	}
	fcntl(listener, F_SETFL, O_NONBLOCK);

 	/* set server address*/

    /* bind listening socket */
	if(minet_bind(listener, (sockaddr_in *)servinfo->ai_addr) < 0) {
		minet_close(listener);
		minet_perror("bind listener error:");
		exit(EXIT_FAILURE);
	}

  	/* start listening */
	if(minet_listen(listener, BACKLOG) < 0) {
		minet_close(listener);
		minet_perror("cannot start listener:");
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "server start listening at port %d ...\n", server_port);
	}

	/* initialize readlist and write list*/
	FD_ZERO(&readlist);
	FD_ZERO(&writelist);
	FD_SET(listener, &readlist);
	maxfd = listener;
	connections.first = NULL;
	connections.last = NULL;

    /* connection handling loop */
    while(1)
    {
    	/* create read and write lists */
		fd_set readlist2 = readlist;
		fd_set writelist2 = writelist;

    	/* do a select */
		if(select(maxfd + 1, &readlist2, &writelist2, NULL, NULL) < 0) {
			minet_close(listener);
			minet_perror("select error:");
			exit(EXIT_FAILURE);
		}

		fprintf(stdout, "out of select\n");

    	/* process sockets that are ready */
		for (int index = 0; index <= maxfd; ++index) {
			/* for each sock on readlist that is readable*/
			if(FD_ISSET(index, &readlist2)) {
				if(index == listener) {
					fprintf(stdout, "selected the listener\n");
					if((sock = minet_accept(listener, &sa2)) < 0) {
						if(errno == EAGAIN) {
							continue;
						} else {
							minet_perror("failed to accept:");
							// close all sockets and exit
						}
					} else {
						fcntl(sock, F_SETFL, O_NONBLOCK);
						FD_SET(sock, &readlist);
						if(sock > maxfd)
							maxfd = sock;
						insert_connection(sock, &connections);
						fprintf(stdout, "accepted a socket\n");
					}
				} else {
					for(i = connections.first; i != NULL; i = i->next) {
						if(i->sock == index) {
							if(i->state == READING_HEADERS) {
								fprintf(stdout, "Reading Headers on socket %d\n", index);
								read_headers(i);
							}
							else if(i->state == NEW) {
								fprintf(stdout, "New on socket %d\n", index);
								//do not know what to do here
								i->state = READING_HEADERS;
							}
							else
								fprintf(stdout, "readlist socket %d state error\n", index);
						} else if(i->fd == index) {
							if(i->state == READING_FILE) {
								fprintf(stdout, "Reading File on fd %d\n", index);
								read_file(i);
								if(i->state == WRITING_FILE) {
									FD_CLR(index, &readlist);
									FD_SET(index, &writelist);
								}
							} else
								fprintf(stdout, "readlist file %d state error\n", index);
						}
					}
				}
			}

			if(FD_ISSET(index, &writelist2)) {
				for(i = connections.first; i != NULL; i = i -> next) {
					if(i->sock == index) {
						if(i->state == WRITING_RESPONSE) {
							fprintf(stdout, "Writing response on socket %d\n", index);
							write_response(i);
						}
						else
							fprintf(stdout, "writelist socket %d state error\n", index);
					} else if(i->fd == index) {
						if(i->state == WRITING_FILE) {
							fprintf(stdout, "Writing file on fd %d\n", index);
							write_file(i);
							if(i->state == READING_FILE) {
								FD_CLR(index, &writelist);
								FD_SET(index, &readlist);
							}
						} else {
							fprintf(stdout, "writelist file %d state error\n", index);
						}
					}
				}
			}
		}
    }
}

void read_headers(connection *con)
{
    fprintf(stdout, "entering into read headers");
    int rc = readnbytes(con->sock, con->buf, BUFSIZE);
  /* first read loop -- get request and headers*/
    if (rc < 0) {
        if (errno == EAGAIN) {
            return;
        } else {
            minet_close(con->sock);
            con->state = CLOSED;
            minet_perror("read headers\n");
            return;
        }
    } else if (rc == 0) {
        minet_close(con->sock);
        con->state = CLOSED;
        fprintf(stderr, "reader: client socket closed\n");
        return;
    }

    fprintf(stdout, "read whole request\n");
    con->headers_read = rc;
    // assuming requests are no longer than buffer
    con->buf[rc] = '\0';
    // when come here header reqests are read completed

    const char HEADERENDSEP[] = "\r\n\r\n";
    /* parse request to get file name */

    if ((con->endheaders = strstr(con->buf, HEADERENDSEP)) == NULL) {
        minet_close(con->sock);
        con->state = CLOSED;
        fprintf(stderr, "discard invalid http request\n");
        return;
    }
    /* assumption: this is a get request and filename contains no spaces*/
    int count = con->endheaders - con->buf;
    char *request_line = new char[count+1];
    strncpy(request_line, con->buf, count);
    request_line[count] = '\0';
    // assume filename are always smaller than FILENAMESIZE;
    char *sp1 = strchr(request_line, ' ');
    char *sp2 = strchr(sp1+1, ' ');
    assert(sp2 > sp1);
    assert((sp2-sp1) <= FILENAMESIZE);

    /* get file name and size, set to non-blocking */
    /* get name */
    if (sp1[1] != '/') {
        con->ok = false;

    } else {
        strncpy(con->filename, sp1+2, sp2-sp1-1);
        con->filename[sp2-sp1-1] = '\0';
        struct stat filestat;
        memset(&filestat, 0, sizeof(filestat));
        int status = stat(con->filename, &filestat);
        if (status != -1 && S_ISREG(filestat.st_mode)) {
            int fd = open(con->filename, O_RDONLY);
            if (fd != -1) {
                fcntl(fd, F_SETFL, O_NONBLOCK);
                con->fd = fd;
                con->filelen = filestat.st_size;
                con->ok = true;
            } else {
                con->ok = false;
            }
        } else {
            con->ok = false;
        }
    }
    delete[] request_line;
    fprintf(stdout, "GET status: %d\n", con->ok);
    /* try opening the file */

    /* set to non-blocking, get size */
    con->state = WRITING_RESPONSE;
    write_response(con);

}

void write_response(connection *con)
{

    int sock2 = con->sock;
    int rc;
    int written = con->response_written;
    char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                      "Content-type: text/plain\r\n"\
                      "Content-length: %d \r\n\r\n";
    char ok_response[100];
    char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                         "Content-type: text/html\r\n\r\n"\
                         "<html><body bgColor=black text=white>\n"\
                         "<h2>404 FILE NOT FOUND</h2>\n"\
                         "</body></html>\n";
    /* send response */
    fprintf(stdout, "write_response\n");
    if (con->ok) {
    /* send headers */
        const int total = strlen(ok_reponse_f) + 1;
        if (con->response == total) {
            con->state = READING_FILE;
            read_file(con);
        } else {
            int rest = total - con->response_write;
            // maybe should assmue never block here
            rc = writenbytes(sock2, ok_response_f+(con->response_write), rest);
            if (rc < 0) {
                if (errno == EAGAIN) {
                    return;
                } else {
                    minet_close(con->sock);
                    con->state = CLOSED;
                    minet_perror("write 404 response\n");
                    return;
                }
            } else {
                con->response_write += rc;
                return;
            }
        }

    } else {
        //
        const int total = strlen(notok_response) + 1;
        if (con->response_write == total) {
            // finish write response
            //
            minet_close(con->sock);
            con->state = CLOSED;
            return;
        } else {
            int rest = total - con->response_write;
            rc = writenbytes(sock2, notok_response+(con->response_write), rest);
            // maybe should assmue never block here
            if (rc < 0) {
                if (errno == EAGAIN) {
                    return;
                } else {
                    minet_close(con->sock);
                    con->state = CLOSED;
                    minet_perror("write 404 response\n");
                    return;
                }
            } else {
                con->response_write += rc;
                return;
            }
        }
    }
}

void read_file(connection *con)
{
  int rc;

    /* send file */
  rc = read(con->fd,con->buf,BUFSIZE);
  if (rc < 0)
  {
    if (errno == EAGAIN)
      return;
    fprintf(stderr,"error reading requested file %s\n",con->filename);
    return;
  }
  else if (rc == 0)
  {
    con->state = CLOSED;
    minet_close(con->sock);
  }
  else
  {
    con->file_read = rc;
    con->state = WRITING_FILE;
    write_file(con);
  }
}

void write_file(connection *con)
{
  int towrite = con->file_read;
  int written = con->file_written;
  int rc = writenbytes(con->sock, con->buf+written, towrite-written);
  if (rc < 0)
  {
    if (errno == EAGAIN)
      return;
    minet_perror("error writing response ");
    con->state = CLOSED;
    minet_close(con->sock);
    return;
  }
  else
  {
    con->file_written += rc;
    if (con->file_written == towrite)
    {
      con->state = READING_FILE;
      con->file_written = 0;
      read_file(con);
    }
    else
      printf("shouldn't happen\n");
  }
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


// inserts a connection in place of a closed connection
// if there are no closed connections, appends the connection
// to the end of the list

void insert_connection(int sock,connection_list *con_list)
{
  connection *i;
  for (i = con_list->first; i != NULL; i = i->next)
  {
    if (i->state == CLOSED)
    {
      i->sock = sock;
      i->state = NEW;
      return;
    }
  }
  add_connection(sock,con_list);
}

void add_connection(int sock,connection_list *con_list)
{
  connection *con = (connection *) malloc(sizeof(connection));
  init_connection(con);
  con->next = NULL;
  con->state = NEW;
  con->sock = sock;
  if (con_list->first == NULL)
    con_list->first = con;
  if (con_list->last != NULL)
  {
    con_list->last->next = con;
    con_list->last = con;
  }
  else
    con_list->last = con;
}

void init_connection(connection *con)
{
  con->headers_read = 0;
  con->response_written = 0;
  con->file_read = 0;
  con->file_written = 0;
  con->fd = -1;
  con->sock = -1;
}
