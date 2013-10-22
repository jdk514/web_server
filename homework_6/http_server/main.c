/**
 * Redistribution of this file is permitted under the GNU General
 * Public License v2.
 *
 * Copyright 2012 by Gabriel Parmer.
 * Author: Gabriel Parmer, gparmer@gwu.edu, 2012
 */
/* 
 * This is a HTTP server.  It accepts connections on port 8080, and
 * serves a local static document.
 *
 * The clients you can use are 
 * - httperf (e.g., httperf --port=8080),
 * - wget (e.g. wget localhost:8080 /), 
 * - or even your browser.  
 *
 * To measure the efficiency and concurrency of your server, use
 * httperf and explore its options using the manual pages (man
 * httperf) to see the maximum number of connections per second you
 * can maintain over, for example, a 10 second period.
 */

 /*
  * Joel Klein
  * jdk514@gwmail.gwu.edu
  * csci 3411
  *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>
#include <pthread.h>

#include <util.h> 		/* client_process */
#include <server.h>		/* server_accept and server_create */
#include <util.h>
#include <cas.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_DATA_SZ 1024
#define MAX_CONCURRENCY 25
#ifndef BUFFER_LENGTH
#define BUFFER_LENGTH MAX_CONCURRENCY
#endif

volatile int var = 0;

int pthread_counter = 0;		//keep track of threads mode 4
int running[MAX_CONCURRENCY];	//keep track of running threads 4 & 5

pthread_mutex_t lock;			//mutex lock for 6
pthread_cond_t not_full;			//condition for 7
pthread_cond_t not_empty;
struct request *task_queue;		//head of linked_list for mode 5 & 6
struct ring *rb;				//pointer to ring buffer for mode 7

//struct used to send requests to threads
struct request {
	int fd;
	struct request *next;
};

//ring buffer for mode 7
typedef struct ring {
	void* buffer[MAX_CONCURRENCY];
	int head;
	int tail;
}ring;

//following functions define a ring_buffer of size MAX_CONCURRENCY
struct ring* rb_create(void) {
	struct ring *rb = malloc(sizeof(struct ring));
	rb->head = 0;
	rb->tail = rb->head+1;
}

int rb_isempty(struct ring *rb) {
	if (rb->head%MAX_CONCURRENCY == rb->tail-1%MAX_CONCURRENCY) {
		return -1;
	} else {
		return 0;
	}
}

int rb_isfull(struct ring *rb) {
	if (rb->tail == rb->head) {
		return -1;
	} else {
		return 0;
	}
}

int rb_enqueue(struct ring *rb, void* value) {
	if (rb->tail == rb->head) {
		return 0;
	} else {
		if (rb->tail == 0) {
			rb->buffer[MAX_CONCURRENCY-1] = value;
		} else {
			rb->buffer[rb->tail-1] = value;
		}

		if (rb->tail == MAX_CONCURRENCY-1) {
			rb->tail = 0;
		} else {
			rb->tail = rb->tail + 1;
		}
		return -1;
	}
	return 0;
}

void* rb_dequeue(struct ring *rb) {
	void* ret;
	if (rb->head == rb->tail + 1 || (rb->head == MAX_CONCURRENCY-1 && rb->tail == 0)) {
		return NULL;
	} else {
		ret = rb->buffer[rb->head];
		rb->buffer[rb->head] = NULL;
		rb->head = rb->head + 1;
		return ret;
	}
}

void rb_delete(struct ring *rb) {
	free(rb);
	return;
}

void int_print_rb(struct ring *rb) {
	int i;
	for (i=0; i<BUFFER_LENGTH; i++) {
		printf("value at %d is %d, ", i, (int)rb->buffer[i]);
	}
	printf("\n");
	return;
}



/* 
 * This is the function for handling a _single_ request.  Understand
 * what each of the steps in this function do, so that you can handle
 * _multiple_ requests.  Use this function as an _example_ of the
 * basic functionality.  As you increase the server in functionality,
 * you will want to probably keep all of the functions called in this
 * function, but define different code to use them.
 */
void
server_single_request(int accept_fd)
{
	int fd;

	/* 
	 * The server thread will always want to be doing the accept.
	 * That main thread will want to hand off the new fd to the
	 * new threads/processes/thread pool.
	 */
	fd = server_accept(accept_fd);
	client_process(fd);

	return;
}

void
server_multiple_requests(int accept_fd)
{
	int fd;
	/*
	/* For multiple requests the thread must
	/* continously wait for the requests.
	/* This freezes up the server, but keeps requests open
	*/
	while(1) {
		fd = server_accept(accept_fd);
		client_process(fd);
	}

	return;
}

void
server_processes(int accept_fd)
{
	int fd;
	int pid; //id of process
	int endID; //int to see if child is still running
	int status;
	int pid_counter;
	int pid_array[MAX_CONCURRENCY]; //array of pids
	int temp_counter; //index of temp array;
	int i;

	/*
	/* For multiple process requests
	/* continously iterate and create new fork()'s.
	/* Use array to keep track of running processes
	*/

	while(1) {

		fd = server_accept(accept_fd);

		if (pid_counter >= MAX_CONCURRENCY) {
			wait(&status);//ensure that we clear up at least one process
			temp_counter = 0;
			//iterate through and remove any completed processes
			for (i = 0; i<pid_counter; i++) {
				endID = waitpid(pid_array[i], &status, WNOHANG|WUNTRACED);
				if (endID == 0) {
					pid_array[temp_counter] = pid_array[i];
					temp_counter++;
				} else {
					pid_counter--;
				}
			}
		}

		pid = fork();
		if (pid != 0) {
			pid_array[pid_counter] = pid;//add pid to list of running processes
			printf("pid counter is %d\n", pid_counter);
			pid_counter++;
		}
		if (pid == 0){
			client_process(fd);			
			exit(0);
		}
	}
}

struct thread_args {
	int fd;
	int thread_num;
};

void dynamic_server_helper(void* thread_args) {
	struct thread_args* args = (struct thread_args*) thread_args;
	int fd = args->fd;
	int new_var, old_var;
	client_process(fd);
	do {
		old_var = pthread_counter;
		new_var = old_var;
		new_var--;
	} while (__cas(&pthread_counter, old_var, new_var));
	running[args->thread_num] = 0;
	pthread_exit(NULL);
}

void
server_thread_per(int accept_fd)
{
	pthread_t threads[MAX_CONCURRENCY];
	int i, old_var, new_var;
	struct thread_args *args = malloc(sizeof(struct thread_args));

	for (i=0; i<MAX_CONCURRENCY; i++) {
		threads[i] = i;
	}
	/* 
	 * Server will create a thread for 
	*/
	while (1) {
		if (pthread_counter >= MAX_CONCURRENCY) {
			for (i=0; i<MAX_CONCURRENCY; i++) {
				if (running[i] = 1) {
					pthread_join(threads[i], NULL);
					break;
				}
			}
		}
		args->fd = server_accept(accept_fd);
		for (i=0; i<MAX_CONCURRENCY; i++) {
			if (running[i] = 0) {
				args->thread_num = i;
				break;
			}
		}
		running[args->thread_num] = 1;
		pthread_create(&threads[args->thread_num], NULL, &dynamic_server_helper, (void *) args);
		do {
			old_var = pthread_counter;
			new_var = old_var;
			new_var++;
		} while (__cas(&pthread_counter, old_var, new_var));
	}
}

void
server_dynamic(int accept_fd)
{
	return;
}



void put_request(void* node) {
	int new_var, old_var;
	struct request* current_node = (struct request*) node;
	do {
		old_var = task_queue;
		new_var = current_node;
		current_node->next = task_queue;
 	} while (__cas(&task_queue, old_var, new_var));
	return;
}

void get_request() {
	int new_var, old_var;
	while (1) {
		while(task_queue == NULL);
		struct request *head;
		do {
			old_var = task_queue;
			head = task_queue;
			new_var = task_queue->next;
		} while (__cas(&task_queue, old_var, new_var));
		client_process(head->fd);
		free(head);
		do {
			old_var = pthread_counter;
			new_var = old_var;
			new_var--;
		} while (__cas(&pthread_counter, old_var, new_var));
	}
}

void
server_task_queue(int accept_fd)
{
	pthread_t make_request;
	pthread_t threads[MAX_CONCURRENCY];
	int i, new_var, old_var;

	for (i=0; i<MAX_CONCURRENCY; i++) {
		pthread_create(&threads[i], NULL, &get_request, NULL);
	}

	//function to check list length;
	/* 
	 * Server will create a thread for 
	*/
	while (1) {
		while (pthread_counter >= MAX_CONCURRENCY);
		struct request *new_request = malloc(sizeof(struct request));
		new_request->fd = server_accept(accept_fd);
		pthread_create(&make_request, NULL, &put_request, (void *) new_request);
		do {
			old_var = pthread_counter;
			new_var = old_var;
			new_var++;
		} while (__cas(&pthread_counter, old_var, new_var));
		pthread_join(make_request, NULL);
	}
}

void put_request_blocking(void* node) {
	struct request* current_node = (struct request*) node;
	pthread_mutex_lock(&lock);
	current_node->next = task_queue;
	task_queue = current_node;
	pthread_mutex_unlock(&lock);
	return;
}

void get_request_blocking() {
	while (1) {
		while(task_queue == NULL);
		struct request *head;
		pthread_mutex_lock(&lock);
		head = task_queue;
		task_queue = task_queue->next;
		pthread_mutex_unlock(&lock);
		client_process(head->fd);
		free(head);
		pthread_mutex_lock(&lock);
		pthread_counter--;
		pthread_mutex_unlock(&lock);
	}
}

void
server_thread_pool_mutex(int accept_fd)
{
	pthread_t make_request;
	pthread_t threads[MAX_CONCURRENCY];
	int i;

	pthread_mutex_init(&lock, NULL);

	for (i=0; i<MAX_CONCURRENCY; i++) {
		pthread_create(&threads[i], NULL, &get_request_blocking, NULL);
	}

	//function to check list length;
	/* 
	 * Server will create a thread for 
	*/
	while (1) {
		while (pthread_counter >= MAX_CONCURRENCY);
		struct request *new_request = malloc(sizeof(struct request));
		new_request->fd = server_accept(accept_fd);
		pthread_create(&make_request, NULL, &put_request_blocking, (void *) new_request);
		pthread_mutex_lock(&lock);
		pthread_counter++;
		pthread_mutex_unlock(&lock);
		pthread_join(make_request, NULL);
	}
}

void get_request_conditions() {
	int fd;

	while (1) {
		pthread_mutex_lock(&lock);
		if(rb_isempty){
		}
		while (rb_isempty(rb)) {
			pthread_cond_wait(&not_empty, &lock);
		}
		fd = (int) rb_dequeue(rb);
		pthread_counter--;
		pthread_cond_signal(&not_full);
		pthread_mutex_unlock(&lock);
		client_process(fd);
	}
}

void server_thread_pool_conditions(int accept_fd) {
	pthread_t threads[MAX_CONCURRENCY];
	int i, fd;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&not_empty, NULL);
	pthread_cond_init(&not_full, NULL);
	rb = rb_create();
	for (i=0; i<MAX_CONCURRENCY; i++) {
		pthread_create(&threads[i], NULL, &get_request_conditions, NULL);
	}

	while(1) {
		fd = server_accept(accept_fd);
		pthread_mutex_lock(&lock);
		while(rb_isfull(rb)) {
			pthread_cond_wait(&not_full, &lock);
		}
		rb_enqueue(rb, fd);
		pthread_counter++;
		pthread_cond_signal(&not_empty);
		pthread_mutex_unlock(&lock);
	}
	return;
}

void get_request_osqueue() {
	int file, fd, work;
	char output[80];
	file = open("/dev/osqueue", O_RDWR);
	while (1) {
		work = read(file, (void*) output, 80);
		fd = (int) output;
		client_process(fd);
	}
}

void server_char_device_queue(int accept_fd){
	int file, work, i, fd;
	pthread_t threads[MAX_CONCURRENCY];
	char char_fd[10] = "joel";
	char test[1];
	char output[80];

/*	for (i=0; i<MAX_CONCURRENCY; i++) {
		pthread_create(&threads[i], NULL, &get_request_osqueue, NULL);
	}*/
	
	file = open("/dev/osqueue", O_RDWR);
	printf("Did open work %d\n", file);

	while(1) {
		printf("waiting for request\n");
		fd = server_accept(accept_fd);
		printf("fd is %d\n", fd);
		test[0] = fd;
		printf("fd as array is %d\n", test[0]);
		work = write(file, (void*) test, 1);
		printf("Did write work %d\n", work);
		work = read(file, (void*) output, 80);
		printf("Did read work %d\n", work);
		for (i=0; i<80; i++) {
			printf("fd reads as %d\n", output[i]);
		}
	}
	return;
}

void server_wait_queue(int accept_fd) {
	return;
}

typedef enum {
	SERVER_TYPE_ONE = 0,
	SERVER_TYPE_SINGLET,
	SERVER_TYPE_PROCESS,
	SERVER_TYPE_FORK_EXEC,
	SERVER_TYPE_SPAWN_THREAD,
	SERVER_TYPE_TASK_QUEUE,
	SERVER_TYPE_THREAD_POOL_MUTEX,
	SERVER_TYPE_THREAD_POOL_CONDITIONS,
	SERVER_TYPE_CHAR_DEVICE_QUEUE,
	SERVER_TYPE_WAIT_QUEUE,
} server_type_t;

int
main(int argc, char *argv[])
{
	server_type_t server_type;
	short int port;
	int accept_fd;

	if (argc != 3) {
		printf("Proper usage of http server is:\n%s <port> <#>\n"
		       "port is the port to serve on, # is either\n"
		       "0: serve only a single request\n"
		       "1: use only a single thread for multiple requests\n"
		       "2: use fork to create a process for each request\n"
		       "3: Extra Credit: use fork and exec when the path is an executable to run the program dynamically.  This is how web servers handle dynamic (program generated) content.\n"
		       "4: create a thread for each request\n"
		       "5: use atomic instructions to implement a task queue\n"
		       "6: use a thread pool with mutexes\n"
		       "7: use a thread pool with condition variables\n"
		       "8: use a queue implemented in a char device\n"
		       "9: Extra Credit: use a wait queue\n",
		       argv[0]);
		return -1;
	}

	port = atoi(argv[1]);
	accept_fd = server_create(port);
	if (accept_fd < 0) return -1;
	
	server_type = atoi(argv[2]);

	switch(server_type) {
	case SERVER_TYPE_ONE:
		server_single_request(accept_fd);
		break;
	case SERVER_TYPE_SINGLET:
		server_multiple_requests(accept_fd);
		break;
	case SERVER_TYPE_PROCESS:
		server_processes(accept_fd);
		break;
	case SERVER_TYPE_FORK_EXEC:
		server_dynamic(accept_fd);
		break;
	case SERVER_TYPE_SPAWN_THREAD:
		server_thread_per(accept_fd);
		break;
	case SERVER_TYPE_TASK_QUEUE:
		server_task_queue(accept_fd);
		break;
	case SERVER_TYPE_THREAD_POOL_MUTEX:
		server_thread_pool_mutex(accept_fd);
		break;
	case SERVER_TYPE_THREAD_POOL_CONDITIONS:
		server_thread_pool_conditions(accept_fd);
		break;
	case SERVER_TYPE_CHAR_DEVICE_QUEUE:
		server_char_device_queue(accept_fd);
		break;
	case SERVER_TYPE_WAIT_QUEUE:
		server_wait_queue(accept_fd);
		break;
	}
	close(accept_fd);

	return 0;
}
