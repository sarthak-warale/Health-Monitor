# Health Monitor - Data Structures Core

A robust, real-time health telemetry buffer and analytics server built entirely in C. 
This project was developed to cleanly map advanced theoretical software data structures to real-world Engineering concepts (like signal processing pipelines and buffer memory limitations).

## Core Architecture

The system mimics a live medical telemetry stream without relying on memory-leaking arrays or blocking iterations. 

- **Circular Queue:** Maintains a bounded window of high-frequency streaming sensor data safely.
- **Singly Linked Lists:** Maintains permanent historical records utilizing raw pointer arithmetic and `malloc`/`free`.
- **Binary Search Tree (BST):** Fast `O(log n)` patient indexing and name lookups.
- **Undo Stack:** Allows real-time rollback and history stripping via array-based stacking algorithms.
- **Bubble Sort:** Generates `O(n^2)` analytics to detect critical SpO2 dips in historically linked lists.

## Structure

```
├── c_server/           # Backend (Written purely in C)
│   ├── server.c        # The monolithic backend server & API endpoints
│   ├── Makefile        # Compilation instructions
│   └── data/           # Binary persistence storage (Not tracked in Git)
├── web/                # Frontend Web Dashboard
│   ├── index.html      # Main telemetry view
│   ├── style.css       # Custom design configurations
│   └── app.js          # API Polling Logic
├── report_code_snippet.c # Isolated Data structure demo
└── Project_Report.md   # Project overview and Big-O document
```

## How to Run

1. Navigate to the `c_server` folder.
2. Compile and launch the server via `make run` containing the C dependencies.
3. Access the dashboard via `http://localhost:8080/`.
4. Click `Simulate` on any listed patient to automatically pipe raw diagnostic data into the algorithm buffering endpoints.
