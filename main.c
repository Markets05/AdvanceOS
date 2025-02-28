#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define READ 0
#define WRITE 1
#define NOT_IDLE 0
#define IDLE 1

typedef struct {
    int pipeForRead[2];  
    int pipeForWrite[2]; 
    char childName[20]; 
} ChildInfo;
typedef struct {
    int pipeForRead[2];  
    int pipeForWrite[2]; 
    char childName[20]; 
    int childIsIdle;
} ParentInfo;

//Βοηθητικές συναρτήσεις
void writeToFile(int, const char*);
void readFromFile(int);
void check_bytes(ssize_t, char c);
void prepareData(char*, size_t, const char*, const char*, char*);
void getNameFromParent(char*, const char*, char*);
void check_UInput(int, char**);
void cleanup(int, int, ParentInfo *);
void child_cleanup(int , ParentInfo *, pid_t);
int readFromPipe(int, char*, char);
int writeToPipe(int, char*, char);
int calculateMaxFD(int[2] , int[2] , int);

//Διαχείριση Σημάτων SIGTERM, SIGINT (Ctrl+C)
void kill_child(int);
void on_sig_term_intr(int);

long children = 0;
int fd;
char * filename;
ParentInfo * parent_info;
ChildInfo child_info;
pid_t* childrenPids;

int main (int argc, char *argv[]){
    //Παίρνουμε τα ορίσματα
    check_UInput(argc, argv);
    filename = argv[1];
    children = atol(argv[2]);

    //O Γονέας ανοίγει το αρχείο για write
    if ((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600)) ==-1) {
        perror("open for write"); //Απέτυχε η open
        exit(EXIT_FAILURE);
    }

    //Δημιουργία πίνακα του πατέρα όπου κάθε στοιχείο είναι τύπου ParentInfo
    parent_info = (ParentInfo *) malloc (children * sizeof(ParentInfo));  
    if (parent_info == NULL) { 
        perror("Error: malloc for parent info");
        close(fd);
        exit(EXIT_FAILURE);
    }

    //Δημιουργία πίνακα για τα pid των παιδιών
    childrenPids = (pid_t *) malloc (children * sizeof(pid_t));
    if (childrenPids == NULL) { 
        perror("Error: malloc for children pids");
        close(fd);free(parent_info);
        exit(EXIT_FAILURE);
    }

    //Διαδικασία fork
    pid_t pid;
    int maxFD = 0;
    int amountPipes;//Το παιδί ξέρει πόσα pipes του πατέρα να κλείσει
    for (int i=0; i<children; i++){
        //Δημιουργία 2 pipes
        if (pipe(parent_info[i].pipeForWrite) == -1 || pipe(parent_info[i].pipeForRead) == -1) {
            perror("Error: pipes");
            cleanup(fd, i - 1, parent_info);
            exit(EXIT_FAILURE);
        }
        //Βρίσκουμε το current maxFD
        maxFD = calculateMaxFD(parent_info[i].pipeForRead, parent_info[i].pipeForWrite, maxFD);

        //Αντιστοιχούμε τα pipes 
        memcpy(child_info.pipeForRead,parent_info[i].pipeForWrite,sizeof(parent_info[i].pipeForWrite));
        memcpy(child_info.pipeForWrite,parent_info[i].pipeForRead,sizeof(parent_info[i].pipeForRead));

        if ((pid = fork()) == -1){
            perror("Error: fork"); //Απέτυχε η fork
            cleanup(fd, i, parent_info);
            exit(EXIT_FAILURE);
        }
        if (pid==0){
            amountPipes = i;
            break;
        }
        childrenPids[i] = pid;
        snprintf(parent_info[i].childName, sizeof(parent_info[i].childName), "Child %d",i+1);
        parent_info[i].childIsIdle = IDLE;
    }
    
    //Παιδί
    if (pid == 0){
        signal(SIGINT, on_sig_term_intr);
        signal(SIGTERM, on_sig_term_intr);
        close(child_info.pipeForRead[WRITE]);
        close(child_info.pipeForWrite[READ]);
        child_cleanup(amountPipes, parent_info, getpid());

        char data[100];
        char parent_msg[60], child_msg[60];

        //Διαβάζει το όνομα του δηλαδή το πρώτο μύνημα του πατέρα από το pipe
        if(readFromPipe(child_info.pipeForRead[READ], parent_msg, 'c') == -1){
            exit(EXIT_FAILURE);
        }
        getNameFromParent(child_info.childName, parent_msg, child_msg);
        //Στέλνει μύνημα στον πατέρα μέσω του pipe
        if (writeToPipe(child_info.pipeForWrite[WRITE], child_msg, 'c') == -1){
            exit(EXIT_FAILURE);
        }
        strcpy(parent_msg, "");
        strcpy(child_msg, "");
        //Ξεκινάει το loop που τελειώνει όταν σταλθεί σήμα από τον γονέα στο παιδί
        while(1){
            if(readFromPipe(child_info.pipeForRead[READ], parent_msg, 'c') == -1){
                exit(EXIT_FAILURE);
            }
            prepareData(data, sizeof(data), parent_msg, child_info.childName, child_msg);

            //Το παιδί κάνει write στο αρχείο     
            writeToFile(fd, data);
            sleep(1);
            if (writeToPipe(child_info.pipeForWrite[WRITE], child_msg, 'c') == -1){
                exit(EXIT_FAILURE);
            }
            strcpy(parent_msg, "");
            strcpy(child_msg, "");
        }
    }
    //Γονέας
    signal(SIGINT, kill_child);
    signal(SIGTERM, kill_child);

    for(int i=0; i<children; i++){
        close(parent_info[i].pipeForWrite[READ]);
        close(parent_info[i].pipeForRead[WRITE]);
    }

    //Το readFromCh δηλώνει από πόσα παιδιά διάβασε ο πατέρας σε ένα select
    int readFromCh,countSelect=0;
    int taskCount=0;
    char msg_toChild[100], child_msg[30];
    fd_set readfds;
    while(1){
        if (taskCount == 0) { //Αρχικό μύνημα με το όνομα του παιδιού
            for(int i=0; i<children; i++){
                snprintf(msg_toChild, sizeof(msg_toChild), "Hello child, I am your father and I call you: %s", parent_info[i].childName);
                //Στέλνει μύνημα στο παιδί μέσω του pipe
                if (writeToPipe(parent_info[i].pipeForWrite[WRITE], msg_toChild, 'p') == -1){
                    cleanup(fd, children-1, parent_info);
                    exit(EXIT_FAILURE);
                }
                parent_info[i].childIsIdle = NOT_IDLE;
                strcpy(msg_toChild, "");
            }
        }else{
            //Ο γονέας στέλνει tasks σε όσα παιδιά είναι idle
            snprintf(msg_toChild, sizeof(msg_toChild), "Hello child, I am your father and your task is: task%d", taskCount);
            for(int i=0; i<children; i++){
                if (parent_info[i].childIsIdle == IDLE){
                    if (writeToPipe(parent_info[i].pipeForWrite[WRITE], msg_toChild, 'p') == -1){
                        cleanup(fd, children-1, parent_info);
                        exit(EXIT_FAILURE);
                    }
                    parent_info[i].childIsIdle = NOT_IDLE;
                }
            }
        }
        taskCount++;
        FD_ZERO(&readfds);

        //Στο set readfds βάζουμε όλα τα άκρα των pipes που διαβάζει ο πατέρας
        for (int i = 0; i < children; i++) {
            FD_SET(parent_info[i].pipeForRead[READ], &readfds);
        }

        int ready = select(maxFD + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0) {
            perror("Error: select");
            cleanup(fd, children-1, parent_info);
            exit(EXIT_FAILURE);
        }
        countSelect++;
        readFromCh = 0;
        for (int i = 0; i < children; i++) {
            if (FD_ISSET(parent_info[i].pipeForRead[READ], &readfds)) {
                strcpy(child_msg, "");
                if (readFromPipe(parent_info[i].pipeForRead[READ], child_msg, 'p') == -1) {
                    cleanup(fd, children-1, parent_info);
                    exit(EXIT_FAILURE);
                }
                printf("Message from %s: '%s'\n", parent_info[i].childName, child_msg);
                parent_info[i].childIsIdle = IDLE;
                readFromCh++;
            }
        }
        printf("Select: %d - Available Children for read: %d\n\n", countSelect, readFromCh);
    }
}
//=================================Βοηθητικές συναρτήσεις================================
//Γράφει το process στο αρχείο
void writeToFile(int fd, const char* data){
    ssize_t bytes_written = write(fd, data, strlen(data));
    check_bytes(bytes_written,'w');
}
void prepareData(char* data, size_t dataSize, const char* parent_msg, const char* childName, char* child_msg) {
    const char *marker = ": ";
    const char *start = strstr(parent_msg, marker); //Εντοπίζουμε το ": " στο μήνυμα

    int taskReceived = 0;
    char task[50];
    size_t copiedBytes = sizeof(task) - 1;
    if (start != NULL) {
        start += strlen(marker); //Μετακινούμαστε μετά το ": "
        strncpy(task, start, copiedBytes);
        taskReceived = 1;
    } else {
        strncpy(task, "Task not received", copiedBytes);
    }
    task[copiedBytes] = '\0';
    snprintf(data, dataSize, "%s ---> %s\n", childName, task);
    
    if (taskReceived){
        sprintf(child_msg, "%s - Done with %s", childName, task);
    }else{
        sprintf(child_msg, "%s didn't receive a valid task", childName);
    }
}
void getNameFromParent(char* childName, const char* parent_msg, char* child_msg) {
    const char *marker = ": ";
    const char *start = strstr(parent_msg, marker); //Εντοπίζουμε το ": " στο μήνυμα

    int nameReceived = 0;
    char tempName[50];
    size_t copiedBytes = sizeof(tempName) - 1;
    if (start != NULL) {
        start += strlen(marker); //Μετακινούμαστε μετά το ": "
        strncpy(tempName, start, copiedBytes);
        nameReceived = 1;
    } else {
        strncpy(tempName, "Name not received", copiedBytes);
    }
    tempName[copiedBytes] = '\0';
    strcpy(childName,tempName);

    if (nameReceived){
        sprintf(child_msg, "%s has pid %d",childName, getpid());
    }else{
        sprintf(child_msg, "Child with pid %d didn't receive a name", getpid());
    }
}
//Διαβάζει ο γονέας το αρχείο
void readFromFile(int fd){
    char buffer[100]; 
    ssize_t bytes_read;

    printf("File content:\n");

    //Διαβάζει το αρχείο σε κομμάτια και τα εκτυπώνει
    while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }
    check_bytes(bytes_read,'r');
}
//Γράφει το μύνημα του στο pipe
int writeToPipe(int pfd, char* message, char c){

    if (write(pfd, message, strlen(message)) == -1) {
        if (c == 'p') perror("Error: parent sending message to child");
        if (c == 'c') perror("Error: child sending message to parent");
        return -1;
    }
    return 0;
}
//Διαβάζει το μύνημα από το pipe
int readFromPipe(int pfd, char* message, char c){
    char buffer[100]; 
    ssize_t bytes_read;
    if ((bytes_read = read(pfd, buffer, sizeof(buffer) - 1)) == -1) {
        if (c == 'p') perror("Error: parent reading message from child");
        if (c == 'c') perror("Error: child reading message from parent");
        return -1;
    }
    buffer[bytes_read] = '\0';
    strncat(message, buffer, strlen(buffer));
    return 0;
}
//Υπολογίζουμε το maxFD
int calculateMaxFD(int p1fd[2], int p2fd[2], int currentMaxFD) {
    if (p1fd[0] > currentMaxFD) {
        currentMaxFD = p1fd[0];
    }
    if (p1fd[1] > currentMaxFD) {
        currentMaxFD = p1fd[1];
    }
    if (p2fd[0] > currentMaxFD) {
        currentMaxFD = p2fd[0];
    }
    if (p2fd[1] > currentMaxFD) {
        currentMaxFD = p2fd[1];
    }
    return currentMaxFD;
}
//Το kill_child είναι για τον γονέα και το on_sig_term_intr είναι για το παιδί
void kill_child(int sig){
    printf("Parent received signal <%d>. Shutting down...\n", sig);

    //Στέλνει το σήμα σε όλα τα παιδιά και περιμένει να τερματιστούν όλα
    for (int i = 0; i < children; i++) {
        kill(childrenPids[i],sig);
    }
    int status;
    for (int i = 0; i < children; i++) {
        pid_t child_pid = waitpid(-1, &status, WUNTRACED);//-1 ή 0 νομίζω
        if (child_pid == -1) {
            perror("Error: waitpid");
            cleanup(fd, children-1, parent_info);
            exit(EXIT_FAILURE);
        }
    }
    if (sig == 2) printf("All children have been interrupted!\n");
    if (sig == 15) printf("All children have been terminated!\n");

    //Κλείνει το αρχείο και το ανοίγει για read
    close(fd);
    if ((fd = open(filename, O_RDONLY , 0600)) ==-1) {
        perror("Error: open for read"); //Απέτυχε η open
        cleanup(fd, children-1, parent_info);
        exit(EXIT_FAILURE);
    }
    //Διαβάζει το αρχείο
    readFromFile(fd);
    printf("Pid for children:\n");
    for (int i=0; i<children; i++){
        printf("%s - %d\n", parent_info[i].childName, childrenPids[i]);
    }
    cleanup(fd, children-1, parent_info);

    exit(EXIT_SUCCESS);
}
void on_sig_term_intr(int sig){
    close(child_info.pipeForRead[READ]);
    close(child_info.pipeForWrite[WRITE]);

    if (sig == 2) printf("I am child with pid %d and my father just interrupted me!\n", getpid());
    if (sig == 15) printf("I am child with pid %d and my father just terminated me!\n", getpid());
    exit(sig);
}
//=================================Βοηθητικές συναρτήσεις================================
//========================================Έλεγχοι========================================
void check_UInput(int argc, char * argv[]){
    //Έλεγχος για το πλήθος των ορισμάτων
    if (argc != 3) {
        printf("Usage: %s <filename> <amount of processes>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //Έλεγχος ότι το τρίτο όρισμα είναι ακέραιος
    char *endptr;
    int num = strtol(argv[2], &endptr, 10); //Μετατροπή του τρίτου ορίσματος σε ακέραιο στο δεκαδικό
    if (*endptr != '\0') {
        printf("Error: Third argument must be an integer\n");
        exit(EXIT_FAILURE);
    }
    //Έλεγχος ότι ο αριθμός είναι θετικός και μεγαλύτερος από 0
    if (num <= 0) {
        printf("Error: Third argument must be a positive integer greater than 0\n");
        exit(EXIT_FAILURE);
    }
}
void check_bytes(ssize_t bytes , char c){
    if (bytes == -1){
        if (c == 'w') perror("Error: write");
        if (c == 'r') perror("Error: read");
        exit(EXIT_FAILURE);
    }
}
void cleanup(int fd, int children, ParentInfo *parent_info){
    close(fd);
    for (int i = 0; i <= children; i++) {
        close(parent_info[i].pipeForWrite[WRITE]);
        close(parent_info[i].pipeForWrite[READ]);
        close(parent_info[i].pipeForRead[WRITE]);
        close(parent_info[i].pipeForRead[READ]);
    }
    free(childrenPids);
    free(parent_info);
}
void child_cleanup(int amountPipes, ParentInfo *parent_info, pid_t pid){
    //Για κάθε παιδί κλείνουμε τα pipes που δεν χρειάζεται δηλαδή τα pipes των άλλων παιδιών
    for (int i = 0; i <= amountPipes-1; i++) {
        if (childrenPids[i] != pid){
            close(parent_info[i].pipeForWrite[WRITE]);
            close(parent_info[i].pipeForWrite[READ]);
            close(parent_info[i].pipeForRead[WRITE]);
            close(parent_info[i].pipeForRead[READ]);
        }
    }
    free(childrenPids);
    free(parent_info);
}
//========================================Έλεγχοι========================================