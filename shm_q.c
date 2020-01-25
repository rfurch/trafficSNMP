/* 
 * File:   ifaceData.h
 * Author: rfurch
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdio.h>
#include <ctype.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <sys/timeb.h>
#include <bits/mman-linux.h>

#include "shm_q.h"

//------------------------------------------------------------------------                                   V'
//------------------------------------------------------------------------

// initialize Shared mameory area and mutex lock. 

int shmQueueInit(void **queue, int elementSize, int maxElements)
{
shmQueueHeader        *q=NULL;  // points to 'queue' but it has well known type / size for internal handling
pthread_mutexattr_t   mattr;

// Our memory buffer will be readable and writable: 
int protection = PROT_READ | PROT_WRITE; 

// The buffer will be shared (meaning other processes can access it), but 
// anonymous (meaning third-party processes cannot obtain an address for it), 
// so only this process and its children will be able to use it: 
int visibility = MAP_ANONYMOUS | MAP_SHARED; 

// queue size: header + elements/data
size_t  qSize = sizeof(shmQueueHeader) + elementSize * maxElements;

if ( (elementSize * maxElements) < 1 )
  return(-1);

// we create shared memory area for devices
// The remaining parameters to `mmap()` are not important for this use case, 
// but the manpage for `mmap` explains their purpose. 
//    return mmap(NULL, size, protection, visibility, 0, 0); 
if ( ((*queue) = (void *) mmap(NULL, qSize, protection, visibility, -1, 0)) == NULL )  {
    printf("\n\n ATENTION:   create_shared_memory ERROR on Shared Memory Queue!  \n\n");
    return (-2);
    }     

q = (shmQueueHeader *) (*queue);
// reset shm memory
memset(q, 0, sizeof(shmQueueHeader) + elementSize * maxElements);

q->elementSize = elementSize;
q->maxElements = maxElements;

// intialize mutex lock to assure mutual exclusion to shm area
pthread_mutexattr_init( &mattr );
pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK_NP);
pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

pthread_mutex_init(& (q->lock), &mattr);

return(1);
}

//------------------------------------------------------------------------

int shmQueuePut(void *queue, void *data)
{
int 	                retVal=0;  // error!
shmQueueHeader        *q=NULL;  // points to 'queue' but it has well known type / size for internal handling

// just in case!
if (!queue)  {
    fprintf(stderr, "\n q_put error:  NULL Destination (d) ");
    return(0);
    } 

q = (shmQueueHeader *) queue;
pthread_mutex_lock (& (q->lock));

if (q->n < (q->maxElements))	{ // is there any space?
    memmove( queue + sizeof(shmQueueHeader) + (q->elementSize * q->n), data, q->elementSize);
    
    // check if head returns to 0 (circular buffer!)
    (q->head) =  ( q->head < (q->n - 1)) ? (q->head)+1 : 0;
    (q->n)++;  

    if (_verbose > 4)  { 
      printf("   *** QUEUE *** n: %i head: %i tail: %i ", q->n, q->head, q->tail); 
      fflush(stdout); 
      }

    retVal=1;  	  
  }
else
  fprintf(stderr, "\n ERROR:  SHMQ FULL! ");
  
pthread_mutex_unlock (& (q->lock));

return(retVal); 
} 

//------------------------------------------------------------------------

int shmQueueGet(void *queue, void *data)
{
int 	                retVal=0;  // error!
shmQueueHeader        *q=NULL;  // points to 'queue' but it has well known type / size for internal handling

// just in case!
if (!queue)  {
    fprintf(stderr, "\n q_put error:  NULL Destination (d) ");
    return(0);
    } 

q = (shmQueueHeader *) queue;
pthread_mutex_lock (& (q->lock));

if (q->n > 0)	// are there any elements in queue?
  {
  memmove(data, queue + sizeof(shmQueueHeader) + (q->elementSize * q->n), q->elementSize);
  
  // check if tail returns to 0 (circular buffer!)
  (q->tail) =  ( q->tail < (q->n - 1)) ? (q->tail)+1 : 0;
  (q->n)--;  

  if (_verbose > 4)  { 
    printf("   *** QUEUE *** n: %i head: %i tail: %i ", q->n, q->head, q->tail); 
    fflush(stdout); 
    }

  retVal=1;  	  
  }
  
pthread_mutex_unlock (& (q->lock));
return(retVal); 
} 

//------------------------------------------------------------------------
//------------------------------------------------------------------------
