/*********************************************************************************
** Author: James Meehan
** Coure: CS344 
** Project: smallsh
** Date:  5/25/2018
** Desc:  This program works as its own shell in C, similar to bash. It redirects
** input and output and allows for foreground and background processes.  It also
** supports three built-in functions, status (status of last exited foreground
** process), cd (change directory), and exit.
*********************************************************************************/

// The lectures and notes were used extensively to create this program. 
// Where noted, stackoverflow posts were also used to help create some small pieces of the program 

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>

// global variables
int argCount  = 0;
int bgCount = 0;
int fpONLY = 0;		// flag for foreground processes only
int bgPID = -5;		// store background pid
int bgPIDS[100];	// store background pids
pid_t parent;

void exitStatus(int status);
void catchSIGTSTP(int signo);
void catchSIGQUIT(int signo);

int main()
{
	// get pid of parent
	parent = getpid();

	// create signal handler structs (taken from lecture materials)
	// create ignore signal handler
	struct sigaction ignore_action = {0};
	ignore_action.sa_handler = SIG_IGN;

	// create default signal handler
	struct sigaction default_action = {0};
	default_action.sa_handler = SIG_DFL;

	// create handler for SIGINT and set it to default for the parent
	struct sigaction SIGINT_action = {0};
	sigaction(SIGINT, &ignore_action, NULL);
	
	// create signal handler for SIGTSTP.  This will be used to toggle b etween foreground and background modes
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	// create handler for SIGQUIT.  This will terminate all child processes when the user enters "exit"
	struct sigaction SIGQUIT_action = {0};
	SIGQUIT_action.sa_handler = catchSIGQUIT;
	sigfillset(&SIGQUIT_action.sa_mask);
	sigaction(SIGQUIT, &SIGQUIT_action, NULL);

	// set up/initialize some variables before the while loop starts
	char* args[512];  // this will hold user entered commands, argument,s and files that will be sent to execvp
	char* userInput = NULL;  
	char* outputFile = NULL;
	char* inputFile = NULL;
	int childExit = 0;  // initialize exit status for child

	// this while loop will run until user enters "exit" or the program is killed
	while (1)
	{
		int foreground = 1;  // initialize each process to foreground process to start
		int argCount = 0;  // holds the number of arguments the user entered
		int inputRedirect = 0;  // flag to indicate if input redirection is needed
		int outputRedirect = 0; // falg to indicate if output redirection is needed
		memset(args, '\0', sizeof(args));  // clear the args array on each iteration
		
		// print prompt and flush stdout
		printf(": ");
		fflush(stdout);

		int numCharsEntered = -5;
		size_t bufferSize = 0;

		// check for terminated background processes
		int status;
		
		int cpid = waitpid(-1, &status, WNOHANG);
		while(cpid > 0 && cpid == bgPID) 
		{
			printf("background process %d is done: ", cpid);
			exitStatus(status);
			cpid = waitpid(-1, &status, WNOHANG);
		}

	
		// get line from user.  some extra code here to help getline handle signal interruptoins (from lectures)
		while(1)
		{	
			numCharsEntered = getline(&userInput, &bufferSize, stdin);
			if (numCharsEntered == -1 )
				clearerr(stdin);
			else
				break;
		}
		userInput[strcspn(userInput, "\n")] = '\0'; // Remove the trailing \n that getline adds
	
		// check is user entered a blank line and go back to start of loop if they did
		if (numCharsEntered == 1)
		{
			continue;		
		}
	
		// else extract the user input word by word using strtok
		char* str;
		str = strtok(userInput, " \n");
		
		// if the first characte is #, then the whole line is a comment and is to be ignored
	        if (str[0] == '#')
		{		
			continue;
		}
		// if user enters built-in command "status", display the exit status or terminating signal of the last foreground process
		else if (strcmp(str, "status") == 0)
		{
			exitStatus(childExit);
		}
		// if user enters built-in command "exit", kill any other running process and then terminate
		else if (strcmp(str, "exit") == 0)
		{
			// terminate all child processes
			kill(-parent, SIGQUIT);
			sleep(1); // give program 1 second for children to receive and process signal

			// clean up any background processes
			cpid = waitpid(-1, &status, WNOHANG);
			while(cpid > 0 && cpid == bgPID) 
			{
				printf("background process %d is done: ", cpid);
				exitStatus(status);
				cpid = waitpid(-1, &status, WNOHANG);
			}

			// clean up
			free(inputFile);
			free(outputFile);

			// exit the program
			printf("Program exiting...\n");
			fflush(stdout);
			exit(0);
		}
		// if user enters built-in command "cd", change directory to the home directory.  If user enters "cd [name]", change to 
		// to that directory.  This command accepts relative and absolute paths
		else if (strcmp(str, "cd") == 0)
		{
			// chceck if user supplied any arguments. if not, change to the home directory
			str = strtok(NULL, " \n");
			if (str == NULL)
			{
				chdir(getenv("HOME"));
			}
			else
			{
				// check if there is "$$" anywhere and expand to the parent pid if found
				int shellpid = getpid();
				int length = snprintf(NULL, 0, "%d", shellpid);
				char* strPPID = malloc(length + 1);
				snprintf(strPPID, length + 1, "%d", shellpid);
				
				char* ppid = strstr(str,"$$");
				if (ppid != NULL)
				{
					int inputLen = strlen(str);
					int len = strlen(ppid);
					char* firstHalf = strndup(str, inputLen - len);
					char* expanded = malloc(inputLen + length);
					strcpy(expanded, firstHalf);
					strcat(expanded, strPPID);

					if (len > 2)
					{
						char* secondHalf = strndup(ppid+2, len - 2);
						strcat(expanded, secondHalf);
					}
					str = expanded;
					free(strPPID);
					strPPID = NULL;
				}	

				// check if user entered valid absolute path
				if(chdir(str) == -1)
				{
					// otherwise check for valid relative path
					// get current working directory and store in directory array
					char directory[PATH_MAX];
					memset(directory, '\0', sizeof(directory));
					size_t size = PATH_MAX;
					getcwd(directory, size);
				
					// append relative path to current working directory	
					strcat(directory, "/");
					strcat(directory, str);
	
					if(chdir(directory) == -1)
					{
						printf("Error: cannot open directory %s \n", str);
						fflush(stdout);
					}
				}
			}
		}
		// otherwise user has not entered a bulit-in command.  Store the commands and arguments in an array to be used by a chlid process
		else
		{
			while (str != NULL)
			{
				// check if there is input redirection.  store input file name and set input redirect flag
				if(strcmp(str, "<") == 0)
				{
					str = strtok(NULL, " \n");
					inputFile = str;
					inputRedirect = 1;
					str = strtok(NULL, " \n");
				} 
				// check if there is output redirection.  store output file name and set output redirect falg
				else if(strcmp(str, ">") == 0)
				{
					str = strtok(NULL, " \n");
					outputFile = str;
					outputRedirect = 1;
					str = strtok(NULL, " \n");
				}
				// check if this is a background process 
				else if(strcmp(str, "&") == 0)
				{
					// get the next token from strok.  If it is null, then the last word was "&" and it is a background process
					char* token = strtok(NULL, " \n");
					if (token == NULL)
					{
						str = token;
						if (!fpONLY)
						{
							foreground = 0;
						}
					}
					// else it is not the last word and treat it as normal text
					else
					{
					args[argCount] = malloc(2048 * sizeof(char));
					strcpy(args[argCount], str);
					argCount++;
					str = token;
					}
				}			
				// add token to args array
				if (str != NULL && (strcmp(str, ">") != 0) && (strcmp(str, "<") != 0))
				{
					args[argCount] = malloc(2048 * sizeof(char));
					strcpy(args[argCount], str);
					str = strtok(NULL, " \n");
					argCount++;
				}
			}

			// DEBUG: print out argument array
			/*int i = 0;
			for(; i < argCount; i++)
				printf("Arg %d: %s   ", i, args[i]);
			printf("\n");*/
	
			pid_t spawnpid;
			spawnpid = fork();
			if (spawnpid == -1)
			{
				perror("Hull Breach1\n");
				exit(1);
			}
			else if (spawnpid == 0)
			{
				// if it's a foreground process, change SIGINT handling to its default setting
				if (foreground)
				{
					sigaction(SIGINT, &default_action, NULL); 
				}
				sigaction(SIGTSTP, &ignore_action, NULL);
				
				// check the args array and expand any "$$" found to the parent pid
				int i = 0;
				for(; i < argCount; i++)
				{
					char* ppid = strstr(args[i],"$$");
					if (ppid != NULL)
					{
						int shellpid = getppid();
						int length = snprintf(NULL, 0, "%d", shellpid);
						char* strPPID = malloc(length + 1);
						snprintf(strPPID, length + 1, "%d", shellpid);
						int inputLen = strlen(args[i]);
						int len = strlen(ppid);
						char* firstHalf = strndup(args[i], inputLen - len);
						char* expanded = malloc(inputLen + length);
						strcpy(expanded, firstHalf);
						strcat(expanded, strPPID);

						if (len > 2)
						{
							char* secondHalf = strndup(ppid+2, len - 2);
							strcat(expanded, secondHalf);
						}
						args[i] = expanded;
						free(strPPID);
						strPPID = NULL;
					}	
						
				}	

				// if output redirection, set output to user specified file
				if (outputRedirect)
				{
					int targetFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if (targetFD == -1) 
					{
						perror("Target file failed to open");
						fflush(stdout);
						exit(1);
					}
					fcntl(targetFD, F_SETFD, FD_CLOEXEC);

					int result = dup2(targetFD, 1);
					if (result == -1)
					{
						perror("Error with target dup2 command");
						fflush(stdout);
						exit(2);
					}
					
				}

				// if input redirection, set input to user specified file
				if (inputRedirect)
				{	
					int sourceFD = open(inputFile, O_RDONLY);
					if (sourceFD == -1)
					{
						perror("Source file failed to open");
						exit(1);
					}
					fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
					
					int result = dup2(sourceFD, 0);
					if (result == -1 )
					{
						perror("Error with source dup2 command");
						exit(2);
					}					
				}
				
				// if background process, and no output file specified, redirect output to /dev/null
				/*** source: 
				https://stackoverflow.com/questions/14846768/in-c-how-do-i-redirect-stdout-fileno-to-dev-null-using-dup2-and-then-redirect?
				***/
				if (!foreground && outputFile == NULL)
				{
					int devNull = open("/dev/null", O_WRONLY);
					if (devNull == -1)
					{
						perror("Output redirect to /dev/null failed");
						fflush(stdout);
						exit(1);
					}
						
					int result = dup2(devNull, 1);
					if (result == -1)
					{
						perror("Error with background dev/null target dup2 command");
						fflush(stdout);
						exit(2);
					}
				}
				
				// if background process and no input file specified, redirect input from /dev/null 
				if (!foreground && inputFile == NULL)
				{
					int currDir = open("/dev/null", O_RDONLY);
					if (currDir == -1)
					{
						perror("Input redirect to current directory failed");
						fflush(stdout);
						exit(1);
					}
						
					int result = dup2(currDir, 0);
					if (result == -1)
					{
						perror("Error with background current directory target dup2 command");
						fflush(stdout);
						exit(2);
					}
				}

				if (!foreground)
				{
					printf("This shouldn't be printing b/c we are in the background mode");
					printf("input file: %s    output file: %s\n", inputFile, outputFile);
				}	

				// run execvp with args array to have command executed
				if (execvp(*args, args) < 0)
				{
					perror("Exec failure! ");
					exit(1);
				}
		
				exit(0);

			}
			else
			{
				free(userInput); // Free the memory allocated by getline() or else memory leak
				userInput = NULL;		
			
				// if foreground process, wait for process to terminate.  If process is terminated by a signal, display signal termination.
				if (foreground)
				{
					pid_t childPID = waitpid(spawnpid, &childExit, 0);
					if (WIFSIGNALED(childExit))
					{
						int termSignal = WTERMSIG(childExit);
						printf("terminated by signal %d\n", termSignal);
						fflush(stdout);
					}
				}
				// in background child
				else
				{
					printf("background pid is %d\n", spawnpid);
					fflush(stdout);
					bgPID = spawnpid;
					bgPIDS[bgCount] = bgPID;
					bgCount++; 
				}
			
			// check for any terminated background processes
			cpid = waitpid(-1, &status, WNOHANG);
			while(cpid > 0 && cpid == bgPID) 
			{
				printf("background process %d is done: ", cpid);
				exitStatus(status);
				cpid = waitpid(-1, &status, WNOHANG);
			}
		

			// reset output and input files to NULL
			outputFile = NULL;
			inputFile = NULL;
			}
		}	
	}
	
	// we should never get here but just in case
	return 0;	
}


/************************************************************************************************************************
** Description: exitStatus() returns the exit code or termination signal of the most recently exitied foregroudn function 
************************************************************************************************************************/
void exitStatus(int status)
{	
	if(WIFEXITED(status))
	{
		int exitStatus = WEXITSTATUS(status);
		printf("exit value %d\n", exitStatus);
	}
	else if (WIFSIGNALED(status))
	{
		int termSignal = WTERMSIG(status);
		printf("terminated by signal %d\n", termSignal);
	}
	else
		printf("HULL BREACH! Uh oh. Process was not exited normally or by a signal!?!\n");
}

/***************************************************************************************************************
** Description: catchSIGTSTP() is the signal handler for SIGSTSTP.  When a TSTP signal is received, the mode
** is switched to foreground-only mode from foreground/background mode or vice versa.
****************************************************************************************************************/
void catchSIGTSTP(int signo)
{
	if (!fpONLY)
	{
		char* message = "\nEntering foreground-only mode (& is now ignored)\n: ";
		write(STDOUT_FILENO, message, 52);
		fpONLY = 1;
	}
	else
	{
		char* message = "\nExiting foreground-only mode\n: ";
		write(STDOUT_FILENO, message, 32);
		fpONLY = 0;
	}	
}

/***************************************************************************************************************
** Description: catchSIGQUIT() is th signal handler for SIGQUIT.  This is used in the exit command to clear up
** all runnign processes.  This will send the quit command to all processes except for the parent.  The parent
** then terminates itself using the exit command. 
****************************************************************************************************************/
void catchSIGQUIT(int signo)
{
	pid_t child = getpid();
	if (parent != child)
		_exit(0);
}
