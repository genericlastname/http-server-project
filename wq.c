#include <stdlib.h>
#include "wq.h"
#include "utlist.h"
#include <pthread.h>

/* Initializes a work queue WQ. */

static pthread_mutex_t wq_mutex;
static pthread_cond_t wq_cond;


void wq_init(wq_t *wq) {
  pthread_mutex_init(&wq_mutex, NULL);
  pthread_cond_init(&wq_cond, NULL);
  /* TODO: Make me thread-safe! */
  pthread_mutex_lock(&wq_mutex);
  wq->size = 0;
  wq->head = NULL;
  pthread_mutex_unlock(&wq_mutex);
}

/* Remove an item from the WQ. This function should block until there
 * is at least one item on the queue. */
int wq_pop(wq_t *wq) {

  /* TODO: Make me blocking and thread-safe! */
  
  
      pthread_mutex_lock(&wq_mutex);
      while(wq->size == 0) {
        pthread_cond_wait(&wq_cond, &wq_mutex);
    }
      wq_item_t *wq_item = wq->head;
      int client_socket_fd = wq->head->client_socket_fd;
      wq->size--;
      DL_DELETE(wq->head, wq->head);
      pthread_mutex_unlock(&wq_mutex);
      free(wq_item);
      return client_socket_fd;
   
}

/* Add ITEM to WQ. */
void wq_push(wq_t *wq, int client_socket_fd) {

  /* TODO: Make me thread-safe! */
  pthread_mutex_lock(&wq_mutex);
  wq_item_t *wq_item = calloc(1, sizeof(wq_item_t));
  wq_item->client_socket_fd = client_socket_fd;
  DL_APPEND(wq->head, wq_item);
  wq->size++;
  pthread_mutex_unlock(&wq_mutex); 
  pthread_cond_signal(&wq_cond);
}
