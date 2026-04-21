# Health Monitoring System — Implementation Plan

A complete, dual-course mini-project combining **Microcontroller** (ESP32 + heart rate sensor over WiFi) and **Data Structures & Programming** (C server with proper DS implementation). Serves as a real, running embedded system with a premium web UI dashboard.

---

## What This Project Does

| Layer | Technology | Role |
|---|---|---|
| **Sensor** | ESP32 + MAX30102 | Reads Heart Rate & SpO2, POSTs to C server |
| **Backend** | C (WinSock2 HTTP server) | All data logic, DS operations, REST API |
| **Frontend** | HTML + CSS + JS | Dashboard, patient management, live charts |
| **Storage** | Binary files (`patients.bin`, `readings.bin`) | Persistent data across restarts |

**Data flows:** ESP32 → (WiFi HTTP POST) → C Server → (REST API / JSON) → Web Dashboard

---

## Data Structures Used (mapped to syllabus)

| Data Structure | Where Used | Complexity | Syllabus Topic |
|---|---|---|---|
| **Struct + Array (static)** | Patient records array | O(1) access | Exp 3 – Arrays |
| **Linked List** | Per-patient reading history (dynamic, malloc/free) | O(n) insert | Exp 6 – Linked List |
| **Circular Queue** | Live sensor ring buffer (last 50 readings per patient) | O(1) enqueue/dequeue | Exp 8 – Queue |
| **Binary Search Tree (BST)** | Patient lookup by name (fast search) | O(log n) avg | Exp 9 – Tree |
| **Stack** | Undo last sensor reading per patient | O(1) push/pop | Exp 7 – Stack |
| **Bubble Sort** | Sort patient readings by Heart Rate or SpO2 | O(n²) | Exp 5 – Sorting |
| **Linear Search** | Find patient by name in array | O(n) | Exp 5 – Searching |

This covers **all 4 Course Outcomes** and hits every major marking criteria.

---

## Project Structure

```
Health Monitor/
├── c_server/
│   ├── main.c              ← HTTP server, routing, main loop
│   ├── patient.h/c         ← Patient struct + static array
│   ├── reading_list.h/c    ← Linked list of readings per patient
│   ├── sensor_buffer.h/c   ← Circular queue (ring buffer) for live feed
│   ├── patient_bst.h/c     ← BST for fast patient search
│   ├── undo_stack.h/c      ← Stack for undo last reading
│   ├── sorting.h/c         ← Bubble sort on readings
│   ├── file_io.h/c         ← Binary file persistence
│   ├── http_utils.h/c      ← HTTP request parser + JSON builder
│   └── Makefile
├── web/
│   ├── index.html          ← Dashboard (live monitor)
│   ├── patients.html       ← Patient management page
│   ├── style.css           ← Premium dark theme
│   └── app.js              ← Fetch API calls, Chart.js live graph
└── esp32/
    └── src/
        └── main.cpp        ← WiFi + MAX30102 + HTTP POST to C server
```

---

## C Server — REST API Endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/patients` | List all patients (JSON) |
| `POST` | `/api/patients` | Add new patient (`name`, `age`) |
| `GET` | `/api/patients/{id}` | Get patient details + readings |
| `DELETE` | `/api/patients/{id}` | Delete patient |
| `GET` | `/api/patients/{id}/readings` | All readings (sorted if ?sort=hr) |
| `POST` | `/api/readings` | Add reading (ESP32 uses this endpoint) |
| `GET` | `/api/live` | Last reading from circular buffer for all patients |
| `POST` | `/api/readings/{id}/undo` | Pop last reading from undo stack |
| `GET` | `/` | Serve index.html |
| `GET` | `/patients.html` | Serve patients.html |

---

## Detailed Module Design

### `patient.h/c` — Patient Array (Static)
```c
#define MAX_PATIENTS 20

typedef struct {
    int    id;
    char   name[64];
    int    age;
    int    reading_count;
} Patient;

// Array of patients (static allocation)
extern Patient patients[MAX_PATIENTS];
extern int patient_count;

int  patient_add(const char *name, int age);      // O(1)
int  patient_find_by_id(int id);                  // O(n) linear search
int  patient_find_by_name(const char *name);      // O(n) linear search
void patient_delete(int id);
```

### `reading_list.h/c` — Linked List of Readings
```c
typedef struct ReadingNode {
    int                 hr;
    int                 spo2;
    long long           timestamp;
    struct ReadingNode *next;     // pointer arithmetic / dynamic memory
} ReadingNode;

ReadingNode* reading_insert(ReadingNode **head, int hr, int spo2); // malloc
void         reading_delete_all(ReadingNode **head);               // free chain
void         reading_to_json(ReadingNode *head, char *out, int max_n);
```

### `sensor_buffer.h/c` — Circular Queue (Ring Buffer)
```c
#define RING_SIZE 50

typedef struct {
    int hr[RING_SIZE];
    int spo2[RING_SIZE];
    int head, tail, count;
    int patient_id;
} SensorRingBuffer;

void ring_enqueue(SensorRingBuffer *rb, int hr, int spo2);  // O(1)
int  ring_dequeue(SensorRingBuffer *rb, int *hr, int *spo2); // O(1)
void ring_peek_last(SensorRingBuffer *rb, int n, char *json_out);
```

### `patient_bst.h/c` — BST for Patient Lookup
```c
typedef struct BSTNode {
    int          patient_id;
    char         name[64];
    struct BSTNode *left, *right;
} BSTNode;

BSTNode* bst_insert(BSTNode *root, int id, const char *name);  // O(log n)
BSTNode* bst_search(BSTNode *root, const char *name);          // O(log n)
BSTNode* bst_delete(BSTNode *root, const char *name);
void     bst_inorder(BSTNode *root, char *out);  // sorted patient list
```

### `undo_stack.h/c` — Stack for Undo
```c
#define STACK_SIZE 20

typedef struct {
    int hr[STACK_SIZE];
    int spo2[STACK_SIZE];
    long long timestamps[STACK_SIZE];
    int top;
    int patient_id;
} UndoStack;

void undo_push(UndoStack *s, int hr, int spo2, long long ts); // O(1)
int  undo_pop(UndoStack *s, int *hr, int *spo2);              // O(1)
```

### `sorting.h/c` — Bubble Sort on Readings
```c
// Sort a copy of the linked list by HR or SpO2
// Returns dynamically allocated sorted array — caller must free()
typedef struct { int hr; int spo2; long long ts; } ReadingFlat;

ReadingFlat* sort_readings_by_hr(ReadingNode *head, int *out_count);  // O(n²)
ReadingFlat* sort_readings_by_spo2(ReadingNode *head, int *out_count);
```

---

## Web Frontend Design

**Dark premium theme** — glassmorphism cards, animated live line charts (Chart.js via CDN), responsive 2-column layout.

### `index.html` — Dashboard
- Top bar with system title + server status indicator
- **Live Monitor Section**: Dropdown to select patient → real-time chart polling `/api/live` every 1s
- **Recent Readings Card**: Last 5 readings table for selected patient
- **Stats Card**: Average HR, min/max SpO2

### `patients.html` — Patient Management
- Table of all patients (from BST inorder → sorted by name)
- "Add Patient" modal form
- Per-patient: View history, Sort readings (by HR / SpO2), Undo last reading
- Delete patient button

---

## ESP32 Code

```cpp
// esp32/src/main.cpp
// Reads MAX30102, POSTs JSON to C server every 2 seconds

#include <WiFi.h>
#include <HTTPClient.h>
#include "MAX30105.h"  // SparkFun library
#include "heartRate.h"
#include "spo2_algorithm.h"

const char* ssid     = "YOUR_WIFI";
const char* password = "YOUR_PASS";
const char* server   = "http://192.168.x.x:8080/api/readings";
int patient_id = 1;  // which patient this sensor is assigned to

// loop(): read sensor → HTTP POST {"patient_id":1,"hr":75,"spo2":98}
```

> [!NOTE]
> The ESP32 code can also be simulated from the web UI (manual "Add Reading" button) so the project works even without hardware during demo.

---

## File Persistence (`file_io.h/c`)

Saves patient and reading data to binary files on every write:
- `data/patients.bin` — array of `Patient` structs
- `data/readings.bin` — flat array of `{patient_id, hr, spo2, timestamp}` structs

On server startup, data is loaded and the BST + ring buffers are rebuilt from file.

---

## Big-O Summary (Required for Report)

| Operation | Data Structure | Complexity |
|---|---|---|
| Add patient | Array | O(1) |
| Find patient by name | Linear Search | O(n) |
| Find patient by name | BST | O(log n) avg, O(n) worst |
| Insert reading | Linked List | O(1) at head |
| Live buffer enqueue | Circular Queue | O(1) |
| Undo reading | Stack | O(1) |
| Sort readings by HR | Bubble Sort | O(n²) |
| BST inorder traversal | BST | O(n) |

---

## Assessment Alignment

| Criteria | How We Address It | Weight |
|---|---|---|
| Domain Understanding | Real sensor data, correct DS per task | 20% |
| Memory Efficiency | `malloc`/`free` in linked list, BST nodes, dynamic sort array | 30% |
| Algorithmic Logic | BST search, bubble sort, circular queue, stack operations all correct | 20% |
| Documentation | Every function commented with purpose + complexity | 15% |
| Innovation | Real ESP32 hardware + web dashboard + multiple DS in one project | 15% |

---

## Build & Run

```bash
# Build C server (on Windows with MinGW)
cd c_server
mingw32-make

# Run server
./server.exe
# → Server running on http://localhost:8080

# Open web dashboard
# Open web/index.html in browser (or serve via server)
```

---

## Final Decisions

| # | Decision |
|---|---|
| 1 | No MAX30102 sensor → **Simulate Reading button** in UI sends fake data to C server |
| 2 | **MinGW GCC 6.3.0** confirmed — using `-std=c99` flag |
| 3 | **C server serves HTML files** from `../web/` — single process, no CORS, just `http://localhost:8080` |
| 4 | **HR + SpO2 only** — no temperature |

## User Review Required

> [!IMPORTANT]
> **ESP32 + Sensor**: Do you have the MAX30102 heart rate sensor module? If not, the ESP32 section can be skipped or simulated — the web UI will have a "Simulate Sensor" button that sends fake data to the C server so everything still works for demo.

> [!IMPORTANT]
> **MinGW / GCC on Windows**: The C server needs a C compiler. Do you have MinGW or GCC installed? Run `gcc --version` in terminal to check. I'll include Makefile + compile instructions either way.

> [!WARNING]
> **File Paths in C**: Since you're on Windows and the previous code used WinSock2, the new server will also use WinSock2 + Windows file paths. This is fine for demo but won't compile on Linux as-is.

---

## Open Questions

1. Do you have the MAX30102 sensor module, or should I focus more on the simulation/manual entry path?
2. Is MinGW/GCC available on your machine (`gcc --version`)?
3. Do you want the web frontend served **directly by the C server** (simpler, one process) OR as separate static HTML files opened in browser?
4. Do you want temperature sensor support too, or just HR + SpO2?
