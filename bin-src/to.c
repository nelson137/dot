#define _DEFAULT_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

#define ARRLEN(x) sizeof(x)/sizeof(x[0])
#define MAX 512

// Bitflags for options
#define HELP      1  // 0b00001
#define COMPILE   2  // 0b00010
#define EXECUTE   4  // 0b00100
#define REMOVE    8  // 0b01000
#define DRYRUN   16  // 0b10000

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

char *USAGE = "Usage: to [-h] [-c] [-e] [-r] [-l LANG] [--dry-run] <infile>\n"
              "       [-x [ARGS...]]\n";


/*************************************************
 * Utility Functions
 ************************************************/


/**
 * Print an error message to stderr and exit with code 1.
 */
void error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}


void help() {
    printf("%s", USAGE);
    puts("");
    puts("Positional Arguments");
    puts("  infile          The source file for a single-file C, C++, or x86");
    puts("                  Assembly program");
    puts("");
    puts("Command Options");
    puts("  -c, --compile   Compile the program");
    puts("  -e, --execute   Execute the compiled program");
    puts("  -r, --remove    Remove the binary and any compilation files");
    puts("");
    puts("Options");
    puts("  -h, --help      Print this help message and exit");
    puts("  -l, --lang      Set the language to compile for");
    puts("  --dry-run       Print out the commands that would be executed in");
    puts("                  response to the -c, -e, and -r options");
    exit(0);
}


int isOpt(char *arg) {
    return arg[0] == '-' && strlen(arg) >= 2;
}


int isLongOpt(char *arg) {
    return arg[0] == '-' && arg[1] == '-' && strlen(arg) > 2;
}


int isCompoundOpt(char *arg) {
    return arg[0] == '-' && arg[1] != '-' && strlen(arg) > 2;
}


void print_args(char *args[], int len) {
    for (int i=0; i<len-1; i++)
        printf(
            "%s%s",
            i > 0 ? " " : "",
            args[i]);
    printf("\n");
}


void read_yesno(char response[], int size, char *prompt) {
    size--;
    int index = 0;
    char c;

    printf("%s", prompt);

    do {
        c = getchar();
        if (c == '\n')
            break;
        if (index < size)
            response[index++] = c;
    } while (1);

    response[index] = '\0';
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
int compile_asm(int dryrun, char *src_name, char *obj_name, char *bin_name) {
    char *nasm_args[] = {
        "/usr/bin/nasm", "-f", "elf64", src_name, "-o", obj_name, NULL};
    char *ld_args[] = {
        "/usr/bin/ld", obj_name, "-o", bin_name, NULL};

    if (dryrun) {
        print_args(nasm_args, ARRLEN(nasm_args));
        print_args(ld_args, ARRLEN(ld_args));
    } else {
        PRet nasm_ret, ld_ret;

        execute(&nasm_ret, nasm_args, ARRLEN(nasm_args));
        if (!nasm_ret.exited || nasm_ret.exitstatus != 0) {
            error("Could not create object file for infile: %s\n\n", src_name);
            error(nasm_ret.err);
            return nasm_ret.exitstatus;
        }

        execute(&ld_ret, ld_args, ARRLEN(ld_args));
        if (!ld_ret.exited || ld_ret.exitstatus != 0) {
            error("Could not link object file: %s\n\n", obj_name);
            error(ld_ret.err);
            return ld_ret.exitstatus;
        }
    }

    return 0;
}


/**
 * Compile the program for C.
 */
int compile_c(int dryrun, char *src_name, char *bin_name) {
    // Get the cflags for the python library
    PRet pylibs;
    char *pylib_args[] = {
        "/usr/bin/pkg-config", "--cflags", "--libs", "python3", NULL};
    execute(&pylibs, pylib_args, 5);
    if (!pylibs.exited || pylibs.exitstatus != 0) {
        error("Could not get gcc flags for the python3 library\n");
        return pylibs.exitstatus;
    }

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
    int pylib_flags_len = n;
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
        src_name, "-o", bin_name,
        "-lm", "-ljson-c", "-lmylib"};
    int base_args_len = ARRLEN(base_args);

    // Combine base_args, pylib_flags, and NULL sentinel
    int args_len = base_args_len + pylib_flags_len + 1;
    char *args[args_len];
    int count = 0;
    for (int i=0; i<base_args_len; i++)
        args[count++] = base_args[i];
    for (int i=0; i<pylib_flags_len; i++)
        args[count++] = pylib_flags[i];
    args[count] = NULL;

    if (dryrun) {
        print_args(args, args_len);
    } else {
        PRet ret;
        execute(&ret, args, args_len);
        if (!ret.exited || ret.exitstatus != 0) {
            error("Could not compile infile: %s\n\n", src_name);
            error(ret.err);
            return ret.exitstatus;
        }
    }

    return 0;
}


/**
 * Compile the program for C++.
 */
int compile_cpp(int dryrun, char *src_name, char *bin_name) {
    char *args[] = {
        "/usr/bin/g++",
        "-std=c++11", "-O3", "-Wall", "-Werror",
        src_name, "-o", bin_name,
        NULL};

    if (dryrun) {
        print_args(args, ARRLEN(args));
    } else {
        PRet ret;
        execute(&ret, args, ARRLEN(args));
        if (!ret.exited || ret.exitstatus != 0) {
            error("Could not compile infile: %s\n\n", src_name);
            error(ret.err);
            return ret.exitstatus;
        }
    }

    return 0;
}


/*************************************************
 * Main
 ************************************************/


int main(int orig_argc, char *orig_argv[]) {
    if (orig_argc < 3) {
        error(USAGE, orig_argv[0]);
        return 1;
    }

    int exitstatus = 0;

    /**
     * Pre-process arguments
     */

    int argc = 0;
    int parsing_opts = 1;
    for (int i=0; i<orig_argc; i++) {
        if (strcmp(orig_argv[i], "--") == 0)
            parsing_opts = 0;
        else if (parsing_opts && isCompoundOpt(orig_argv[i]))
            argc += strlen(orig_argv[i]) - 2;
        argc++;
    }

    /**
     * Split compound options
     * For example, -abc gets expanded to -a, -b, -c in-place
     * If orig_argv is {"./t", "-abc", "--long", "-def"}, then
     * argv will be {"./t", "-a", "-b", "-c", "--long", "-d", "-e", "-f"}
     */

    char *argv[argc];

    char compoundOpts[argc * 3];
    char *coPtr = compoundOpts;
    int argv_i = 0;
    for (int i=0; i<orig_argc; i++) {
        if (isCompoundOpt(orig_argv[i])) {
            for (int j=1; j<strlen(orig_argv[i]); j++) {
                argv[argv_i++] = coPtr;
                *(coPtr++) = '-';
                *(coPtr++) = orig_argv[i][j];
                *(coPtr++) = '\0';
            }
        } else {
            argv[argv_i++] = orig_argv[i];
        }
    }

    /**
     * Parse arguments
     */

    parsing_opts = 1;
    int options = 0;
    char *src_name = NULL;
    int too_many_src_fns = 0;
    char *forced_ext = NULL;
    char **sub_args = NULL;
    int sub_args_c = 0;
    int sub_args_i = 0;

    for (int i=1; i<argc; i++) {
        if (parsing_opts && isOpt(argv[i])) {
            if (strMatchesAny(argv[i], "--", NULL))
                parsing_opts = 0;
            else if (strMatchesAny(argv[i], "-h", "--help", NULL))
                options |= HELP;
            else if (strMatchesAny(argv[i], "-c", "--compile", NULL))
                options |= COMPILE;
            else if (strMatchesAny(argv[i], "-e", "--execute", NULL))
                options |= EXECUTE;
            else if (strMatchesAny(argv[i], "-r", "--remove", NULL))
                options |= REMOVE;
            else if (strMatchesAny(argv[i], "-l", "--language", NULL))
                forced_ext = argv[++i];
            else if (strMatchesAny(argv[i], "-x", "--args", NULL))
                parsing_opts = 0, sub_args_c = argc - (i + 1),
                    sub_args = malloc(sizeof(char*) * sub_args_c);
            else if (strMatchesAny(argv[i], "--dry-run", NULL))
                options |= DRYRUN;
            else {
                error("%s option not recognized: %s\n",
                    isLongOpt(argv[i]) ? "Long" : "Short",
                    argv[i]);
                goto end1;
            }
        } else {
            if (src_name == NULL)
                src_name = argv[i];
            else if (sub_args != NULL)
                sub_args[sub_args_i++] = argv[i];
            else
                too_many_src_fns = 1;
        }
    }

    /**
     * Error messages and setup
     */

    if (options & HELP)
        help();

    if (! (options & (COMPILE|EXECUTE|REMOVE))) {
        error("No commands were given\n");
        goto end1;
    }

    if (! (options & COMPILE)) {
        error("Program requires compilation\n");
        goto end1;
    }

    // None or more than one src_name was given
    if (src_name == NULL || too_many_src_fns) {
        error(USAGE, argv[0]);
        goto end1;
    }

    if (access(src_name, F_OK) != 0) {
        error("Infile does not exist\n");
        goto end1;
    }

    // Get the programming language as an integer
    enum Lang lang;
    if (forced_ext == NULL) {
        lang = autoDetermineLang(src_name);
        if (lang == LangUnknown) {
            error("Could not determine language from filename: %s\n",
                src_name);
            goto end1;
        }
    } else {
        if (strlen(forced_ext) == 0) {
            error("Language cannot be empty string\n");
            goto end1;
        }
        lang = determineLang(lower(forced_ext));
        if (lang == LangUnknown) {
            error("Language not recognized: %s\n", forced_ext);
            goto end1;
        }
    }

    // Get the executable filename
    char *bin_name = malloc(strlen(src_name) + 3 + 1);
    sprintf(bin_name, "%s.to", src_name);

    if (access(bin_name, F_OK) == 0) {
        char *fmt = "Executable file '%s' exists, overwrite it [y/n]? ";
        char prompt[strlen(fmt) - 2 + strlen(bin_name) + 1];
        sprintf(prompt, fmt, bin_name);

        char response[3];
        read_yesno(response, 3, prompt);
        lower(response);
        if (strcmp(response, "y") != 0)
            goto end2;
    }

    // Get the object file name
    // Only used for ASM
    char *obj_name = malloc(strlen(bin_name) + 2 + 1);
    sprintf(obj_name, "%s.o", bin_name);

    if (access(obj_name, F_OK) == 0) {
        char *fmt2 = "Object file '%s' exists, overwrite it [y/n]? ";
        char prompt2[strlen(fmt2) - 2 + strlen(obj_name) + 1];
        sprintf(prompt2, fmt2, obj_name);

        char response2[3];
        read_yesno(response2, 3, prompt2);
        lower(response2);
        if (strcmp(response2, "y") != 0)
            goto end3;
    }

    /**
     * Execute commands
     */

    int retstat = 0;
    if (lang == LangASM)
        retstat = compile_asm(options & DRYRUN, src_name, obj_name, bin_name);
    else if (lang == LangC)
        retstat = compile_c(options & DRYRUN, src_name, bin_name);
    else if (lang == LangCPP)
        retstat = compile_cpp(options & DRYRUN, src_name, bin_name);

    if (retstat != 0) {
        exitstatus = retstat;
        goto end3;
    }

    if (options & EXECUTE) {
        char exec_call[2 + strlen(bin_name) + 1];
        sprintf(exec_call, "./%s", bin_name);

        char *all_exec_args[1 + sub_args_c + 1];
        int exec_args_i = 0;
        all_exec_args[exec_args_i++] = exec_call;
        for (int i=0; i<sub_args_c; i++)
            all_exec_args[exec_args_i++] = sub_args[i];
        all_exec_args[exec_args_i] = NULL;

        if (options & DRYRUN) {
            print_args(all_exec_args, ARRLEN(all_exec_args));
        } else {
            PRet execRet;
            execute(&execRet, all_exec_args, ARRLEN(all_exec_args));
            printf("%s", execRet.out);
            error("%s", execRet.err);
            exitstatus = execRet.exitstatus;
        }
    }

    if (options & REMOVE) {
        char *rm_args[lang == LangASM ? 4 : 3];
        int rm_args_i = 0;
        rm_args[rm_args_i++] = "/bin/rm";
        if (lang == LangASM)
            rm_args[rm_args_i++] = obj_name;
        rm_args[rm_args_i++] = bin_name;
        rm_args[rm_args_i] = NULL;

        if (options & DRYRUN) {
            print_args(rm_args, ARRLEN(rm_args));
        } else {
            PRet rmRet;
            execute(&rmRet, rm_args, ARRLEN(rm_args));
            if (!rmRet.exited || rmRet.exitstatus != 0) {
                if (lang == LangASM) {
                    error("Could not remove files: %s %s\n", obj_name,
                        bin_name);
                } else {
                    error("Could not remove executable: %s\n", bin_name);
                    goto end3;
                }
            }
        }
    }

end3:
    free(obj_name);
end2:
    free(bin_name);
end1:
    free(sub_args);
    return exitstatus;
}
