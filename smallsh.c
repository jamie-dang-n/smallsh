#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>

// Constants
#define TRUE 1
#define FALSE 0
#define MAX_COMMAND_LEN 2049

// **** Global Var -- FOREGROUND MODE ONLY
int foreground_mode = FALSE;

// **** Structs
// Struct to hold linked list of pids (for background processes)
typedef struct Node {
	pid_t data;
	struct Node* next;
} Node;

// Struct to hold user input
// User input format: command [arg1 arg2 ..] [< input_file] [> output_file] [&]
// Square brackets indicate optional parts, which can be set as NULL
// or 0. 
typedef struct Input {
	char* command;
	int num_args;
	char** args;
	char* input_file;
	char* output_file; 
	int is_background;
} Input;


// **** Initializers/Freeing Functions
// Initializer function to initialize default values for a new
// Input struct object
Input init_input() {
	Input in;
	in.command = NULL;
	in.num_args = 0;
	in.args = NULL;
	in.input_file = NULL;
	in.output_file = NULL;
	in.is_background = FALSE;
	return in;
}

// Free the whole linked list for background pids
void free_nodes(Node** head) {
	Node* temp = *head;
	while (temp) {
		*head = (*head)->next;
		free(temp);
		temp = *head;
	}
	*head = NULL;
}

// Frees all dynamic data members in Input struct object
void free_input(Input* in) {
	// free command
	if (in->command) {
		free(in->command);
		in->command = NULL;
	}
	// free arguments (2D array)
	if (in->args) {
		int i = 0;
		for (i = 0; i < in->num_args; i++) {
			free(in->args[i]);
			in->args[i] = NULL;
		}
		free(in->args);
		in->args = NULL;
	}
	// free input file
	if (in->input_file) {
		free(in->input_file);
		in->input_file = NULL;
	}
	// free output file
	if (in->output_file) {
		free(in->output_file);
		in->output_file = NULL;
	}
}

// **** Built-in Commands
// exit_smallsh(): Exits smallsh entirely; no arguments taken
// will kill any other background processes it has started
void exit_smallsh(Node* pid_head) {
	int exit_code;
	if (pid_head) {
		Node* temp = pid_head;
		while (temp) {
			kill(temp->data, SIGKILL);	
			waitpid(temp->data, &exit_code, 0);
			temp = temp->next;
		}
	}
}

// cd_smallsh(): Change current working directory of smallsh
// if given no arguments, change the working directory
// to the path stored in the $HOME environment var.
// if given an argument, change working directory
// to the path given by the argument.
// returns an exit status int, where -1 indicates
// an error and 0 indicates success. 
int cd_smallsh(char* path) {
	int exit_code = 0; // successful by default
	if (path) {
		// change working directory to the given path
		exit_code = chdir(path);
	} else {
		// change working directory to the $HOME environment var
		exit_code = chdir(getenv("HOME")); 
	}
	return exit_code;
}

// status_smallsh(): Get status -> print out the exit code (exit status)
// OR the terminating signal of the most recently executed foreground command.
// takes no arguments and parses exit_code from waitpid().
void status_smallsh(int exit_code) {
	if (WIFSIGNALED(exit_code)) {
		// process terminated by a signal
		// extract terminating signal
		int term_sig = WTERMSIG(exit_code);
		printf("terminated by signal %d\n", term_sig);
	} else if (WIFEXITED(exit_code)) {
		// process exited normally
		// extract exit status number
		int exit_status = WEXITSTATUS(exit_code);
		printf("exit value %d\n", exit_status);

	}

}

// **** Other Commands Support
// helper function to clean up memory from main() in early exit
void dynamic_mem_clean(Input* in, Node** pid_head) {
	// release all dynamically allocated mem
	// free input struct
	free_input(in);
	// Free pid array
	if (pid_head) {
		free_nodes(pid_head);
	}
}

// helper function to free cmd from the execute_smallsh() function
void free_cmd(char*** cmd, int num_fields) {
	// free cmd
	if (*cmd) {
		int i = 0;
		for (i = 0; i < num_fields; i++) {
			free((*cmd)[i]);
			(*cmd)[i] = NULL;
		}
		free(*cmd);
		*cmd = NULL;
	}
}

// Helper function to exit out of child process during error
// and clean up cmd from the execute_smallsh() function
void exit_in_err_child(int* exit_code, char* message, Input* in, Node** pid_head, char*** cmd, int num_fields) {
	printf("%s\n", message);
	*exit_code = 1;
	// free memory from main()
	dynamic_mem_clean(in, pid_head);
	// free memory from execute_smallsh()
	free_cmd(cmd, num_fields);
	exit(1);
}

// expand_smallsh(): Support expansion of shell variable $$
// Upon finding the character sequence in the command
// string, it must be expanded to the PID of smallsh itself
void expand_smallsh(char** cmd_string) {
	pid_t pid = getpid();

	// stringify pid
	char pid_str[10]; // maximum pid is 327680, do 10 digits to be safe
	sprintf(pid_str, "%d", (int) pid);

	int i = 0;
	char* command_copy = (char*) calloc(MAX_COMMAND_LEN * sizeof(char), sizeof(char));

	for (i = 0; i < strlen(*cmd_string); i++) {
		if ((i + 1) < strlen(*cmd_string) 
				&& (*cmd_string)[i + 1] == '$' && (*cmd_string)[i] == '$') {
			// concatenate pid
			strcat(command_copy, pid_str);
			i++;
		} else {
			// concatenate the current char
			char temp[2]; // stores current char + null terminator to strcat
			temp[0] = (*cmd_string)[i];
			temp[1] = '\0';
			strcat(command_copy, temp);	
		}
	}

	// copy expanded command to in->command
	free(*cmd_string);
	(*cmd_string) = (char*) calloc((strlen(command_copy) + 1) * sizeof(char), sizeof(char));
	strcpy((*cmd_string), command_copy);

	// free command copy
	free(command_copy);
	command_copy = NULL;
}

// *** Signal Handlers
// Define and activate sigaction to default behavior on SIGINT
void default_sigint() {
	// sigaction to default on SIGINT
	struct sigaction sigint_default = {0};
	sigint_default.sa_handler = SIG_DFL;
	sigaction(SIGINT, &sigint_default, NULL);
}

// Define and activate sigaction to ignore SIGINT
void ignore_sigint() {
	// sigaction to ignore SIGINT
	struct sigaction sigint_ign = {0};
	sigint_ign.sa_handler = SIG_IGN;
	sigaction(SIGINT, &sigint_ign, NULL);
}

// Signal handler for parent process on SIGTSTP
// uses write() so that the function is re-entrant
void set_foreground_mode() {
	// flush buffer
	fflush(stdout);
	if (foreground_mode == TRUE) {
		char message[MAX_COMMAND_LEN] = "\nExiting foreground-only mode\n: ";
		write(STDOUT_FILENO, message, strlen(message)); // re-entrant write to console
		foreground_mode = FALSE;
	} else {
		char message[MAX_COMMAND_LEN] = "\nEntering foreground-only mode (& is now ignored)\n: ";
		write(STDOUT_FILENO, message, strlen(message)); // re-entrant write to console
		foreground_mode = TRUE;
	}
}

// Set up signal handlers for parent processes
void parent_handler() {
	// ignore SIGINT
	ignore_sigint();

	// SIGTSTP foreground only mode setup
	struct sigaction foreground_sigtstp = {0};
	foreground_sigtstp.sa_handler = set_foreground_mode;
	sigfillset(&foreground_sigtstp.sa_mask); // block all signals

	// set of flags to modify behavior of signal - set the SA_RESTART flag
	// SA_RESTART allows interrupted operations to be restarted
	foreground_sigtstp.sa_flags = SA_RESTART;

	// use sigaction() to set foreground-only 
	// mode on SIGTSTP
	sigaction(SIGTSTP, &foreground_sigtstp, NULL);
}

// set up signal handlers for child processes
void child_handler(Input* in) {
	// initialize sigaction to ignore SIGTSTP for ALL child processes
	struct sigaction sa_sigtstp = {0};
	// ignore SIGTSTP
	sa_sigtstp.sa_handler = SIG_IGN;
	sigfillset(&sa_sigtstp.sa_mask); // block all signals
	// set of flags to modify behavior of signal - no flags
	sa_sigtstp.sa_flags = 0; 
	// activate sigaction struct with sigaction() to ignore SIGTSTP
	sigaction(SIGTSTP, &sa_sigtstp, NULL);

	if (in->is_background == TRUE) {
		// background ignores SIGINT
		ignore_sigint();
	} else {
		// foreground defaults on SIGINT
		default_sigint();
	}
}

// execute_smallsh(): Executes non-built-in commands by creating
// a child process with fork() and executing
// with exec()-family functions.
// returns an exit code int from the command
// executed.
int execute_smallsh(Input* in, Node** pid_head) {
	int exit_code = 0;
	pid_t fork_pid = fork(); // fork a new process

	if (fork_pid == 0) {
		// the child process -> execute the given command with execvp
		// setup child signal handlers
		child_handler(in);

		// Create the command to be passed into execvp
		int num_fields = in->num_args + 2; // +2 for command and null termination
		char** cmd = (char**) malloc(sizeof(char*) * num_fields);
		int i = 0;

		// allocate first field to be the commmand name
		cmd[0] = (char*) malloc((strlen(in->command) + 1) * sizeof(char));
		strcpy(cmd[0], in->command);

		// copy over all args
		for (i = 0; i < in->num_args; i++) {
			if (i + 1 < num_fields - 1) {
				cmd[i+1] = (char*) malloc((strlen(in->args[i]) + 1) * sizeof(char));
				strcpy(cmd[i+1], in->args[i]);
			}
		}

		// allocate last field to be the null terminator
		cmd[num_fields - 1] = NULL;

		// Redirect input/output
		if (in->input_file) {
			// open() to open input file for reading only
			int input_file = open(in->input_file, O_RDONLY);
			if (input_file == -1) {
				// file couldn't be opened, set exit code 1 and exit
				exit_in_err_child(&exit_code, "Error: input file could not be opened", in, pid_head, &cmd, num_fields);
			} else {
				dup2(input_file, STDIN_FILENO);
			}
		} else {
			// use dup2() to redirect to /dev/null if background
			// process with no input file
			if (in->is_background == 1) {
				int open_null = open("/dev/null", O_RDONLY);
				dup2(open_null, STDIN_FILENO);
			}
		}

		if (in->output_file) {
			// open() to open output file for writing, creating, or truncating
			int output_file = open(in->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
			if (output_file == -1) {
				// file couldn't be opened, set exit code 1 and exit
				exit_in_err_child(&exit_code, "Error: output file could not be opened", in, pid_head, &cmd, num_fields);
			} else {
				dup2(output_file, STDOUT_FILENO);
			}
		} else {
			// use dup2() to redirect to /dev/null if background
			// process with no output file
			if (in->is_background == 1) {
				int open_null = open("/dev/null", O_WRONLY);
				dup2(open_null, STDOUT_FILENO);
			}
		}

		// if anything had an error, don't execute any commands
		if (exit_code != 1) {
			// execute command with execvp()
			exit_code = execvp(in->command, cmd);
			// free cmd
			free_cmd(&cmd, num_fields);

			// handle error from execvp
			if (exit_code == -1) {
				exit_in_err_child(&exit_code, "Error: command could not be processed", in, pid_head, &cmd, num_fields);
			}
		}

	} else if (fork_pid == -1) {
		// forking fails - give error exit code 1
		exit_code = 1;
	} else {
		// the parent process
		// setup parent signal handlers
		parent_handler();

		if (in->is_background == 0) {
			// not background - wait on it in the foreground
			waitpid(fork_pid, &exit_code, 0);
		   	// foreground process has completed -- check exit code
			if (WIFSIGNALED(exit_code)) {
				printf("terminated by signal %d\n", WTERMSIG(exit_code));
			} 
			// if exited successfully, exit_code will be parsed in status_smallsh()
		} else {
			// is background - update the linked list of pids with new background process to wait on later
			Node* new_node = (Node*) malloc(sizeof(Node));
			new_node->data = fork_pid;
			new_node->next = *pid_head;
			*pid_head = new_node;
			// print out confirmation message
			printf("Background pid is %d.\n", (int) fork_pid);
		}

	}
	return exit_code;
}

// parse_input(): Parse a line of user input in the format
// command [arg1 arg2 ..] [< input_file] [> output_file] [&]
// to separate out each part into the Input struct
// returns an int exit code with the following codes:
// TRUE: successfully parsed
// FALSE: comment -- no action done
int parse_input(char** input, Input* in) {
	int exit_code = TRUE;
	char** tokens = NULL;
	int num_tokens = 1; // there will always at least be a command
	int i = 0; // for loop counter

	// expand the input
	expand_smallsh(input);


	// number of tokens = num of space delims + 1
	for (i = 0; (*input)[i] != '\0'; i++) {
		if ((*input)[i] == ' ') {
			num_tokens++;
		}
	}	

	// Allocate Dynamic Array of Tokens
	tokens = (char**) malloc(num_tokens * sizeof(char*));

	// tokenize data, set up array of tokens
	char* command = strtok(*input, " ");
	tokens[0] = (char*) calloc((strlen(command) + 1) * sizeof(char), sizeof(char));
	strcpy(tokens[0], command);
	for (i = 1; i < num_tokens; i++) {
		char* dummyPtr = strtok(NULL, " ");
		tokens[i] = (char*) calloc((strlen(dummyPtr) + 1) * sizeof(char), sizeof(char));
		strcpy(tokens[i], dummyPtr);
	}

	// check if command begins with #
	// if it does, free tokens, don't do anything, and return 1
	if (tokens[0][0] == '#') {
		exit_code = FALSE;
	} else {
		// Copy command into Input struct
		in->command = (char*) calloc(sizeof(char) * (strlen(tokens[0]) + 1), sizeof(char));
		strcpy(in->command, tokens[0]); // will always be at least 1 command

		// count up number of args
		int num_args = 0;
		int end_of_args = 1;
		int j = 1; // used for indexing tokens[]
		while (end_of_args && j < num_tokens) {	
			// arguments follow commands, so num spaces before < or > or \n indicate an arg
			if ((strcmp(tokens[j], "<") == 0) || (strcmp(tokens[j], ">") == 0) || (strcmp(tokens[j], "&") == 0)) {
				end_of_args = 0;
			} else {
				num_args++;
				j++;
			}
		}

		// set background flag (if applicable)
		// background flag should always be the last token
		if(strcmp(tokens[num_tokens - 1], "&") == 0 && foreground_mode == FALSE) {
			// set flag
			in->is_background = TRUE;
		}

		// allocate args, copy all args into Input struct (if applicable)
		// (all args are after index 0 -> i = 1 to i < 1 + num_args)
		if (num_args > 0) {
			in->num_args = num_args;
			in->args = (char**) malloc(num_args * sizeof(char*));
			j = 0; // reset j to index in.args[])
			for (i = 1; i < num_args + 1; i++) {
				in->args[j] = (char*) calloc((strlen(tokens[i]) + 1) * sizeof(char), sizeof(char));
				strcpy(in->args[j], tokens[i]);
				j++;
			}	
		}

		// get input and output file into Input struct (if applicable)
		// start from num_args, increment to the end
		for (i = num_args; i < num_tokens; i++) {
			if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0) {
				// next index is a file
				int file_name_index = i + 1;
				int file_name_size = strlen(tokens[file_name_index]) + 1;
				if (strcmp(tokens[i], "<") == 0) {
					// the file is an input file
					in->input_file = 
						(char*) calloc(sizeof(char) * (file_name_size + 1), sizeof(char));
					strcpy(in->input_file, tokens[file_name_index]);
				} else if (strcmp(tokens[i], ">") == 0) {
					// the file is an output file
					in->output_file = 
						(char*) calloc(sizeof(char) * (file_name_size + 1), sizeof(char));
					strcpy(in->output_file, tokens[file_name_index]);
				}
			}
		}



	}

	// Free 2D array of tokens
	for (i = 0; i < num_tokens; i++) {
		free(tokens[i]);
		tokens[i] = NULL;
	}
	free(tokens);
	tokens = NULL;

	return exit_code;
}

// get_input(): returns an integer status that indicates whether to continue
// running the shell or stop it. continuing = -1 indicates
// "exit" as the struct object command -> call exit_smallsh()
int get_input(Input* in, int* parse_status) {
	int program_status = TRUE; // Default continuing = True
	char* input = NULL;
	size_t input_buffer_size;

	// flush buffer, print colon
	fflush(stdout);
	printf(": ");	

	// get user input
	if (getline(&input, &input_buffer_size, stdin) != -1) {
		size_t input_length = strlen(input);

		// trim newline, replace with '\0'
		input[input_length - 1] = '\0';

		// Parse input if string isn't empty
		// (pressing enter makes input_length = 1)
		if (input_length > 1) {
			*parse_status = parse_input(&input, in);
		}

		// if a string was successfully parsed,
		// check if it is "exit". if it is, 
		// stop the program running
		if (*parse_status == TRUE) {
			if (strcmp(in->command, "exit") == 0) {
				program_status = FALSE;
			}
		}

		// Clean dynamic char array
		if (input) {
			free(input);
			input = NULL;
		}
	} else {
		program_status = FALSE; // stop continuing
	}

	return program_status;
}

// wait_background(): waits on background processes. if a background 
// process is done, it removes that process from the linked pid list
// with head pid_head.
void wait_background(Node** pid_head) {
	Node* temp = *pid_head;
	int exit_code;
	while (temp) {
		pid_t wait_result = waitpid(temp->data, &exit_code, WNOHANG);
		// wait_result = 0 if still waiting
		// wait_result = -1 if error state/term by signal
		// wait_result = terminated pid if done waiting
		if (wait_result != 0) {
			// a child was terminated
			// check exit code
			if (WIFEXITED(exit_code) && wait_result != -1) {
				// child terminated normally
				// WIFEXITED(exit_code) returns True if child terminated normally
				printf("background pid %d is done: exit value %d\n", (int) wait_result, WEXITSTATUS(exit_code));
			} else if (WIFSIGNALED(exit_code)) {
				// child terminated by a signal
				// WIFSIGNALED(exit_code) returns True if child was terminated by a signal
				// get signal code only if WIFSTOPPED() is True
				printf("background pid %d is done: terminated by signal %d\n", (int) wait_result, WTERMSIG(exit_code));
			}

			// remove terminated child from pid_head (linked list)
			// wait_result stores pid of removed child
			Node* curr = *pid_head;
			if (curr->data == wait_result) {
				// case 1: remove from head
				*pid_head = (*pid_head)->next;
				curr->next = NULL;
				free(curr);
			} else {
				Node* prev = curr;
				while (curr->next) {
					// case 2: remove from tail or middle
					prev = curr;
					curr = curr->next;
					if (curr->data == wait_result) {
						prev->next = curr->next;
						curr->next = NULL;
						free(curr);	
					}
				}
			}

		} 

		// condition checks edge case that there was only 1 process in the linked list
		if (*pid_head) {
			// if the linked list still exists, keep iterating
			temp = temp->next;
		} else {
			// otherwise, set temp to NULL and exit the loop
			temp = NULL;
		}

	}

}

// **** main(): Get user input, direct execution in the small shell
int main() {
	// setup signal handler for parent
	parent_handler();
	// Program status variables
	int program_status = TRUE;
	int parse_status = TRUE;
	int exit_code = 0;

	// pid linked list is for background pids only (foreground waits 
	// immediately, background waits after)
	Node* pid_head = NULL; // empty linked list of PIDs

	do {
		// remove terminated pids from the pid list
		wait_background(&pid_head);	

		// get user input and parse it
		Input in = init_input();
		program_status = get_input(&in, &parse_status);

		// direct execution to the correct functions
		// if program_status == FALSE, stop running
		// the shell.
		if (program_status == FALSE){
			// run exit
			exit_smallsh(pid_head);
		} else if (in.command) {
			if (strcmp(in.command, "cd") == 0) {
				// run cd
				if (in.args) {
					exit_code = cd_smallsh(in.args[0]);
				} else {
					exit_code = cd_smallsh(NULL);
				}
			} else if (strcmp(in.command, "status") == 0) {
				// run status
				status_smallsh(exit_code);				
			} else {
				// run execute
				exit_code = execute_smallsh(&in, &pid_head);
			}
		}

		// Free Input struct
		free_input(&in);

	} while (program_status == TRUE);


	// Free pid array
	if (pid_head) {
		free_nodes(&pid_head);
	}

	return 0;
}
