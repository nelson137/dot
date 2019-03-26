#ifndef __MYLIBPP_H

#define __MYLIBPP_H


#include <algorithm>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <wait.h>

using namespace std;


struct exec_ret {
    int exitstatus;
    string out;
    string err;
};


void catchSig(int, void *(int));

bool file_exists(string);

string trim_whitespace(string);

/**
 * Join the given vector with delim.
 * The default delim is a space.
 * Example:
 *     vector<string> v = {"a", "b", "c"};
 *     join(v);  // "a b c"
 */
template<typename T>
string join(vector<T> tokens, string delim=" ") {
    ostringstream str;

    if (tokens.size()) {
        str << tokens[0];
        for (unsigned i=1; i<tokens.size(); i++)
            str << delim << tokens[i];
    }

    return str.str();
}

vector<string> split(string, string=" ");


class any {

private:
    enum type {Char, Int, Float, Double, Size_t, String};
    char   CHAR;
    int    INT;
    float  FLOAT;
    double DOUBLE;
    size_t SIZE_T;
    string STRING;

public:
    type m_type;

    any(char   c) { this->m_type = Char;   this->CHAR   = c; }
    any(int    i) { this->m_type = Int;    this->INT    = i; }
    any(float  f) { this->m_type = Float;  this->FLOAT  = f; }
    any(double d) { this->m_type = Double; this->DOUBLE = d; }
    any(size_t s) { this->m_type = Size_t; this->SIZE_T = s; }
    any(string s) { this->m_type = String; this->STRING = s; }

    string str() {
        switch (this->m_type) {
            case Char:   return string(1, this->CHAR);
            case Int:    return to_string(this->INT);
            case Float:  return to_string(this->FLOAT);
            case Double: return to_string(this->DOUBLE);
            case Size_t: return to_string(this->SIZE_T);
            case String: return this->STRING;
        }
    }

};


void die(int=1);


template<typename T, typename... Ts>
void die(int code, T t, Ts... ts) {
    vector<string> tokens = {any(t).str(), any(ts).str()...};
    cerr << join(tokens) << endl;
    die(code);
}


template<typename T, typename... Ts>
void die(T t, Ts... ts) {
    die(1, t, ts...);
}


bool read_fd(int, string&);

template<typename T>
int execute(exec_ret& er, T& args, bool capture_output=false) {
    // Get args as a vector<char*>
    vector<char*> argv(args.size() + 1);
    transform(args.begin(), args.end(), argv.begin(),
        [](string& s){ return const_cast<char*>(s.c_str()); });
    argv.push_back(NULL);

    int pipes[2][2];
    int *out_pipe = pipes[0];
    int *err_pipe = pipes[1];

    // Create the stdout and stderr pipes
    if (capture_output) {
        if (pipe(out_pipe) < 0)
            die("Could not create pipe for stdout");
        if (pipe(err_pipe) < 0)
            die("Could not create pipe for stderr");
    }

    int child_out_fd = out_pipe[1];
    int child_err_fd = err_pipe[1];
    int parent_out_fd = out_pipe[0];
    int parent_err_fd = err_pipe[0];

    int pid = fork();

    if (pid == -1) {
        die("Could not fork");

    } else if (pid == 0) {
        /**
         * Child
         */

        if (capture_output) {
            dup2(child_out_fd, STDOUT_FILENO);
            dup2(child_err_fd, STDERR_FILENO);
        }

        close(child_out_fd);
        close(child_err_fd);
        close(parent_out_fd);
        close(parent_err_fd);

        execv(argv[0], argv.data());
        _exit(0);
    } else {
        /**
         * Parent
         */

        // Child's end of the pipes are not needed
        close(child_out_fd);
        close(child_err_fd);

        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);

        er.exitstatus = WEXITSTATUS(status);

        if (capture_output) {
            // Read child's stdout
            if (! read_fd(parent_out_fd, er.out))
                die("Could not read stdout");
            // Read child's stderr
            if (! read_fd(parent_err_fd, er.err))
                die("Could not read stderr");
        }

        close(parent_out_fd);
        close(parent_err_fd);
    }

    return er.exitstatus;
}

template<typename T>
exec_ret easy_execute(T& args, bool capture_output=false) {
    exec_ret er;
    execute(er, args, capture_output);
    return er;
}


namespace listbox {


const string NO_TITLE = "__NO_TITLE";
const string DEFAULT_CURSOR = "*";


template<typename T>
struct LB {
    string title;
    bool show_title;
    vector<T> choices;
    string cursor;
    string cursor_spaces;
    bool show_instructs;

    void print(string str, bool prefix_cursor=false) {
        cout << (prefix_cursor ? this->cursor : this->cursor_spaces)
             << " " << str << endl;
    }

    void draw(unsigned current_i) {
        for (unsigned i=0; i<this->choices.size(); i++)
            this->print(string(this->choices[i]), i==current_i);
    }

    void redraw(unsigned current_i) {
        // Go back to the top of the listbox output, clearing each line
        for (unsigned i=0; i<this->choices.size(); i++)
            cout << "\33[A\33[2K";
        this->draw(current_i);
    }

    LB(string t, vector<T> cs, string c=DEFAULT_CURSOR, bool si=true)
        : title(t)
        , show_title(t != NO_TITLE)
        , choices(cs)
        , cursor(c)
        , cursor_spaces(string(c.size(), ' '))
        , show_instructs(si)
    {}
};


template<typename T>
T run_listbox(LB<T> lb) {
    if (lb.show_instructs) {
        cout << "Press k/j or up/down arrows to move up and down." << endl
             << "Press q to quit." << endl
             << "Press Enter to confirm the selection." << endl
             << endl;
    }

    if (lb.show_title) {
        // Print the title
        lb.print(lb.title);
        // Print the underline
        lb.print(string(lb.title.length(), '-'));
    }

    // Save the current terminal settings
    struct termios oldt = {0};
    tcgetattr(STDIN_FILENO, &oldt);

    // Copy the old settings
    struct termios newt = oldt;
    // Disable canonical mode and echo
    newt.c_lflag &= ~(ICANON|ECHO);
    // Minimum number of character to read
    newt.c_cc[VMIN] = 1;
    // Block until read is performed
    newt.c_cc[VTIME] = 0;
    // Apply the new settings
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    bool quit = false;
    unsigned current = 0;

    int chosen = -1;
    lb.draw(current);

    do {
        switch (cin.get()) {
            // Up
            case 'k':
            case 'A':  // Up arrow
                if (current > 0)
                    lb.redraw(--current);
                break;

            // Top
            case 'K':
                lb.redraw(current = 0);
                break;

            // Down
            case 'j':
            case 'B':  // Down arrow
                if (current < lb.choices.size()-1)
                    lb.redraw(++current);
                break;

            // Bottom
            case 'J':
                lb.redraw(current = lb.choices.size() - 1);
                break;

            // Quit
            case 'q':
                quit = true;
                break;

            // Confirm selection
            case '\n':
                chosen = current;
                quit = true;
                break;
        }
    } while (quit == false);

    // Restore term
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    return lb.choices[chosen];
}


template<typename T>
T run_listbox(string title, vector<T>& choices, string cursor=DEFAULT_CURSOR,
              bool show_instructs=true) {
    return run_listbox(LB<T>(title, choices, cursor, show_instructs));
}


}  // namespace listbox


template<typename T>
ostream& operator<<(ostream& os, const vector<T>& vec) {
    os << "{";
    if (vec.size()) {
        os << '"' << vec[0] << '"';
        for (unsigned i=1; i<vec.size(); i++)
            os << ", " << '"' << vec[i] << '"';
    }
    os << "}";
    return os;
}


template<typename T>
ostream& operator<<(ostream& os, const vector<T>&& vec) {
    return operator<<(os, vec);
}


template<typename L, typename R>
void append(L& l, R const& r) {
    l.insert(l.end(), r.begin(), r.end());
}


#endif
