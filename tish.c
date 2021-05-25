#include "tish.h"

static void signal_handler(int sig_num){
  switch (sig_num){
    case SIGTSTP:{
      signal_process(-1, SIGTSTP);
      break;
    }
    case SIGINT:{
      signal_process(-1, SIGINT);
      break;
    }
  default:
    break;
  }
}

static int last_ret = 0;
static int debug_flag = 0;

int main(int argc, char **argv, char *envp[]){
  //  on_exit(clean_up, NULL); // clean up everything on exit.
  start_up();
  parse_environ_variables(envp);
  char *buf = NULL, *buf_cpy = NULL, *arg1 = NULL;
  int alloc_count = 0, i = 0, num_paths = 0, buf_size = 0;
  char *path = find_path(envp), c; // path variables for various executables we can use.
  if (path == NULL){
    fprintf(stderr, "Error. System has no PATH variables.\n");
    exit(1);
  }

  // Ignoring SIGQUIT, so us sending it to a child wont QUIT entire process
  struct sigaction sigact;
  sigact.sa_handler = signal_handler;
  sigact.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  // attach sig handler to SIGINT, SIGSTOP (Ctrl-C, Ctrl-Z)
  if (sigaction(SIGINT, (const struct sigaction * restrict) &sigact, NULL) == -1){
    fprintf(stderr, "Error. Could not attach signal handler on SIGINT, error code %d", errno);
    return 1;
  }
  if (sigaction(SIGTSTP, (const struct sigaction * restrict) &sigact, NULL) == -1){
    fprintf(stderr, "Error. Could not attach signal handler on SIGSTOP, error code %d", errno);
    return 1;
  }
  if (sigaction(SIGQUIT, (const struct sigaction * restrict) &sigact, NULL) == -1){
    fprintf(stderr, "Error. Could not attach signal handler on SIGQUIT, error code %d", errno);
    return 1;
  }
  buf = (char *) malloc(BUF_SIZE); // have buffer in increments of PAGE_SIZE (4KB)
  MALLOC_ERR(buf, BUF_SIZE);
  buf_cpy = (char *) malloc(BUF_SIZE);
  MALLOC_ERR(buf_cpy, BUF_SIZE);
  // we will build a char * list of path's.
  int j = 0;
  while ((j = path[i]) != '\0'){ // counts ':' = counts # args - 1
    if (j == ':')
      num_paths++;
    i++;
  }
  i = 0;
  num_paths += 1;
  char *dup_path = strdup(path);
  replace_path(dup_path); // calling strtok on path will splice up the string. I need to have the full version for $PATH
  arg1 = strtok(path, ":");
  char *paths[num_paths];
  while (i < num_paths){
    paths[i] = arg1;
    arg1 = strtok(NULL, ":");
    i++;
  }
  // check if were given a command line argument. if so we should run the shell non-interactively.
  if (argc > 1){ // go ahead and call the handle sh function.
    if (!strncmp(argv[1], "-d", 2)){
      debug_flag = TRUE; // set global debugging ON
    }
    else{
      handle_sh(argv[1], num_paths, paths);
      goto exit;
    }
    if (argc > 2){
      if (!strncmp(argv[2], "-d", 2)){
        fprintf(stderr, "Error. Invalid argments.\n Usage: ./tish (-d) (filename)\n");
	last_ret = EINVAL;
	goto exit;
      }
      handle_sh(argv[2], num_paths, paths);
      goto exit;
    }
    if (argc > 3){
      fprintf(stderr, "Error. Too many arguments to 'tish'.\n");
      last_ret = EINVAL;
      goto exit;
    }
  }
  // main loop, prompts user for the command they wish to execute, and executes it.
  while (1){
    buf_size = 0;
    printf("tish> ");
    // grab input from the user.
    while ((c = fgetc(stdin)) != '\n'){
      buf[buf_size] = c;
      buf_size++;
      if (buf_size % BUF_SIZE == 0){
	buf = (char *) realloc(buf, buf_size+BUF_SIZE);
        REALLOC_ERR(buf, buf_size+BUF_SIZE);
	buf_cpy = (char *) realloc(buf_cpy, buf_size+BUF_SIZE);
        REALLOC_ERR(buf_cpy, buf_size+BUF_SIZE);
      }
    }
    buf[buf_size] = '\0'; // null terminator
    if (strcpy(buf_cpy, buf) == NULL){
      fprintf(stderr, "Error. Could not create a copy of input buffer.");
      exit(1); // FIXME: Is there a better way?
    }
    buf_cpy[buf_size] = '\0';
    arg1 = strtok(buf, " "); // using strtok to parse arguments
    if (buf_size > 0){
      c = buf[0];
      j = 0;
      while (buf[j] != '\0'){
	if (buf[j] == '#'){
	  buf[j] = '\0'; // dont execute anything with comments
	  buf_size = j;
	}
	j++;
      }
    }
    if (buf_size == 0 || buf[0] == -1)
      goto cont;
    if (!strcmp(arg1, "exit")){
      goto exit; // simply exit tish
    }
    else if (!strcmp(arg1, "sh")){
      last_ret = handle_sh(strtok(NULL, " "), num_paths, paths);
    }
    else{ // could be any external command. Time to utilize a void * array to have pointer to the various args
      last_ret = process_command(buf, buf_cpy, paths, num_paths);
    }
    cont:
      memset(buf, 0, buf_size);
      buf_size = 0; // reset the input buffer.
  }
  exit:
    return 0;
}

char *find_path(char *envp[]){
  int i = 0;
  while (envp[i] != NULL){
    if (!strncmp(envp[i], "PATH", 4)){
      return envp[i]+5;
    }
    i++;
  }
  return NULL;
}

int handle_kill(char *args[]){
  // kill doesnt necessarily kill process. It sends a signal to it.
  // PARSE SIGNAL NUM
  struct job *j = NULL;
  char *endptr = NULL;
  int signal_num = 0, ret = 0, ret2 = 0, job = 0;
  if (args[1][0] != '-'){
    fprintf(stderr, "Error. Invalid argument '%s' to kill.\n", args[1]);
    return -1;
  }
  // ignore the '-'
  args[1] = args[1] + 1;
  signal_num = strtol(args[1], &endptr, 10);
  if (endptr == NULL){
    fprintf(stderr, "Error. Invalid argument '%s' to kill.\n", args[1]);
    return -1;
  }
  // GET JOB NUM, SEE IF VALID
  job = strtol(args[2], &endptr, 10);
  if (endptr == NULL){
    fprintf(stderr, "Error. Invalid argument '%s' to kill.\n", args[1]);
    return -1;
  }
  j = find_job(job);
  if (j == NULL){
    fprintf(stderr, "Error. Job '%d' does not exist.\n", job);
    return -1;
  }
  ret = kill(j->pid, signal_num);
  ret2 = kill(j->exec_pid, signal_num);
  if (ret){
    if (errno == EINVAL)
      fprintf(stderr, "Error. Invalid SIGNAL '%d' to kill.\n", signal_num);
    else if (errno == EPERM)
      fprintf(stderr, "Error. Permission denied.\n");
    else
      fprintf(stderr, "Error. Invalid arguments.\n");
    return -1;
  }
  if (ret2){
    if (errno == EINVAL)
      fprintf(stderr, "Error. Invalid SIGNAL '%d' to kill.\n", signal_num);
    else if (errno == EPERM)
      fprintf(stderr, "Error. Permission denied.\n");
    else
      fprintf(stderr, "Error. Invalid arguments.\n");
    return -1;
  }
  switch(signal_num){
  case 1:{
    j->status = HUP;
    j->status_string = "Hangup";
    // PRINT DEBUG IF ENDED
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 2:{
    j->status = INT;
    j->status_string = "Interrupt";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 3:{
    j->status = QUIT;
    j->status_string = "Quit";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 4:{
    j->status = ILL;
    j->status_string = "Illegal Instruction";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 6:{
    j->status = ABRT;
    j->status_string = "Abort";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 8:{
    j->status = FPE;
    j->status_string = "Floating Point";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 9:{
    j->status = KILL;
    j->status_string = "Killed";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 11:{
    j->status = SEGV;
    j->status_string = "Invalid Memory";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 13:{
    j->status = PIPE;
    j->status_string = "Broken Pipe";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 14:{
    j->status = ALRM;
    j->status_string = "Alarm";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 15:{
    j->status = TERM;
    j->status_string = "Terminated";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 18:{
    j->status = RUNNING;
    j->status_string = "Running";
    if (debug_flag){
      fprintf(stderr, "CONTINUED: <%s>\n", j->process_name);
    }
    break;
  }
  case 19:{
    j->status = STOPPED;
    j->status_string = "Stopped";
    if (debug_flag){
      fprintf(stderr, "STOPPED: <%s>\n", j->process_name);
    }
    break;
  }
  case 20:{
    j->status = STOPPED;
    j->status_string = "Stopped";
    if (debug_flag){
      fprintf(stderr, "STOPPED: <%s>\n", j->process_name);
    }
    break;
  }
  case 21:{
    j->status = TTIN;
    j->status_string = "Terminal Input";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  case 22:{
    j->status = TTOU;
    j->status_string = "Terminal Output";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
    break;
  }
  default:{
    j->status = KILL;
    j->status_string = "Other(Term)";
    if (debug_flag){
      fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, -1);
    }
  }
  }
  return 0;
}

struct job *find_job(int job_num){ // find the specified job
  struct job *j = head->next;
  while (j != NULL){
    if (j->job_id == job_num)
      return j;
    j = j->next;
  }
  return NULL;
}

int handle_fg(char *args[]){
  // grab the desired job ID. If none, well grab first job we see.
  int job_id = 0, arg_count = 0, wstatus;
  char *endptr = NULL;
  int i = 1;
  struct job *j = NULL;
  while (args[i] != NULL){
    if (arg_count > 1){
      fprintf(stderr, "Error. Too many arguments to 'fg'.\n");
      return -1; // error. Too many arguments, misunderstanding the command.
    }
    arg_count++;
    job_id = strtol(args[i], &endptr, 10);
    if (endptr == NULL){
      fprintf(stderr, "Error. Second argument must be an integer.\n");
      return -1;
    }
    i++;
  }
  j = find_stopped_job(job_id); // find the next stopped job (needs to be stopped)
  if (j == NULL){
    fprintf(stderr, "Error. No jobs available to be placed in fg\n");
    return -1;
  }
  else if (j->status != STOPPED){
    fprintf(stderr, "Error. Requested job cannot be resumed; job must be STOPPED.\n");
    return -1;
  }
  // perform the resumption of job.
  j->status = RUNNING;
  j->fg = 1;
  j->status_string = "Running";
  kill(j->exec_pid, SIGCONT);
  kill(j->pid, SIGCONT);
  if (debug_flag){
      fprintf(stderr, "CONTINUED: <%s>\n", j->process_name);
  }
  waitpid(j->pid, &wstatus, __WALL | WUNTRACED);
  return 0;
}

int handle_bg(void *exe_args){
  // grab the desired job ID. If none, well grab first job we see.
  char **args = (char **) exe_args;
  int job_id = 0, arg_count = 1, ret = 0, wstatus;
  char *endptr = NULL;
  int i = 1;
  struct job *j = NULL;
  while (args[i] != NULL){
    if (arg_count == 2){
      fprintf(stderr, "Error. Too many arguments to 'fg'.\n");
      return -1; // error. Too many arguments, misunderstanding the command.
    }
    arg_count++;
    job_id = strtol(args[i], &endptr, 10);
    if (endptr == NULL){
      fprintf(stderr, "Error. Second argument must be an integer.\n");
      return -1;
    }
    i++;
  }
  j = find_stopped_job(job_id); // find the next stopped job (needs to be stopped)
  if (j == NULL){
    fprintf(stderr, "Error. No jobs available to be placed in fg\n");
    return -1;
  }
  else if (j->status != STOPPED){
    fprintf(stderr, "Error. Requested job cannot be resumed; job must be STOPPED.\n");
    return -1;
  }
  // perform the resumption of job.
  j->status = RUNNING;
  j->status_string = "Running";
  kill(j->exec_pid, SIGCONT);
  kill(j->pid, SIGCONT);
  if (debug_flag){
    fprintf(stderr, "CONTINUED (bg): <%s>\n", j->process_name);
  }
  printf("[%d] %d\n", j->job_id, j->pid);
  ret = waitpid(j->pid, &wstatus, __WALL| WUNTRACED | WNOHANG);
  if (ret == -1){
    fprintf(stderr, "Error. Cannot resume process'%d' properly.\n", j->pid);
  }
  return 0;
}

int resume_job(void *arg){
  int wstatus, ret = 0;
  struct job *j = (struct job *) arg;
  kill(j->pid, SIGCONT);
  ret = waitpid(j->pid, &wstatus, __WALL| WUNTRACED);
  if (ret == -1){
    fprintf(stderr, "Error. Cannot resume process'%d' properly.\n", j->pid);
    return -1;
  }
  else
    return 0;
}
  
struct job *find_stopped_job(int job_id){ // find a job we can resume in the fg. must be STOPPED to resume it.
  struct job *ptr = head->next;
  if (job_id){ // separate loop; looking for specific job
    while (ptr != NULL){
      if (ptr->job_id == job_id)
	return ptr;
      ptr = ptr->next;
    }
  }
  else{ // if not job_id provided, find first stopped job.
    while (ptr != NULL){
      if (ptr->status == STOPPED)
	return ptr;
      ptr = ptr->next;
    }
  }  
  return NULL;
}

int handle_pwd(char *buf, char* buf_cpy){
  int bg = 0, child_pid = 0, alloc_count = 0, wstatus = 0;
  void *stack = NULL, *stack_end = NULL;
  char *last = NULL;
  stack = malloc(BUF_SIZE);
  MALLOC_ERR(stack, BUF_SIZE);
  stack_end = stack + BUF_SIZE;
  struct job *j = NULL;
  last = strtok(NULL, " ");
  if (last != NULL && !strcmp(last, "&")){
    bg = 1;
    j = create_job(buf_cpy);
  }
  if ((child_pid = clone(&pwd, stack_end, CLONE_VM | CLONE_IO, (void *) j)) == -1){ // fork for the child process to do the work.
    if (pwd() != 0)
      fprintf(stderr, "Error. Could not find or execute command 'pwd'. Error code: %d\n", errno);
  }
  if (bg){
    printf("[%d] %d\n", j->job_id, child_pid);
    waitpid(child_pid, &wstatus, WNOHANG | __WCLONE);
    if (!WIFEXITED(wstatus))
      fprintf(stderr, "Error. Could not find or execute command 'pwd'.\n");
  }
  else{
    waitpid(child_pid, &wstatus, __WCLONE | WUNTRACED);
    if (!WIFEXITED(wstatus))
      fprintf(stderr, "Error. Could not find or execute command 'pwd'.\n");
  }
  free(stack);
  return 0;
}

struct job *create_job(char *buf_cpy){
  int alloc_count = 0;
  struct job *j = (struct job *) malloc(sizeof(struct job));
  j->job_id = get_job_id(); // searches for next available job id
  j->pid = -1;
  j->status = RUNNING;
  j->status_string = "Running";
  j->process_name = (char *) malloc(strlen(buf_cpy)*sizeof(char));
  MALLOC_ERR(j->process_name, strlen(buf_cpy)*sizeof(char));
  if (strncpy(j->process_name, buf_cpy, strlen(buf_cpy)) == NULL){
    fprintf(stderr,"Error. Process name could not be copied effectively.");
    j->process_name = "N/A";
  }
  j->prev = NULL;
  j->next = NULL;
  return j;
}

int pwd(){
  char *cwd = getcwd(NULL, 0);// job finished
  if (cwd == NULL){
    fprintf(stderr, "Error. Could not find current working directory.\n");
    return errno;
  }
  printf("%s\n", cwd);
  return 0;
}

int cd(char *dir){
  int status = chdir(dir);
  if (status){
    switch(errno){
    case EACCES:{
      fprintf(stderr, "Error changing to directory '%s'. Permission denied.\n", dir);
      break;
    }
    case ENOTDIR: {
      fprintf(stderr, "Error changing to directory '%s'. Not a directory.\n", dir);
      break;
    }
    case ENOENT: {
      fprintf(stderr, "Error changing to directory '%s'. Path does not exist.\n", dir);
      break;
    }
    default:
      fprintf(stderr, "Error changing to directory '%s'. Error code %d.\n", dir, errno);
    }
    return errno;
  }
  return 0;
}

int ls(char *buf){
  int i = 0, ret = 0;
  char *ptr = NULL, *arg = NULL;
  while ((ptr = strchr(buf, ' ')) != NULL){ // counts spaces = counts # args - 1 = counts # args except 'ls'
    i++;
  }
  char *args[i];
  args[0] = "/bin/ls";
  i = 1;
  while ((arg = strtok(NULL, " ")) != NULL){
    args[i] = arg;
    i++;
  }
  args[i] = (char *) NULL;
  ret = execv("/bin/ls", (char** const) args);
  if (!ret)
    return errno;
  else
    return 0;
}

char *find_exe_path(char *buf, char *paths[], int paths_count){
  char *b;
  // FIXME: size of malloc
  b = (char *) malloc(BUF_SIZE);
  char *arg = buf;// technically, buf stops at the first arg. 
  int i = 0;
  while (i < paths_count){
    memset(b, 0, BUF_SIZE);
    strncpy(b, paths[i], strlen(paths[i]));
    b += strlen(paths[i]);
    b[0] = '/';
    b += 1;
    strncpy(b, arg, strlen(arg));
    b -= 1;
    b -= strlen(paths[i]);
    if (!access(b, F_OK)){
      if (!access(b, X_OK)){
	errno = 0;
        return b;
      }
    }
    i++;
  }
  return NULL;
}

int process_command(char *buf, char *buf_cpy, char *paths[], int paths_count){
  char *ptr = NULL, *arg = NULL, *new_var;
  int i = 0, ret = 0, ret2 = 0, bg = 0, alloc_count = 0, child_pid = 0, wstatus, redir = 0, arg_count = 0; // counter for number of args.
  ptr = buf_cpy;
  while ((ptr = strchr(ptr, ' ')) != NULL){ // counts spaces = counts # args - 1 = counts # args except 'ls'
    ptr = 1+ptr;
    i++;
  }
  char *args[arg_count];
  char *filename = NULL;
  char *option = NULL;
  struct job *j = NULL;
  void *stack = NULL, *stack_end = NULL;
  // Will need to check for each item in path.
  arg_count = 1;
  // switch statement could go here.//
  if (!strncmp(buf, "echo", 4)){ // perform 'echo' command
    args[0] = "echo";
  }
  else if (!strncmp(buf, "pwd", 3)) { // pwd command
    args[0] = "pwd";
  }
  else if (!strncmp(buf, "cd", 2)) { // cd command
    args[0] = "cd";
  }
  else if (!strncmp(buf, "jobs", 4)){ // jobs command
    args[0] = "jobs";
  }
  else if (!strncmp(buf, "fg", 2)){ // fg command
    args[0] = "fg";
  }
  else if (!strncmp(buf, "bg", 2)){ // bg command
    args[0] = "bg";
  }
  else if (!strncmp(buf, "kill", 4)){ // kill command
    args[0] = "kill";
  }
  else if (buf[0] == '.' && buf[1] == '/'){ // refers to an executable.
    args[0] = buf;
  }
  else if ((ptr = strchr(buf, '=')) != NULL){ // variable set-ing
    new_var = strdup(buf);
    new_var +=1;
    ptr = strchr(new_var, '=');
    ptr[0] = '\0';
    add_environ_var(new_var, ptr+1);
    return 0; // no child process needed to set a new variable!
  }
  else{
    args[0] = find_exe_path(buf, paths, paths_count);
    if (args[0] == NULL){
      fprintf(stderr, "Error. Command '%s' not found.\n", buf);
      return EINVAL;
    }
  }
  i = 1;
  while ((arg = strtok(NULL, " ")) != NULL){
    if (strchr(arg, '<') != NULL || strchr(arg, '>') != NULL){
      redir = TRUE;
      option = arg;
    }
    else if (redir && filename != NULL){
      break; // silently ignore other argments.
    }
    else if (redir)
      filename = arg;
    else{
      args[i] = arg;
      arg_count++;
      i++;
    }
  }
  // If its an internal command related to jobs, we need to execute in context of parent, i.e. no clone!
  j = create_job(buf_cpy);
  if (j == NULL){
    fprintf(stderr, "Error. Could not create a job for the command '%s'.\n", buf);
    return EAGAIN;
  }
  // ONLY ADD JOB TO LIST IF ITS NON-INTERNAL. NO POINT IF ITS INTERNAL
  if (strncmp(args[0], "jobs", 4) && strncmp(args[0], "kill", 4) && strncmp(args[0], "fg", 2) && strncmp(args[0], "bg", 2))
    add_job(j);
  j->parent_pid = getpid();
  if (!strcmp(args[i-1], "&")){// indicates this process is to be run in the background
    args[i-1] = (char *) NULL;
    bg = TRUE;
    j->fg = 0;
  }
  else{
    args[i] = (char *) NULL;
    j->fg = 1;
  }
  if (!strncmp(args[0], "fg", 2) || !strncmp(args[0], "bg", 2) || !strncmp(args[0], "kill", 4)){
    return tish_handle(args, option, filename, redir, buf_cpy);
  }
  stack = malloc(BUF_SIZE);
  MALLOC_ERR(stack, BUF_SIZE);
  stack_end = stack + BUF_SIZE;
  void *clone_args[7];
  clone_args[0] = args[0];
  clone_args[1] = args;
  clone_args[2] = j;
  clone_args[3] = &redir;
  clone_args[4] = option;
  clone_args[5] = filename;
  clone_args[6] = &arg_count;
  if (redir){ // clone the process to handle the command. Clone allows us to update job when its finished and share VM with parent.
    if ((child_pid = clone(&exe_command, stack_end, CLONE_VM | CLONE_IO | CLONE_FS, (void *) clone_args)) == -1){ // fork for the child process to do the work.
      if (pwd(buf) != 0){
        fprintf(stderr, "Error. Could not execute command '%s'\n", args[0]);
	return child_pid;
      }
    }
  }
  else{
    if ((child_pid = clone(&exe_command, stack_end, CLONE_VM | CLONE_IO | CLONE_FILES | CLONE_FS, (void *) clone_args)) == -1){ // fork for the child process to do the work.
      if (pwd(buf) != 0){
        fprintf(stderr, "Error. Could not execute command '%s'\n", args[0]);
	return child_pid;
      }
    }
  }
  if (bg == TRUE){
    waitpid(child_pid, &wstatus, __WALL| WUNTRACED);
    if (!WIFSTOPPED(wstatus))
      return errno;
    printf("[%d] %d\n", j->job_id, child_pid);
    ret = kill(j->exec_pid, SIGCONT); // give child process the all clear to go
    ret2 = kill(child_pid, SIGCONT); // give child process the all clear to go
    if (ret || ret2){
      fprintf(stderr, "Error. Process handling command '%s' unreachable.\n", buf);
      return errno;
    }
  }
  else{
    waitpid(child_pid, &wstatus, __WALL| WUNTRACED);
    //free(stack);
    ret = WEXITSTATUS(wstatus);
    if (!WIFEXITED(wstatus) && !WIFSTOPPED(wstatus))
      return errno;
    }
  return ret;
}

void reap_children(){
  struct job *j = head->next;
  int ret = 0, wstatus;
  while (j != NULL){
    if (j->status != DONE){ // check if the process ended!
      ret = waitpid(j->pid, &wstatus, __WALL | WNOHANG);
      if (WIFSIGNALED(wstatus))
	printf("signaled.\n");
      if (WIFSTOPPED(wstatus))
	printf("stopped.\n");
      if (!WIFEXITED(wstatus))
	printf("exited\n");
      printf("pid: %d, ret: %d, wstatus: %d\n", j->pid, ret, wstatus);
    }
    j = j->next;
  }
}

int exe_command(void *clone_args){
  signal(SIGTSTP, SIG_DFL); // ignore SIGSTP, as the parent process will take care of that for us.
  signal(SIGQUIT, SIG_DFL); // sigquit should work on child processes.
  void **args = (void **) clone_args;
  char *name = (char *) args[0];
  char ** const exe_args = (char ** const) args[1];
  int redirect = *((int *) args[3]);
  char *option = args[4];
  char *filename = (char *) args[5];
  int arg_count = *((int *) args[6]);
  int fd_cpy = 0, fd2_cpy = 0, w_fd = 0, old_fd = 1, option_size = 0, right = FALSE, ret = 0, ret2 = 0;// right is direction of redirect
  // CHECK IF SPECIFIED FD WE ARE DIRECTING.
  if (redirect){
    exe_args[arg_count] = (char *) NULL;
    option_size = strlen(option);
    if (option_size > 2){
      fprintf(stderr, "Error. Invalid options for redirect operator '%s'", option);
      return EINVAL;
    }
    if (option_size == 2){//check the fd's we should redirect
      switch (option[0]){
      case '0':{
  	old_fd = 0;
  	break;
      }
      case '1':{
  	old_fd = 1;
  	break;
      }
      case '2': {
  	old_fd = 2;
  	break;
      }
      case '&': {
  	old_fd = 3; // represents 1 & 2
  	break;
      }
      default: {
  	fprintf(stderr, "Error. Invalid option for redirect operator '%c'", option[0]);
  	return EINVAL;
  	break;
      }
      }
      if (option[1] == '>')
  	right = TRUE;
      else
  	right = FALSE;
    }
    else{
      if (option[0] == '>'){
  	right = TRUE;
  	old_fd = 1;
      }
      else{
  	right = FALSE;
  	old_fd = 0;
      }
    }
    // OPEN FILE
    if (right){
      if ((w_fd = open(filename, O_WRONLY | O_CREAT, S_IRWXU)) < 0){
  	fprintf(stderr, "Error. Problem opening the desired file for write '%s'.\n", filename);
  	return EBADF;
      }
    }
    else{
      if ((w_fd = open(filename, O_RDONLY)) < 0){
  	fprintf(stderr, "Error. Problem opening the desired file for read '%s'.\n", filename);
  	return EBADF;
      }
    }
    // PERFORM THE REDIRECTION
    if (old_fd == 3){ // dup both stdout/stderr
      fd_cpy = dup(1);
      fd2_cpy = dup(2);
      ret = dup2(w_fd, 1);
      if (ret < 0)
	goto error;
      ret = dup2(w_fd, 2);
      if (ret < 0)
	goto error;
      close(w_fd);
    }
    else{
      fd_cpy = dup(old_fd);
      ret = dup2(w_fd, old_fd);
      if (ret < 0)
	goto error;
      close(w_fd);
    }
  }
  struct job *j = (struct job *) args[2];
  j->pid = getpid();
  // PRINT DEBUG IF RUNNING
  if (debug_flag){
    fprintf(stderr, "RUNNING: <%s>\n", j->process_name);
  }
  int child_pid, wstatus;
  if (!strncmp(name, "echo", 4)){
    if ((child_pid = fork()) == 0){
      ret = handle_echo(exe_args);
      if (ret){
        fprintf(stderr, "Error. Could not execute command '%s'\n", name);
        exit(1);
      }
      if (!strncmp(args[1], "$?", 2)) // dont reset the last_ret
	exit(last_ret);
      else
	exit(ret);
    }
  }
  else if (!strncmp(name, "pwd", 3)){
    if ((child_pid = fork()) == 0){
      ret = pwd();
      if (ret){
	fprintf(stderr, "Error. Could not execute command '%s'\n", name);
        exit(1);
      }
     exit(0);
    }
  }
  else if (!strncmp(name, "jobs", 4)){
    if ((child_pid = fork()) == 0){
      ret = jobs();
      if (ret){
	fprintf(stderr, "Error. Could not execute command '%s'\n", name);
        exit(1);
      }
     exit(0);
    }
  }
  else if (!strncmp(name, "cd", 2)){
    ret = cd(exe_args[1]);
    if (ret){
      fprintf(stderr, "Error. Could not execute command '%s'\n", name);
      return ret;
    }
  }
  else{
    if ((child_pid = fork()) == 0){
      ret = execv(name, exe_args);
        if (ret){
	  fprintf(stderr, "Error. Could not execute command '%s'\n", name);
          exit(1);
       }
      exit(ret);
    }
  }
  j->exec_pid = child_pid;
  if (!j->fg){
    kill(j->exec_pid, SIGTSTP); // wait for parent to give the go ahead.
    kill(j->pid, SIGTSTP); // wait for parent to give the go ahead.
  }
  waitpid(child_pid, &wstatus, 0);
  // PRINT DEBUG IF RNNING
  if (debug_flag){
    fprintf(stderr, "ENDED: <%s> (ret=%d)\n", j->process_name, wstatus);
  }
  // clean up, mark job as complete, etc.
  if (!WIFSTOPPED(wstatus) && !WIFEXITED(wstatus)){
    fprintf(stderr, "Error. Could not successfully execute command '%s'\n", name);
    return ENOEXEC;
  }
  ret2 = WEXITSTATUS(wstatus);
  j->status = DONE;
  j->status_string = "Done";
  j->fg = 0;
  if (redirect){
    if (old_fd == 3){
      ret = dup2(fd_cpy, 1);
      if (ret < 0)
        goto error;
      ret = dup2(fd2_cpy, 2);
      if (ret < 0)
        goto error;
    }
    else{
      ret = dup2(fd_cpy, old_fd);
      if (ret < 0)
     	goto error;
    }
  }
  last_ret = ret2;
  return ret2;
  error:
    fprintf(stderr, "Error. dup/dup2 could not be performed.\n");
    return errno;
}

int start_up(void){ // allocates memory for jobs list, etc.
  head = (struct job *) malloc(sizeof(struct job));
  head->process_name = "head";
  tail = head;
  h_var = (struct variable *) malloc(sizeof(struct variable));
  t_var = h_var;
  return 0;
}

void add_job(struct job *j){ // adding the job to the list.
  tail->next = j;
  tail->prev = tail;
  j->prev = tail;
  j->next = NULL;
  tail = j;
}

void rm_job(struct job *j){
  if (tail == j){
    tail = j->prev; // if its the tail, move the tail back one.
  }
  (j->prev)->next = j->next;
  if (j->next != NULL)
    (j->next)->prev = j->prev;
  free(j->process_name); // alloc'd it before.
  free(j); // completley free/remove the jobs in our list.
}

int jobs(void){ // iterates through the linked list, printing jobs information to the console.
  struct job *ptr = head->next, *prev = ptr;
  while (ptr != NULL && ptr != head){
    printf("[%d] %d %s\t\t%s\n", ptr->job_id, ptr->pid, ptr->status_string, ptr->process_name);      
    prev = ptr;
    ptr = ptr->next;
    if (prev != head && prev->status != RUNNING && prev->status != STOPPED){ // if the job is done, free it and remove it.
      rm_job(prev);
    }
  }
  return 0;
}

int get_job_id(){
  int id = 1;
  struct job *ptr = head->next;
  while(ptr != NULL){
    if (ptr->job_id == id){ // check next id, see if its available
      id++;
      ptr = head;
    }
    ptr = ptr->next;
  }
  return id;
}

struct job *find_running_process(){
  struct job *ptr = head->next;
  while (ptr != NULL){
    if (ptr->fg == 1 && ptr->status == RUNNING)
      return ptr;
    ptr = ptr->next;
  }
  return NULL;
}

int signal_process(int pid, int signal){
  struct job *j = NULL;
  if (pid == -1){
    j = find_running_process(); // finds the job running in fg, if any.
    if (j == NULL){
      return 0; // no running jobs. return normally.
    }
  }
  // a job is running. kill or stop it, according to inputs.
  if (signal == SIGTSTP){// stop the current running process, if there is one.
    kill(j->pid, SIGTSTP);
    kill(j->exec_pid, SIGTSTP);
    j->status = STOPPED;
    j->status_string = "Stopped";
    if (debug_flag){
      fprintf(stderr, "STOPPED: <%s>\n", j->process_name);
    }
  }
  else if (signal == SIGINT){// kill the current running process, if there is one.
    kill(j->pid, SIGINT);
    kill(j->exec_pid, SIGINT);
    j->status = KILLED;
    j->status_string = "Killed";
    if (debug_flag){
      fprintf(stderr, "INTERRUPTED: <%s> (ret=-1)\n", j->process_name);
    }
  }
  j->fg = 0;
  return 0;
}

int parse_environ_variables(char *envp[]){ // insert the given environ variables into our list of variables
  int envp_size = 0, j = 0; // get number of environment variables
  char *ptr = envp[envp_size];
  while (ptr != NULL){
    envp_size++;
    ptr = envp[envp_size];
  }
  char *equals = NULL, *name = NULL, *val = NULL, c;
  for (int i = 0; i < envp_size; i++){
    // need to parse the environment variables into name, value pairs. then create a struct and add them to list.
    equals = strchr(envp[i], '=');
    if (equals == NULL){
      fprintf(stderr, "Error. Error occured parsing system variables\n");
      return -1;
    }
    val = equals+1; // need to malloc the names :(
    name = (char *) malloc(strlen(envp[i]) - strlen(val) - 1);
    j = 0;
    while ((c = envp[i][j]) != '='){
      name[j] = c;
      j++;
    }
    name[j] = '\0';
    add_environ_var(name, val);
  }
  return 0;
}

void add_environ_var(char *name, char *value){ // add to our environment variable list
  struct variable *v = (struct variable *) malloc(sizeof(struct variable));
  v->name = name;
  v->value = value;
  t_var->next = v;
  v->prev = t_var;
  t_var = v;
}

int handle_echo(char *args[]){
  char *message = args[1];
  char *var = NULL;
  if (message == NULL){
    message = "";
  }
  if (strlen(message) < 1)
    return 0; // cant echo nothing!
  if (message[0] == '$'){ // references a variable in this case.
    var = message+1;
    if (message[1] == '\0')
      return 0; // cant echo nothing!
    if (message[1] == '?'){
      printf("%d\n", last_ret); // echo last return value
      return 0;
    } 
    message = find_variable(var); // find the references variable if it exists.
    if (message == NULL){
      fprintf(stderr, "Error. Variable '%s' does not exist.\n", var);
      return -1;
    }
  }
  else if (message[0] == '"'){ // its a string. need to parse rest of commands.
    printf("%s", args[1] + 1); // ignore the first "
    int i = 2;
    while (args[i] != NULL){
      if (strchr(args[i], '"')){
	int j = 0;
	char c;
	printf(" ");
	while ((c = args[i][j]) != '"'){
	  printf("%c", c);
	  j++;
	}
	printf("\n");
	return 0;
      }
      printf(" %s", args[i]);
      i++;
    }
    printf("\n");
    return 0;
  }
  printf("%s\n", message);
  return 0;
}

char *find_variable(char *var){
  struct variable *ptr = h_var->next;
  while (ptr != NULL){
    if (!strncmp(var, ptr->name, strlen(var)))
      return ptr->value;
    ptr = ptr->next;
  }
  return NULL;
}

void replace_path(char *path){
  // find struct holding PATH
  struct variable *v = h_var->next;
  while (v != NULL && strncmp(v->name, "PATH", 4))
    v = v->next;
  v->value = path;
}

int handle_sh(char *filename, int num_paths, char *paths[]){ // basically follows the same principle as the main loop in main func.
  // OPEN THE FILE FOR READING.
  int buf_size = 0, alloc_count = 0;
  FILE *f = NULL;
  char *buf = (char *) malloc(BUF_SIZE), *arg1 = NULL;
  char *buf_cpy = (char *) malloc(BUF_SIZE);
  //if ((fd = open(filename, O_RDONLY)) < 0){
  if ((f = fopen(filename, "r")) == NULL){
    fprintf(stderr, "Error. Could not open the file '%s' for command parsing.\n", filename);
    return errno;
  }
  int c = 0;
  while (c != EOF){
    buf_size = 0;
    // grab next command from file
    while ((c = fgetc(f)) != '\n' && c != EOF){
      buf[buf_size] = c;
      buf_size++;
      if (buf_size % BUF_SIZE == 0){
	buf = (char *) realloc(buf, buf_size+BUF_SIZE);
        REALLOC_ERR(buf, buf_size+BUF_SIZE);
	buf_cpy = (char *) realloc(buf_cpy, buf_size+BUF_SIZE);
        REALLOC_ERR(buf_cpy, buf_size+BUF_SIZE);
      }
    }
    if (buf_size == 0 || buf[0] == -1)
      continue;
    buf[buf_size] = '\0'; // null terminator
    if (buf[0] != '#'){ // if its a comment, ignore it
      if (strcpy(buf_cpy, buf) == NULL){
	fprintf(stderr, "Error. Could not create a copy of input buffer.");
	return -1;
      }
      buf_cpy[buf_size] = '\0';
      arg1 = strtok(buf, " "); // using strtok to parse arguments
      if (!strcmp(arg1, "exit")){
	goto exit; // simply exit tish
      }
      process_command(buf, buf_cpy, paths, num_paths);
    }
    memset(buf, 0, buf_size);
    buf_size = 0; // reset the input buffer.
  }
  exit:
    fflush(f);
    fclose(f);
    free(buf);
    free(buf_cpy);
    return 0;
}

int tish_handle(char *args[], char *option, char *filename, int redirect, char *buf){ // used to handle kill, bg, and fg
  int option_size = 0, old_fd = 0, w_fd = 0, fd_cpy = 0, fd2_cpy = 0, right = 0, ret = 0;
  if (redirect){
    option_size = strlen(option);
    if (option_size > 2){
      fprintf(stderr, "Error. Invalid options for redirect operator '%s'", option);
      return EINVAL;
    }
    if (option_size == 2){//check the fd's we should redirect
      switch (option[0]){
      case '0':{
  	old_fd = 0;
  	break;
      }
      case '1':{
  	old_fd = 1;
  	break;
      }
      case '2': {
  	old_fd = 2;
  	break;
      }
      case '&': {
  	old_fd = 3; // represents 1 & 2
  	break;
      }
      default: {
  	fprintf(stderr, "Error. Invalid option for redirect operator '%c'", option[0]);
  	return EINVAL;
  	break;
      }
      }
      if (option[1] == '>')
  	right = TRUE;
      else
  	right = FALSE;
    }
    else{
      if (option[0] == '>'){
  	right = TRUE;
  	old_fd = 1;
      }
      else{
  	right = FALSE;
  	old_fd = 0;
      }
    }
    // OPEN FILE
    if (right){
      if ((w_fd = open(filename, O_WRONLY | O_CREAT, S_IRWXU)) < 0){
  	fprintf(stderr, "Error. There was a problem opening the desired file for write '%s'.\n", filename);
  	return ENOENT;
      }
    }
    else{
      if ((w_fd = open(filename, O_RDONLY)) < 0){
  	fprintf(stderr, "Error. There was a problem opening the desired file for read '%s'.\n", filename);
  	return ENOENT;
      }
    }
    // PERFORM THE REDIRECTION
    if (old_fd == 3){ // dup both stdout/stderr
      fd_cpy = dup(1);
      fd2_cpy = dup(2);
      dup2(w_fd, 1);
      dup2(w_fd, 2);
      close(w_fd);
    }
    else{
      fd_cpy = dup(old_fd);
      dup2(w_fd, old_fd);
      close(w_fd);
    }
  }
  // PRINT DEBUG IF RUNNING
  if (debug_flag){
    fprintf(stderr, "RUNNING: <%s>\n", buf);
  }
  if (!strncmp(args[0], "fg", 2)){
    ret = handle_fg(args);
  }
  else if (!strncmp(args[0], "bg", 2)){
    ret = handle_bg(args);
  }
  else if (!strncmp(args[0], "kill", 4)){
    ret = handle_kill(args);
  }
  // PRINT DEBUG IF ENDED
  if (debug_flag){
    fprintf(stderr, "ENDED: <%s> (ret=%d)\n", buf, ret);
  }
  if (redirect){
    if (old_fd == 3){
      dup2(fd_cpy, 1);
      dup2(fd2_cpy, 2);
    }
    else
      dup2(fd_cpy, old_fd);
  }
  return ret;
}

void clean_up(){ // cleans up jobs, as well as variables stored.
  // CLEAN UP JOBS. KILL AND FREE
  struct job *j = tail, *jp = NULL;
  while (j != head){
    jp = j;
    j = j->prev;
    kill(jp->pid, 9); // SIGKILL process
    free(jp->process_name);
    free(jp);
  }
  free(j); // free the head last
  // FREE MEMORY FOR ENVIRONMENT VARIABLES
  struct variable *v = t_var, *vp = NULL;
  while (v != h_var){
    vp = v;
    v = v->prev;
    free(vp->name);
    free(vp);
  }
  free(v); // free the head last
}
