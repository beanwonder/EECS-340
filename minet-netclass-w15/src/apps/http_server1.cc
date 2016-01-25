#include "minet_socket.h"
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
using namespace std;

#define BUFSIZE 1024
#define FILENAMESIZE 100

int handle_connection(int);
int writenbytes(int,char *,int);
int readnbytes(int,char *,int);

int main(int argc,char *argv[])
{
  int server_port;
  int sock,sock2;
  struct sockaddr_in sa,sa2;
  int rc;

  /* parse command line args */
  if (argc != 3) {
    fprintf(stderr, "usage: http_server1 k|u port\n");
    exit(EXIT_FAILURE);
  }

  server_port = atoi(argv[2]);
  if (server_port < 1500) {
    fprintf(stderr,"INVALID PORT NUMBER: %d; can't be < 1500\n", server_port);
    exit(EXIT_FAILURE);
  }

  /* initialize and make socket */
  if (toupper(*(argv[1])) == 'K') {
      minet_init(MINET_KERNEL);
  } else if (toupper(*(argv[1])) == 'U') {
      minet_init(MINET_USER);
  } else {
      minet_perror("usage: http_server k|u port\n");
      exit(EXIT_FAILURE);
  }

  sock = minet_socket(SOCK_STREAM);
  if (sock < 0) {
      minet_perror("server: unable to create socket\n");
      exit(EXIT_FAILURE);
  }

  /* set server address*/
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = INADDR_ANY;
  sa.sin_port = htons(server_port);

  /* bind listening socket */
  if (minet_bind(sock ,&sa) < 0) {
      minet_close(sock);
      fprintf(stderr, "server: unable to bind socket to %d\n", server_port);
      exit(EXIT_FAILURE);
  }
  /* start listening */
  int backlog = 20;
  if (minet_listen(sock, backlog) < 0) {
      minet_close(sock);
      fprintf(stderr, "server: failed to listen on port %d\n", server_port);
      exit(EXIT_FAILURE);
  } else {
      fprintf(stdout, "server: listening on port %d\n", server_port);
  }

  /* connection handling loop */
  while(1) {
    /* handle connections */
    memset(&sa2, 0, sizeof(sa2));
    sock2 = minet_accept(sock, &sa2);
    if (sock2 < 0) {
        minet_perror("error on accept\n");
        continue;
    } else {
        rc = handle_connection(sock2);
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
  int datalen = 0;
  char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                      "Content-type: text/plain\r\n"\
                      "Content-length: %d \r\n\r\n";
  char ok_response[100];
  char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                         "Content-type: text/html\r\n\r\n"\
                         "<html><body bgColor=black text=white>\n"\
                         "<h2>404 FILE NOT FOUND</h2>\n"
                         "</body></html>\n";
  bool ok = false;

  /* first read loop -- get request and headers*/
  //while ((datalen = read(sock2, buf, BUFSIZE)) > 0) {
  //    fprintf(stdout, "reading %d\n", datalen);
  //    inStream.append(buf, datalen);
  //}
  memset(buf, 0, BUFSIZE+1);
  datalen = minet_read(sock2, buf, BUFSIZE);

  if (datalen < 0) {
      minet_perror("unable to read from client socket\n");
      return -1;
  }

  string inStream;
  inStream.append(buf, datalen);
  // fprintf(stdout, "handle2\n");
  //cout << inStream << '\n';

  /* parse request to get file name */
  /* Assumption: this is a GET request and filename contains no spaces*/
  const string CRLF("\r\n");
  int end = inStream.find_first_of(CRLF);
  string request_line = inStream.substr(0, end);
  int pos1 = request_line.find_first_of(" ") + 1;
  int pos2 = request_line.find_last_of(" ");

  string url;
  if (pos1 < pos2) {
    url = request_line.substr(pos1, pos2 - pos1);
  }

  /* try opening the file */
  string filenameStr = url.substr(1);
  cout << filenameStr << '\n';

  ifstream fs;
  fs.clear();
//  memset(filestat, 0, sizeof(filestat));
  int status = stat(filenameStr.c_str(), &filestat);
  if (status != -1 && S_ISREG(filestat.st_mode)) {
    try {
        fs.open(filenameStr, std::ios::in);
        ok = true;
    } catch (const exception& e) {
        ok = false;
    }
  }

  cout << "ok: " << ok << '\n';

  /* send response */
  if (ok) {
    /* send headers */
    stringstream ss;
    ss << fs.rdbuf();
    string bodyStr = ss.str();
    minet_write(sock2, ok_response, strlen(ok_response));
    /* send file */
    char *buff = new char[bodyStr.size() + 1];
    strcpy(buff, bodyStr.c_str());
    minet_write(sock2, buff, bodyStr.size());
    delete[] buff;
  } else {
    // no such file or unable to open file
    // 404 response
    // char test[] = "HTTP/1.0 404 FILE NOT FOUND\r\n\r\nsksksk999sss";
    rc = minet_write(sock2, notok_response, strlen(notok_response));
    // cout << "write " << rc << "bytes\n";
  }

  /* close socket and free space */
  minet_close(sock2);
  fs.close();

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

  if (rc < 0) {
    return -1;
  }
  else {
    return totalwritten;
  }
}

