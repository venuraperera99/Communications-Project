#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>

#include "socket.h"
#include "jobprotocol.h"

#define QUEUE_LENGTH 5
#define MAX_CLIENTS 20

#ifndef JOBS_DIR
    #define JOBS_DIR "jobs/"
#endif

// Global list of jobs
JobList job_list;

// Flag to keep track of SIGINT received
int sigint_received;

/* SIGINT handler:
 * We are just raising the sigint_received flag here. Our program will
 * periodically check to see if this flag has been raised, and any necessary
 * work will be done in main() and/or the helper functions. Write your signal
 * handlers with care, to avoid issues with async-signal-safety.
 */
void sigint_handler(int code) {
    sigint_received = 1;
}

// TODO: SIGCHLD (child stopped or terminated) handler: mark jobs as dead
void sigchld_handler(int code);

/* Return the highest fd between all clients and job pipes.
 */
int get_highest_fd(int listen_fd, Client *clients, JobList *job_list){
    int max_fd = listen_fd;
    for(int i = 0; i < MAX_CLIENTS; i++){
        if(clients[i].socket_fd > max_fd){
            max_fd = clients[i].socket_fd;
        }
    }
    struct job_node *current;
    current = job_list->first;
    while(current != NULL){
        if(current->stdout_fd > max_fd){
	    max_fd = current->stdout_fd;
	}
	if(current->stderr_fd > max_fd){
	    max_fd = current->stderr_fd;
	}
        current = current->next;
    }
    return max_fd;

}

/*
 *  Client management
 */

/* Accept a connection and adds them to list of clients.
 * Return the new client's file descriptor or -1 on error.
 */
int setup_new_client(int listen_fd, Client *clients){

    int user_index = 0;
    while (user_index < MAX_CLIENTS && clients[user_index].socket_fd != -1) {
        user_index++;
    }

    int client_fd = accept_connection(listen_fd);
    if (client_fd < 0) {
        return -1;
    }

    if (user_index >= MAX_CLIENTS) {
        fprintf(stderr, "server: max concurrent connections\n");
        close(client_fd);
        return -1;
    }

    clients[user_index].socket_fd = client_fd;
    return client_fd;
}

/* Closes a client and removes it from the list of clients.
 * Return the highest fd between all clients.
 */
int remove_client(int listen_fd, int client_index, Client *clients, JobList *job_list){
	if(clients[client_index].socket_fd != -1){
	    clients[client_index].socket_fd = -1;
	    clients[client_index].buffer.buf[0] = '\0';
	    clients[client_index].buffer.inbuf = 0;
	}
	return get_highest_fd(listen_fd, clients, job_list);
}

/* Read message from client and act accordingly.
 * Return their fd if it has been closed or 0 otherwise.
 */
int process_client_request(Client *client, JobList *job_list, fd_set *all_fds){
    if(client->socket_fd != -1){
	int fd = client->socket_fd;
	int num_read = read_to_buf(fd ,&(client->buffer)); // Read to clients buffer
	if(num_read == 0){
	    return fd;
	}
	else if(num_read == -1){
	    perror("Reading Error\n");
	    return fd;
	}

	// checks if the command is too long
	int where = find_network_newline(client->buffer.buf, client->buffer.inbuf);
	if(where == -1 && client->buffer.inbuf > BUFSIZE){  // TODO or is it >= BUFSIZE
	    char *too_long = "*(SERVER)* Buffer from job %d is full. Aborting job.\r\n";
	    char inv[BUFSIZE + 2];
	    strcpy(inv, too_long);
	    if(write(client->socket_fd, inv, strlen(inv)) == -1){
		perror("write invalid command length failed\n");
		return fd;
	    }
	}
        if(where != -1){ // find_network_newline passed which means command is 1 str with \r\n
            client->buffer.buf[where-2] = '\0';
        }

	// Parsing the command to check if its legal
	char str[BUFSIZE];
	strcpy(str, client->buffer.buf);
	int ll = strlen(str);
        char *token = strtok(str, " ");
	int where2 = find_network_newline(token, strlen(token));
        if(where2 != -1){ // find_network_newline passed which means command is 1 str with \r\n
	    token[where2-2] = '\0';
	}
        if(strcmp(token, "jobs") != 0 && strcmp(token, "run") != 0 && strcmp(token, "watch") != 0 && strcmp(token, "kill") != 0){
            printf("[SERVER] Invalid command: %s", client->buffer.buf);
        }
	if(strcmp(token, "jobs") == 0){ // print list of jobs and return 0
	    if(job_list->count == 0){
		char *zero_jobs = "[SERVER] No currently running jobs\r\n";
		if(write(client->socket_fd, zero_jobs, strlen(zero_jobs)) == -1){
		    perror("couldnt write no running jobs to client");
		    return fd;
		}
	    }
	    else{
	        char msg[BUFSIZE+2];
	        struct job_node *current;
    	        current = job_list->first;
	        char process_id[BUFSIZE];
		strcpy(msg, "[SERVER]");
    	        while(current != NULL){
		    strcat(msg, " ");
		    sprintf(process_id, "%d", current->pid);
		    strcat(msg, process_id);
                    current = current->next;
    	        }
	        strcat(msg, "\r\n");
	        if(write(client->socket_fd, msg, strlen(msg)) == -1){
                    perror("couldnt write the running jobs pid to client");
		    return fd;
                }
		int l = strlen(msg);
                for(int i = 0; i < l; i++){
                    process_id[i] = '\0';
                    msg[i] = '\0';
                }
	    }
	}
	else if(strcmp(token, "run") == 0){ // runs jobcommand
	    if(job_list->count < MAX_JOBS){
	        token = strtok(NULL, " "); // gets jobname
		char exe_file[BUFSIZE];
		//printf("%d\n", snprintf(exe_file, BUFSIZE, "%s%s", JOBS_DIR, token));
		if(snprintf(exe_file, BUFSIZE, "%s%s", JOBS_DIR, token) < 0){
			perror("Job doesnt exist");
			return fd;
		}
		struct stat statbuff;
    		if (lstat(exe_file, &statbuff) == -1){

        		return 0;
    		}
		char *command_args[BUFSIZE];
		int arg_counter = 0;
		token = strtok(NULL, " ");
	        while(token != NULL){ // While there are tokens (args) in string
		    int where4 = find_network_newline(token, strlen(token));
        	    if(where4 != -1){ // fixes the last arg
            	        token[where4-2] = '\0';
        	    }
		    command_args[arg_counter] = token;
		    token = strtok(NULL, " ");
                }

		JobNode *job_node = start_job(exe_file, command_args);
//	        if(job_node != NULL){
		    add_job(job_list, job_node);
		    FD_SET(job_node->stdout_fd, all_fds);
		    FD_SET(job_node->stderr_fd, all_fds);
//		}
		char job_created[BUFSIZE];
                if(snprintf(job_created, BUFSIZE, "[SERVER] Job %d created\r\n", job_node->pid) < 0){
                        perror("Job created but cannot print out name");
                }
		if(write(fd, job_created, strlen(job_created)) == -1){
                    perror("writing to client after too many jobs failed");
                    return fd;
                }
		return 0;
	    }
	    else{
		char *max_jobs = "[SERVER] MAXJOBS exceeded\r\n";
		if(write(fd, max_jobs, strlen(max_jobs)) == -1){
		    perror("writing to client after too many jobs failed");
		    return fd;
		}
	    }
	}
	else if(strcmp(token, "kill") == 0){ // kills given pid
	    token = strtok(NULL, " "); // gets the pid
	    int kill_this_pid = strtol(token, NULL, 10);
	    int killed_job = kill_job(job_list, kill_this_pid);
	    if(killed_job == 1){ // job not found
		char *kill_msg = "[SERVER] Job %d not found\r\n";
		char final_kill_write[BUFSIZE];
		snprintf(final_kill_write, BUFSIZE, kill_msg, kill_this_pid);
		if(write(fd, final_kill_write, strlen(final_kill_write)) == -1){
		    perror("Error writing job not found when trying to kill with kill_job");
		}
	    }
	    else if(killed_job == -1){ // error
		perror("error finding job and killing it with kill_job");
	    }
	    else{ // success
		mark_job_dead(job_list, kill_this_pid, kill_this_pid+1);
		remove_job(job_list, kill_this_pid);
	    }
	}
	else if(strcmp(token, "watch") == 0){ // watches clients

	}
	for(int i = 0; i < ll; i++){
            str[i] = '\0';
        }

    }
    return 0;
}

/* Read characters from fd and store them in buffer. Announce each message found
 * to watchers of job_node with the given format, eg. "[JOB %d] %s\n".
 */
void process_job_output(JobNode *job_node, int fd, Buffer *buffer, char *format){
    int num_read = read(fd, buffer->buf, BUFSIZE);
    printf("BUFFER IN PROCESS JOB OUTPUT IS: %s\n", buffer->buf);
    if(num_read == -1){
	perror("Error reading from process_job_output");
    }
    else if(num_read == 0){
	perror("file fd is closed process_job_output");
    }
    else{
	printf(format, job_node->pid, buffer->buf);
    }
}

/* Frees up all memory and exits.
 */
void clean_exit(int listen_fd, Client *clients, JobList *job_list, int exit_status){
    int i = 0;
    char *buf = "[SERVER] Shutting down\r\n";
    char msg[BUFSIZE + 2];
    strcpy(msg ,buf);
    while(clients[i].socket_fd != -1){
	if(write(clients[i].socket_fd, msg, strlen(msg)) == -1){
	    perror("write failed\n");
	    exit(1);
	}
	close(clients[i].socket_fd);
	i++;
    }
    empty_job_list(job_list);
    exit(exit_status);
}

int main(void) {
    // This line causes stdout and stderr not to be buffered.
    // Don't change this! Necessary for autotesting.
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    struct sockaddr_in *self = init_server_addr(PORT);
    // TODO: Set up server socket
    int listen_fd = setup_server_socket(self, QUEUE_LENGTH);


    // TODO: Set up SIGCHLD handler
/*    struct sigaction newact_sigchld;
    newact_sigchld.sa_handler = sigchld_handler;
    newact_sigchld.sa_flags = 0;
    sigemptyset(&newact_sigchld.sa_mask);
    sigaction(SIGCHLD, &newact_sigchld, NULL);
*/
    // TODO: Set up SIGINT handler
    struct sigaction newact_sigint;
    newact_sigint.sa_handler = sigint_handler;
    newact_sigint.sa_flags = 0;
    sigemptyset(&newact_sigint.sa_mask);
    sigaction(SIGINT, &newact_sigint, NULL);

    // TODO: Initialize client tracking structure (array list)
    Client clients[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket_fd = -1;
	clients[i].buffer.inbuf = 0;
	clients[i].buffer.buf[0] = '\0';
    }

    // TODO: Initialize job tracking structure (linked list)
    //job_list = malloc(sizeof(struct job_list));
    job_list.first = malloc(sizeof(struct job_node));
    job_list.count = 0;
    job_list.first->next = NULL;
    job_list.first->pid = 0;
    job_list.first->stdout_fd = 0;
    job_list.first->stderr_fd = 0;
    job_list.first->dead = 0;

    // TODO: Set up fd set(s) that we want to pass to select()
    int max_fd = listen_fd;
    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(listen_fd, &all_fds);

    while (1) {
        // Use select to wait on fds, also perform any necessary checks---------------------------------------------------------------------------------------------------------------------------------------
        // for errors or received signals

        // select updates the fd_set it receives, so we always use a copy and retain the original.
        listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
	if(sigint_received == 1){
	    break;
	}
        if (nready == -1) {
            perror("server: select\n");
            exit(1);
        }
        // Accept incoming connections-------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	if (FD_ISSET(listen_fd, &listen_fds)) {
            int client_fd = setup_new_client(listen_fd, clients);
            if (client_fd < 0) {
                continue;
            }
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            printf("Accepted connection\n");
        }

        // Check our job pipes, update max_fd if we got children---------------------------------------------------------------------------------------------------------------------------------------------

	// Updating max_fd
//	max_fd = get_highest_fd(listen_fd, clients, &job_list);
	// Checking jobs for msgs waiting to be outputted
/*	struct job_node *current = NULL;
	current = job_list.first;
	char *format = "[JOB %d] %s\n";
	char *format2 = "*(JOB %d)* %s\n";
	while(current != NULL){
	    if(FD_ISSET(current->stdout_fd, &listen_fds)){
  	        process_job_output(current, current->stdout_fd, &(current->stdout_buffer), format);
	    }
	    if(FD_ISSET(current->stderr_fd, &listen_fds)){
		process_job_output(current, current->stderr_fd, &(current->stderr_buffer), format2);
	    }
	    current = current->next;
	}
*/      // Check on all the connected clients, process any requests-----------------------------------------------------------------------------------------------------------------------------------------
        // or deal with any dead connections etc.

	// Check the clients
	for(int i = 0; i < MAX_CLIENTS; i++){
	    if(clients[i].socket_fd > -1 && FD_ISSET(clients[i].socket_fd, &listen_fds)){
		// Process Requests
		int processed_request_fd = process_client_request(&clients[i], &job_list, &all_fds);
		if(processed_request_fd > 0){// closed
		    FD_CLR(processed_request_fd, &all_fds);
		    // Client Termination
		    if(close(processed_request_fd) == -1){
			perror("Closing Request Failed\n");
		    }
		    printf("[CLIENT %d] Connection closed\n", clients[i].socket_fd);
		    remove_client(listen_fd, i, clients, &job_list);
		}
		else{
		    // Logging client msgs
		    char *client_msg = "[CLIENT %d] %s\n";
		    printf(client_msg, clients[i].socket_fd, clients[i].buffer.buf);
		}
	    }
	}
	if(sigint_received == 1){ // TODO probably not needed clean up if not needed
	    break;
	}
    }
    clean_exit(listen_fd, clients, &job_list, 0);


    free(self);
    close(listen_fd);
    return 0;
}

