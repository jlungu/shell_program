
// struct functions to house the job information
struct job {
  int job_id; // job's id (1, 2, 3)
  int pid; // process' ID for the job.
  int exec_pid; // execv's fork process ID
  int status; // RUNNING, STOPPED, DONE, KILLED, etc
  int fg; // tells system this job is running in the foreground.
  int parent_pid; // parent's process ID
  char *status_string;
  char *process_name;
  struct job *next;
  struct job *prev;
};
// will be utiliing a linked list to house the job information.
struct job *head;
struct job *tail;

struct variable { // a simple way to keep track of the environment variables in BASH
  char *name;
  char *value;
  struct variable *next; // I like linked lists. This is going to be a linked list
  struct variable *prev;
};

struct variable *h_var;
struct variable *t_var;

// VARIOUS DEFINITIONS
#define _GNU_SOURCE
#include <linux/sched.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <signal.h>

#define TRUE 1
#define FALSE 0
#define RUNNING 10
#define STOPPED 11
#define DONE 12
#define ERROR 13
#define KILLED 14
#define HUP 15
#define INT 16
#define QUIT 17
#define ILL 18
#define ABRT 19
#define FPE 20
#define KILL 21
#define SEGV 22
#define PIPE 23
#define ALRM 24
#define TERM 25
#define CONT 26
#define STOP 27
#define TSTOP 28
#define TTIN 29
#define TTOU 30
#define BUF_SIZE getpagesize()
#define MALLOC_ERR(ptr, size) {			\
  while (ptr == NULL){ \
    if (alloc_count == 5){ \
      fprintf(stderr, "Error allocating memory in function '%s'. Out of Memory.", __func__); \
      exit(1); /* FIXME: Exit gracefully using on_exit(3) */ \
    } \
    fprintf(stderr, "Error allocating memory in function '%s'. Re-trying in a moment", __func__); \
    alloc_count++; \
    sleep(1); \
    ptr = (char *) malloc(size);			\
  } \
}
#define REALLOC_ERR(ptr, size) {		\
  while (ptr == NULL){ \
    if (alloc_count == 5){ \
      fprintf(stderr, "Error reallocating memory in function '%s'. Out of Memory.", __func__); \
      exit(1); /* FIXME: Exit gracefully using on_exit(3) */ \
    } \
    fprintf(stderr, "Error reallocating memory in function '%s'. Re-trying in a moment", __func__); \
    alloc_count++; \
    sleep(1); \
    ptr = (char *) realloc(ptr, size);		\
  } \
} 

#define MYDEBUG printf("DEBUGGER :|: MADE IT TO LINE %d\n", __LINE__); exit(1);
extern int pwd();
extern int cd(char *dir);
extern int ls(char *buf);
extern int jobs(void);
extern int start_up(void);
extern void add_job(struct job *j);
extern int get_job_id(void);
extern int handle_pwd(char *buf, char *buf_cpy);
extern int process_command(char *buf, char *buf_cpy, char *paths[], int paths_count);
extern char *find_exe_path(char *buf, char *paths[], int paths_count);
extern struct job *create_job(char *buf_cpy);
extern int exe_command(void *args);
extern struct job *find_stopped_job(int job_id);
extern int signal_process(int job_id, int signal);
extern struct job *find_running_process();
extern int handle_fg(char *args[]);
extern int handle_bg(void *args);
extern int resume_job(void *arg);
extern void reap_children();
extern int redirect_file(char *args[], char *option, char *filename, int arg_count);
extern int parse_environ_variables(char *envp[]);
extern void add_environ_var(char *name, char *value);
extern int handle_echo(char *args[]);
extern char *find_variable(char *var);
extern void replace_path(char *path);
extern struct job *find_job(int job_num);
extern int handle_kill(char *args[]);
extern struct job *find_job(int job_num);
extern int handle_sh(char *filename, int num_paths, char *paths[]);
extern int tish_handle(char *args[], char *option, char *filename, int redirect, char *buf);
extern char *find_path(char *envp[]); // find the path variable amongst environ variables
extern void clean_up();
