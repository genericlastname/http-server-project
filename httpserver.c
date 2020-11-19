#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"


static volatile int threads_keepalive;
static volatile int threads_on_hold;
/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
pthread_t *workers = NULL;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

typedef struct tThpool {
	pthread_t *ids;
	int maxthreads;
	int numthreads;
} thpool;

void load_file(int fd, char* path, size_t size) {
  FILE* f;
  char* data;

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(path));
  http_send_header(fd, "Server", "httpserver/1.0");
  http_end_headers(fd);

  f = fopen(path, "rb");
  if (!f) {
    printf("couldn't open path: %s\n", path);
    return;
  }

  data = malloc(size);
  fread(data, size, 1, f);
  http_send_data(fd, data, size);
  fclose(f);
  free(data);
}

void load_dir(int fd, char* path) {
  DIR* d;
  d = opendir(path);
  if (!d) {
    return;
  }
  struct dirent* dir;

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", "text/html");
  http_send_header(fd, "Server", "httpserver/1.0");
  http_end_headers(fd);
  http_send_string(fd, "<head><link rel=\"icon\" href=\"data:,\"></head>");

  char temp[600];
  sprintf(temp, "<center><h1>%s</h1><hr></center>", path);
  http_send_string(fd, temp);
  http_send_string(fd, "<ul style=\"list-style-type:none;\">");

  // loop through files to list names
  while ((dir = readdir(d))) {
    sprintf(temp, "<li><a href=\"%s\">%s</a></li>", dir->d_name, dir->d_name);
    http_send_string(fd, temp);
  }
  http_send_string(fd, "</ul>");
  closedir(d);
}

char* join_path(char* p1, char* p2) {
  char* joined;
  int p1_end = strlen(p1) - 1;
  int p1_len;

  if (p1[p1_end] == '/') {
    p1_len = strlen(p1) - 1;
  } else {
    p1_len = strlen(p1);
  }
  // joined = malloc(p1_len + sizeof(p2));
  if (strlen(p2) == 1) {
    joined = malloc(p1_len + 2);
    memset(joined, 0, p1_len + 2);
    strncpy(joined, p1, p1_len);
    strcat(joined, "/\0");
  } else {
    joined = malloc(100);
    // printf("bytes allocated: %lu\n", sizeof(joined));
    memset(joined, 0, p1_len + sizeof(*p2));
    strncpy(joined, p1, p1_len);
    strcat(joined, p2);
  }
  return joined;
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {
  struct http_request *request = http_request_parse(fd);
  struct stat s;
  char* joined_path;

  joined_path = join_path(server_files_directory, request->path);
  printf("%s\n\n", joined_path);

  if (stat(joined_path, &s) == 0) {
    if (s.st_mode & S_IFDIR) {
      // if path is a directory
      DIR* d = opendir(joined_path);
      struct dirent *dir;
      // check if directory contains an index.html
      while ((dir = readdir(d))) {
        if (strcmp(dir->d_name, "index.html") == 0) {
          char* new_path = join_path(joined_path, "/index.html");
          stat(new_path, &s);
          closedir(d);
          load_file(fd, new_path, s.st_size);
          return;
        }
      }
      closedir(d);
      load_dir(fd, joined_path);
    } else if (s.st_mode & S_IFREG) {
      // if path is a file
      load_file(fd, joined_path, s.st_size);
    } 
  } else {
    // 404 error
    http_start_response(fd, 404);
    http_send_header(fd, "Content-Type", "text/html");
    http_send_header(fd, "Server", "httpserver/1.0");
    http_end_headers(fd);
    http_send_string(fd,
        "<center>"
        "<h1>404 ERROR</h1>"
        "<hr>"
        "<p>No page found</p>"
        "</center>");
  }
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;

  }

  /* 
  * TODO: Your solution for task 3 belongs here! 
  */
}

void *startwork(void*handler){
  void (*request_handler)(int) = handler;
  while(1){
    int client_socket_number = wq_pop(&work_queue);

    request_handler(client_socket_number);
    close(client_socket_number);
  }
  return NULL;
}


void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  threads_on_hold = 0;
  threads_keepalive = 1;
  if (num_threads < 0){
    num_threads = 0;
  }
  /* Make new thread pool */
  thpool* thpool_p;
  thpool_p = (thpool*)malloc(sizeof( thpool));
  if (thpool_p == NULL){
    fprintf(stderr, "thpool_init(): Could not allocate memory for thread pool\n");
    return;
  }
  thpool_p->numthreads = 0;
  thpool_p->maxthreads = 5;
  thpool_p->ids = (pthread_t *)malloc(sizeof(pthread_t ) * thpool_p->maxthreads);

  /* Initialise the job queue */
  if (thpool_p->ids == NULL){
    fprintf(stderr, "thpool_init(): Could not allocate memory for job queue\n");
    free(thpool_p);
    return;
  }
  /* Thread init */
  int n;
  for (n=0; n<thpool_p->maxthreads; n++){
    int status = pthread_create(&thpool_p->ids[n], NULL, &startwork, (void*)request_handler);
    if(status != 0){
      printf("Problem creating thread \n");
    }
  }
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }
    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
    wq_push(&work_queue, client_socket_number);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
