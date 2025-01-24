#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>

#define READ 0
#define WRITE 1

typedef struct {
    sem_t* childSemWrite;
    int pipeForRead[2];  
    int pipeForWrite[2]; 
} ChildInfo;
typedef struct {
    int pipeForRead[2];  
    int pipeForWrite[2]; 
    sem_t* childSem;
    char childName[20]; 
} ParentInfo;

//Βοηθητικές συναρτήσεις
void writeToFile(int, const char*);
void readFromFile(int);
void check_bytes(ssize_t, char c);
void prepareData(char*, size_t, const char*, const pid_t);
void check_UInput(int, char**);
void cleanup(int, int, ParentInfo *);
int readFromPipe(int, char*, char);
int writeToPipe(int, char*, char);

int main (int argc, char *argv[]){
    //Παίρνουμε τα ορίσματα
    check_UInput(argc, argv);
    char * filename = argv[1];
    long children = atol(argv[2]);

    //O Γονέας ανοίγει το αρχείο για write
    int fd;
    if ((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600)) ==-1) {
        perror("open for write"); //Απέτυχε η open
        exit(EXIT_FAILURE);
    }

    //Δημιουργία πίνακα του πατέρα όπου κάθε στοιχείο είναι τύπου ParentInfo
    ParentInfo * parent_info = (ParentInfo *)malloc(children * sizeof(ParentInfo));  
    if (parent_info == NULL) { 
        perror("Error: malloc");
        close(fd);
        exit(EXIT_FAILURE);
    }

    //Αρχικοποίηση σεμαφόρων
    char semName[40];
    sem_t* tempSem;
    for (int i = 0; i < children; i++) {
        snprintf(semName, sizeof(semName), "child_sem_%d", i);
        sem_unlink(semName);
        tempSem = sem_open(semName, O_CREAT | O_EXCL, 0600, 0); //Locked

        if (tempSem == SEM_FAILED) {
            perror("Error: create child semaphore");
            cleanup(fd, i, parent_info);
            exit(EXIT_FAILURE);
        }
        parent_info[i].childSem = tempSem;
        snprintf(parent_info[i].childName, sizeof(parent_info[i].childName), "Child %d",i+1);
    }

    //Διαδικασία fork
    pid_t pid;
    ChildInfo child_info;

    for (int i=0; i<children; i++){
        //Δημιουργία 2 pipes
        if (pipe(parent_info[i].pipeForWrite) == -1 || pipe(parent_info[i].pipeForRead) == -1) {
            perror("Error: pipes");
            cleanup(fd, i - 1, parent_info);
            exit(EXIT_FAILURE);
        }

        //Αντιστοιχούμε τα pipes 
        memcpy(child_info.pipeForRead,parent_info[i].pipeForWrite,sizeof(parent_info[i].pipeForWrite));
        memcpy(child_info.pipeForWrite,parent_info[i].pipeForRead,sizeof(parent_info[i].pipeForRead));
        child_info.childSemWrite = parent_info[i].childSem;

        if ((pid = fork()) == -1){
            perror("Error: fork"); //Απέτυχε η fork
            cleanup(fd, children-1, parent_info);
            exit(EXIT_FAILURE);
        }
        if (pid==0){
            break;
        }
    }
    
    //Παιδί
    if (pid == 0){
        char data[100];
        char parent_msg[60];

        close(child_info.pipeForRead[WRITE]);
        close(child_info.pipeForWrite[READ]);

        //Διαβάζει το μύνημα του πατέρα από το pipe
        if(readFromPipe(child_info.pipeForRead[READ], parent_msg, 'c') == -1){
            exit(EXIT_FAILURE);
        }
        prepareData(data, sizeof(data), parent_msg, getpid());
        printf("Data Prepared: %s",data);

        sem_wait(child_info.childSemWrite);
        //Το παιδί κάνει write στο αρχείο     
        writeToFile(fd, data);
        // sem_post(child_info.childSemWrite);
        
        //Στέλνει μύνημα στον πατέρα μέσω του pipe
        if (writeToPipe(child_info.pipeForWrite[WRITE], "done", 'c') == -1){
            exit(EXIT_FAILURE);
        }
        close(child_info.pipeForRead[READ]);
        close(child_info.pipeForWrite[WRITE]);
        exit(EXIT_SUCCESS);
    }
    //Γονέας
    char msg_toChild[100];

    for(int i=0; i<children; i++){
        close(parent_info[i].pipeForWrite[READ]);
        close(parent_info[i].pipeForRead[WRITE]);
        snprintf(msg_toChild, sizeof(msg_toChild), "Hello child, I am your father and I call you: %s", parent_info[i].childName);
        
        //Στέλνει μύνημα στο παιδί μέσω του pipe
        if (writeToPipe(parent_info[i].pipeForWrite[WRITE], msg_toChild, 'p') == -1){
            cleanup(fd, children-1, parent_info);
            exit(EXIT_FAILURE);
        }
    }
    
    int status;
    char child_msg[30];
    //Ξυπνάει ένα παιδί, διαβάζει το μύνημα του, περιμένει να τελειώσει και ξυπνάει το επόμενο
    for (int i=0; i < children; i++){
        //Ξυπνάει το παιδί
        sem_post(parent_info[i].childSem);
        //Κάνουμε reset την τιμή του γιατί έχει μυνήματα από προηγούμενα παιδιά
        strcpy(child_msg,"");

        //Διαβάζει το μύνημα του παιδιού από το pipe
        if(readFromPipe(parent_info[i].pipeForRead[READ], child_msg, 'p') == -1){
            cleanup(fd, children-1, parent_info);
            exit(EXIT_FAILURE);
        }
        // printf("Message from %s: %s\n",parent_info[i].childName, child_msg);
        
        pid_t child_pid = waitpid(-1, &status, WUNTRACED);//-1 ή 0 νομίζω
        if (child_pid == -1) {
            perror("Error: waitpid");
            cleanup(fd, children-1, parent_info);
            exit(EXIT_FAILURE);
        }
    }

    //Κλείνει το αρχείο και το ανοίγει για read
    close(fd);
    if ((fd = open(filename, O_RDONLY , 0600)) ==-1) {
        perror("Error: open for read"); //Απέτυχε η open
        cleanup(fd, children-1, parent_info);
        exit(EXIT_FAILURE);
    }
    //Διαβάζει το αρχείο
    readFromFile(fd);
    cleanup(fd, children-1, parent_info);
}

//=================================Βοηθητικές συναρτήσεις================================
//Γράφει το process στο αρχείο
void writeToFile(int fd, const char* data){
    ssize_t bytes_written = write(fd, data, strlen(data));
    check_bytes(bytes_written,'w');
}
void prepareData(char* data, size_t dataSize, const char* parent_msg, const pid_t pid) {
    const char *marker = ": ";
    const char *start = strstr(parent_msg, marker); //Εντοπίζουμε το ": " στο μήνυμα

    char childName[50];
    size_t copiedBytes = sizeof(childName) - 1;
    if (start != NULL) {
        start += strlen(marker); //Μετακινούμαστε μετά το ": "
        strncpy(childName, start, copiedBytes);
    } else {
        strncpy(childName, "Name not received", copiedBytes);
    }
    childName[copiedBytes] = '\0';
    snprintf(data, dataSize, "%d ---> %s\n", pid, childName);
}
//Διαβάζει ο γονέας το αρχείο
void readFromFile(int fd){
    char buffer[100]; 
    ssize_t bytes_read;

    printf("Περιεχόμενο του αρχείου:\n");

    //Διαβάζει το αρχείο σε κομμάτια και τα εκτυπώνει
    while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }
    check_bytes(bytes_read,'r');
}
//Γράφει το μύνημα του στο pipe
int writeToPipe(int pfd, char* message,char c){

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
    char semName[40];
    for (int i = 0; i <= children; i++) {
        sem_close(parent_info[i].childSem);
        snprintf(semName, sizeof(semName), "child_sem_%d", i);
        sem_unlink(semName);
    }
    free(parent_info);
}
//========================================Έλεγχοι========================================