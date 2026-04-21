#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#pragma comment(lib, "ws2_32.lib")

/* === patient.h === */
/*
 * patient.h - Patient data structure
 *
 * Stores patients in a static array.
 *   Add:          O(1) amortised
 *   Find by id:   O(n) linear search
 *   Find by name: O(n) linear search  [use BST for O(log n)]
 *   Delete:       O(n) array shift
 */

#ifndef PATIENT_H
#define PATIENT_H

#define MAX_PATIENTS 20
#define NAME_LEN     64

/* ---- Patient record ---- */
typedef struct {
    int  id;              /* Unique patient ID (auto-increment) */
    char name[NAME_LEN];  /* Patient full name                  */
    int  age;             /* Age in years                       */
} Patient;

/* ---- Global store (static array allocation) ---- */
extern Patient patients[MAX_PATIENTS];
extern int     patient_count;
extern int     next_patient_id;

/* ---- CRUD ---- */
int  patient_add(const char *name, int age);       /* returns new id, -1 if full */
int  patient_find_idx_by_id(int id);               /* O(n) - returns array index */
int  patient_find_idx_by_name(const char *name);   /* O(n) - returns array index */
void patient_delete_by_idx(int idx);               /* O(n) - shifts array left   */

/* ---- JSON serialisation ---- */
void patient_to_json(int idx, char *out, int max_len);
void patients_to_json(char *out, int max_len);

#endif /* PATIENT_H */


/* === reading_list.h === */
/*
 * reading_list.h - Singly Linked List of health readings per patient
 *
 * Newest reading is always at the HEAD.
 *   Insert at head: O(1) - uses malloc()
 *   Remove head:    O(1) - for undo, uses free()
 *   Traverse:       O(n)
 *   Delete all:     O(n) - frees every node
 */

#ifndef READING_LIST_H
#define READING_LIST_H

/* ---- Linked list node ---- */
typedef struct ReadingNode {
    int                 hr;         /* Heart rate in BPM  */
    int                 spo2;       /* Blood oxygen %     */
    long long           timestamp;  /* Unix epoch seconds */
    struct ReadingNode *next;       /* Pointer to older reading */
} ReadingNode;

/* One list head per patient (indexed by patient array index) */
#define MAX_RL_PATIENTS 20
extern ReadingNode *reading_heads[MAX_RL_PATIENTS];

/* ---- Operations ---- */
void reading_insert(int patient_idx, int hr, int spo2, long long ts);  /* O(1) */
void reading_remove_head(int patient_idx);                              /* O(1) */
void reading_delete_all(int patient_idx);                              /* O(n) */
int  reading_count(int patient_idx);                                   /* O(n) */

/* Serialise to JSON array. max_count=-1 for all. Newest first. */
void readings_to_json(int patient_idx, int max_count,
                      char *out, int max_len);                         /* O(n) */

#endif /* READING_LIST_H */


/* === sensor_buffer.h === */
/*
 * sensor_buffer.h - Circular Queue (Ring Buffer) for live sensor data
 *
 * Stores the last RING_SIZE readings from a sensor in a fixed-size
 * circular queue.  When full, the oldest reading is automatically
 * overwritten (O(1) for both enqueue and peek).
 *
 *   Enqueue: O(1)
 *   Peek:    O(1)
 *   Full traversal: O(RING_SIZE)
 */

#ifndef SENSOR_BUFFER_H
#define SENSOR_BUFFER_H

#define RING_SIZE          50   /* Max readings held per patient */
#define MAX_RING_PATIENTS  20

/* ---- Circular queue struct ---- */
typedef struct {
    int       hr[RING_SIZE];          /* Heart rate values          */
    int       spo2[RING_SIZE];        /* SpO2 values                */
    long long timestamps[RING_SIZE];  /* Corresponding timestamps   */
    int       head;    /* Index of the oldest element               */
    int       tail;    /* Next write position                        */
    int       count;   /* Number of elements currently stored        */
    int       patient_id; /* Patient this buffer belongs to          */
} SensorRingBuffer;

/* One ring buffer per patient (indexed by patient array index) */
extern SensorRingBuffer ring_buffers[MAX_RING_PATIENTS];

/* ---- Operations ---- */
void ring_init(int patient_idx, int patient_id);                         /* O(1) */
void ring_enqueue(int patient_idx, int hr, int spo2, long long ts);      /* O(1) */
void ring_peek_latest(int patient_idx, char *json_out, int max_len);     /* O(1) */
void ring_peek_last_n(int patient_idx, int n,
                      char *json_out, int max_len);                      /* O(n) */

/* Build JSON of latest reading for every patient (for /api/live) */
void live_all_to_json(char *out, int max_len);

#endif /* SENSOR_BUFFER_H */


/* === patient_bst.h === */
/*
 * patient_bst.h - Binary Search Tree for fast patient lookup by name
 *
 * Each node stores a patient_id and name.  Ordered lexicographically
 * by name, so inorder traversal returns patients in alphabetical order.
 *
 *   Insert: O(log n) average, O(n) worst (unbalanced)
 *   Search: O(log n) average
 *   Delete: O(log n) average
 *   Inorder traversal: O(n)
 */

#ifndef PATIENT_BST_H
#define PATIENT_BST_H

/* ---- BST node (dynamically allocated with malloc) ---- */
typedef struct BSTNode {
    int             patient_id;  /* Links back to the patient array */
    char            name[64];    /* Key used for comparison          */
    struct BSTNode *left;        /* Left child  (name < this)        */
    struct BSTNode *right;       /* Right child (name > this)        */
} BSTNode;

/* Global BST root */
extern BSTNode *bst_root;

/* ---- Operations ---- */
BSTNode* bst_insert(BSTNode *root, int patient_id, const char *name);  /* O(log n) */
BSTNode* bst_search(BSTNode *root, const char *name);                  /* O(log n) */
BSTNode* bst_find_min(BSTNode *root);                                  /* O(log n) */
BSTNode* bst_delete(BSTNode *root, const char *name);                  /* O(log n) */

/* Inorder traversal: writes sorted patient list into out[] */
void bst_inorder_json(BSTNode *root, char *out, int *pos, int max_len); /* O(n) */

/* Post-order free: releases all BST nodes */
void bst_free_all(BSTNode *root);                                       /* O(n) */

#endif /* PATIENT_BST_H */


/* === undo_stack.h === */
/*
 * undo_stack.h - Array-based Stack for undoing reading insertions
 *
 * When a reading is added, it is pushed here.  On undo, we pop the
 * most recent reading and also remove the head of the linked list.
 *
 *   Push: O(1)
 *   Pop:  O(1)
 *   Peek: O(1)
 */

#ifndef UNDO_STACK_H
#define UNDO_STACK_H

#define UNDO_STACK_CAPACITY 30  /* Max undo history per patient */
#define MAX_UNDO_PATIENTS   20

/* ---- Stack struct (fixed-size array) ---- */
typedef struct {
    int       hr[UNDO_STACK_CAPACITY];          /* Stored HR values    */
    int       spo2[UNDO_STACK_CAPACITY];        /* Stored SpO2 values  */
    long long timestamps[UNDO_STACK_CAPACITY];  /* Stored timestamps   */
    int       top;   /* Index of top element; -1 when empty            */
} UndoStack;

/* One stack per patient (indexed by patient array index) */
extern UndoStack undo_stacks[MAX_UNDO_PATIENTS];

/* ---- Operations ---- */
void undo_init(int patient_idx);                                         /* O(1) */
void undo_push(int patient_idx, int hr, int spo2, long long ts);         /* O(1) */
int  undo_pop(int patient_idx, int *hr, int *spo2, long long *ts);       /* O(1) returns 1 ok, 0 empty */
int  undo_is_empty(int patient_idx);                                     /* O(1) */

#endif /* UNDO_STACK_H */


/* === sorting.h === */
/*
 * sorting.h - Bubble Sort on health readings
 *
 * Converts the linked list to a flat heap-allocated array, then
 * sorts it in-place.  Caller must free() the returned array.
 *
 *   Convert to flat array: O(n)
 *   Bubble sort:           O(n²)  [required by DSP syllabus]
 *   Best case (sorted):    O(n)   with early-exit optimisation
 *   Serialise to JSON:     O(n)
 */

#ifndef SORTING_H
#define SORTING_H



/* ---- Flat struct for sorting (copied from linked list nodes) ---- */
typedef struct {
    int       hr;
    int       spo2;
    long long timestamp;
} ReadingFlat;

/* Convert linked list to a malloc'd flat array. *out_count = length.
 * Returns NULL if no readings.  Caller must free() the result. */
ReadingFlat* readings_to_flat_array(int patient_idx, int *out_count);  /* O(n) */

/* In-place bubble sort (ascending). O(n²) */
void bubble_sort_by_hr(ReadingFlat *arr, int n);    /* sort by heart rate  */
void bubble_sort_by_spo2(ReadingFlat *arr, int n);  /* sort by blood oxygen */

/* Serialise sorted flat array to JSON array string. O(n) */
void flat_array_to_json(ReadingFlat *arr, int n, char *out, int max_len);

#endif /* SORTING_H */


/* === file_io.h === */
/*
 * file_io.h - Binary file persistence for patient and reading data
 *
 * Data is saved to binary files after every write operation so that
 * the server can resume from the last known state after a restart.
 *
 * File format:
 *   patients.bin  : [int count] [int next_id] [Patient structs...]
 *   readings.bin  : [int total] [records: pid,hr,spo2,timestamp ...]
 *                   Records are stored oldest-first so that loading
 *                   with head-inserts reconstructs the correct list.
 */

#ifndef FILE_IO_H
#define FILE_IO_H

#define PATIENTS_FILE "data/patients.bin"
#define READINGS_FILE "data/readings.bin"

void save_all_data(void);  /* Save patients + readings to binary files */
void load_all_data(void);  /* Load on startup; rebuilds BST & ring buffers */

#endif /* FILE_IO_H */


/* === http_utils.h === */
/*
 * http_utils.h - HTTP request parser and response helpers
 *
 * Provides a minimal HTTP/1.1 parser for the C server and helper
 * functions to send JSON API responses or static web files.
 */

#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H




/* ---- Parsed HTTP request ---- */
typedef struct {
    char method[8];    /* "GET", "POST", "DELETE"                  */
    char path[512];    /* "/api/patients/1/readings"               */
    char query[256];   /* "sort=hr" (part after '?' in URL)        */
    char body[8192];   /* Raw request body (JSON from POST)        */
    int  body_len;
} HttpRequest;

/* ---- Request parsing ---- */
void parse_request(const char *raw, HttpRequest *req);

/* ---- Response helpers ---- */
void send_json_response(SOCKET sock, int status, const char *json);
void send_static_file(SOCKET sock, const char *filepath);
void send_404(SOCKET sock);
void send_ok_empty(SOCKET sock);
void send_error(SOCKET sock, int code, const char *msg_json);

/* ---- JSON value extraction (simple string-based) ---- */
int json_get_str(const char *json, const char *key,
                 char *out, int max_len);   /* returns 1 ok, 0 missing */
int json_get_int(const char *json, const char *key, int *out);

/* ---- URL helpers ---- */
void url_decode(char *dst, const char *src, int max_len);

#endif /* HTTP_UTILS_H */


/* === patient.c === */
/*
 * patient.c - Patient array implementation
 *
 * Static array of Patient structs.  Provides O(1) add, O(n) search,
 * and O(n) delete (shifts array to fill the gap).
 * JSON helpers serialise patient records for HTTP responses.
 */






/* ---- Global definitions ---- */
Patient patients[MAX_PATIENTS];
int     patient_count    = 0;
int     next_patient_id  = 1;

/* ------------------------------------------------------------------
 * patient_add
 * Add a patient at the next free array slot.
 * Returns the new patient id, or -1 if the array is full.
 * Time: O(1)
 * ------------------------------------------------------------------ */
int patient_add(const char *name, int age)
{
    if (patient_count >= MAX_PATIENTS) return -1;

    int idx = patient_count++;
    patients[idx].id  = next_patient_id++;
    patients[idx].age = age;
    strncpy(patients[idx].name, name, NAME_LEN - 1);
    patients[idx].name[NAME_LEN - 1] = '\0';

    return patients[idx].id;
}

/* ------------------------------------------------------------------
 * patient_find_idx_by_id
 * Linear search through the patient array.
 * Returns array index, or -1 if not found.
 * Time: O(n)
 * ------------------------------------------------------------------ */
int patient_find_idx_by_id(int id)
{
    int i;
    for (i = 0; i < patient_count; i++)
        if (patients[i].id == id) return i;
    return -1;
}

/* ------------------------------------------------------------------
 * patient_find_idx_by_name
 * Case-sensitive linear search by name.
 * Returns array index, or -1 if not found.
 * Time: O(n)
 * ------------------------------------------------------------------ */
int patient_find_idx_by_name(const char *name)
{
    int i;
    for (i = 0; i < patient_count; i++)
        if (strcmp(patients[i].name, name) == 0) return i;
    return -1;
}

/* ------------------------------------------------------------------
 * patient_delete_by_idx
 * Remove the patient at position idx by shifting the rest left.
 * Time: O(n)
 * ------------------------------------------------------------------ */
void patient_delete_by_idx(int idx)
{
    int i;
    if (idx < 0 || idx >= patient_count) return;
    for (i = idx; i < patient_count - 1; i++)
        patients[i] = patients[i + 1];
    patient_count--;
}

/* ------------------------------------------------------------------
 * patient_to_json / patients_to_json
 * Serialise one or all patients to a JSON string.
 * ------------------------------------------------------------------ */
void patient_to_json(int idx, char *out, int max_len)
{
    int cnt = reading_count(idx);
    snprintf(out, max_len,
        "{\"id\":%d,\"name\":\"%s\",\"age\":%d,\"reading_count\":%d}",
        patients[idx].id, patients[idx].name, patients[idx].age, cnt);
}

void patients_to_json(char *out, int max_len)
{
    int i, pos = 0;
    pos += snprintf(out + pos, max_len - pos, "[");
    for (i = 0; i < patient_count; i++) {
        char tmp[256];
        patient_to_json(i, tmp, sizeof(tmp));
        pos += snprintf(out + pos, max_len - pos, "%s%s",
                        tmp, (i < patient_count - 1) ? "," : "");
    }
    snprintf(out + pos, max_len - pos, "]");
}


/* === reading_list.c === */
/*
 * reading_list.c - Singly linked list of health readings
 *
 * Each patient has its own linked list.  Insertions are at the HEAD
 * so that the newest reading is always O(1) to access.  The undo
 * operation therefore just removes the head node.
 *
 * Dynamic memory: every node is malloc'd on insert and free'd on
 * remove or delete_all.  This satisfies the course requirement to
 * demonstrate malloc/free and pointer arithmetic.
 */






/* ---- Global array of list heads ---- */
ReadingNode *reading_heads[MAX_RL_PATIENTS];   /* all NULL by default (BSS) */

/* ------------------------------------------------------------------
 * reading_insert
 * Allocate a new node with malloc and insert it at the head.
 * The new node's next pointer = old head (pointer arithmetic).
 * Time: O(1)   |   Space: O(1) dynamic allocation
 * ------------------------------------------------------------------ */
void reading_insert(int patient_idx, int hr, int spo2, long long ts)
{
    ReadingNode *node = (ReadingNode *)malloc(sizeof(ReadingNode));
    if (!node) {
        fprintf(stderr, "[reading_list] malloc failed\n");
        return;
    }
    node->hr        = hr;
    node->spo2      = spo2;
    node->timestamp = ts;
    node->next      = reading_heads[patient_idx];  /* link to old head */
    reading_heads[patient_idx] = node;             /* new head */
}

/* ------------------------------------------------------------------
 * reading_remove_head
 * Remove and free the head node (most recent reading).
 * Used by the undo operation.
 * Time: O(1)
 * ------------------------------------------------------------------ */
void reading_remove_head(int patient_idx)
{
    ReadingNode *old = reading_heads[patient_idx];
    if (!old) return;
    reading_heads[patient_idx] = old->next;
    free(old);
}

/* ------------------------------------------------------------------
 * reading_delete_all
 * Traverse the list and free every node.
 * Time: O(n)
 * ------------------------------------------------------------------ */
void reading_delete_all(int patient_idx)
{
    ReadingNode *cur = reading_heads[patient_idx];
    while (cur) {
        ReadingNode *nxt = cur->next;
        free(cur);
        cur = nxt;
    }
    reading_heads[patient_idx] = NULL;
}

/* ------------------------------------------------------------------
 * reading_count
 * Count nodes by traversing the list.
 * Time: O(n)
 * ------------------------------------------------------------------ */
int reading_count(int patient_idx)
{
    int n = 0;
    ReadingNode *cur = reading_heads[patient_idx];
    while (cur) { n++; cur = cur->next; }
    return n;
}

/* ------------------------------------------------------------------
 * readings_to_json
 * Serialise up to max_count nodes to a JSON array.
 * Passes newest-first (head of list first).
 * max_count = -1 means no limit.
 * Time: O(n)
 * ------------------------------------------------------------------ */
void readings_to_json(int patient_idx, int max_count,
                      char *out, int max_len)
{
    int pos = 0, cnt = 0;
    pos += snprintf(out + pos, max_len - pos, "[");
    ReadingNode *cur = reading_heads[patient_idx];
    while (cur && (max_count < 0 || cnt < max_count)) {
        pos += snprintf(out + pos, max_len - pos,
            "%s{\"hr\":%d,\"spo2\":%d,\"timestamp\":%I64d}",
            (cnt > 0) ? "," : "",
            cur->hr, cur->spo2, cur->timestamp);
        cur = cur->next;
        cnt++;
    }
    snprintf(out + pos, max_len - pos, "]");
}


/* === sensor_buffer.c === */
/*
 * sensor_buffer.c - Circular queue (ring buffer) implementation
 *
 * The ring buffer maintains the last RING_SIZE readings for a patient.
 * When the buffer is full, the oldest entry is automatically discarded
 * as the tail advances (wrap-around using modular arithmetic).
 *
 * This is efficient for live sensor feeds: we always have the latest
 * window of readings available in O(1) without growing memory.
 */






/* ---- Global array of ring buffers ---- */
SensorRingBuffer ring_buffers[MAX_RING_PATIENTS];

/* ------------------------------------------------------------------
 * ring_init
 * Zero-initialise a ring buffer for a patient.
 * Time: O(1)
 * ------------------------------------------------------------------ */
void ring_init(int patient_idx, int patient_id)
{
    memset(&ring_buffers[patient_idx], 0, sizeof(SensorRingBuffer));
    ring_buffers[patient_idx].patient_id = patient_id;
    /* head=0, tail=0, count=0 already set by memset */
}

/* ------------------------------------------------------------------
 * ring_enqueue
 * Write a reading to tail position and advance tail.
 * If buffer is full, also advance head (discard oldest).
 * Uses modular arithmetic: (index + 1) % RING_SIZE
 * Time: O(1)
 * ------------------------------------------------------------------ */
void ring_enqueue(int patient_idx, int hr, int spo2, long long ts)
{
    SensorRingBuffer *rb = &ring_buffers[patient_idx];

    rb->hr[rb->tail]         = hr;
    rb->spo2[rb->tail]       = spo2;
    rb->timestamps[rb->tail] = ts;
    rb->tail = (rb->tail + 1) % RING_SIZE;   /* wrap around */

    if (rb->count < RING_SIZE) {
        rb->count++;
    } else {
        /* Buffer full: discard oldest by moving head forward */
        rb->head = (rb->head + 1) % RING_SIZE;
    }
}

/* ------------------------------------------------------------------
 * ring_peek_latest
 * Return the most recently enqueued reading as JSON.
 * tail-1 (mod RING_SIZE) is the last written slot.
 * Time: O(1)
 * ------------------------------------------------------------------ */
void ring_peek_latest(int patient_idx, char *json_out, int max_len)
{
    SensorRingBuffer *rb = &ring_buffers[patient_idx];
    if (rb->count == 0) {
        snprintf(json_out, max_len,
                 "{\"hr\":0,\"spo2\":0,\"timestamp\":0}");
        return;
    }
    int last = (rb->tail - 1 + RING_SIZE) % RING_SIZE;
    snprintf(json_out, max_len,
        "{\"hr\":%d,\"spo2\":%d,\"timestamp\":%I64d}",
        rb->hr[last], rb->spo2[last], rb->timestamps[last]);
}

/* ------------------------------------------------------------------
 * ring_peek_last_n
 * Return the last n readings (newest first) as a JSON array.
 * Walks backwards from tail.
 * Time: O(n)
 * ------------------------------------------------------------------ */
void ring_peek_last_n(int patient_idx, int n,
                      char *json_out, int max_len)
{
    SensorRingBuffer *rb = &ring_buffers[patient_idx];
    int actual = (n < rb->count) ? n : rb->count;
    int pos = 0, i;
    pos += snprintf(json_out + pos, max_len - pos, "[");
    for (i = 0; i < actual; i++) {
        /* Walk backwards: tail-1-i (mod RING_SIZE) */
        int idx = (rb->tail - 1 - i + RING_SIZE * 100) % RING_SIZE;
        pos += snprintf(json_out + pos, max_len - pos,
            "%s{\"hr\":%d,\"spo2\":%d,\"timestamp\":%I64d}",
            (i > 0) ? "," : "",
            rb->hr[idx], rb->spo2[idx], rb->timestamps[idx]);
    }
    snprintf(json_out + pos, max_len - pos, "]");
}

/* ------------------------------------------------------------------
 * live_all_to_json
 * Build a JSON array with the latest reading for every patient.
 * Used by GET /api/live to power the dashboard.
 * Time: O(P) where P = patient_count
 * ------------------------------------------------------------------ */
void live_all_to_json(char *out, int max_len)
{
    int i, pos = 0;
    pos += snprintf(out + pos, max_len - pos, "[");
    for (i = 0; i < patient_count; i++) {
        SensorRingBuffer *rb = &ring_buffers[i];
        int hr = 0, spo2 = 0;
        long long ts = 0;
        if (rb->count > 0) {
            int last = (rb->tail - 1 + RING_SIZE) % RING_SIZE;
            hr   = rb->hr[last];
            spo2 = rb->spo2[last];
            ts   = rb->timestamps[last];
        }
        pos += snprintf(out + pos, max_len - pos,
            "%s{\"patient_id\":%d,\"name\":\"%s\","
            "\"hr\":%d,\"spo2\":%d,\"timestamp\":%I64d}",
            (i > 0) ? "," : "",
            patients[i].id, patients[i].name,
            hr, spo2, ts);
    }
    snprintf(out + pos, max_len - pos, "]");
}


/* === patient_bst.c === */
/*
 * patient_bst.c - Binary Search Tree keyed by patient name
 *
 * All nodes are heap-allocated with malloc.
 * Inorder traversal yields an alphabetically sorted patient list.
 *
 * Delete uses the inorder-successor method (2-child case).
 */






/* ---- Global BST root ---- */
BSTNode *bst_root = NULL;

/* ------------------------------------------------------------------
 * bst_insert
 * Insert a new node into the BST, ordered by name.
 * If the name already exists, update the patient_id.
 * Time: O(log n) average, O(n) worst (degenerate tree)
 * ------------------------------------------------------------------ */
BSTNode* bst_insert(BSTNode *root, int patient_id, const char *name)
{
    if (!root) {
        BSTNode *node = (BSTNode *)malloc(sizeof(BSTNode));
        if (!node) return NULL;
        node->patient_id = patient_id;
        strncpy(node->name, name, 63);
        node->name[63]   = '\0';
        node->left = node->right = NULL;
        return node;
    }
    int cmp = strcmp(name, root->name);
    if      (cmp < 0) root->left  = bst_insert(root->left,  patient_id, name);
    else if (cmp > 0) root->right = bst_insert(root->right, patient_id, name);
    else              root->patient_id = patient_id; /* update on duplicate */
    return root;
}

/* ------------------------------------------------------------------
 * bst_search
 * Find a node by name.  Returns pointer or NULL.
 * Time: O(log n) average
 * ------------------------------------------------------------------ */
BSTNode* bst_search(BSTNode *root, const char *name)
{
    if (!root) return NULL;
    int cmp = strcmp(name, root->name);
    if (cmp == 0) return root;
    if (cmp < 0)  return bst_search(root->left,  name);
    return              bst_search(root->right, name);
}

/* ------------------------------------------------------------------
 * bst_find_min
 * Return the leftmost (smallest) node in a subtree.
 * Used as the inorder successor in deletion.
 * Time: O(log n)
 * ------------------------------------------------------------------ */
BSTNode* bst_find_min(BSTNode *root)
{
    while (root->left) root = root->left;
    return root;
}

/* ------------------------------------------------------------------
 * bst_delete
 * Remove a node by name.  Three cases:
 *   1. No children  → free and return NULL
 *   2. One child    → free and return the child
 *   3. Two children → replace with inorder successor, delete successor
 * Time: O(log n) average
 * ------------------------------------------------------------------ */
BSTNode* bst_delete(BSTNode *root, const char *name)
{
    if (!root) return NULL;
    int cmp = strcmp(name, root->name);
    if (cmp < 0) {
        root->left  = bst_delete(root->left,  name);
    } else if (cmp > 0) {
        root->right = bst_delete(root->right, name);
    } else {
        /* Found the node to delete */
        if (!root->left) {
            BSTNode *tmp = root->right;
            free(root);
            return tmp;
        }
        if (!root->right) {
            BSTNode *tmp = root->left;
            free(root);
            return tmp;
        }
        /* Two children: find inorder successor (leftmost in right subtree) */
        BSTNode *succ = bst_find_min(root->right);
        root->patient_id = succ->patient_id;
        strncpy(root->name, succ->name, 63);
        root->right = bst_delete(root->right, succ->name);
    }
    return root;
}

/* ------------------------------------------------------------------
 * bst_inorder_json
 * Inorder traversal: left → visit → right.
 * Visits nodes in alphabetical name order.
 * Writes {"id":X,"name":"Y"}, entries to out[].
 * Caller strips the trailing comma.
 * Time: O(n)
 * ------------------------------------------------------------------ */
void bst_inorder_json(BSTNode *root, char *out, int *pos, int max_len)
{
    if (!root) return;
    bst_inorder_json(root->left, out, pos, max_len);
    *pos += snprintf(out + *pos, max_len - *pos,
        "{\"id\":%d,\"name\":\"%s\"},",
        root->patient_id, root->name);
    bst_inorder_json(root->right, out, pos, max_len);
}

/* ------------------------------------------------------------------
 * bst_free_all
 * Post-order traversal freeing every node.
 * Time: O(n)
 * ------------------------------------------------------------------ */
void bst_free_all(BSTNode *root)
{
    if (!root) return;
    bst_free_all(root->left);
    bst_free_all(root->right);
    free(root);
}


/* === undo_stack.c === */
/*
 * undo_stack.c - Array-based stack for undoing reading additions
 *
 * Each patient has its own fixed-size stack.  When a reading is added
 * via the API, it is also pushed here.  The undo endpoint pops this
 * stack and simultaneously removes the head of the linked list.
 *
 * If the stack is full (UNDO_STACK_CAPACITY readings), the oldest
 * element is discarded by shifting the array down (rare, bounded cost).
 */




/* ---- Global array of undo stacks ---- */
UndoStack undo_stacks[MAX_UNDO_PATIENTS];

/* ------------------------------------------------------------------
 * undo_init
 * Initialise the stack for a patient (set top = -1 = empty).
 * Time: O(1)
 * ------------------------------------------------------------------ */
void undo_init(int patient_idx)
{
    undo_stacks[patient_idx].top = -1;
}

/* ------------------------------------------------------------------
 * undo_push
 * Push a reading onto the stack.
 * If full, shift down to discard the oldest entry, then push.
 * Time: O(1) amortised
 * ------------------------------------------------------------------ */
void undo_push(int patient_idx, int hr, int spo2, long long ts)
{
    UndoStack *s = &undo_stacks[patient_idx];
    if (s->top >= UNDO_STACK_CAPACITY - 1) {
        /* Shift stack down by one to make room at top */
        int i;
        for (i = 0; i < UNDO_STACK_CAPACITY - 1; i++) {
            s->hr[i]         = s->hr[i + 1];
            s->spo2[i]       = s->spo2[i + 1];
            s->timestamps[i] = s->timestamps[i + 1];
        }
        s->top = UNDO_STACK_CAPACITY - 2;
    }
    s->top++;
    s->hr[s->top]         = hr;
    s->spo2[s->top]       = spo2;
    s->timestamps[s->top] = ts;
}

/* ------------------------------------------------------------------
 * undo_pop
 * Pop the top reading.  Returns 1 on success, 0 if stack is empty.
 * Time: O(1)
 * ------------------------------------------------------------------ */
int undo_pop(int patient_idx, int *hr, int *spo2, long long *ts)
{
    UndoStack *s = &undo_stacks[patient_idx];
    if (s->top < 0) return 0;   /* Empty */
    *hr   = s->hr[s->top];
    *spo2 = s->spo2[s->top];
    *ts   = s->timestamps[s->top];
    s->top--;
    return 1;
}

/* ------------------------------------------------------------------
 * undo_is_empty
 * Return 1 if the stack is empty, 0 otherwise.
 * Time: O(1)
 * ------------------------------------------------------------------ */
int undo_is_empty(int patient_idx)
{
    return undo_stacks[patient_idx].top < 0;
}


/* === sorting.c === */
/*
 * sorting.c - Bubble sort on health readings
 *
 * The linked list is first flattened into a heap-allocated flat array
 * (O(n) traversal + malloc), sorted in place with bubble sort (O(n²)),
 * then serialised to JSON.  The flat array must be free()'d by the caller.
 *
 * Bubble sort is chosen to satisfy the DSP syllabus requirement and to
 * demonstrate in-place comparison-based sorting with swaps.
 */






/* ------------------------------------------------------------------
 * readings_to_flat_array
 * Traverse the linked list and copy each node into a malloc'd array.
 * The array is newest-first (mirrors the list order).
 * *out_count receives the number of elements.
 * Returns NULL if there are no readings.
 * Caller MUST call free() on the returned pointer.
 * Time: O(n)   |   Space: O(n) dynamic allocation
 * ------------------------------------------------------------------ */
ReadingFlat* readings_to_flat_array(int patient_idx, int *out_count)
{
    int n = reading_count(patient_idx);
    *out_count = n;
    if (n == 0) return NULL;

    ReadingFlat *arr = (ReadingFlat *)malloc(n * sizeof(ReadingFlat));
    if (!arr) { *out_count = 0; return NULL; }

    ReadingNode *cur = reading_heads[patient_idx];
    int i = 0;
    while (cur && i < n) {
        arr[i].hr        = cur->hr;
        arr[i].spo2      = cur->spo2;
        arr[i].timestamp = cur->timestamp;
        cur = cur->next;
        i++;
    }
    return arr;
}

/* ------------------------------------------------------------------
 * bubble_sort_by_hr
 * In-place bubble sort ascending by heart rate.
 * Includes an early-exit optimisation: if no swap happens in a pass,
 * the array is already sorted → best case O(n).
 * Worst case: O(n²)
 * ------------------------------------------------------------------ */
void bubble_sort_by_hr(ReadingFlat *arr, int n)
{
    int i, j, swapped;
    for (i = 0; i < n - 1; i++) {
        swapped = 0;
        for (j = 0; j < n - i - 1; j++) {
            if (arr[j].hr > arr[j + 1].hr) {
                ReadingFlat tmp = arr[j];
                arr[j]         = arr[j + 1];
                arr[j + 1]     = tmp;
                swapped = 1;
            }
        }
        if (!swapped) break;   /* Early exit — already sorted */
    }
}

/* ------------------------------------------------------------------
 * bubble_sort_by_spo2
 * In-place bubble sort ascending by SpO2.
 * Time: O(n²) worst, O(n) best
 * ------------------------------------------------------------------ */
void bubble_sort_by_spo2(ReadingFlat *arr, int n)
{
    int i, j, swapped;
    for (i = 0; i < n - 1; i++) {
        swapped = 0;
        for (j = 0; j < n - i - 1; j++) {
            if (arr[j].spo2 > arr[j + 1].spo2) {
                ReadingFlat tmp = arr[j];
                arr[j]         = arr[j + 1];
                arr[j + 1]     = tmp;
                swapped = 1;
            }
        }
        if (!swapped) break;
    }
}

/* ------------------------------------------------------------------
 * flat_array_to_json
 * Serialise the flat array to a JSON array string.
 * Time: O(n)
 * ------------------------------------------------------------------ */
void flat_array_to_json(ReadingFlat *arr, int n, char *out, int max_len)
{
    int pos = 0, i;
    pos += snprintf(out + pos, max_len - pos, "[");
    for (i = 0; i < n; i++) {
        pos += snprintf(out + pos, max_len - pos,
            "%s{\"hr\":%d,\"spo2\":%d,\"timestamp\":%I64d}",
            (i > 0) ? "," : "",
            arr[i].hr, arr[i].spo2, arr[i].timestamp);
    }
    snprintf(out + pos, max_len - pos, "]");
}


/* === file_io.c === */
/*
 * file_io.c - Binary file persistence
 *
 * Patients and readings are saved to binary files after every write.
 * On startup, load_all_data() reads them back and reconstructs all
 * in-memory data structures (patient array, linked lists, ring
 * buffers, BST).  The undo stack is NOT restored — only readings
 * added in the current session can be undone.
 *
 * Readings are saved OLDEST-FIRST so that head-inserts during loading
 * leave the newest reading at the list head (correct order).
 */












/* ------------------------------------------------------------------ */
/*  SAVE                                                               */
/* ------------------------------------------------------------------ */

/* save_patients: write patient count + ID counter + array to binary  */
static void save_patients(void)
{
    FILE *f = fopen(PATIENTS_FILE, "wb");
    if (!f) { perror("[file_io] cannot open patients.bin"); return; }
    fwrite(&patient_count,   sizeof(int), 1, f);
    fwrite(&next_patient_id, sizeof(int), 1, f);
    fwrite(patients, sizeof(Patient), patient_count, f);
    fclose(f);
}

/* save_readings: flatten each linked list oldest-first and write     */
static void save_readings(void)
{
    FILE *f = fopen(READINGS_FILE, "wb");
    if (!f) { perror("[file_io] cannot open readings.bin"); return; }

    /* Pre-count total readings */
    int total = 0, i;
    for (i = 0; i < patient_count; i++)
        total += reading_count(i);
    fwrite(&total, sizeof(int), 1, f);

    /* Write each patient's readings oldest-first (reverse of the list) */
    for (i = 0; i < patient_count; i++) {
        int cnt = 0;
        ReadingFlat *arr = readings_to_flat_array(i, &cnt);
        if (!arr) continue;

        int pid = patients[i].id;
        int k;
        /* arr[0]=newest, arr[cnt-1]=oldest → write cnt-1 downto 0 */
        for (k = cnt - 1; k >= 0; k--) {
            fwrite(&pid,        sizeof(int),       1, f);
            fwrite(&arr[k].hr,  sizeof(int),       1, f);
            fwrite(&arr[k].spo2,sizeof(int),       1, f);
            fwrite(&arr[k].timestamp, sizeof(long long), 1, f);
        }
        free(arr);
    }
    fclose(f);
}

void save_all_data(void)
{
    save_patients();
    save_readings();
}

/* ------------------------------------------------------------------ */
/*  LOAD                                                               */
/* ------------------------------------------------------------------ */

void load_all_data(void)
{
    int i;

    /* -- Load patient array -- */
    FILE *fp = fopen(PATIENTS_FILE, "rb");
    if (fp) {
        fread(&patient_count,   sizeof(int), 1, fp);
        fread(&next_patient_id, sizeof(int), 1, fp);
        fread(patients, sizeof(Patient), patient_count, fp);
        fclose(fp);
        printf("[file_io] Loaded %d patient(s).\n", patient_count);
    } else {
        printf("[file_io] No patients.bin — starting fresh.\n");
        return;
    }

    /* Rebuild BST and initialise per-patient structures */
    for (i = 0; i < patient_count; i++) {
        bst_root = bst_insert(bst_root, patients[i].id, patients[i].name);
        ring_init(i, patients[i].id);
        undo_init(i);
    }

    /* -- Load readings -- */
    FILE *fr = fopen(READINGS_FILE, "rb");
    if (!fr) {
        printf("[file_io] No readings.bin — no history loaded.\n");
        return;
    }

    int total = 0;
    fread(&total, sizeof(int), 1, fr);
    printf("[file_io] Loading %d reading(s)...\n", total);

    int r;
    for (r = 0; r < total; r++) {
        int      pid  = 0, hr = 0, spo2 = 0;
        long long ts  = 0;
        fread(&pid,  sizeof(int),       1, fr);
        fread(&hr,   sizeof(int),       1, fr);
        fread(&spo2, sizeof(int),       1, fr);
        fread(&ts,   sizeof(long long), 1, fr);

        int idx = patient_find_idx_by_id(pid);
        if (idx < 0) continue;

        /* Insert at head: since we saved oldest-first, the last inserted
         * (newest) ends up at the head of the list. Correct. */
        reading_insert(idx, hr, spo2, ts);
        ring_enqueue(idx, hr, spo2, ts);
        /* NOTE: NOT pushing to undo stack — undo only works for the
         *       current session's additions. */
    }
    fclose(fr);
}


/* === http_utils.c === */
/*
 * http_utils.c - HTTP request parsing and response utilities
 *
 * parse_request(): extracts method, path, query string, and body.
 * send_*():        builds proper HTTP/1.1 response frames.
 * json_get_*():    simple substring-based JSON value extraction.
 * send_static_file(): reads a file from disk and streams it to the
 *                     socket with the correct Content-Type header.
 */









/* ------------------------------------------------------------------ */
/*  Request parsing                                                    */
/* ------------------------------------------------------------------ */

void parse_request(const char *raw, HttpRequest *req)
{
    const char *p = raw;
    int i = 0;

    memset(req, 0, sizeof(HttpRequest));

    /* Method */
    while (*p && *p != ' ' && i < 7) req->method[i++] = *p++;
    req->method[i] = '\0';
    if (*p == ' ') p++;

    /* Full path (may include ?query) */
    char full[512]; i = 0;
    while (*p && *p != ' ' && i < 511) full[i++] = *p++;
    full[i] = '\0';

    /* Split path from query string */
    char *q = strchr(full, '?');
    if (q) {
        *q = '\0';
        strncpy(req->query, q + 1, 255);
        req->query[255] = '\0';
    }
    strncpy(req->path, full, 511);
    req->path[511] = '\0';

    /* Body (everything after \r\n\r\n) */
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        int len = (int)strlen(body_start);
        if (len > 8191) len = 8191;
        memcpy(req->body, body_start, len);
        req->body[len] = '\0';
        req->body_len  = len;
    }
}

/* ------------------------------------------------------------------ */
/*  Response helpers                                                   */
/* ------------------------------------------------------------------ */

void send_json_response(SOCKET sock, int status, const char *json)
{
    const char *txt = (status == 200) ? "OK"
                    : (status == 201) ? "Created"
                    : (status == 400) ? "Bad Request"
                    : (status == 404) ? "Not Found"
                    : "Internal Server Error";
    int len = (int)strlen(json);
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, txt, len);
    send(sock, hdr, (int)strlen(hdr), 0);
    send(sock, json, len, 0);
}

void send_error(SOCKET sock, int code, const char *msg_json)
{
    send_json_response(sock, code, msg_json);
}

void send_ok_empty(SOCKET sock)
{
    const char *r =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{}";
    send(sock, r, (int)strlen(r), 0);
}

void send_404(SOCKET sock)
{
    const char *body = "<h1>404 Not Found</h1>";
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        (int)strlen(body));
    send(sock, hdr,  (int)strlen(hdr),  0);
    send(sock, body, (int)strlen(body), 0);
}

/* ------------------------------------------------------------------ */
/*  Static file serving                                                */
/* ------------------------------------------------------------------ */

void send_static_file(SOCKET sock, const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        send_404(sock);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); send_404(sock); return; }
    fread(buf, 1, (size_t)size, f);
    fclose(f);

    /* Determine Content-Type */
    const char *ct = "text/plain";
    if (strstr(filepath, ".html")) ct = "text/html; charset=utf-8";
    else if (strstr(filepath, ".css"))  ct = "text/css";
    else if (strstr(filepath, ".js"))   ct = "application/javascript";
    else if (strstr(filepath, ".json")) ct = "application/json";

    char hdr[512];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        ct, size);
    send(sock, hdr, (int)strlen(hdr), 0);
    send(sock, buf, (int)size, 0);
    free(buf);
}

/* ------------------------------------------------------------------ */
/*  JSON value extraction                                              */
/* ------------------------------------------------------------------ */

/*
 * json_get_str
 * Find "key":"value" in a JSON string and copy the value.
 * Returns 1 on success, 0 if key not found or value is not a string.
 */
int json_get_str(const char *json, const char *key,
                 char *out, int max_len)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ':' || *p == ' ') p++;
    if (*p != '"') return 0;
    p++;    /* skip opening quote */
    int i = 0;
    while (*p && *p != '"' && i < max_len - 1) out[i++] = *p++;
    out[i] = '\0';
    return 1;
}

/*
 * json_get_int
 * Find "key":number in a JSON string and parse the integer.
 * Returns 1 on success, 0 if key not found.
 */
int json_get_int(const char *json, const char *key, int *out)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ':' || *p == ' ') p++;
    *out = atoi(p);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  URL decoding                                                       */
/* ------------------------------------------------------------------ */

void url_decode(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (*src && i < max_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}


/* === main.c === */
/*
 * main.c - Health Monitor C Server
 *
 * Single-threaded HTTP/1.1 server using WinSock2.
 * Handles REST API endpoints and serves static web files.
 *
 * API Endpoints:
 *   GET  /api/patients                    List all patients
 *   POST /api/patients                    Add patient {name, age}
 *   GET  /api/patients/{id}               Get patient details
 *   DEL  /api/patients/{id}               Delete patient
 *   GET  /api/patients/{id}/readings      Get readings (opt ?sort=hr|spo2)
 *   POST /api/patients/{id}/undo          Undo last reading
 *   POST /api/readings                    Add reading {patient_id,hr,spo2}
 *   POST /api/simulate/{id}              Simulate a random reading
 *   GET  /api/live                        Latest reading for all patients
 *   GET  /                                Serve index.html
 *   GET  /*.html or *.css or *.js       Serve from ../web/
 *
 * Data structures used:
 *   Patient array  (patient.h)      - static storage, O(1) add
 *   Linked list    (reading_list.h) - per-patient history, O(1) insert
 *   Circular queue (sensor_buffer.h)- live ring buffer, O(1) enqueue
 *   BST            (patient_bst.h)  - name lookup, O(log n)
 *   Stack          (undo_stack.h)   - undo last addition, O(1)
 *   Bubble sort    (sorting.h)      - sort readings, O(n^2)
 */



#pragma comment(lib, "ws2_32.lib")















#define PORT        8080
#define WEB_ROOT    "../web"
#define RECV_BUF    16384

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */
static void init_sample_data(void);
static void handle_request(SOCKET sock, HttpRequest *req);
static void handle_api(SOCKET sock, HttpRequest *req);
static void handle_static(SOCKET sock, const char *path);

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    WSADATA           wsa;
    SOCKET            server_fd, client_sock;
    struct sockaddr_in addr;
    int               addrlen = sizeof(addr);
    char              buf[RECV_BUF];

    srand((unsigned int)time(NULL));

    /* Restore saved data */
    load_all_data();

    /* Seed sample patients when running for the first time */
    if (patient_count == 0)
        init_sample_data();

    /* WinSock2 initialisation */
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        return 1;
    }

    /* Allow fast restart without TIME_WAIT errors */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        return 1;
    }
    listen(server_fd, 5);

    printf("===========================================\n");
    printf("  Health Monitor Server  v1.0\n");
    printf("  http://localhost:%d\n", PORT);
    printf("===========================================\n");
    printf("  Patients loaded : %d\n", patient_count);
    printf("  Web root        : %s\n", WEB_ROOT);
    printf("===========================================\n\n");

    while (1) {
        client_sock = accept(server_fd, (struct sockaddr*)&addr, &addrlen);
        if (client_sock == INVALID_SOCKET) continue;

        memset(buf, 0, sizeof(buf));
        recv(client_sock, buf, sizeof(buf) - 1, 0);

        HttpRequest req;
        parse_request(buf, &req);

        printf("[%s] %s\n", req.method, req.path);

        handle_request(client_sock, &req);
        closesocket(client_sock);
    }

    WSACleanup();
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Request dispatcher                                                 */
/* ------------------------------------------------------------------ */
static void handle_request(SOCKET sock, HttpRequest *req)
{
    /* CORS preflight */
    if (strcmp(req->method, "OPTIONS") == 0) {
        const char *h =
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET,POST,DELETE,OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n";
        send(sock, h, (int)strlen(h), 0);
        return;
    }

    if (strncmp(req->path, "/api/", 5) == 0)
        handle_api(sock, req);
    else
        handle_static(sock, req->path);
}

/* ------------------------------------------------------------------ */
/*  Static file server                                                 */
/* ------------------------------------------------------------------ */
static void handle_static(SOCKET sock, const char *path)
{
    char filepath[600];
    if (strcmp(path, "/") == 0)
        snprintf(filepath, sizeof(filepath), "%s/index.html", WEB_ROOT);
    else
        snprintf(filepath, sizeof(filepath), "%s%s", WEB_ROOT, path);
    send_static_file(sock, filepath);
}

/* ------------------------------------------------------------------ */
/*  API router                                                         */
/* ------------------------------------------------------------------ */
static void handle_api(SOCKET sock, HttpRequest *req)
{
    const char *method = req->method;
    const char *path   = req->path;
    const char *body   = req->body;
    const char *query  = req->query;
    char json_buf[32768];

    /* ---- GET /api/live ---- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/live") == 0) {
        live_all_to_json(json_buf, sizeof(json_buf));
        send_json_response(sock, 200, json_buf);
        return;
    }

    /* ---- GET /api/patients ---- */
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/patients") == 0) {
        patients_to_json(json_buf, sizeof(json_buf));
        send_json_response(sock, 200, json_buf);
        return;
    }

    /* ---- POST /api/patients ---- */
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/patients") == 0) {
        char name[NAME_LEN] = "";
        int  age = 0;
        json_get_str(body, "name", name, NAME_LEN);
        json_get_int(body, "age", &age);
        if (strlen(name) == 0) {
            send_error(sock, 400, "{\"error\":\"name is required\"}");
            return;
        }
        int id = patient_add(name, age);
        if (id < 0) {
            send_error(sock, 400, "{\"error\":\"patient list is full\"}");
            return;
        }
        int idx = patient_find_idx_by_id(id);
        ring_init(idx, id);
        undo_init(idx);
        bst_root = bst_insert(bst_root, id, name);
        save_all_data();
        patient_to_json(idx, json_buf, sizeof(json_buf));
        send_json_response(sock, 201, json_buf);
        return;
    }

    /* ---- POST /api/simulate/{id} ---- */
    if (strcmp(method, "POST") == 0 &&
        strncmp(path, "/api/simulate/", 14) == 0) {
        int pid = atoi(path + 14);
        int idx = patient_find_idx_by_id(pid);
        if (idx < 0) {
            send_error(sock, 404, "{\"error\":\"patient not found\"}");
            return;
        }
        /* Generate realistic random vitals */
        int hr_s   = 58 + (rand() % 45);    /* 58 - 102  BPM */
        int spo2_s = 94 + (rand() % 7);     /* 94 - 100  %   */
        long long ts = (long long)time(NULL);
        reading_insert(idx, hr_s, spo2_s, ts);
        ring_enqueue(idx, hr_s, spo2_s, ts);
        undo_push(idx, hr_s, spo2_s, ts);
        save_all_data();
        snprintf(json_buf, sizeof(json_buf),
            "{\"ok\":true,\"patient_id\":%d,\"hr\":%d,"
            "\"spo2\":%d,\"timestamp\":%I64d}",
            pid, hr_s, spo2_s, ts);
        send_json_response(sock, 201, json_buf);
        return;
    }

    /* ---- Routes with /api/patients/{id}[/sub] ---- */
    if (strncmp(path, "/api/patients/", 14) == 0) {
        const char *after = path + 14;
        int pid = atoi(after);

        /* Extract optional sub-resource ("readings", "undo") */
        char sub[64] = "";
        const char *slash = strchr(after, '/');
        if (slash) strncpy(sub, slash + 1, 63);

        int idx = patient_find_idx_by_id(pid);

        /* GET /api/patients/{id} */
        if (strcmp(method, "GET") == 0 && sub[0] == '\0') {
            if (idx < 0) { send_error(sock, 404, "{\"error\":\"not found\"}"); return; }
            patient_to_json(idx, json_buf, sizeof(json_buf));
            send_json_response(sock, 200, json_buf);
            return;
        }

        /* DELETE /api/patients/{id} */
        if (strcmp(method, "DELETE") == 0 && sub[0] == '\0') {
            if (idx < 0) { send_error(sock, 404, "{\"error\":\"not found\"}"); return; }
            char name[NAME_LEN];
            strncpy(name, patients[idx].name, NAME_LEN);
            /* Free linked list memory */
            reading_delete_all(idx);
            /* Shift parallel arrays to fill the gap */
            int i;
            for (i = idx; i < patient_count - 1; i++) {
                reading_heads[i]  = reading_heads[i + 1];
                ring_buffers[i]   = ring_buffers[i + 1];
                undo_stacks[i]    = undo_stacks[i + 1];
            }
            reading_heads[patient_count - 1] = NULL;
            /* Remove from patient array and BST */
            patient_delete_by_idx(idx);
            bst_root = bst_delete(bst_root, name);
            save_all_data();
            send_ok_empty(sock);
            return;
        }

        /* GET /api/patients/{id}/readings[?sort=hr|spo2] */
        if (strcmp(method, "GET") == 0 && strcmp(sub, "readings") == 0) {
            if (idx < 0) { send_error(sock, 404, "{\"error\":\"not found\"}"); return; }

            if (strstr(query, "sort=hr") || strstr(query, "sort=spo2")) {
                int cnt = 0;
                ReadingFlat *arr = readings_to_flat_array(idx, &cnt);
                if (arr) {
                    if (strstr(query, "sort=hr"))   bubble_sort_by_hr(arr, cnt);
                    else                             bubble_sort_by_spo2(arr, cnt);
                    flat_array_to_json(arr, cnt, json_buf, sizeof(json_buf));
                    free(arr);
                } else {
                    strcpy(json_buf, "[]");
                }
            } else {
                readings_to_json(idx, -1, json_buf, sizeof(json_buf));
            }
            send_json_response(sock, 200, json_buf);
            return;
        }

        /* POST /api/patients/{id}/undo */
        if (strcmp(method, "POST") == 0 && strcmp(sub, "undo") == 0) {
            if (idx < 0) { send_error(sock, 404, "{\"error\":\"not found\"}"); return; }
            int hr, spo2;
            long long ts;
            if (undo_pop(idx, &hr, &spo2, &ts)) {
                reading_remove_head(idx);
                save_all_data();
                snprintf(json_buf, sizeof(json_buf),
                    "{\"ok\":true,\"removed\":{\"hr\":%d,\"spo2\":%d}}", hr, spo2);
                send_json_response(sock, 200, json_buf);
            } else {
                send_error(sock, 400, "{\"error\":\"nothing to undo\"}");
            }
            return;
        }
    }

    /* No route matched */
    send_error(sock, 404, "{\"error\":\"unknown API endpoint\"}");
}

/* ------------------------------------------------------------------ */
/*  Sample data (first run only, no patients.bin)                      */
/* ------------------------------------------------------------------ */
static void init_sample_data(void)
{
    long long now = (long long)time(NULL);
    int id, idx;

    printf("[init] No data found — seeding sample patients.\n");

    /* Patient 1 */
    id  = patient_add("Sarthak", 20);
    idx = patient_find_idx_by_id(id);
    ring_init(idx, id);
    undo_init(idx);
    bst_root = bst_insert(bst_root, id, "Sarthak");
    reading_insert(idx, 78, 98, now - 300);  ring_enqueue(idx, 78, 98, now - 300);
    reading_insert(idx, 82, 97, now - 240);  ring_enqueue(idx, 82, 97, now - 240);
    reading_insert(idx, 76, 99, now - 180);  ring_enqueue(idx, 76, 99, now - 180);
    reading_insert(idx, 80, 98, now - 120);  ring_enqueue(idx, 80, 98, now - 120);
    reading_insert(idx, 79, 97, now -  60);  ring_enqueue(idx, 79, 97, now -  60);
    /* Push last one to undo stack for demo */
    undo_push(idx, 79, 97, now - 60);

    /* Patient 2 */
    id  = patient_add("Rahul", 22);
    idx = patient_find_idx_by_id(id);
    ring_init(idx, id);
    undo_init(idx);
    bst_root = bst_insert(bst_root, id, "Rahul");
    reading_insert(idx, 72, 99, now - 200);  ring_enqueue(idx, 72, 99, now - 200);
    reading_insert(idx, 75, 98, now - 100);  ring_enqueue(idx, 75, 98, now - 100);
    reading_insert(idx, 73, 99, now -  50);  ring_enqueue(idx, 73, 99, now -  50);
    undo_push(idx, 73, 99, now - 50);

    save_all_data();
    printf("[init] Created 2 sample patients.\n\n");
}


