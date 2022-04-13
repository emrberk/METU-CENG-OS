#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "parser.h"

using namespace std;

char* stocp(string& s) {
    int size = s.size();
    int i;
    char* charp = (char*) malloc((size + 1) * sizeof(char));
    for (i = 0; i < s.size(); i++) {
        charp[i] = s[i];
    }
    charp[i] = '\0';
    return charp;
}
string cptos(char* cptr) {
    string newstr;
    for (char* it = cptr; *it != '\0'; it++) {
        newstr.push_back(*it);
    }
    return newstr;
}

int charArraySize(char* input) {
    int size = 0;
    char* i;
    for (i = input; *i != '\0'; i++) {
        size++;
    }
    return size;
}

class Process {
private:
    string commandName;
    char* outputDir;
    char* inputDir;
    char* repeaterInput;
public:
    vector<string> args; 
    Process(string commandName) {
        this->commandName = commandName;
    }
    void setInputDir(char* inputDir) {
        this->inputDir = inputDir;
    }
    void setOutputDir(char* outputDir) {
        this->outputDir = outputDir;
    }
    void setRepeaterInput(char* repeaterInput) {
        this->repeaterInput = repeaterInput;
    }
    void run() {
        int fd[2];
        if (inputDir) {
            fd[0] = open(inputDir, O_RDONLY, 0666);
            dup2(fd[0], 0);
            close(fd[0]);
        } 
        if (outputDir) {
            fd[1] = open(outputDir, O_WRONLY | O_APPEND | O_CREAT, 0666);
            dup2(fd[1], 1);
            close(fd[1]);
        }
        char* command = stocp(commandName);
        char* arg[args.size() + 2];
        arg[0] = command;
        for (int i = 0; i < args.size(); i++) {
            arg[i + 1] = stocp(args[i]);
        }
        arg[args.size() + 1] = NULL; // last element should be null
        execvp(command, arg);
        perror("Process could not be executed.\n");
    }
};

class Bundle {
private:
    string name;
    char* outputDir;
    char* inputDir;
    char* repeaterInput;
public:
    vector<Process*> processes;
    Bundle(string name) {
        this->name = name;
    }
    char* getInputDir() {
        return this->inputDir;
    }
    void setInputDir(char* inputDir) {
        this->inputDir = inputDir;
        for (int i = 0; i < processes.size(); i++) {
            processes[i]->setInputDir(inputDir);
        }
    }
    char* getOutputDir() {
        return this->outputDir;
    }
    void setOutputDir(char* outputDir) {
        this->outputDir = outputDir;
        for (int i = 0; i < processes.size(); i++) {
            processes[i]->setOutputDir(outputDir);
        }
    }
    char* getRepeaterInput() {
        return this->repeaterInput;
    }
    void setRepeaterInput(char* repeaterInput) {
        this->repeaterInput = repeaterInput;
    }
    void runProcesses() {
        int runningProcessCount = 0;
        int status;
        int pid;
        int pid2;
        int fd[2];
        for (int i = 0; i < processes.size(); i++) {
            if (inputDir) {
                processes[i]->setInputDir(inputDir);
            } 
            if (outputDir) {
                processes[i]->setOutputDir(outputDir);
            }
            pid = fork();
            if (pid == 0) {
                if (repeaterInput) { // stream repeater input to processes
                    int status2;
                    pipe(fd);
                    pid2 = fork();
                    if (pid2 == 0) { // stream contents to pipe
                        close(fd[0]);
                        int size = charArraySize(repeaterInput);
                        write(fd[1], repeaterInput, size);
                        close(fd[1]);
                        exit(0);
                    } else if (pid2 > 0) { // input processes will take this stream from their stdin
                        close(fd[1]);
                        dup2(fd[0], 0);
                        close(fd[0]);
                        wait(&status2);
                        processes[i]->run();
                    } else {
                        perror("Error on creating fork.\n");
                    }
                } else { // the bundle is the last (or only) element in the pipe.
                    processes[i]->run();
                }
            } else if (pid < 0) {
                perror("Error on creating fork.\n");
            } else {
                runningProcessCount++;
            }
        }
        while (runningProcessCount) { // loop until all processes terminate
            pid = wait(&status);
            runningProcessCount--;
        }
        setInputDir(nullptr);
        setOutputDir(nullptr);
        setRepeaterInput(nullptr);
    }
};

class Repeater {
private:
    Bundle* inputBundle;
    Bundle* outputBundle;
    char* contents;
public:
    Repeater(Bundle* inputBundle, Bundle* outputBundle) {
        this->inputBundle = inputBundle;
        this->outputBundle = outputBundle;
    }
    void setInputBundle(Bundle* inputBundle) {
        this->inputBundle = inputBundle;
    }
    Bundle* getInputBundle() {
        return this->inputBundle;
    }
    void setOuputBundle(Bundle* outputBundle) {
        this->outputBundle = outputBundle;
    }
    Bundle* getOutputBundle() {
        return this->outputBundle;
    }
    char* getContents() {
        return this->contents;
    }
    void setContents(char* contents) {
        this->contents = contents;
    }
    void redirectStream() {
        int fd[2];
        int status;
        pipe(fd);
        int forkPid = fork(); 
        if (forkPid == 0) { // run source
            close(fd[0]);
            dup2(fd[1], 1);
            close(fd[1]);
            inputBundle->runProcesses(); // inputBundle will write to its stdout.
            exit(0);
        } else if (forkPid > 0) { // take contents
            close(fd[1]);
            wait(&status);
            std::string buffer;
            int readCharCount = 0;
            char currentChar[10];
            while ((readCharCount = read(fd[0], currentChar, 10)) > 0) {
                for (int i = 0; i < readCharCount; i++) {
                    buffer.push_back(currentChar[i]);
                }
            }
            this->contents = stocp(buffer);
            outputBundle->setRepeaterInput(this->contents); // give contents to the next bundle
            close(fd[0]);
        } else {
            perror("Error on fork.\n");
        }
    }
};

int main() {
    bool quit = false;
    vector<Bundle*> bundles;
    unordered_map<string, int> nameMap;
    int creation = 0;
    parsed_input* pi = new parsed_input;
    string line;
    Bundle* currentBundle;
    Process* currentProcess;
    while (!quit) {
        line = "";
        getline(cin, line);
        line.append("\n");
        char* charp = stocp(line);
        parse(charp, creation, pi);
        if (pi->command.type == PROCESS_BUNDLE_CREATE) {
            creation = 1;
            string bundleName = cptos(pi->command.bundle_name);
            currentBundle = new Bundle(bundleName);
            bundles.push_back(currentBundle);
            nameMap[bundleName] = bundles.size() - 1;
        } else if (pi->command.type == PROCESS_BUNDLE_STOP) {
            creation = 0;
        } else if (pi->command.type == QUIT) {
            quit = true;
        } else if (pi->command.type != PROCESS_BUNDLE_EXECUTION) { // command
            currentProcess = new Process(pi->argv[0]);
            for (int i = 1; pi->argv[i] != nullptr; i++) {
                currentProcess->args.push_back(pi->argv[i]);
            }
            currentBundle->processes.push_back(currentProcess);
        } else { // execution
            vector<Bundle*> executionList;
            vector<Repeater*> repeaters;
            for (int i = 0; i < pi->command.bundle_count; i++) {
                Bundle* addedBundle = bundles[nameMap[pi->command.bundles[i].name]];
                if (pi->command.bundles[i].input) {
                    addedBundle->setInputDir(pi->command.bundles[i].input);
                }
                if (pi->command.bundles[i].output) {
                    addedBundle->setOutputDir(pi->command.bundles[i].output);
                }
                executionList.push_back(addedBundle);
            }
            for (int i = 0; i < executionList.size() - 1; i++) {
                Repeater* rep = new Repeater(executionList[i], executionList[i+1]);
                repeaters.push_back(rep);
            }
            for (int i = 0; i < repeaters.size(); i++) {
                repeaters[i]->redirectStream();
            }
            executionList[executionList.size() - 1]->runProcesses();
        }
    }
    return 0;
}