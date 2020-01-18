/* 
 * File:   ifaceData.h
 * Author: rfurch
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
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

#include "shm_q.h"


//------------------------------------------------------------------------                                   V'
//------------------------------------------------------------------------

int q_init(shm_queue **s, int key, int *id)
{
int 		shmid=0;
char 		*sshm=NULL;	
long int 	s_size=0;

s_size= (((int)(((float) sizeof(shm_queue)) / ((float)getpagesize()))) + 1) * getpagesize(); 

if (_verbose > 2)
  {
  printf("\n requiered space: %li Allocated Size: %li. PAGE_SIZE: %i  \n", sizeof(shm_queue), s_size, getpagesize() ); 
  printf("\n sizoef data: %li ", sizeof(shm_data));
  fflush(stdout);  
  }
  
//if ((shmid = shmget(key, sizeof(shm_queue), IPC_CREAT | 0666)) < 0) 
if ((shmid = shmget(key, s_size, IPC_CREAT | 0666)) < 0) 
  {
  perror("shmget");
  exit(1);
  }

if (_verbose > 2)
  printf("\n SHM ID: %i \n ", shmid);
fflush(stdout);  

if ((sshm = shmat(shmid, NULL, 0)) == (char *) -1) 
  {
  perror("shmat");
  exit(1);
  }
  
*id=shmid;
*s = (shm_queue *) sshm;
(*s)->n = (*s)->head = (*s)->tail = 0;

if (sem_init(&((*s)->sem), 1, 1) < 0) 
  {
  perror("semaphore initialization");
  exit(1);
  }

return(1);
}

//------------------------------------------------------------------------

int q_put(shm_queue *s, shm_data *d)
{
int 	ok=0;  // error!
int 	pn=0, ph=0, pt=0;
	
// just ins case!
if (!d)
  {
  fprintf(stderr, "\n q_put error:  NULL Destination (d) ");
  return(0);
  } 

sem_wait(&(s->sem));
if (s->n < (SHMQ_MAX))	// is there any space?
  {
  memmove(&(s->data[s->head]), d, sizeof(shm_data));

  pn=s->n; ph=s->head; pt=s->tail;  
  // check if head returns to 0 (circular buffer!)
  (s->head) =  ( s->head < (SHMQ_MAX - 1)) ? (s->head)+1 : 0;
  (s->n)++;  


  if (_verbose > 4)
	{ printf(" | n: %i->%i head: %i->%i tail:  %i->%i ", pn, s->n, ph, s->head, pt, s->tail); fflush(stdout); }

  ok=1;  	  
  }
else
  fprintf(stderr, "\n ERROR:  SHMQ FULL! ");
  
sem_post(&(s->sem));
return(ok); 
} 

//------------------------------------------------------------------------

int q_get(shm_queue *s, shm_data *d)
{
int 	ok=0;  // error!
int 	pn=0, ph=0, pt=0;

// just in case!
if (!d)
  {
  fprintf(stderr, "\n q_put error:  NULL Destination (d) ");
  return(0);
  } 

sem_wait(&(s->sem));
if (s->n > 0)	// are there any elements in queue?
  {
  memmove(d, &(s->data[s->tail]), sizeof(shm_data));
  
  pn=s->n; ph=s->head; pt=s->tail;  
  // check if tail returns to 0 (circular buffer!)
  (s->tail) =  ( s->tail < (SHMQ_MAX - 1)) ? (s->tail)+1 : 0;
  (s->n)--;  

  if (_verbose > 4)
	{ printf(" | n: %i->%i head: %i->%i tail:  %i->%i |  ", pn, s->n, ph, s->head, pt, s->tail); fflush(stdout); }

  ok=1;  	  
  }
  
sem_post(&(s->sem));
return(ok); 
} 

//------------------------------------------------------------------------
//------------------------------------------------------------------------
