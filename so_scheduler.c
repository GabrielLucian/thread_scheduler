#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#include "so_scheduler.h"

typedef enum status // statusurile unui thread
{
    New,
    Ready,
    Running,
    Waiting,
    Terminated
} Status;

typedef struct thread
{
    int priority;
    int running_time;
    Status status;
    tid_t id;
    so_handler *function;
    int waiting_signal;
    sem_t semaphore;
} Thread;

typedef struct scheduler
{
    int exists;
    int events;
    int time_quantum;
    Thread *running;
    Thread **threads;
    int threads_no;
    int threads_capacity;
} Scheduler;

static Scheduler scheduler;

typedef struct node
{
    Thread *thread;
    struct node *next;
    struct node *prev;
} Node;

typedef struct queue
{
    Node *first;
    Node *last;
    int len;
} Queue;

Queue *init_queue() // initializare queue
{
    Queue *queue = malloc(sizeof(Queue));
    queue->first = NULL;
    queue->last = NULL;
    queue->len = 0;
    return queue;
}

void enqueue(Queue *queue, Thread *thread) // adauga in queue in fct de prioritate
{
    Node *node = malloc(sizeof(Node)); // initializez un nod
    node->thread = thread;
    node->next = NULL;
    node->prev = NULL;
    node->thread->status = Ready;

    if (queue->len == 0) // adaug in queue daca e gol
    {
        queue->first = node;
        queue->last = node;
        queue->len = 1;
        return;
    }
    // adaug pe prima pozitie daca e cazul
    if (node->thread->priority > queue->first->thread->priority)
    {
        node->next = queue->first;
        queue->first->prev = node;
        queue->first = node;
        queue->len++;
        return;
    }
    // adaug pe ultima pozitie daca e cazul
    if (node->thread->priority <= queue->last->thread->priority)
    {
        node->prev = queue->last;
        queue->last->next = node;
        queue->last = node;
        queue->len++;
        return;
    }
    // daca nu, parcurg queueul de la final pana cand gasesc unde trb adaugat threadul
    Node *current = queue->last;
    while (current->thread->priority < node->thread->priority && current->prev != NULL)
        current = current->prev;
    if (!current->prev || current->thread->priority >= node->thread->priority)
    {
        node->prev = current;
        node->next = current->next;
        current->next->prev = node;
        current->next = node;
        queue->len++;
    }
}
void waiting_enqueue(Queue *queue, Thread *thread) // adaug in queue fara vreo sortare
{
    Node *node = malloc(sizeof(Node));
    node->thread = thread;
    node->next = NULL;
    node->prev = NULL;
    node->thread->status = Waiting;

    if (queue->len == 0) // adaug cand e gol
    {
        queue->first = node;
        queue->last = node;
        queue->len = 1;
        return;
    }

    Node *current = queue->last; // adaug la final
    node->prev = current;
    current->next = node;
    queue->last = node;
    queue->len++;
}

Thread *dequeue(Queue *queue) // scot primul element din queue
{
    if (queue->len == 0)
        return NULL;

    Thread *thread;
    if (queue->len == 1)
    {
        thread = queue->first->thread;
        free(queue->first);
        queue->first = NULL;
        queue->last = NULL;
        queue->len--;
        return thread;
    }
    thread = queue->first->thread;
    Node *first = queue->first;
    queue->first->next->prev = NULL;
    queue->first = queue->first->next;
    free(first);
    queue->len--;
    return thread;
}

Queue *ready;   // queue pentru threadurile gata de rulare
Queue *waiting; // queue pentru threadurile in asteptare

int so_init(unsigned int time_quantum, unsigned int io)
{
    // verific conditiile
    if (scheduler.exists == 1)
        return -1;
    if (io > SO_MAX_NUM_EVENTS)
        return -1;
    if (time_quantum == 0)
        return -1;
    // initializez queueurile
    ready = init_queue();
    waiting = init_queue();
    // initializez vectorul de threaduri
    scheduler.threads = malloc(50 * sizeof(Thread *));
    scheduler.threads_capacity = 50;
    scheduler.threads_no = 0;
    // initializez schedulerul
    scheduler.events = io;
    scheduler.time_quantum = time_quantum;
    scheduler.running = NULL;
    scheduler.exists = 1;
    return 0;
}

static void *start_thread(void *args) // functie auxiliara pentru pornire thread
{
    Thread *thread = (Thread *)args;
    sem_wait(&thread->semaphore);
    thread->function(thread->priority); // execut functia
    thread->status = Terminated;        // threadul a terminat
    so_exec();                          // continui executia
}

tid_t so_fork(so_handler *func, unsigned int priority)
{
    // verific conditiile
    if (priority > SO_MAX_PRIO)
        return INVALID_TID;
    if (func == NULL)
        return INVALID_TID;

    // initializez threadul
    Thread *thread = malloc(sizeof(Thread));
    thread->function = func;
    thread->priority = priority;
    thread->waiting_signal = -1;
    thread->status = New;
    thread->running_time = scheduler.time_quantum;

    // creez semaforul pentru thread
    sem_init(&thread->semaphore, 0, 0);
    pthread_create(&thread->id, NULL, start_thread, thread); // creez threadul in sine

    // verific daca trb realocat vectorul de threaduri
    if (scheduler.threads_capacity <= scheduler.threads_no)
    {
        scheduler.threads = realloc(scheduler.threads, scheduler.threads_capacity * 2 * sizeof(Thread *));
        scheduler.threads_capacity = scheduler.threads_capacity * 2;
    }
    // adaug threadul in vector
    scheduler.threads[scheduler.threads_no] = thread;
    scheduler.threads_no++;

    if (scheduler.running == NULL) // daca nu ruleaza nimic atunci pun threadul nou creat
    {
        thread->status = Running;
        scheduler.running = thread;
        sem_post(&thread->semaphore);
    }
    else
    {
        // daca threadul nou are prioritate mai mare decat cel care ruleaza atunci ii ia locul
        if (thread->priority > scheduler.running->priority)
        {
            Thread *current = scheduler.running;
            current->status = Ready;
            current->running_time = scheduler.time_quantum;
            enqueue(ready, current);
            thread->status = Running;
            scheduler.running = thread;
            sem_post(&thread->semaphore);
            sem_wait(&current->semaphore);
        }
        else
        {
            // altfel, il bag in queue de ready si execut mai departe
            thread->status = Ready;
            enqueue(ready, thread);
            so_exec();
        }
    }
    return thread->id;
}

void so_exec()
{
    if (!scheduler.running)
        return;

    Thread *current, *next;
    current = scheduler.running;
    current->running_time--; // scad timpul de rulare pentru threadul care ruleaza

    if (current->status == Terminated && ready->len != 0)
    {
        // daca threadul a terminat si mai sunt altele in asteptare scot pe urmatorul si il pornesc
        next = dequeue(ready);
        next->status = Running;
        next->running_time = scheduler.time_quantum;
        scheduler.running = next;
        sem_post(&next->semaphore);
        return;
    }

    if (ready->len == 0 && current->running_time == 0)
    {
        // daca nu mai e alt thread in asteptare il pun tot pe asta
        current->running_time = scheduler.time_quantum;
        return;
    }
    if (ready->len != 0 && current->running_time == 0)
    {
        if (ready->first->thread->priority >= current->priority)
        {
            // daca a expirat timpul si urmatorul are prioritate mai mare sau egala il pun pe ala in executie
            next = dequeue(ready);
            current->status = Ready;
            enqueue(ready, current);
            next->running_time = scheduler.time_quantum;
            scheduler.running = next;
            next->status = Running;
            sem_post(&next->semaphore);
            sem_wait(&current->semaphore);
            return;
        }
    }
    if (ready->len != 0 && current->running_time != 0)
    {
        if (ready->first->thread->priority > current->priority)
        {
            // daca nu a expirat timpul dar exista altul cu prioritate mai mare il pun pe ala
            next = dequeue(ready);
            current->status = Ready;
            current->running_time = scheduler.time_quantum;
            enqueue(ready, current);
            next->running_time = scheduler.time_quantum;
            scheduler.running = next;
            next->status = Running;
            sem_post(&next->semaphore);
            sem_wait(&current->semaphore);
            return;
        }
    }
}

int so_wait(unsigned int io)
{
    // verific conditia
    if (io >= scheduler.events)
        return -1;

    Thread *current = scheduler.running;
    // actualizez threadul si il pun in queueul de waiting
    current->waiting_signal = io;
    current->status = Waiting;
    waiting_enqueue(waiting, current);
    scheduler.running = dequeue(ready);

    if (scheduler.running)
    {
        // daca mai era altul in ready il pornesc
        scheduler.running->running_time = scheduler.time_quantum;
        scheduler.running->status = Running;
        sem_post(&scheduler.running->semaphore);
    }
    // il opresc pe cel pus pe wait
    sem_wait(&current->semaphore);
    return 0;
}

int so_signal(unsigned int io)
{
    // verific conditiile
    if (io >= scheduler.events)
        return -1;

    int count = 0;
    Thread *thread;

    int length = waiting->len;
    // scot din waiting, verific daca asteapta semnalul dorit, daca da il deblochez
    for (int i = 0; i < length; i++)
    {
        thread = dequeue(waiting);
        if (thread->waiting_signal == io)
        {
            count++;
            thread->waiting_signal = -1;
            enqueue(ready, thread);
        }
        else
            waiting_enqueue(waiting, thread); // il bag inapoi daca nu
    }
    so_exec(); // execut mai departe
    return count;
}

void thread_destroy(Thread *thread) // functie pentru a distruge un thread
{
    sem_destroy(&thread->semaphore);
    pthread_join(thread->id, NULL);
    free(thread);
}

void so_end()
{
    if (scheduler.exists == 1) // daca este initializat un scheduler il distrug
    {
        // distrug fiecare thread
        for (int i = 0; i < scheduler.threads_no; i++)
            thread_destroy(scheduler.threads[i]);
        // eliberez restul de memorie
        free(scheduler.threads);
        scheduler.exists = 0;
        free(ready);
        free(waiting);
    }
}