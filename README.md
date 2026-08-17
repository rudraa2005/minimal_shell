# Mini Shell

A minimal Unix shell written in C to understand how shells work under the hood.

## Features

* Execute commands using `fork()` and `execvp()`
* Input/output redirection
* Pipes
* Background processes using `&`
* Basic job control with `jobs`, `fg`, and `bg`
* Process groups and signal handling

## Build

```bash
gcc -Wall -Wextra -o myshell minishell.c
```

## Run

```bash
./myshell
```

## Examples

```bash
myshell> ls
myshell> ls | grep ".c"
myshell> ls > output.txt
myshell> cat < output.txt
myshell> sleep 30 &
myshell> jobs
myshell> fg %1
myshell> bg %1
```

## Concepts

This project focuses on learning core Unix concepts:

* Processes
* `fork()` / `exec()`
* `waitpid()`
* Pipes
* File descriptors and `dup2()`
* Signals
* Process groups
* Basic terminal job control

This is an educational shell and does not aim to replace Bash or other full-featured shells.
