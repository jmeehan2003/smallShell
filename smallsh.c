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
int fpONLY = 0;		// flag for foreground processes only
int bgPID = -5;		
int bgProcess = 1;

void exitStatus(int status);

void parseLine(char *line, char **argv)
{
	argCount = 0; 
 	// until we hit the end of the line
 	while (*line != '\0') 
	{       
		// replace the whitespace with \0
        	while (*line == ' ' || *line == '\t' || *line == '\n')
               		*line++ = '\0';    
	 // save the argument position
         *argv++ = line;    
	 argCount++;      
	
	 // skipe the argument until
         while (*line != '\0' && *line != ' ' && *line != '\t' && *line != '\n') 
               line++;            
        }
	// mark the end of the argument list
        *argv = '\0';                 
}

/*void catchSIGINT(int signo)
{
	char* message = "SIGINT. Use CTRL-Z to Stop. Terminated by signal ";
	int termSignal = WTERMSIG(signo);
	int length = snprintf(NULL, 0, "%d", termSignal);
	char* fullmessage = malloc(49 + length + 1);
	strcpy(fullmessage,message); 
	// malloc for length of message (28) plus length of termSignal + 1
	char* signal= malloc(length + 1);
	snprintf(signal, length + 1, "%d", termSignal);
	strcat(fullmessage, signal);
	strcat(fullmessage, "\n");
	
	write(STDOUT_FILENO, fullmessage, 52);
}*/

void backgroundExit(int status)
{
	printf("background pid %d is done: ", bgPID);
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

void catchSIGTSTP(int signo)
{
	if (!fpONLY)
	{
		printf("\nEntering foreground-only mode (& is now ignored)\n: ");
		fpONLY = 1;
	}
	else
	{
		printf("Exiting foreground-only mode\n: ");
		fpONLY = 0;
	}	
}

void catchSIGCHLD(int signo)
{
	printf("INSIDE SIG CHLD\n");
	int status;
	while (waitpid(-1, &status, WNOHANG) > 0){};
	printf("bgprocess: %d\n", bgProcess);
	bgProcess = 1;
	if (bgProcess)
	{
		backgroundExit(status);
		bgProcess = 0;
	}
}

void checkFinish()
{
 	 int status;
	 pid_t pid = waitpid(-1, &status, WNOHANG);
  	 if (pid < 0 && errno != ECHILD)
    		perror("Waitpid failed");
      	 else if (pid > 0)
    		printf("Process %d finished with status %d", pid, status);
}

int main()
{
	struct sigaction ignore_action = {0};
	ignore_action.sa_handler = SIG_IGN;

	struct sigaction default_action = {0};
	default_action.sa_handler = SIG_DFL;

	struct sigaction SIGINT_action = {0};
//	SIGINT_action.sa_handler = catchSIGINT;
//	sigfillset(&SIGINT_action.sa_mask);
	sigaction(SIGINT, &ignore_action, NULL);

/*	struct sigaction SIGCHLD_action = {0};
	SIGCHLD_action.sa_handler = catchSIGCHLD;
	sigfillset(&SIGCHLD_action.sa_mask);
	sigaction(SIGCHLD, &default_action, NULL);
*/
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	char* args[512];
	char* userInput = NULL;
	char* outputFile = NULL;
	char* inputFile = NULL;
//	int pid = getpid();
//	printf("%s\n", pid);
	int ppid = getpid();
	printf("%d\n", ppid);
	int length = snprintf(NULL, 0, "%d", ppid);
	char* strPPID = malloc(length + 1);
	snprintf(strPPID, length + 1, "%d", ppid);
	int childExit = 0;

	int run = 1;
	while (run)
	{
//		sigaction(SIGCHLD, &SIGCHLD_action, NULL);
		int foreground = 1;
		int argCount = 0;
		int inputRedirect = 0;
		int outputRedirect = 0;
		memset(args, '\0', sizeof(args));

		int numCharsEntered = -5;
		size_t bufferSize = 0;
		char cwd[PATH_MAX];
		getcwd(cwd, sizeof(cwd));
//		printf("cdw: %s\n", cwd);
		if (cwd == NULL)
		{
			perror("getcwd() error");
			fflush(stdout);
			exit(1);
		}
		// check for terminated background processes
		int status;
		int cpid = waitpid(-1, &status, WNOHANG);
//		printf("bgpid is: %d\n", bgPID);
		while(cpid > 0 && cpid == bgPID) 
		{
			printf("background process %d is done: ", cpid);
			exitStatus(status);
			cpid = waitpid(-1, &status, WNOHANG);
		}

		int pid = getpid();		
	//	printf("%d: ", pid);
		printf(": ");
		// always flush stdout after printing
		fflush(stdout);
		
		while(1)
		{	
			numCharsEntered = getline(&userInput, &bufferSize, stdin);
//			printf("numChars: %d\n", numCharsEntered);
			if (numCharsEntered == -1 )
				clearerr(stdin);
			else
				break;
		}
//		printf("Outside getline loop\n");
		userInput[strcspn(userInput, "\n")] = '\0'; // Remove the trailing \n that getline adds
	
		// check is user entered a blank line and go back to start of loop if they did
		if (numCharsEntered == 1)
		{
//			printf("blank line\n");
			continue;		
		}
	
		// else extract the user input word by word using strtok
		char* str;
		str = strtok(userInput, " \n");
		
		// if the first characte is #, then the whole line is a comment and is to be ignored
	        if (str[0] == '#')
		{		
//			printf("This line is a comment and is being ignored\n");
			continue;
		}
		else if (strcmp(str, "status") == 0)
		{
			exitStatus(childExit);
		}

	//	printf("First strtok is: %s\n", str);
		else if (strcmp(str, "exit") == 0)
		{
			printf("Program exiting...\n");
			exit(0);
		}
		else if (strcmp(str, "cd") == 0)
		{
			str = strtok(NULL, " \n");
			if (str == NULL)
			{
				chdir(getenv("HOME"));
			}
			else
			{

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
				}	
				//printf("str is: %s\n", str);
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
				//	printf("directory: %s\n", directory);
					if(chdir(directory) == -1)
					{
						printf("Error: cannot open directory %s \n", str);
						fflush(stdout);
					}
				}
			}
		}
		else
		{
			while (str != NULL)
			{
				if(strcmp(str, "<") == 0)
				{
					str = strtok(NULL, " \n");
					inputFile = str;
					inputRedirect = 1;
					str = strtok(NULL, " \n");
				} 
				else if(strcmp(str, ">") == 0)
				{
					str = strtok(NULL, " \n");
					outputFile = str;
					outputRedirect = 1;
					str = strtok(NULL, " \n");
				}
				else if(strcmp(str, "&") == 0)
				{
					char* token = strtok(NULL, " \n");
					if (token == NULL)
					{
						str = token;
						if (!fpONLY)
						{
							foreground = 0;
						}
					}
					else
					{
					args[argCount] = malloc(2048 * sizeof(char));
					strcpy(args[argCount], str);
					argCount++;
					str = token;
					}
				}			
				if (str != NULL && (strcmp(str, ">") != 0) && (strcmp(str, "<") != 0))
				{
					args[argCount] = malloc(2048 * sizeof(char));
					strcpy(args[argCount], str);
					str = strtok(NULL, " \n");
					argCount++;
				}
			}

//			printf("last arg is: %s\n", args[argCount - 1]);
		/*	if (strcmp(args[argCount - 1], "&") == 0)
			{
				//printf("backgroudn process started\n");
				foreground = 0;		
				//printf("foreground val: %d\n", foreground);
				strcpy(args[argCount - 1], "\0");
			}*/

			int i = 0;
		/*	for(; i < argCount; i++)
				printf("Arg %d: %s   ", i, args[i]);
			printf("\n");
		*/	
			pid_t spawnpid;
			spawnpid = fork();
			if (spawnpid == -1)
			{
				perror("Hull Breach1\n");
				exit(1);
			}
			else if (spawnpid == 0)
			{
			//	printf("inside child. foreground is: %d\n", foreground);
				//signal( SIGINT, SIG_DFL );
				//sigaction(SIGCHLD, &ignore_action, NULL);
	//			printf("FOREGROUND: %d\n", foreground);
				if (foreground)
				{
					sigaction(SIGINT, &default_action, NULL); 
				}
				else
				{
					bgProcess = 1;
				}
				int i = 0;
				for(; i < argCount; i++)
				{
					char* ppid = strstr(args[i],"$$");
					if (ppid != NULL)
					{
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
					}	
						
				}	
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
				
				// if background process, redirect output to /dev/null
				/*** source: 
				https://stackoverflow.com/questions/14846768/in-c-how-do-i-redirect-stdout-fileno-to-dev-null-using-dup2-and-then-redirect?
				***/
				if (!foreground && outputFile == NULL)
				{
			//		printf("inside bckgrd output redirect\n");
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
				
				if (!foreground && inputFile == NULL)
				{
			//		printf("inside bckgrd input redirect\n");
			//		printf("currDir: %s\n", cwd);
					int currDir = open("junk", O_RDONLY);
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
					printf("exited input redire\n");
				}

				

		//		printf("foregrond: %d\n", foreground);
				if (!foreground)
				{
					printf("This should be printing b/c we are in the background mode");
					printf("input file: %s    output file: %s\n", inputFile, outputFile);
				}	


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
			
				if (foreground)
				{
					pid_t childPID = waitpid(spawnpid, &childExit, 0);
			//		printf("SpawnPID: %d\n", spawnpid);
					if (WIFSIGNALED(childExit))
					{
						int termSignal = WTERMSIG(childExit);
						printf("terminated by signal %d\n", termSignal);
					}
				}
				// in background child
				else
				{
					printf("background pid is %d\n", spawnpid);
					bgPID = spawnpid;
		//			sigaction(SIGCHLD, &SIGCHLD_action, NULL); 
				}
			
			cpid = waitpid(-1, &status, WNOHANG);
//			printf("bgpid is: %d\n", bgPID);
			while(cpid > 0 && cpid == bgPID) 
			{
				printf("background process %d is done: ", cpid);
				exitStatus(status);
				cpid = waitpid(-1, &status, WNOHANG);
			}
//			free(outputFile);
//			free(inputFile);
			outputFile = NULL;
			inputFile = NULL;
			
			}

		}	
	}
	printf("Exiting from parent\n");
	return 0;
	exit(0);	
}

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
