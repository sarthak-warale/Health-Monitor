/**
 * Health Monitor Server - Core Data Structures (Condensed for Report)
 * 
 * This file highlights the primary data structures used in the project:
 * 1. Circular Queue (Live Sensor Ring Buffer)
 * 2. Singly Linked List (Historical Reading Log)
 * 3. Stack (Undo Buffer)
 * 4. Binary Search Tree (Patient Name Lookup)
 * 5. Bubble Sort (Analytics Sorting)
 * 
 * Note: HTTP networking, file I/O, and JSON serialization have been 
 * omitted from this snippet for brevity in the project report.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING_SIZE 50
#define NAME_LEN 64
#define MAX_UNDO 30

/* ================================================================= */
/* 1. STRUCTS AND CORE DEFINITIONS                                   */
/* ================================================================= */

/* Linked List Node for historical reading logging */
typedef struct ReadingNode {
    int hr, spo2;
    long long timestamp;
    struct ReadingNode *next; 
} ReadingNode;

/* Circular Queue for real-time sensor buffering */
typedef struct {
    int hr[RING_SIZE], spo2[RING_SIZE];
    long long timestamps[RING_SIZE];
    int head, tail, count;
} SensorRingBuffer;

/* Binary Search Tree Node for patient lookup */
typedef struct BSTNode {
    int patient_id;
    char name[NAME_LEN];
    struct BSTNode *left, *right;
} BSTNode;

/* Array-based Stack for undo operations */
typedef struct {
    int hr[MAX_UNDO], spo2[MAX_UNDO];
    long long timestamps[MAX_UNDO];
    int top;
} UndoStack;


/* ================================================================= */
/* 2. CIRCULAR QUEUE (SENSOR BUFFER) - O(1) TIME                     */
/* ================================================================= */
void ring_enqueue(SensorRingBuffer *rb, int hr, int spo2, long long ts) {
    /* Insert at tail */
    rb->hr[rb->tail] = hr;
    rb->spo2[rb->tail] = spo2;
    rb->timestamps[rb->tail] = ts;
    
    /* Advance tail with modulo to wrap around bounds natively */
    rb->tail = (rb->tail + 1) % RING_SIZE;

    if (rb->count < RING_SIZE) {
        rb->count++;
    } else {
        /* If full, forcefully advance head to discard oldest reading */
        rb->head = (rb->head + 1) % RING_SIZE;
    }
}


/* ================================================================= */
/* 3. SINGLY LINKED LIST (HISTORY LOGGING) - O(1) TIME               */
/* ================================================================= */
void reading_insert(ReadingNode **head, int hr, int spo2, long long ts) {
    /* Malloc prevents fixed array limitations for lifetime logs */
    ReadingNode *node = (ReadingNode *)malloc(sizeof(ReadingNode));
    if (!node) return;
    
    node->hr = hr;
    node->spo2 = spo2;
    node->timestamp = ts;
    
    /* Always insert at HEAD to access newest historical readings instantly */
    node->next = *head;
    *head = node;
}


/* ================================================================= */
/* 4. BINARY SEARCH TREE (SEARCHING) - O(Log N) TIME                 */
/* ================================================================= */
BSTNode* bst_insert(BSTNode *root, int patient_id, const char *name) {
    if (!root) {
        BSTNode *node = (BSTNode *)malloc(sizeof(BSTNode));
        node->patient_id = patient_id;
        strcpy(node->name, name);
        node->left = node->right = NULL;
        return node;
    }
    
    /* Lexicographical string comparison dictates tree direction */
    int cmp = strcmp(name, root->name);
    if (cmp < 0) root->left = bst_insert(root->left, patient_id, name);
    else if (cmp > 0) root->right = bst_insert(root->right, patient_id, name);
    else root->patient_id = patient_id; /* Overwrite on exact duplicate */
    
    return root;
}


/* ================================================================= */
/* 5. STACK (UNDO MECHANISM) - O(1) TIME                             */
/* ================================================================= */
void undo_push(UndoStack *s, int hr, int spo2, long long ts) {
    if (s->top >= MAX_UNDO - 1) {
        /* Discard oldest by shifting stack down if bounds reached */
        for (int i = 0; i < MAX_UNDO - 1; i++) {
            s->hr[i] = s->hr[i + 1];
        }
        s->top = MAX_UNDO - 2;
    }
    s->top++;
    s->hr[s->top] = hr;
    s->spo2[s->top] = spo2;
    s->timestamps[s->top] = ts;
}

int undo_pop(UndoStack *s, int *hr, int *spo2) {
    if (s->top < 0) return 0; /* Stack empty */
    *hr = s->hr[s->top];
    *spo2 = s->spo2[s->top];
    s->top--;
    return 1;
}


/* ================================================================= */
/* 6. BUBBLE SORT (ANALYTICS ALGORITHM) - O(N^2) TIME                */
/* ================================================================= */
/* Flattens LL node structure into continuous block to swap pointers   */
typedef struct { int hr, spo2; long long timestamp; } ReadingFlat;

void bubble_sort_by_hr(ReadingFlat *arr, int n) {
    int i, j;
    for (i = 0; i < n - 1; i++) {
        int swapped = 0;
        for (j = 0; j < n - i - 1; j++) {
            if (arr[j].hr > arr[j+1].hr) {
                /* Swap logic */
                ReadingFlat temp = arr[j];
                arr[j] = arr[j+1];
                arr[j+1] = temp;
                swapped = 1;
            }
        }
        if (!swapped) break; /* Early exit optimization */
    }
}
