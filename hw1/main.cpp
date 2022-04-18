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

vector<char*> allocatedCharp;

char* stocp(string& s) {
    int size = s.size();
    int i;
    char* charp = (char*) malloc((size + 1) * sizeof(char));
    for (i = 0; i < size; i++) {
        charp[i] = s[i];
    }
    charp[i] = '\0';
    allocatedCharp.push_back(charp);
    return charp;
}

class Process {
private:
    string commandName;
    char* outputDir;
    char* inputDir;
public:
    vector<string> args; 
    Process(string commandName) {
        this->commandName = commandName;
        this->inputDir = nullptr;
        this->outputDir = nullptr;
    }
    void setInputDir(char* inputDir) {
        this->inputDir = inputDir;
    }
    void setOutputDir(char* outputDir) {
        this->outputDir = outputDir;
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
        perror("Process could not be executed.");
    }
};

class Bundle {
private:
    string name;
    char* outputDir;
    char* inputDir;
public:
    vector<Process*> processes;
    Bundle(string name) {
        this->name = name;
        this->outputDir = nullptr;
        this->inputDir = nullptr;
    }
    void setInputDir(char* inputDir) {
        this->inputDir = inputDir;
        for (int i = 0; i < processes.size(); i++) {
            processes[i]->setInputDir(inputDir);
        }
    }
    void setOutputDir(char* outputDir) {
        this->outputDir = outputDir;
        for (int i = 0; i < processes.size(); i++) {
            processes[i]->setOutputDir(outputDir);
        }
    }
    void runProcesses(bool first) {
        int runningProcessCount = 0;
        int numProcesses = processes.size();
        int status;
        int pid;
        int multipleFd[numProcesses][2];
        for (int i = 0; i < numProcesses; i++) {
            pipe(multipleFd[i]);
        }
        for (int i = 0; i < numProcesses; i++) {
            if (inputDir) {
                processes[i]->setInputDir(inputDir);
            } 
            if (outputDir) {
                processes[i]->setOutputDir(outputDir);
            }
            pid = fork();
            if (pid == 0) {
                dup2(multipleFd[i][0], 0);
                for (int j = 0; j < numProcesses; j++) {
                    close(multipleFd[j][0]);
                    close(multipleFd[j][1]);
                }
                processes[i]->run();
            } else if (pid < 0) {
                perror("Error on creating fork.");
            }
        }
        for (int i = 0; i < numProcesses; i++) {
            close(multipleFd[i][0]);
        }
        if (!first) {
            int readCharCount = 0;
            char currentChar[10];
            while ((readCharCount = read(0, currentChar, 10)) > 0) {
                for (int k = 0; k < numProcesses; k++) {
                    write(multipleFd[k][1], currentChar, readCharCount);
                }
            }
        }
        for (int i = 0; i < numProcesses; i++) {
            close(multipleFd[i][1]);
        }
        int numForks = numProcesses;
        while (numForks) {
            wait(&status);
            numForks--;
        }
        setInputDir(nullptr);
        setOutputDir(nullptr);
        exit(0);
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
            string bundleName = pi->command.bundle_name;
            currentBundle = new Bundle(bundleName);
            bundles.push_back(currentBundle);
            nameMap[bundleName] = bundles.size() - 1;
        } else if (pi->command.type == PROCESS_BUNDLE_STOP) {
            creation = 0;
        } else if (pi->command.type == QUIT) {
            quit = true;
            delete pi;
            for (int i = 0; i < allocatedCharp.size(); i++) {
                delete allocatedCharp[i];
            }
            for (int i = 0; i < bundles.size(); i++) {
                for (int j = 0; j < bundles[i]->processes.size(); j++) {
                    delete bundles[i]->processes[j];
                }
                delete bundles[i];
            }
        } else if (pi->command.type != PROCESS_BUNDLE_EXECUTION) { // command
            currentProcess = new Process(pi->argv[0]);
            for (int i = 1; pi->argv[i] != nullptr; i++) {
                currentProcess->args.push_back(pi->argv[i]);
            }
            currentBundle->processes.push_back(currentProcess);
        } else { // execution
            vector<Bundle*> executionList;
            int numBundles;
            int status;
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
            
            int multipleFd[executionList.size()][2];
            for (int i = 0; i < executionList.size(); i++) {
                pipe(multipleFd[i]);
            }
            for (int i = 0; i < executionList.size(); i++) {
                int forkPid = fork();
                if (forkPid == 0) {
                    if (i != 0) {
                        dup2(multipleFd[i - 1][0], 0);
                    }
                    dup2(multipleFd[i][1], 1);
                    for (int j = 0; j < executionList.size(); j++) {
                        close(multipleFd[j][0]);
                        close(multipleFd[j][1]);
                    }
                    executionList[i]->runProcesses(i == 0);
                } 
            }
            for (int i = 0; i < executionList.size(); i++) {
                if (i != executionList.size() - 1) {
                    close(multipleFd[i][0]);
                }
                close(multipleFd[i][1]);
            }
            int readCharCount = 0;
            char currentChar[10];
            while ((readCharCount = read(multipleFd[executionList.size() - 1][0], currentChar, 10)) > 0) {
                for (int i = 0; i < readCharCount; i++) {
                    write(1, currentChar + i, 1);
                }
            }
            close(multipleFd[executionList.size() - 1][0]);
            int totalFork = executionList.size();
            while (totalFork) {
                wait(&status);
                totalFork--;
            }
            fflush(stdout);
        }
    }

    return 0;
}
