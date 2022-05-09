/******************************************************
 * IOS Project 2
 * Description: Process synchronization project
 *
 * Author: Oliver Gurka
 * Date: 2022-04-27
 ******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

#define NO_ERR 0
#define ERR 1

#define OXYGEN 42
#define HYDROGEN 420

#define LOG_FILE_NAME "proj2.out"
#define LOGGING_MUTEX "xgurka00-logging_mutex"
#define OXYGEN_QUEUE "xgurka00-oxygen_queue"
#define HYDROGEN_QUEUE "xgurka00-hydrogen_queue"
#define MOLECULE_MUTEX "xgurka00-molecule"
#define SYNTHESIZABLE_MUTEX "xgurka00-synthesizable_mutex"
#define MUTEX "xgurka00-mutex"

// Structure containing resources for working 2 phase barrier
typedef struct {
    size_t id;
    size_t n;
    size_t count;
    sem_t mutex;
    sem_t turnstile;
    sem_t turnstile2;
} barrier_t;

typedef struct {
    unsigned long int oxygen;           // no. of oxygen processes
    unsigned long int hydrogen;         // no. of hydrogen processes
    unsigned short int queuing_delay;   // sleep time before going to queue
    unsigned short int synthesis_delay; // sleep time before created molecule
} args_t;

/**
 * Initializes barrier
 * @param bar barrier to initialize
 * @param n number of processes to meet at barrier
 * @return 0 on success
 */
int barrier_init(barrier_t *bar, size_t n);

/**
 * Rendezvous point for processes at barrier
 * @param bar barrier to wait at
 */
void barrier_phase1(barrier_t *bar);

/**
 * End of critical section of barrier
 * @param bar barrier to wait at
 */
void barrier_phase2(barrier_t *bar);

/**
 * Destroys semaphores in barrier, not the memory
 * @param bar barrier to destroy
 */
void barrier_destroy(barrier_t *bar);

// Structure containing number of atoms that entered queue
typedef struct {
    unsigned long int oxy_in_queue;
    unsigned long int hydro_in_queue;
} queue_counter_t;

// Parses input arguments
int parse_args(int argc, char **argv, args_t *args);

// Allocates memory for semaphores
int semaphores_allocation();

// Frees and destroys semaphores
void semaphores_free();

/**
 * Creates new named semaphore
 * @param sem - pointer to pointer to a semaphore memory
 * @param name - name of new semaphore
 * @param init_value - initial value of semaphore
 * @return Returns 0 on success, otherwise 1
 */
int create_semaphore(sem_t **sem, const char* name, unsigned int init_value);

/**
 * Destroys named semaphore
 * @param sem - pointer to pointer to a semaphore memory
 * @param name - name of semaphore to destroy
 */
void destroy_semaphore(sem_t **sem, const char* name);

/**
 * Allocates all shared memory needed for this program
 * @return 0 on success
 */
int shm_allocation();

/**
 * Frees allocated shared memory
 */
void shm_free();

/**
 * Allocates all resources (memory, semaphores, barriers) needed by this program
 * @return 0 on success
 */
int get_resources();

/**
 * Frees all resources
 */
void free_resources();

// Kills all processes with pid in pid_t *processes
void kill_processes(pid_t *processes, size_t size);

// Code for oxygen process
void oxygen_process(unsigned int id, unsigned short int max_queuing_delay, unsigned short int max_synthesis_delay);

// Code for hydrogen process
void hydrogen_process(unsigned int id, unsigned short int max_queuing_delay);

FILE *log_file;

// Semaphores
sem_t *logging_mutex;               // mutex for writing to file
sem_t *oxygen_queue;
sem_t *hydrogen_queue;
sem_t *molecule_mutex;              // mutex for modifying and using molecule_counter
sem_t *synthesizable_mutex;         // mutex for modifying and using synthesizable
sem_t *mutex;                       // mutex for entering the queue

int barrier_mem_id;
barrier_t *barrier;

// Shared variables
int oxygen_counter_mem_id;
unsigned int *oxygen_counter;       // number of oxygen atoms in queue

int hydrogen_counter_mem_id;
unsigned int *hydrogen_counter;     // number of hydrogen atoms in queue

int action_counter_mem_id;
unsigned int *action_counter;       // keeps track of line numbers in file

int molecule_counter_mem_id;
unsigned int *molecule_counter;     // no. of created molecules

int queue_counter_mem_id;
queue_counter_t *queue_counter;     // no. of atoms which entered the queue

int synthesizable_mem_id;
bool *synthesizable;                // tells atoms if they can be used to create molecule

int err;

args_t args_glob;

int main(int argc, char **argv) {

    // Arguments parsing
    args_t args;
    int return_code = parse_args(argc, argv, &args);
    args_glob = args;

    if (return_code != 0) {
        exit(ERR);
    }

    pid_t processes_created[args.oxygen + args.hydrogen];
    size_t no_processes = 0;

    // Open logging file
    log_file = fopen(LOG_FILE_NAME, "w");
    if (log_file == NULL) {
        fprintf(stderr, "Error occurred while opening log file!\n");
        exit(ERR);
    }
    setbuf(log_file, NULL); // No output buffering

    if (get_resources() != NO_ERR) {
        exit(ERR);
    }

    queue_counter->hydro_in_queue = 0, queue_counter->oxy_in_queue = 0;

    // barrier init
    if (barrier_init(barrier, 3) != NO_ERR) {
        free_resources();
        fclose(log_file);
        exit(ERR);
    }
    *synthesizable = true;

    // Oxygen generation
    for (unsigned long int i = 1; i < args.oxygen + 1; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Error occurred while generating processes!\n");
            kill_processes(processes_created, no_processes);
            err = 1;
            break;
        } else if (pid == 0) {
            oxygen_process(i, args.queuing_delay, args.synthesis_delay);
        } else { // register processes created
            processes_created[no_processes] = pid;
            no_processes++;
            continue;
        }
    }

    // Hydrogen generation
    for (unsigned long int i = 1; i < args.hydrogen + 1; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Error occurred while generating processes!\n");
            kill_processes(processes_created, no_processes);
            err = 1;
            break;
        } else if (pid == 0) {
            hydrogen_process(i, args.queuing_delay);
        } else { // register processes created
            processes_created[no_processes] = pid;
            no_processes++;
            continue;
        }
    }

    while(wait(NULL) > 0); // waiting for children to end

    free_resources();
    if (fclose(log_file) == EOF) {
        fprintf(stderr, "Error occurred while closing file!\n");
    }

    if (err != 0)
        exit(ERR);

    exit(NO_ERR);
}

void kill_processes(pid_t *processes, size_t size) {
    for (size_t i = 0; i < size; i++) {
        kill(processes[i], SIGTERM);
    }
}

int parse_args(int argc, char **argv, args_t *args) {
    if (argc != 5) {
        fprintf(stderr, "Invalid argument count!\n");
        return 1;
    }

    long arg;
    char *ptr;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '\0') {
            fprintf(stderr, "Invalid argument count!\n");
            return 1;
        }
        arg = strtol(argv[i], &ptr, 10);

        // Check argument validity
        if (ptr[0] != '\0') {
            fprintf(stderr, "Invalid argument \"%s\"!\n", argv[i]);
            return 1;
        }

        // Every argument must be natural number
        if (arg < 0) {
            fprintf(stderr, "Invalid argument \"%s\" (must be >= 0)!\n", argv[i]);
            return 1;
        }

        // Assign argument
        switch (i) {
            case 1:
                args->oxygen = arg;
                break;
            case 2:
                args->hydrogen = arg;
                break;
            case 3:
                if (arg > 1000) {
                    fprintf(stderr, "Invalid argument \"%s\" (must be <= 1000)!\n", argv[i]);
                    return 1;
                }
                args->queuing_delay = arg;
                break;
            case 4:
                if (arg > 1000) {
                    fprintf(stderr, "Invalid argument \"%s\" (must be <= 1000)!\n", argv[i]);
                    return 1;
                }
                args->synthesis_delay = arg;
                break;
            default:
                fprintf(stderr, "Unknown error!\n");
                break;
        }
    }

    return 0;
}

// Logs to proj2.out process started
void log_started(int type, unsigned int id) {
    sem_wait(logging_mutex);
    // Critical section - writing to file
    ++(*action_counter);
    if (type == OXYGEN) {
        fprintf(log_file, "%u: O %u: started\n", *action_counter, id);
    } else if (type == HYDROGEN) {
        fprintf(log_file, "%u: H %u: started\n", *action_counter, id);
    }
    sem_post(logging_mutex);
}

// Logs to proj2.out going to queue
void log_going_to_queue(int type, unsigned int id) {
    sem_wait(logging_mutex);
    // Critical section - writing to file
    ++(*action_counter);
    if (type == OXYGEN) {
        fprintf(log_file, "%u: O %u: going to queue\n", *action_counter, id);
    } else if (type == HYDROGEN) {
        fprintf(log_file, "%u: H %u: going to queue\n", *action_counter, id);
    }
    sem_post(logging_mutex);
}

// Logs to proj2.out creating molecule
void log_creating_molecule(int type, unsigned int id) {
    sem_wait(logging_mutex);
    sem_wait(molecule_mutex);
    // Critical section - writing to file
    ++(*action_counter);
    if (type == OXYGEN) {
        fprintf(log_file, "%u: O %u: creating molecule %u\n", *action_counter, id, *molecule_counter);
    } else if (type == HYDROGEN) {
        fprintf(log_file, "%u: H %u: creating molecule %u\n", *action_counter, id, *molecule_counter);
    }
    sem_post(molecule_mutex);
    sem_post(logging_mutex);
}

// Logs to proj2.out molecule created
void log_created_molecule(int type, unsigned int id) {
    sem_wait(logging_mutex);
    // Critical section - writing to file
    ++(*action_counter);
    if (type == OXYGEN) {
        fprintf(log_file, "%u: O %u: molecule %u created\n", *action_counter, id, *molecule_counter);
    } else if (type == HYDROGEN) {
        fprintf(log_file, "%u: H %u: molecule %u created\n", *action_counter, id, *molecule_counter);
    }
    sem_post(logging_mutex);
}

// Logs to proj2.out not enough
void log_not_enough(int type, unsigned int id) {
    sem_wait(logging_mutex);
    // Critical section - writing to file
    ++(*action_counter);
    if (type == OXYGEN) {
        fprintf(log_file, "%u: O %u: not enough H\n", *action_counter, id);
    } else if (type == HYDROGEN) {
        fprintf(log_file, "%u: H %u: not enough O or H\n", *action_counter, id);
    }
    sem_post(logging_mutex);
}

int create_semaphore(sem_t **sem, const char* name, unsigned int init_value) {
    *sem = sem_open(name, O_CREAT | O_EXCL, DEFFILEMODE, init_value);
    if (*sem == SEM_FAILED) {
        return ERR;
    }
    return NO_ERR;
}

void destroy_semaphore(sem_t **sem, const char* name) {
    sem_close(*sem);
    sem_unlink(name);
}

/**
 * Creates new shared memory
 * @param mem_id pointer to location where mem_id should be stored
 * @param mem_addr pointer to memory pointer, where shared memory address should be stored
 * @param size size of wanted memory segment
 * @return 0 on success
 */
int create_shmem(int *mem_id, void **mem_addr, size_t size) {
    *mem_id = shmget(IPC_PRIVATE, size, IPC_CREAT | DEFFILEMODE);
    if (*mem_id == -1) {
        return ERR;
    }

    *mem_addr = shmat(*mem_id, NULL, 0);
    if (*mem_addr == NULL) {
        return ERR;
    }
    return NO_ERR;
}

/**
 * Destroys shared memory segment
 * @param mem_id memory id of segment to destroy
 */
void destroy_shmem(int mem_id) {
    shmctl(mem_id, IPC_RMID, NULL);
}

int semaphores_allocation() {
    if (create_semaphore(&logging_mutex, LOGGING_MUTEX, 1) != 0 ||
            create_semaphore(&oxygen_queue, OXYGEN_QUEUE, 0) != 0 ||
            create_semaphore(&hydrogen_queue, HYDROGEN_QUEUE, 0) != 0 ||
            create_semaphore(&molecule_mutex, MOLECULE_MUTEX, 1) != 0 ||
            create_semaphore(&synthesizable_mutex, SYNTHESIZABLE_MUTEX, 1) != 0 ||
            create_semaphore(&mutex, MUTEX, 1) != 0) {
        semaphores_free();
        return ERR;
    }
    return NO_ERR;
}

void semaphores_free() {
    destroy_semaphore(&logging_mutex, LOGGING_MUTEX);
    destroy_semaphore(&oxygen_queue, OXYGEN_QUEUE);
    destroy_semaphore(&hydrogen_queue, HYDROGEN_QUEUE);
    destroy_semaphore(&molecule_mutex, MOLECULE_MUTEX);
    destroy_semaphore(&synthesizable_mutex, SYNTHESIZABLE_MUTEX);
    destroy_semaphore(&mutex, MUTEX);
}

int shm_allocation() {
    if (    create_shmem(&oxygen_counter_mem_id,(void **) &oxygen_counter, sizeof(unsigned int)) != NO_ERR ||
            create_shmem(&hydrogen_counter_mem_id, (void **) &hydrogen_counter, sizeof(unsigned int)) != NO_ERR ||
            create_shmem(&action_counter_mem_id, (void **) &action_counter, sizeof(unsigned  int)) != NO_ERR ||
            create_shmem(&molecule_counter_mem_id, (void **) &molecule_counter, sizeof(unsigned int)) != NO_ERR ||
            create_shmem(&queue_counter_mem_id, (void **) &queue_counter, sizeof(queue_counter_t)) != NO_ERR ||
            create_shmem(&synthesizable_mem_id, (void **) &synthesizable, sizeof(queue_counter_t)) != NO_ERR ||
            create_shmem(&barrier_mem_id, (void **) &barrier, sizeof(barrier_t)) != NO_ERR){
        shm_free();
        return ERR;
    }
    return NO_ERR;
}

void shm_free() {
    destroy_shmem(oxygen_counter_mem_id);
    destroy_shmem(hydrogen_counter_mem_id);
    destroy_shmem(action_counter_mem_id);
    destroy_shmem(molecule_counter_mem_id);
    destroy_shmem(queue_counter_mem_id);
    destroy_shmem(synthesizable_mem_id);
    destroy_shmem(barrier_mem_id);
}

int get_resources() {
    if (semaphores_allocation() != NO_ERR) {
        fprintf(stderr, "Error occurred while creating semaphores!\n");
        semaphores_free();
        return ERR;
    }

    if (shm_allocation() != NO_ERR) {
        fprintf(stderr, "Error occurred while allocating shared memory!\n");
        semaphores_free();
        shm_free();
        return ERR;
    }
    return NO_ERR;
}

void free_resources() {
    barrier_destroy(barrier);
    shm_free();
    semaphores_free();
}

// Sleeps for random time in range <0, max_delay> milliseconds
void sleep_random(unsigned int max_delay) {
    srand(getpid() + time(NULL));
    unsigned int wait = rand() % (max_delay + 1);
    usleep(wait * 1000); // sleep wait milliseconds
}

void oxygen_process(unsigned int id, unsigned short int max_queuing_delay, unsigned short int max_synthesis_delay) {
    log_started(OXYGEN, id);

    sleep_random(max_queuing_delay);

    log_going_to_queue(OXYGEN, id);

    sem_wait(mutex);
    (*oxygen_counter)++;
    queue_counter->oxy_in_queue++;

    // Entering queue
    sem_wait(molecule_mutex);
    if (*hydrogen_counter >= 2) { // molecule can be created
        (*molecule_counter)++;

        (*hydrogen_counter) -= 2;
        sem_post(hydrogen_queue);
        sem_post(hydrogen_queue);

        (*oxygen_counter)--;
        sem_post(oxygen_queue);
    } else if ((queue_counter->hydro_in_queue == args_glob.hydrogen &&
                queue_counter->oxy_in_queue == args_glob.oxygen)) { // last atom entered the queue

        sem_wait(synthesizable_mutex);
        *synthesizable = false;                                     // sets all other which leave the queue not synthesizable
        sem_post(synthesizable_mutex);

        // Frees the queues
        for (size_t i = 0; i < args_glob.oxygen; i++) {
            sem_post(oxygen_queue);
        }
        for (size_t i = 0; i < args_glob.hydrogen; i++) {
            sem_post(hydrogen_queue);
        }
    } else {                                                        // molecule can not be created, going to queue
        sem_post(mutex);
    }
    sem_post(molecule_mutex);
    sem_wait(oxygen_queue);

    // Checks if it is synthesizable
    sem_wait(synthesizable_mutex);
    if (!*synthesizable) {
        sem_post(synthesizable_mutex);
        log_not_enough(OXYGEN, id);
        sem_post(mutex);

        if (fclose(log_file) == EOF) {
            fprintf(stderr, "Error occurred while closing file!\n");
        }
        exit(NO_ERR);
    }
    sem_post(synthesizable_mutex);

    log_creating_molecule(OXYGEN, id);
    sleep_random(max_synthesis_delay);
    barrier_phase1(barrier); // rendezvous for 2 hydrogen atoms and 1 oxygen atom

    // Critical section
    log_created_molecule(OXYGEN, id);
    barrier_phase2(barrier);

    sem_wait(molecule_mutex);

    // When last molecule was created and oxygen was last atom
    if (queue_counter->hydro_in_queue == args_glob.hydrogen && queue_counter->oxy_in_queue == args_glob.oxygen) {

        sem_wait(synthesizable_mutex);
        *synthesizable = false;
        sem_post(synthesizable_mutex);

        for (size_t i = 0; i < args_glob.oxygen; i++) {
            sem_post(oxygen_queue);
        }
        for (size_t i = 0; i < args_glob.hydrogen; i++) {
            sem_post(hydrogen_queue);
        }
    }
    sem_post(molecule_mutex);

    sem_post(mutex);

    if (fclose(log_file) == EOF) {
        fprintf(stderr, "Error occurred while closing file!\n");
    }
    exit(NO_ERR);
}

void hydrogen_process(unsigned int id, unsigned short int max_queuing_delay) {
    log_started(HYDROGEN, id);

    sleep_random(max_queuing_delay);

    log_going_to_queue(HYDROGEN, id);

    // Entering queue
    sem_wait(mutex);
    (*hydrogen_counter)++;
    queue_counter->hydro_in_queue++;

    sem_wait(molecule_mutex); // locking molecule counter in case molecule can be created
    if (*hydrogen_counter >= 2 && *oxygen_counter >= 1) { // molecule can be created
        (*molecule_counter)++;

        (*hydrogen_counter) -= 2;
        sem_post(hydrogen_queue);
        sem_post(hydrogen_queue);

        (*oxygen_counter)--;
        sem_post(oxygen_queue);

    } else if ((queue_counter->hydro_in_queue == args_glob.hydrogen && queue_counter->oxy_in_queue == args_glob.oxygen)) {
        // last molecule entering queue

        sem_wait(synthesizable_mutex);
        *synthesizable = false;
        sem_post(synthesizable_mutex);

        for (size_t i = 0; i < args_glob.oxygen; i++) {
            sem_post(oxygen_queue);
        }
        for (size_t i = 0; i < args_glob.hydrogen; i++) {
            sem_post(hydrogen_queue);
        }
    } else {
        sem_post(mutex);
    }
    sem_post(molecule_mutex);
    sem_wait(hydrogen_queue);

    // Check if atom can be ever used in synthesis
    sem_wait(synthesizable_mutex);
    if (!*synthesizable) {
        sem_post(synthesizable_mutex);
        log_not_enough(HYDROGEN, id);

        if (fclose(log_file) == EOF) {
            fprintf(stderr, "Error occurred while closing file!\n");
        }
        exit(NO_ERR);
    }
    sem_post(synthesizable_mutex);

    log_creating_molecule(HYDROGEN, id);
    barrier_phase1(barrier);    // rendezvous for 2 hydrogen atoms and 1 oxygen atom

    // Critical section
    log_created_molecule(HYDROGEN, id);
    barrier_phase2(barrier);

    if (fclose(log_file) == EOF) {
        fprintf(stderr, "Error occurred while closing file!\n");
    }
    exit(NO_ERR);
}

/* Barrier methods */
int barrier_init(barrier_t *bar, size_t n) {
    bar->n = n;
    bar->count = 0;
    if (sem_init(&bar->mutex, 1, 1) != NO_ERR ||
        sem_init(&bar->turnstile, 1, 0) != NO_ERR ||
        sem_init(&bar->turnstile2, 1, 0) != NO_ERR) {
        return ERR;
    }
    return NO_ERR;
}

void barrier_phase1(barrier_t *bar) {
    sem_wait(&bar->mutex);
    bar->count++;
    if (bar->count == bar->n) {
        for (size_t i = 0; i < bar->n; i++) {
            sem_post(&bar->turnstile);
        }
    }
    sem_post(&bar->mutex);
    sem_wait(&bar->turnstile);
}

void barrier_phase2(barrier_t *bar) {
    sem_wait(&bar->mutex);
    bar->count--;
    if (bar->count == 0) {
        for (size_t i = 0; i < bar->n; i++) {
            sem_post(&bar->turnstile2);
        }
    }
    sem_post(&bar->mutex);
    sem_wait(&bar->turnstile2);
}

void barrier_destroy(barrier_t *bar) {
    sem_close(&bar->mutex);
    sem_close(&bar->turnstile);
    sem_close(&bar->turnstile2);
}
