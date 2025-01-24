#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string.h>

#define DATA_LENGTH 30
#define PARENT_NAME_FILE "[PARENT] --> "
#define CHILD_NAME_FILE "[CHILD] --> "

//Βοηθητικές συναρτήσεις
void writeToFile(int, const char*);
void readFromFile(int);
void check_bytes(ssize_t, char c);
void prepareData(char*, const char*, const pid_t);
void check_UInput(int, char**);
void cleanup(int, int, sem_t **);


typedef struct {
    sem_t* childSemWrite;
} ChildInfo;

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

    //Δημιουργία πίνακα για τους σεμαφόρους των παιδιών 
    sem_t **child_sems = (sem_t **)malloc(children * sizeof(sem_t*));  
    if (child_sems == NULL) { 
        perror("Error: malloc");
        close(fd);
        exit(EXIT_FAILURE);
    }
    //Αρχικοποίηση πίνακα σεμαφόρων
    char semName[40];
    for (int i = 0; i < children; i++) {
        snprintf(semName, sizeof(semName), "child_sem_%d", i);
        child_sems[i] = sem_open(semName, O_CREAT | O_EXCL, 0600, 0); //Locked

        if (child_sems[i] == SEM_FAILED) {
            perror("Error: create child semaphore");
            cleanup(fd, i, child_sems);
            exit(EXIT_FAILURE);
        }
    }

    //Διαδικασία fork
    pid_t pid;
    ChildInfo child_info;

    for (int i=0; i<children; i++){
        child_info.childSemWrite = child_sems[i];
        if ((pid = fork()) == -1){
            perror("Error: fork"); //Απέτυχε η fork
            cleanup(fd, children-1, child_sems);
            exit(EXIT_FAILURE);
        }
        if (pid==0){
            break;
        }
    }
    
    //Παιδί
    if (pid == 0){
        char data[50];
        prepareData(data, CHILD_NAME_FILE, getpid());
        printf("Data Prepared: %s",data);

        sem_wait(child_info.childSemWrite);
        //Το παιδί κάνει write στο αρχείο     
        writeToFile(fd, data);
        sem_post(child_info.childSemWrite);
        exit(EXIT_SUCCESS);
    }
    //Γονέας
    char data[50];
    prepareData(data, PARENT_NAME_FILE, getpid());
    printf("Data Prepared: %s",data);

    //O Γονέας κάνει write στο αρχείο     
    writeToFile(fd, data);
    
    //Ξυπνάει ένα παδί, περιμένει να τελειώσει και ξυπνάει το επόμενο
    int status;
    for (int i=0; i < children; i++){
        //Ξυπνάει το παιδί
        sem_post(child_sems[i]);

        pid_t child_pid = waitpid(-1, &status, WUNTRACED);//-1 ή 0 νομίζω
        if (child_pid == -1) {
            perror("Error: waitpid");
            cleanup(fd, children-1, child_sems);
            exit(EXIT_FAILURE);
        }
    }

    //Κλείνει το αρχείο και το ανοίγει για read
    close(fd);
    if ((fd = open(filename, O_RDONLY , 0600)) ==-1) {
        perror("Error: open for read"); //Απέτυχε η open
        cleanup(fd, children-1, child_sems);
        exit(EXIT_FAILURE);
    }
    //Διαβάζει το αρχείο
    readFromFile(fd);
    cleanup(fd, children-1, child_sems);
}

//=================================Βοηθητικές συναρτήσεις================================
//Γράφει το process στο αρχείο
void writeToFile(int fd, const char* data){
    ssize_t bytes_written = write(fd, data, strlen(data));
    check_bytes(bytes_written,'w');
}
void prepareData(char* data, const char* procName, const pid_t pid){
    // Μετατροπή pid σε string
    char pidStr[20]; 
    snprintf(pidStr, sizeof(pidStr), "%d", pid); 

    strncat(data, procName, strlen(procName));
    strncat(data, pidStr, strlen(pidStr));
    strcat(data, "\n");
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
void cleanup(int fd, int children, sem_t **child_sems){
    close(fd);
    char semName[40];
    for (int i = 0; i <= children; i++) {
        sem_close(child_sems[i]);
        snprintf(semName, sizeof(semName), "child_sem_%d", i);
        sem_unlink(semName);
    }
    free(child_sems);
}
//========================================Έλεγχοι========================================