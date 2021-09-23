#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>

#define ERROR_AT_THREAD -1
#define THREAD_SUCCESS 0

//#define DEBUG

/*-------------------------- Atomicity Variables Declarations --------------------------*/

pthread_mutex_t queue_lock;
pthread_mutex_t mutex;
pthread_mutex_t mutex2;
pthread_cond_t wait_for_start_cv;
pthread_cond_t queue_not_empty_cv;
pthread_cond_t done_cv; /* a conditional variable which tells the main thread if we have done*/

atomic_int workers = 0; /* tells us how many threads are currently working */
atomic_int files_founded = 0;

/*--- The template (not atomic) ---*/
/* Should be sent to the threads as an argument, but I wanted to debug and pass to every thread its id; */

char *template; 

/*----------------------------- Queue Implementation -----------------------------*/

struct node_t{
    struct node_t *next;
    struct node_t *prev;
    char *dir_name; /* node's content */
    unsigned int curr_empty_char_index;
};

typedef struct node_t node;

typedef struct{
    node *tail;
    node *head;
}queue;

void append_string(node *a, char* content){
    //fprintf(stderr,"At %s\n", __func__);
    int cl = strlen(content);
    for (int i=0; i < cl; i++){
        (a->dir_name)[(a->curr_empty_char_index) + i] = content[i];
    }
    a->curr_empty_char_index += cl;
    (a->dir_name)[a->curr_empty_char_index] = '\0';
}

node *create_node(char *content){
    //fprintf(stderr,"At %s\n", __func__);
    node *result = malloc(sizeof(node));
    if (result == NULL){
        printf("Error: Failed when trying to allocate memory.\n");
        exit(1);
    }
    result -> next = NULL;
    result -> prev = NULL;
    result -> dir_name = malloc((PATH_MAX+1) * sizeof(char));
    result -> curr_empty_char_index = 0;
    append_string(result, content);

    return result;
}

void insert_last(queue *q, char *content){
    //fprintf(stderr,"At %s\n", __func__);
    pthread_mutex_lock(&queue_lock);
    node *new_node = create_node(content);

    if ((q -> tail) == NULL){
        q -> tail = new_node;
        q -> head = new_node;
    }
    else{
        node *tail = q -> tail;
        tail -> prev = new_node;
        new_node -> next = tail;
        q -> tail = new_node;
    }
    pthread_mutex_unlock(&queue_lock);

    pthread_cond_broadcast(&queue_not_empty_cv);
}

int is_empty(queue *q){
    //fprintf(stderr,"At %s\n", __func__);
    int res = ((q -> head) == NULL);
    return res;
}

char *pop(queue *q){
    //fprintf(stderr,"At %s\n", __func__);
    pthread_mutex_lock(&queue_lock);
    while (is_empty(q) == 1){
        pthread_cond_wait(&queue_not_empty_cv, &queue_lock);
    }

    if ((q -> head) == NULL){
        fprintf(stderr, "Tried to pop an empty queue.");
    }

    q->head->dir_name[q->head->curr_empty_char_index] = '\0';
    
    char *head_dir_name = (q -> head -> dir_name);

    if ((q -> head) == (q -> tail)){
        q -> head = NULL;
        q -> tail = NULL;
    }
    else{
        q -> head = q -> head -> prev;
    }

    char *res = head_dir_name; /* returns the previous head of the queue content */
    pthread_mutex_unlock(&queue_lock);
    return res;
}

queue *create_queue(){
    //fprintf(stderr,"At %s\n", __func__);
    queue *q = malloc(sizeof(queue));
    if (q == NULL){
        printf("Error: Failed when trying to allocate memory.\n");
        exit(1);
    }
    q -> head = NULL;
    q -> tail = NULL;

    return q;
}

/* not neccasary */
void queue_free(queue *q){
    //fprintf(stderr,"At %s\n", __func__);
    node *curr, *next;
    curr = q -> tail;
    while (curr != NULL){ /*(and not != tail) because we want to free all*/
        next = (curr -> next);
        free(curr);
        curr = next;
    }

    free(q);
}

void print_queue(queue *q){
    //fprintf(stderr,"At %s\n", __func__);
    pthread_mutex_lock(&queue_lock);
    node *curr = q -> tail;
    while (curr != (q -> head)){
        (curr -> dir_name)[curr->curr_empty_char_index] = '\0';
        fprintf(stderr, "%s ----> ", curr->dir_name);
        curr = curr -> next;
    }
    if (curr != NULL){
        (curr -> dir_name)[curr->curr_empty_char_index] = '\0';
        fprintf(stderr, "%s ----> \n", curr->dir_name);
        curr = curr -> next;
    }

    pthread_mutex_unlock(&queue_lock);
}

/* --- */
queue *dirs_queue;

/*--------------------------- Searcher Implementation ---------------------------*/

int is_searchable(char *dir_name);

void *search(void *a);

/*------------- main -------------*/
int main(int argc ,char **argv){
    //fprintf(stderr,"At %s\n", __func__);
    if (argc != 4){
        fprintf(stderr, "Error: Invalid number of arguments were passed.\n");
        exit(1);
    }

    char *root_name = argv[1];
    char *temp = argv[2]; /* searched template; */
    template = malloc(sizeof(char) * (PATH_MAX + 1));
    sprintf(template, "%s", temp);
    int n_threads = atoi(argv[3]);

    /* checking if root folder is searchable */
    if (!is_searchable(root_name)){
        fprintf(stderr, "Error: Directory is not searchable.\n");
        exit(1);
    }

    /*---------------- Starting searhcing ----------------*/
    dirs_queue = create_queue();

    pthread_t *thread_ids = malloc(sizeof(pthread_t) * n_threads);

    /*--------- Creating the threads--------- */

    /* Initializing mutex and condition variable objects */
    pthread_mutex_init(&queue_lock, NULL);
    pthread_cond_init(&wait_for_start_cv, NULL);
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&queue_not_empty_cv, NULL);
    pthread_mutex_init(&mutex2, NULL);
    pthread_cond_init(&done_cv, NULL);

    /* making the threads joinable. not needed (at first I thouht it was) */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for (int i=0; i < n_threads; i++){
        int *arg = malloc(sizeof(int));
        *arg = i;
        int rc = pthread_create(&thread_ids[i], &attr, search, (void *) arg);
        if (rc) {
            fprintf(stderr, "Failed creating thread: %s\n", strerror(rc));
            exit(EXIT_FAILURE);
        }
    }
    sleep(1); /* giving threads time to lock or wait for a CV*/
    #ifdef DEBUG
    fprintf(stderr, "--- Starting the threads ---\n");
    #endif
    pthread_cond_broadcast(&wait_for_start_cv);

    sleep(1); /* giving threads time to lock the queue_lock or wait for him through a CV */
    insert_last(dirs_queue, root_name); /* also conditional broadcasts the cv "queue_not_empty_cv" */

    /* --- Waiting for the threads --- */
    
    int queue_empty = 0;

    pthread_mutex_lock(&mutex2);
    while ((workers > 0) || (!queue_empty)){
        pthread_cond_wait(&done_cv, &mutex2);
        pthread_mutex_lock(&queue_lock);
        queue_empty = is_empty(dirs_queue);
        pthread_mutex_unlock(&queue_lock);
    }
    pthread_mutex_unlock(&mutex2);

    /* We get here if and only if all of the threads were terminated */

    int exit_code = 0;

    for (int i=0; i < n_threads; i++){
        pthread_cancel(thread_ids[i]);

        if (errno == ESRCH){ /* Thread was allready canceled and the only reason is an error. */
            exit_code = 1;
        }
        #ifdef DEBUG
        fprintf(stderr, "Thread: %ld, was terminated\n", thread_ids[i]);
        #endif
    }

    /* --- Exiting (and freeing up memory although not necessary --- */

    printf("Done searching, found %d files\n", files_founded);

    queue_free(dirs_queue);
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&queue_lock);
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&mutex2);
    pthread_cond_destroy(&wait_for_start_cv);
    pthread_cond_destroy(&queue_not_empty_cv);
    pthread_cond_destroy(&done_cv);

    exit(exit_code);
}


int is_searchable(char *dir_name){
    //fprintf(stderr,"At %s\n", __func__);
    /* method taken from: https://stackoverflow.com/questions/14863203/linux-check-if-process-has-read-access-to-file-in-c-c*/

    struct stat buf;
    int readable = 0;
    int executable = 0;

    // Get UID and GID of the process.
    char *path_to_proc = malloc(sizeof(char) * 1025);
    sprintf(path_to_proc, "/proc/%d", getpid());

    stat(path_to_proc, &buf);
    uid_t proc_uid = buf.st_uid;
    gid_t proc_gid = buf.st_gid;

    // Get UID and GID of the file.
    stat(dir_name, &buf);

    // If the process owns the file, check if it has read access.
    if (proc_uid == buf.st_uid && buf.st_mode & S_IRUSR) {
        readable = 1;
    }

    // Check if the group of the process's UID matches the file's group
    // and if so, check for read/write access.
    else if (proc_gid == buf.st_gid && buf.st_mode & S_IRGRP) {
        readable = 1;
    }

    // The process's UID is neither the owner of the file nor does its GID
    // match the file's.  Check whether the file is world readable.
    else if (buf.st_mode & S_IROTH) {
        readable =  1;
    }

    // If the process owns the file, check if it has read access.
    if (proc_uid == buf.st_uid && buf.st_mode & S_IXUSR) {
        executable = 1;
    }

    // Check if the group of the process's UID matches the file's group
    // and if so, check for read/write access.
    else if (proc_gid == buf.st_gid && buf.st_mode & S_IXGRP) {
        executable = 1;
    }

    // The process's UID is neither the owner of the file nor does its GID
    // match the file's.  Check whether the file is world readable.
    else if (buf.st_mode & S_IXOTH) {
        executable =  1;
    }

    return ((readable) && (executable));
}

int includes_pattern(char *string, const char *pattern){
    //fprintf(stderr,"At %s\n", __func__);
    int pattern_length = strlen(pattern);
    int str_length = strlen(string);
    char saved;

    if (strlen(string) < strlen(pattern)){
        return 0;
    }

    if (strcmp(string, pattern) == 0){
        return 1;
    }

    for (int i=0; i < str_length - pattern_length; i++){
        saved = string[i + pattern_length];
        string[i + pattern_length] = '\0';
        int equals = strcmp(string+i, pattern);
        string[i + pattern_length] = saved;

        if (equals == 0){
            return 1;
        }
    }

    if (strcmp(string+str_length-pattern_length, pattern) == 0){
        return 1;
    }

    /* else */
    return 0;
}

/*---------- searcher ----------*/

void *search(void *t){
    //fprintf(stderr,"At %s\n", __func__);
    /* struct args_t *args = (struct args_t *) a; 
    int pthread_num = args -> pthread_num;
    char *template = args -> template; */

    #ifdef DEBUG
    int pthread_num = *((int  *) t);
    #endif

    int *retval = malloc(sizeof(int));

    pthread_mutex_lock(&mutex);
    #ifdef DEBUG
    fprintf(stderr, "Thread: %d waiting\n", (int) pthread_self());
    #endif
    pthread_cond_wait(&wait_for_start_cv, &mutex);
    pthread_mutex_unlock(&mutex);

    #ifdef DEBUG
    fprintf(stderr, "Thread: %d launched\n", (int) pthread_self());
    #endif

    while (1) {
        /* Thread gets the directory he has to work on, after waiting for queue for being non-empty */
        char *dir_name = pop(dirs_queue);

        /* Starting to work */
        workers ++; /* an atomic_int; does not need to be protected by a lock. */

        DIR *stream = opendir(dir_name); /* A directory from the queue is searchable */
        #ifdef DEBUG
        fprintf(stderr, "Thread: %d Opened: %s\n", (int) (pthread_self()), dir_name);
        #endif

        if (stream == NULL){
            fprintf(stderr, "Error when trying to open a directory: %s\n", strerror(errno));
            *retval = ERROR_AT_THREAD;
            pthread_exit(retval);
        }

        struct dirent *dir_entry;
        
        while((dir_entry = readdir(stream)) != NULL){
            #ifdef DEBUG
            //print_queue(dirs_queue);
            #endif
            char *entry_name = dir_entry -> d_name;
            char *full_entry_name = malloc(sizeof(char)*(PATH_MAX+1));
            sprintf(full_entry_name, "%s/%s", dir_name, entry_name);
            #ifdef DEBUG
            fprintf(stderr, "Thread: %d, Unpacked: %s, From: %s\n", (int) pthread_self(), entry_name, dir_name);
            #endif
            struct stat *a = malloc(sizeof(struct stat));
            lstat(full_entry_name, a);

            int permissions = a->st_mode;

            if (!(S_ISDIR(permissions))){ /* file isn't a directory */
                //print the filename if needed (if the template is included in its name);
                if (includes_pattern(entry_name, template) == 1){
                    printf("%s\n", full_entry_name);
                    files_founded ++;  /* an atomic_int */
                }
                #ifdef DEBUG
                fprintf(stderr, "A file\n");
                #endif
            }
            else{ /* file is a directory */
                int trivial = (strcmp(entry_name, ".") == 0) || (strcmp(entry_name, "..") == 0);
                if (!trivial){
                    if (!is_searchable(full_entry_name)){
                        printf("Directory %s: Permission denied.\n", full_entry_name);
                        continue;
                    }
                    #ifdef DEBUG
                    fprintf(stderr, "Directory: Not trivail\n");
                    #endif
                    /* else: directory is searchable. add it to the queue */
                    insert_last(dirs_queue, full_entry_name);
                }
            }
        }

        closedir(stream);

        /* Thread has done its work for now */
        workers --; /* again an atomic_int which does not need to be protected by a lock. */
        pthread_cond_signal(&done_cv); /* notify main thread as all of the work may allready be done */
    }

    /* 
    Not getting here; There is a while loop up there, and we'll stop when the main thread will 
    understand that the work has been done. The decision is done according the number of workers
    and the queues content;
    */

    *retval = THREAD_SUCCESS;
    return retval;
}