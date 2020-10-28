#include "page_generator.h"

void generate_directory_page(int fd) {
  struct http_request* request = http_request_parse(fd);
  char line[80];

  sprintf(line, "<center><h1>%s</h1><hr>", "Hello");
  http_send_string(fd, line);
}
