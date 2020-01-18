
//   data type as reference
#define		  SSTRING					  10
#define	    SFLOAT     				11	
#define	    SINT     				  12
#define	    SDOUBLE     			13
#define	    SLONG     				14
#define	    SLONG64    				15


#define		SHMQ_MAX				200

// some queue concepts
//
// max size: SHMQ_MAX 
// we write on HEAD position, then move head one position forwards, rotating to 0 if it has reached the end   
// we read from TAIL, and then we move TAIL  one position forwards, rotating to 0 if it has reached the end   

// this the data structure for QUEUE (N elements) 
// this will be allocated in shared memory

typedef struct shm_data
  {
  char 				name[200];
  char 				ip[50];
  int 				n;
  int 				response;
  int 				data1;
  long long int 	data2;
  float   			data3;
  double   			data4;
  int 				arr1[20];
  }shm_data;

// this is the data structure for CONTROL. It has semaphore, count, head, tail, etc
// this will also be allocated in shared memory
typedef struct shm_queue
  {
  int 			n;
  int 			tail;
  int 			head;
  sem_t   		sem;
  shm_data 		data[SHMQ_MAX];
  }shm_queue;

extern int             _verbose;


//------------------------------------------------------------------------

// functions / methods declarations

int q_init(shm_queue **s, int key, int *id);
int q_put(shm_queue *s, shm_data *d);
int q_get(shm_queue *s, shm_data *d);

//------------------------------------------------------------------------
//------------------------------------------------------------------------
