#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function
 * parameters. */
#define unused __attribute__((unused))

/* Global environment variables. */
const char* INPUT_FILE = "./.input";
const char* OUTPUT_FILE = "./.output";
const size_t BUF_SIZE = 4096;

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_cd, "cd",
     "Changes the current working directory to that specified directory"},
    {cmd_pwd, "pwd",
     "Prints the current working directory to standard output"}};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

/* Changes the current working directory to that specified directory */
int cmd_cd(struct tokens* tokens) {
  char* path = tokens_get_token(tokens, 1);
  chdir(path);
  return 1;
}

/* Prints the current working directory to standard output */
int cmd_pwd(unused struct tokens* tokens) {
  char* cwd = getcwd(NULL, 0);
  printf("%s\n", cwd);
  free(cwd);
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell
     * until it becomes a foreground process. We use SIGTTIN to pause the shell.
     * When the shell gets moved to the foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

/*
 * Execute program by [offset~offset+argc) tokens arguments in struct tokens.
 * The program will be execute in child process. The stdin will be redirect to *
 * ind and stdout will be redirect to outd.
 */
void execute_program(struct tokens* tokens, size_t offset, size_t argc,
                     int indes, int outdes) {
  pid_t cpid = fork();
  if (cpid == 0) {
    // Redirect stdin and stdout.
    dup2(indes, STDIN_FILENO);
    dup2(outdes, STDOUT_FILENO);
    /* Create arguments pointers array. */
    char* relative_path = tokens_get_token(tokens, offset);
    char* argv[argc + 1];
    for (size_t i = 0; i < argc; i++)
      argv[i] = tokens_get_token(tokens, offset + i);

    argv[argc] = NULL;

    // Execute program.
    execv(relative_path, argv);

    // Need environment variables to get absolute path
    // to run program.
    char* pathstr = getenv("PATH");
    char absolute_path[128];
    for (char* prefix_path = strtok(pathstr, ":"); prefix_path;
         prefix_path = strtok(NULL, ":")) {
      strcpy(absolute_path, prefix_path);
      strcat(absolute_path, "/");
      strcat(absolute_path, relative_path);
      execv(absolute_path, argv);
    }

    // Command not found.
    printf("%s: command not found\n", relative_path);
    exit(0);
  } else {
    wait(NULL);
  }
}

int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive) fprintf(stdout, "%d: ", line_num);

  while (fgets(line, BUF_SIZE, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0)
      cmd_table[fundex].fun(tokens);
    else {
      /* Need to run specified program in command-line. */
      // Get total command arguments count(s).
      size_t command_argc = tokens_get_length(tokens);
      int filedes;

      if (is_contains_word(tokens, "|")) {
        // Pipes.
        // Get the number of process to be run.
        int input_filedes = open(INPUT_FILE, O_CREAT | O_RDWR, S_IRWXU);
        int output_filedes = open(OUTPUT_FILE, O_CREAT | O_RDWR, S_IRWXU);
        char buf[BUF_SIZE];

        // Clear file.
        ftruncate(input_filedes, 0);
        ftruncate(output_filedes, 0);

        size_t process_num = 1;
        size_t tokens_len = tokens_get_length(tokens);
        for (size_t i = 0; i < tokens_len; i++)
          if (!strcmp(tokens_get_token(tokens, i), "|")) process_num++;

        size_t slow = 0, fast = 0;
        for (size_t i = 0; i < process_num; i++) {
          // Execute next program.
          while (fast < tokens_len &&
                 strcmp(tokens_get_token(tokens, fast), "|"))
            fast++;

          lseek(input_filedes, 0, SEEK_SET);
          execute_program(tokens, slow, fast - slow, input_filedes,
                          output_filedes);

          // Move cotent of output file to input file as the input of
          // next program.
          ftruncate(input_filedes, 0);
          lseek(output_filedes, 0, SEEK_SET);
          ssize_t len;
          while ((len = read(output_filedes, buf, BUF_SIZE)) > 0)
            write(input_filedes, buf, len);
          ftruncate(output_filedes, 0);

          slow = ++fast;
        }

        // Print result to console.
        lseek(input_filedes, 0, SEEK_SET);
        ssize_t len;
        while ((len = read(input_filedes, buf, BUF_SIZE)) > 0)
          write(STDIN_FILENO, buf, len);

        // Close.
        close(input_filedes), close(output_filedes);
      } else if (is_contains_word(tokens, ">")) {
        // Redirection >.
        char* filename = tokens_get_token(tokens, command_argc - 1);
        filedes = open(filename, O_CREAT | O_WRONLY);
        execute_program(tokens, 0, command_argc - 2, STDIN_FILENO, filedes);
        close(filedes);
      } else if (is_contains_word(tokens, "<")) {
        // Redirection <.
        char* filename = tokens_get_token(tokens, command_argc - 1);
        filedes = open(filename, O_RDONLY);
        dup2(filedes, STDIN_FILENO);
        execute_program(tokens, 0, command_argc - 2, filedes, STDOUT_FILENO);
        close(filedes);
      } else {
        // Run program.
        execute_program(tokens, 0, command_argc, STDIN_FILENO, STDOUT_FILENO);
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}