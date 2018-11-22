#define _DEFAULT_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

#define ARRLEN(x) sizeof(x)/sizeof(x[0])
#define MAX 512

#define COMPILE  1
#define EXECUTE  2
#define REMOVE   4

enum Lang {
    LangUnknown,
    LangASM,
    LangC,
    LangCPP
};

typedef struct {
    int exited;
    int exitstatus;
    int signaled;
    int termsig;
    int coredump;
    int stopped;
    int stopsig;
    int continued;
    char out[MAX];
    char err[MAX];
} PRet;

char *USAGE = "Usage: eo <commands> [-x LANG] <file>\n";


/*************************************************
 * Utility Functions
 ************************************************/


/**
 * Print an error message to stderr and exit with code 1.
 */
void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}


/**
 * Convert the string to lowercase.
 */
char *lower(char *str) {
    for (int i=0; str[i]; i++)
        if ('A' <= str[i] && str[i] <= 'Z')
            str[i] += 32;
    return str;
}


/**
 * Return whether the first string matches any of the following strings.
 */
int strMatchesAny(char *str, char *a, ...) {
    if (strcmp(str, a) == 0)
        return 1;

    va_list strings;
    va_start(strings, a);

    char *arg;
    do {
        arg = va_arg(strings, char*);
        if (arg == NULL)
            break;
        if (strcmp(str, arg) == 0)
            return 1;
    } while (1);

    va_end(strings);

    return 0;
}


/**
 * Return a pointer to the extension of the given filename.
 * Return an empty string if no extension can be determined.
 */
char *getExt(char *fn) {
    char *lastdot = strrchr(fn, '.');
    if (!lastdot || lastdot == fn)
        return "";
    return lastdot + 1;
}


int execute(PRet *ret, char *argv[], int len) {
    // Make sure argv ends with NULL sentinel
    if (argv[len-1] != NULL) {
        strcpy(ret->err, "Last argument of argv must be a null pointer\n");
        return -1;
    }

    int pipes[2][2];

    int *out_pipe = pipes[0];
    int *err_pipe = pipes[1];

    if (pipe(out_pipe) < 0) {
        strcpy(ret->err, "Could not create pipe for stdout\n");
        return -2;
    }

    if (pipe(err_pipe) < 0) {
        strcpy(ret->err, "Could not create pipe for stderr\n");
        return -2;
    }

    int parent_out_fd = out_pipe[0];
    int parent_err_fd = err_pipe[0];
    int child_out_fd = out_pipe[1];
    int child_err_fd = err_pipe[1];

    int pid = fork();

    if (pid == -1) {
        strcpy(ret->err, "Could not fork\n");
        return -3;
    } else if (pid == 0) {
        /**
         * Child
         */

        dup2(child_out_fd, STDOUT_FILENO);
        dup2(child_err_fd, STDERR_FILENO);

        // None of the pipes are needed in the child
        close(parent_out_fd);
        close(parent_err_fd);
        close(child_out_fd);
        close(child_err_fd);

        execv(argv[0], argv);
        _exit(1);
    } else {
        /**
         * Parent
         */

        // Child pipes are not needed in the parent
        close(child_out_fd);
        close(child_err_fd);

        // Wait for child to complete
        int status;
        /* wait(&status); */
        waitpid(pid, &status, 0);

        ret->exited = WIFEXITED(status);
        ret->exitstatus = WEXITSTATUS(status);
        ret->signaled = WIFSIGNALED(status);
        ret->termsig = WTERMSIG(status);
        ret->coredump = WCOREDUMP(status);
        ret->stopped = WIFSTOPPED(status);
        ret->stopsig = WSTOPSIG(status);
        ret->continued = WIFCONTINUED(status);

        int count;
        char out[MAX], err[MAX];

        // Read child's stdout
        count = read(parent_out_fd, out, sizeof(out)-1);
        if (count == -1) {
            strcpy(ret->err, "Could not read child's stdout\n");
            return -4;
        }
        out[count] = '\0';
        strcpy(ret->out, out);

        // Read child's stderr
        count = read(parent_err_fd, err, sizeof(err)-1);
        if (count == -1) {
            strcpy(ret->err, "Could not read child's stderr\n");
            return -4;
        }
        err[count] = '\0';
        strcpy(ret->err, err);

        close(parent_out_fd);
        close(parent_err_fd);
    }

    return 0;
}


/*************************************************
 * Core Functions
 ************************************************/


/**
 * Return the flag corresponding to the language to compile for.
 */
enum Lang determineLang(char *lang) {
    if (strMatchesAny(lang, "s", "asm", "assembly", "x86", "x86_64", NULL))
        return LangASM;
    else if (strMatchesAny(lang, "c", NULL))
        return LangC;
    else if (strMatchesAny(lang, "cpp", "c++", NULL))
        return LangCPP;
    else
        return LangUnknown;
}


/**
 * Return the flag corresponding to the language indicated by the filename.
 */
int autoDetermineLang(char *fn) {
    char *ext = getExt(fn);
    return strcmp(ext, "") == 0 ? LangUnknown : determineLang(ext);
}


/**
 * Compile the program for assembly.
 */
void compile_asm(char *src_name, char *obj_name, char *exe_name) {
    char *nasm_args[] = {
        "/usr/bin/nasm", "-f", "elf64", src_name, "-o", obj_name, NULL};
    int nasm_args_len = ARRLEN(nasm_args);

    char *ld_args[] = {
        "/usr/bin/ld", obj_name, "-o", exe_name, NULL};
    int ld_args_len = ARRLEN(ld_args);

    PRet nasm_ret;
    execute(&nasm_ret, nasm_args, nasm_args_len);

    PRet ld_ret;
    execute(&ld_ret, ld_args, ld_args_len);
}


/**
 * Compile the program for C.
 */
void compile_c(char *src_name, char *exe_name) {
    // Get the cflags for the python library
    PRet pylibs;
    char *pylib_args[] = {
        "/usr/bin/pkg-config", "--cflags", "--libs", "python3", NULL};
    execute(&pylibs, pylib_args, 5);
    if (!pylibs.exited || pylibs.exitstatus != 0)
        die("Could not get cflags for the python3 library\n");

    // Remove trailing newline
    char *nl = strrchr(pylibs.out, '\n');
    if (nl && nl != pylibs.out)
        *nl = '\0';

    // Split the pylibs string
    // Example string: "abc def ghi\0"
    int n = 1;
    for (int i=0; pylibs.out[i]; i++) {
        if (pylibs.out[i] == ' ') {
            n++;
            // Replace each space with '\0'
            pylibs.out[i] = '\0';
        }
    }
    // Example string would now be: "abc\0def\0ghi\0"
    // Fill the flags array with a pointer to each null-terminated segment
    char *pylib_flags[n];
    char *pointer = pylibs.out;
    pylib_flags[0] = pointer;
    // array with example string will be: {"abc\0", "def\0", "ghi\0"}
    for (int i=1; i<n; i++) {
        pointer = strchr(pointer, '\0') + 1;
        pylib_flags[i] = pointer;
    }

    char *base_args[] = {
        "/usr/bin/gcc",
        "-std=c11", "-O3", "-Wall", "-Werror",
        src_name, "-o", exe_name,
        "-lm", "-ljson-c", "-lmylib"};

    // Combine base_args, pylib_flags, and NULL sentinel
    char *args[ARRLEN(base_args)+ARRLEN(pylib_flags)+1];
    int count = 0;
    for (int i=0; i<ARRLEN(base_args); i++)
        args[count++] = base_args[i];
    for (int i=0; i<ARRLEN(pylib_flags); i++)
        args[count++] = pylib_flags[i];
    args[count] = NULL;

    // Execute
    PRet ret;
    execute(&ret, args, ARRLEN(args));
}


/**
 * Compile the program for C++.
 */
void compile_cpp(char *src_name, char *exe_name) {
    char *args[] = {
        "/usr/bin/g++",
        "-std=c++11", "-O3", "-Wall", "-Werror",
        src_name, "-o", exe_name,
        NULL};

    PRet ret;
    execute(&ret, args, ARRLEN(args));
}


/*************************************************
 * Main
 ************************************************/


int main(int argc, char *argv[]) {
    if (argc < 3)
        die(USAGE, argv[0]);

    /**
     * Determine the commands
     */

    int commands = 0;
    for (int i=0; argv[1][i]; i++) {
        if (argv[1][i] == 'c')
            commands |= COMPILE;
        else if (argv[1][i] == 'e')
            commands |= EXECUTE;
        else if (argv[1][i] == 'r')
            commands |= REMOVE;
        else
            die("Unknown command: %c\n", argv[1][i]);
    }

    if (commands == 0)
        die("No commands were given\n");

    if (! (commands & COMPILE))
        die("Program requires compilation\n");

    /**
     * Parse arguments
     */

    int parsing_opts = 1;
    char *src_name = NULL;
    int too_many_src_fns = 0;
    char *forced_ext = NULL;

    for (int i=2; i<argc; i++) {
        if (parsing_opts && *argv[i] == '-') {
            argv[i]++;
            if (strMatchesAny(argv[i], "-", NULL))
                parsing_opts = 0;
            else if (strMatchesAny(argv[i], "x", "-language", NULL))
                forced_ext = argv[++i];
            else
                die(USAGE, argv[0]);
        } else {
            if (src_name != NULL)
                too_many_src_fns = 1;
            src_name = argv[i];
        }
    }

    // None or more than one src_name was given
    if (src_name == NULL || too_many_src_fns)
        die(USAGE, argv[0]);

    // Get the programming language as an integer
    enum Lang lang;
    if (forced_ext == NULL) {
        lang = autoDetermineLang(src_name);
        if (lang == LangUnknown)
            die("Could not determine language from filename: %s\n",
                src_name);
    } else {
        if (strlen(forced_ext) == 0)
            die("Language cannot be empty string\n");
        lang = determineLang(lower(forced_ext));
        if (lang == LangUnknown)
            die("Language not recognized: %s\n", forced_ext);
    }

    // Get the executable filename
    char exe_name[strlen(src_name)+3+1];
    snprintf(exe_name, sizeof(exe_name), "%s.eo", src_name);

    // Get the object file name
    // Only used for ASM
    char obj_name[strlen(exe_name)+2+1];
    snprintf(obj_name, sizeof(obj_name), "%s.o", exe_name);

    if (lang == LangASM)
        compile_asm(src_name, obj_name, exe_name);
    else if (lang == LangC)
        compile_c(src_name, exe_name);
    else if (lang == LangCPP)
        compile_cpp(src_name, exe_name);

    if (commands & EXECUTE) {
        PRet exeRet;
        char exe_arg[2+strlen(exe_name)+1];
        snprintf(exe_arg, sizeof(exe_arg), "./%s", exe_name);
        char *args[] = {exe_arg, NULL};
        execute(&exeRet, args, ARRLEN(args));
        printf("%s", exeRet.out);
        fprintf(stderr, "%s", exeRet.err);
    }

    if (commands & REMOVE) {
        char *rm_err = "Could not remove file: %s\n";
        if (lang == LangASM && remove(obj_name) < 0)
            die(rm_err, obj_name);
        if (remove(exe_name) < 0)
            die(rm_err, exe_name);
    }

    return 0;
}
