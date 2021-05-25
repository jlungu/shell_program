## Custom Shell Program
Program functions as a shell program similar to 'bash' or 'zsh'. Listed below is a README explaining how I performed each of the listed actions. Written entirely is C! 


### OVERALL STRUCTURE OF THE CODE
My code starts off in the main function. It parses the paths in the $PATH variable, and puts them in an array.
This way the code can easily search for external programs we try to execute. It also attaches two signal handlers
to the SIGTSTP and SIGINT signals; this is to provide alternative behavior to a user pressing Ctrl-Z or Ctrl-C; now, when either is pressed my code has special handling of the stopping or interrupting of the running process. Had
I not included handlers, the entire tish program would be stopped or interrupted, respectively.

The main function moves on to parse any extra optional command line arguments to tish; namely either '-d' to
indicate the user wants debug output, or 'filename', where filename is the name of the file the user wants to run
commands from, to have the shell function non-interactively. Commands are output directly to stdout.

Should the shell be requested to be interactive, the code moves on to an infinite loop. Before each command, it
clears the input buffer (which expands according to how much input the user gives), prints "tish> ", and waits for
user input.

After each command the user gives (commands end with a "\n"), the function sends off the command to the
'process_command' function, where the command is subject to further processing and is run. Should the user request
an 'exit' command, the function exits the infinite loop and exits/terminates completely. Should the user request
a 'sh' function followed by a file, the main function calls on the 'process_sh' function instead.

Commands are parsed using the strtok function.

### PROCESS_FUNCTION
This function is the meat and potatoes of the shell. It is here where the shell clones off to another process to
handle the user's inputted command.

This function parses the remaining arguments to the command using the strtok function. It first checks to see if
the user is requesting an internal or external command. Should it be an external command, the code calls on a separate function to locate where the requested command lies (using the paths from $PATH variable parsed earlier. Code
tries to find a match with each path, i.e. path1/code, path2/code, etc).

This function will also specifically check if the user is trying to declare a shell variable, and if so it handles
that straight away without cloning at all, for simplicity.

The process function, in its remaining argument parsing, checks if the command is to be redirected to a file, or
is to take input from a file. Should it find either, it records the necessary info (direction, filename, etc).

The function then creates a "job" struct (more about this later) for every command (even if its running in the
foreground). This was done to simplify the notion of recording jobs information to better locate past jobs and
store necessary info for the job itself.

The function then checks if the '&' argment was provided, and if so will run the given command in the backgrond.
Afterwords, it prepares to ship off the command to a clone(2) sys call. The clone(2) function is called,
redirecting the new process to the exe_command function. Should the user request the job to be run in the
background, the shell does not wait for it to finish, calling waitpid(2) with the WNOHANG flag. Otherwise, the
shell does wait for the cloned proces to finish, using the 'waitpid(2)' sys call normally.

The cloned process will then attempt to execute the given command. It is here where redirection is set up, done
using dup/dup2. More on that later. For most commands, the cloned process will actually fork, and execute the
process in the forked process. This is done so that the cloned process can 'clean up' the finished process
(mark it as completed, grab its return value, etc). Had I not forked, the execv command, which i use for external,
would completly replace the cloned process image, leaving no room for clean up.

After a cloned process finished, assuming the parent waits for it it will collect its return code, and return to
the main loop, awaiting the next command.

### JOB STRUCTURES
In my code I created a struct to house job information. It looks like this:
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

As noted by the 'next' and 'previous' pointers, the jobs are stored in a LINKED LIST data structure. I
chose a linked list for various reasons, mainly due to the ease of listing the jobs running in the background,
or previously ran. O(n) time to look through all of the jobs, which I believe would be the most efficiant you
can get to look at 'n' jobs. A hash table was an idea of mine, as it would be easier to locate jobs when
calling 'kill', however it would be quite annoying to list through the jobs. I could have used two data structure,
but I decided not to, for fear of over complicating things.

### VARIABLE STRUCT
I also create a struct to house tish variables, including environment variables. It looks like this:
struct variable { // a simple way to keep track of the environment variables in BASH
  char *name;
  char *value;
  struct variable *next; // I like linked lists. This is going to be a linked list
  struct variable *prev;
};

These variables too are stored in a linked list. Looking at it now, It may have been slightly more efficiant to store the variables
in a hash table, for easy O(1) lookup and since we arent ever listing out variables. If I had a bit more time, I suppose I would
change that. However, O(n) lookup of the linked list isnt too bad, plus it is easier to work with since I created the data structure
itself.

## SPECIFIC COMMANDS

### PWD
This command simply calls the getcwd(3) function. Fast an easy, just prints out what that function returns.

### CD
Cd naturally uses the chdir(2) command. For this command, I specially added the CLONE_FS flag to clone(2) func.
This way a cloned process can move the parent processes cwd as well.

Other flags I used for the clone process include CLONE_VM(so clones can modify jobs list of parent),
CLONE_IO(to ensure the clone/parent share same io to user), CLONE_FILES(except when duping, as dupes should only
dupe cloned fds so we done affect parent).

### JOBS
Jobs is super simple. It just lists through all of the jobs in our job list. It should be noted that I typically
add jobs to the job list even if they are running in the fg; this is to ensure I can find the job to kill should
the user type Ctrl-Z or Ctrl-C.

Ctrl-Z and Ctrl-C, by the way, are caught using signal handlers in the parent. The child process ignores the
custom signal handlers of the parent, as the parent ones call an external function to find the process running
in the fg (designated by a flag in the job struct), and sends the appropriate signal (SIGTSTP and SIGINT) to the
running process.

Whenever a process is signaled, its status string listed in jobs is updated appropriately.

### FG & BG
Relatively simply. It should be noted fg, bg, and kill arent run in cloned processes. I could not get them to
work in those clone processes, mainly because kill/bg/fg's parent would become the child process,
and I need the parent to be tish main.

Fg and Bg simply send a SIGCONT to the designated job with the user inputted job id(NOT process id).
After sending it, fg will wait for the process to finish, bg wont and will ask the user for the next command.

Waiting for processes is again done with waitpid(2), using WNOHANG if not waiting, __WALL | WUNTRACED in both
cases. __WALL waits for clones and forks. I suppoes __WCLONE wold work too, however.

### KILL
Kill command works quite simply. Let it be known that the HW doc mentioned the kill command should have been given the
JOB ID, not the PROCESS ID. The Linux kill command accepts the process ID, but following the HW doc my code accepts
the job_id listed in 'jobs'

The kill command searches for the specific job with that ID, and it sends the signal the user requested to the
process. It enters a switch statement to see which SIGNAL was requested, and sets the job status as necessary.

### REDIRECTION
Redirection works by using dup/dup2. For example, with '>' operator, the code follows something like this:

open(fd, filename...) // opens requested file at fd 'fd'

dup(stdin)// to save stdin fd
dup2(fd, stdin); // stdin now points to the opened fd
close(fd);

Then, using the saved stdin, it restore stdin to normal.

I used dup/dup2 since the HW doc mentioned it, and I found this process of the dups to be the easiest.

Redirection to stdin follows a similar process, except for stdin not stdout.

### ECHO
Echo is quite simple. It just prints back what the user gave 'echo' as args.

As for the special 'echo $?' command, I stored the previous return of each process in a global variable named
last_ret. It seemed like the most appropriate way to share this var among all children, who would set it once
their 'fork' process completed.

It should be noted echoing strings with multiple words needs to be enclosed in "". I.e.
- echo "This is what i mean"
This is how i was able to know when to stop looking for args. I realize now how to fix it so it works without
"", but I am currently running out of time :(.

Return vals were extracted using the wstatus param of waitpid(). Using WEXITSTATUS()

### VARIABLES
As noted before, I store variables in a linked list of variable structs.

I utilize the third param of main() to extract environment variables upon start of the program, and store them
in that linked list

When a user attempts to create a new variable, the code will ignore the $ sign and split the statement
into two , left and right of the '=' sign. (Note. Variables are declated $VAR=var, whitespace DOES MATTER).

The left of the sign becomes the 'name', the right becomes the 'value'.
echo $VAR works by recognizing the arg starts with $, and finding the var with name VAR by searching the linked
list.

### NON INTERACTIVE SUPPORT
Non interactive support is implemented, and is how I test the program in the test scripts. It is done fairly
simply;
- if the sh command is called, it opens the file given and parses it line by line, executing each line of the
file one by one. It does ignore comments

- if tish is given a parameter of a filename, it runs the 'sh' function straight away and parses the file,
running each command one by one as in sh. It does NOT enter an infinite loop in this case

### DEBUGGING
If the debugging flag (-d) is given, tish prints out RUNNING/ENDED when a command starts/finishes. It does this
by storing the 'debug_flag' in another global variable, so every function has access and can see if they should
print debug info easily.

### RUNNING THE CODE
'make tish' will generate an executable named 'tish'
- It can be run using ./tish
'make tests' will make all of my test shell scripts and automatically run them
'make clean' will remove the tish exectables and *.o files.

Thanks for reading!
