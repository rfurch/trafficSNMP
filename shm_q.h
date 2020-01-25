#ifndef PTHREAD_INCLUDED
 #define  PTHREAD_INCLUDED
 #include <pthread.h>
#endif


// some queue concepts
// we use circular buffer over shared memory to handle a queue
// we write on HEAD position, then move head one position forwards, rotating to 0 if it has reached the end   
// we read from TAIL, and then we move TAIL  one position forwards, rotating to 0 if it has reached the end   

// location of data in shared memory will be as follows
// header data data data
// header has  the following structure: 


typedef struct shmQueueHeader
  {
  int               elementSize;
  int               maxElements;
  int 			        n;            // current size (number of elements)
  int 			        tail;
  int 			        head;
  pthread_mutex_t   lock;
  }shmQueueHeader;

// this is the data structure for CONTROL. It has semaphore, count, head, tail, etc
// this will also be allocated in shared memory

extern int             _verbose;

//------------------------------------------------------------------------

// functions / methods declarations

int shmQueueInit(void **s, int elementSize, int maxElements);
int shmQueuePut(void *s, void *data);
int shmQueueGet(void *s, void *data);

//------------------------------------------------------------------------
//------------------------------------------------------------------------
