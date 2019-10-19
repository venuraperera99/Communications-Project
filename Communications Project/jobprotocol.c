// TODO: Use this file for helper functions (especially those you want available
// to both executables.

/* Example: Something like the function below might be useful

   // Find and return the location of the first newline character in a string
   // First argument is a string, second argument is the length of the string
   int find_newline(const char *buf, int len);

*/
#include "jobprotocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <unistd.h>

/* Returns the specific JobCommand enum value related to the
 * input str. Returns CMD_INVALID if no match is found.
 */
//JobCommand get_job_command(char*){
//
//}

/* Forks the process and launches a job executable. Allocates a
 * JobNode containing PID, stdout and stderr pipes, and returns
 * it. Returns NULL if the JobNode could not be created.
 */

JobNode* start_job(char * jobname, char * const args[]){

//	printf("%s\n", jobname);
//	printf("%s\n", args[0]);
        int result;
	JobNode *job = malloc(sizeof(struct job_node));
	job->pid = 0;
        int pipe1_fds[2];
	int pipe2_fds[2];
        if(pipe(pipe1_fds) == -1){
            perror("pipe");
        }
	if(pipe(pipe2_fds) == -1){
	    perror("pipe");
	}
        result = fork();
        if(result > 0){ // parent
            if(close(pipe1_fds[PIPE_READ]) == -1){
                perror("close PIPE_READ");
            }
            if(close(pipe1_fds[PIPE_WRITE]) == -1){
                perror("close PIPE_WRITE");
            }
	    if(close(pipe2_fds[PIPE_READ]) == -1){
		perror("close PIPE2_READ");
	    }
	    if(close(pipe2_fds[PIPE_WRITE]) == -1){
		perror("close PIPE2_WRITE");
	    }
	    job->stdout_fd = pipe1_fds[PIPE_READ];
            job->stderr_fd = pipe2_fds[PIPE_READ];
            job->pid = result;
            job->dead = 0;
            job->next = NULL;

        }
        else if(result == 0){ // child
	    if(close(pipe1_fds[PIPE_READ]) == -1){
                perror("close PIPE_READ CHILD");
            }
	    if(close(pipe2_fds[PIPE_READ]) == -1){
                perror("close PIPE2_READ CHILD");
            }
            dup2(pipe1_fds[PIPE_READ], STDOUT_FILENO);
	    dup2(pipe2_fds[PIPE_READ], STDERR_FILENO);
	    execv(jobname, args);
            perror("exec");
        }
	return job;
}

/* Adds the given job to the given list of jobs.
 * Returns 0 on success, -1 otherwise.
 */
int add_job(JobList* joblist, JobNode* job){

    if(joblist->count == MAX_JOBS){ // already max jobs cant add any more
	return -1;
    }
    struct job_node *current;
    current = joblist->first;
    if(joblist->count == 0){
	joblist->first = job;
	joblist->count++;
	return 0;
    }
    while(current->next != NULL){
	current = current->next;
    }
    current->next = job;
    joblist->count++;
    return 0;
}

/* Sends SIGKILL to the given job_pid only if it is part of the given
 * job list. Returns 0 if successful, 1 if it is not found, or -1 if
 * the kill command failed.
 */
int kill_job(JobList* joblist, int job_pid){
    struct job_node *current;
    current = joblist->first;
    while(current != NULL){
	if(current->pid == job_pid){
	    if(kill(current->pid, SIGKILL) == -1){
		return -1;
	    }
   	    return 0;
	}
	current = current->next;
    }
    return 1;
}

/* Removes a job from the given job list and frees it from memory.
 * Returns 0 if successful, or -1 if not found.
 */
int remove_job(JobList* joblist, int job_pid){
    struct job_node *current;
    struct job_node *prev;
    prev = joblist->first;
    current = prev->next;
    if(prev->pid == job_pid){
	free(joblist->first);
	joblist->first = current;
	joblist->count--;
	return 0;
    }
    while(current->next != NULL){
        if(current->pid == job_pid){
	    prev->next = current->next;
	    free(current);
	    joblist->count--;
	    return 0;
	}
        current = current->next;
	prev = prev->next;
    }
    return -1;
}

/* Marks a job as dead.
 * Returns 0 on success, or -1 if not found.
 */
int mark_job_dead(JobList *joblist, int job_pid, int deadvalue){
    struct job_node *current;
    current = joblist->first;
    while(current->next != NULL){
        if(current->pid == job_pid){
            current->dead = deadvalue;
            return 0;
        }
	current = current->next;
    }
    return -1;
}

/* Frees all memory held by a job list and resets it.
 * Returns 0 on success, -1 otherwise.
 */
int empty_job_list(JobList* joblist){

    if(joblist->count == 0){
	return -1;
    }
    struct job_node *current;
    struct job_node *temp;
    current = joblist->first;
    while(current != NULL){
	temp = current;
        current = current->next;
	free(temp);
	joblist->count--;
    }
    joblist = NULL;
    return 0;
}

/* Frees all memory held by a job node and its children.
 */
int delete_job_node(JobNode* job){
    struct job_node *current;
    struct job_node *temp;
    current = job;
    while(current != NULL){
        temp = current;
        current = current->next;
        free(temp);
    }
    job = NULL;
    return 0;
}

/* Kills all jobs. Return number of jobs in list. (Kills all dead jobs?)
 */
int kill_all_jobs(JobList *joblist){
//    int count = joblist->count;
    struct job_node *current;
    current = joblist->first;
    while(current != NULL){
        if(current->dead > 0){
	    if(remove_job(joblist, current->pid) == -1){
		perror("dead job not found kill_all_jobs");
	    }
//	    if(kill(current->pid, SIGKILL) == 0){
//	    joblist->count--;
//	    }
        }
        current = current->next;
    }
    return joblist->count;
}

/* Sends a kill signal to the job specified by job_node.
 * Return 0 on success, 1 if job_node is NULL, or -1 on failure.
 */
int kill_job_node(JobNode *job){

    if(job == NULL){
	return 1;
    }
    if(kill(job->pid, SIGKILL) == -1){
	return -1;
    }
    return 0;

}

/* Replaces the first '\n' or '\r\n' found in str with a null terminator.
 * Returns the index of the new first null terminator if found, or -1 if
 * not found.
 */
int remove_newline(char *buf, int inbuf){
    for(int i = 0; i < inbuf; i++){
	if(buf[i] == '\n'){
	    int index = i;
	    buf[index] = '\0';
	    return index;
	}
	else if(buf[i] == '\r' && buf[i+1] == '\n'){
	    buf[i] = '\0';
	    return i;
	}
    }
    return -1;
}

/* Replaces the first '\n' found in str with '\r', then
 * replaces the character after it with '\n'.
 * Returns 1 + index of '\n' on success, -1 if there is no newline,
 * or -2 if there's no space for a new character.
 */
int convert_to_crlf(char *buf, int inbuf){
    for(int i = 0; i < inbuf-1; i++){
	if(buf[i] == '\n'){
	    buf[i] = '\r';
	    buf[i+1] = '\n';
	    return i+2;
	}
    }
    if(buf[inbuf] == '\n'){
	return -2;
    }
    return -1;
}

/* Search the first n characters of buf for a network newline (\r\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_network_newline(const char *buf, int inbuf){
    for(int i = 0; i < inbuf-1; i++){
        if(buf[i] == '\r'){
            if(buf[i+1] == '\n'){
                int index = i+2;
                return index;
            }
        }
    }
    return -1;
}

/* Search the first n characters of buf for an unix newline (\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found.
 */
int find_unix_newline(const char *buf, int inbuf){
    for(int i = 0; i < inbuf;i++){
        if(buf[i] == '\n'){
            int index = i+1;
            return index;
        }
    }
    return -1;
}

/* Read as much as possible from file descriptor fd into the given buffer.
 * Returns number of bytes read, or 0 if fd closed, or -1 on error.
 */
int read_to_buf(int fd, Buffer* buffer){
    int num_read = read(fd, buffer->buf, BUFSIZE+2);
    buffer->inbuf = num_read;
    if(num_read ==  -1){
	return -1;
    }
    if(num_read == 0){
	return 0;
    }
    return num_read;
}

/* Returns a pointer to the next message in the buffer, sets msg_len to
 * the length of characters in the message, with the given newline type.
 * Returns NULL if no message is left.
 */
char* get_next_msg(Buffer* buffer, int* something, NewlineType something2){
    char *ptr = "hi";

    return ptr;
}

/* Removes consumed characters from the buffer and shifts the rest
 * to make space for new characters.
 */
void shift_buffer(Buffer * buffer){


    return;
}

/* Returns 1 if buffer is full, 0 otherwise.
 */
int is_buffer_full(Buffer * buffer){

    if(buffer->inbuf == BUFSIZE){
        return 1;
    }
    return 0;
}
