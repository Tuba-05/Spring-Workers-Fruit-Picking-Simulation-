// path to open folder/file in WSL: /mnt/c/Users/AA/Desktop/CS_OEL 
// ============================================================
//  SPRING WORKERS - Fruit Picking Sim | GTK GUI VERSION
//  Compile:
//    sudo apt install libgtk-3-dev
//    gcc -o Threads_GTK Threads_GTK.c -lpthread $(pkg-config --cflags --libs gtk+-3.0)
// ============================================================

//  Roll #: CS-22021, CS-23014, CS-23058

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <gtk/gtk.h>

#define NUM_PICKERS    3
#define CRATE_CAPACITY 12

static int DELAY_PICK_MS      = 400;
static int DELAY_PLACE_MS     = 250;
static int DELAY_LOAD_MS      = 600;
static int DELAY_NEW_CRATE_MS = 350;

/* ── Shared simulation state ─────────────────────────────── */
static int *tree      = NULL; // fruit values ka array
static int  tree_size = 0;    // total fruits
static int  next_fruit = 0;   // agla fruit index (tree_mutex se protected)
static int  tree_bare  = 0;   // 1 = tree khatam

// Crate state declaration
typedef enum { CRATE_OPEN, CRATE_FULL, CRATE_SWAPPING } CrateState;

static CrateState crate_state = CRATE_OPEN; // current crate state
static int crate_slots  = 0;                // fruits in crate (0-12)
static int crate_id     = 1;                // crate number (logging only)
static int truck_crates = 0;                // load crates on truck
static int pickers_done = 0;                // how many pickers have finished (for tree bare condition)
static int picker_count[NUM_PICKERS] = {0}; // each picker count

// Crate timing
static struct timespec crate_start_time;    // crate fill start time
static double total_crate_time = 0;         // total fill time (ms)
static int    crates_timed     = 0;         // average ke liye divisor

// Mutexes
static pthread_mutex_t tree_mutex  = PTHREAD_MUTEX_INITIALIZER; // CS-1: tree access
static pthread_mutex_t crate_mutex = PTHREAD_MUTEX_INITIALIZER; // CS-2: crate access

// Condition variables
static pthread_cond_t cond_crate_open = PTHREAD_COND_INITIALIZER; // pickers wait here
static pthread_cond_t cond_crate_full = PTHREAD_COND_INITIALIZER; // loader wait here

static volatile sig_atomic_t sim_interrupted = 0; // Ctrl+C flag

/* ── GTK widgets ─────────────────────────────────────────── */
static GtkWidget     *log_view;                          // activity log text area
static GtkTextBuffer *log_buf;                           // text buffer for log_view
static GtkWidget     *progress_bars[NUM_PICKERS];        // per-picker progress bars
static GtkWidget     *crate_bar;                         // current crate fill bar
static GtkWidget     *truck_label;                       // truck crate count label
static GtkWidget     *status_label;                      // bottom status text
static GtkWidget     *crate_slots_box[CRATE_CAPACITY];   // 12 slot boxes (green/grey)
static GtkWidget     *start_btn;                         // start button
static GtkWidget     *entry_fruits;                      // fruit count input field
static GtkWidget     *summary_label;                     // final summary text

static const char *PICKER_NAME[NUM_PICKERS] = {"Picker-1","Picker-2","Picker-3"}; // thread display names

/* ── Data structs for g_idle_add callbacks ───────────────── */
typedef struct { char *msg; int picker_id; } LogMsg;     /* log message + picker id for color coding */
typedef struct { int picker_id; int count; int total; }  /* picker id + fruits picked + tree size for progress fraction */
PickerBarUpdate; 
typedef struct { int nslots; int ncid; } CrateBarUpdate; /* current crate fill count + crate id for progress bar */
typedef struct { int slot_idx; int fill; } SlotUpdate;   /* crate slot index + filled(1)/empty(0) */
typedef struct { int count; } TruckUpdate;               /* total crates loaded on truck so far */
typedef struct { char *text; } StatusUpdate;             /* status bar message string */

/* Holds final simulation stats passed to the summary GUI callback. */
typedef struct {
    int total_fruits, total_picked, crates;
    int p[NUM_PICKERS];
    int passed; // correctness check (picked == total)
    int spread; // fairness: max - min picker count
} SummaryData;

/* ── SIGINT Handler ──────────────────────────────────────── */
static void handle_sigint(int sig) {
    (void)sig;
    sim_interrupted = 1;
    pthread_mutex_unlock(&tree_mutex); // in case pickers are waiting on tree_mutex
    pthread_mutex_unlock(&crate_mutex); // in case loader is waiting on crate_mutex
    pthread_cond_broadcast(&cond_crate_open); // unblock pickers waiting for crate to open
    pthread_cond_broadcast(&cond_crate_full); // unblock loader waiting for crate to fill
    if (tree) { free(tree); tree = NULL; } // free tree if allocated
    printf("\n[SIGINT] Simulation interrupted. Resources freed.\n");
    exit(0);
}

/* ── Helper ──────────────────────────────────────────────── */
// Millisecond sleep using nanosleep (more accurate than usleep for >1ms)
// usage example: ms_sleep(250); // sleep for 250 milliseconds
static void ms_sleep(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
/* Formats current time as [HH:MM:SS.mmm] into buf. */
static void get_time_str(char *buf, size_t sz) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *t = localtime(&ts.tv_sec);
    snprintf(buf, sz, "[%02d:%02d:%02d.%03ld]",
             t->tm_hour, t->tm_min, t->tm_sec, ts.tv_nsec / 1000000);
}
/* ── GTK idle callbacks (main-thread safe) ───────────────── */
static gboolean cb_append_log(gpointer data) {
    LogMsg *m = (LogMsg *)data; 
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(log_buf, &end);

    const char *color = "#94a3b8";
    if      (m->picker_id == 0)  color = "#4ade80";
    else if (m->picker_id == 1)  color = "#facc15";
    else if (m->picker_id == 2)  color = "#22d3ee";
    else if (m->picker_id == 99) color = "#e879f9";

    GtkTextTag *tag = gtk_text_buffer_create_tag(log_buf, NULL,
        "foreground", color, "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_insert_with_tags(log_buf, &end, m->msg, -1, tag, NULL);
    gtk_text_buffer_get_end_iter(log_buf, &end);
    gtk_text_buffer_insert(log_buf, &end, "\n", -1);

    GtkTextMark *mark = gtk_text_buffer_get_insert(log_buf);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(log_view), mark);

    free(m->msg);
    free(m);
    return G_SOURCE_REMOVE;
}
/* Updates picker progress bar fraction and label text from main thread. */
static gboolean cb_update_picker_bar(gpointer data) {
    PickerBarUpdate *u = (PickerBarUpdate *)data;
    if (u->total > 0)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bars[u->picker_id]),
            (double)u->count / u->total);
    char txt[64];
    snprintf(txt, sizeof(txt), "%s: %d fruits", PICKER_NAME[u->picker_id], u->count);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bars[u->picker_id]), txt);
    free(u);
    return G_SOURCE_REMOVE;
}
/* Updates crate progress bar fraction and label text from main thread. */
static gboolean cb_update_crate_bar(gpointer data) {
    CrateBarUpdate *u = (CrateBarUpdate *)data;
    double frac = (double)u->nslots / CRATE_CAPACITY;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(crate_bar), frac);
    char txt[64];
    snprintf(txt, sizeof(txt), "Crate-%d: %d/%d", u->ncid, u->nslots, CRATE_CAPACITY);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(crate_bar), txt);
    free(u);
    return G_SOURCE_REMOVE;
}

/* Toggles a single crate slot box between filled and empty CSS class. */
static gboolean cb_update_slot(gpointer data) {
    SlotUpdate *u = (SlotUpdate *)data;
    if (u->slot_idx >= 0 && u->slot_idx < CRATE_CAPACITY) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(crate_slots_box[u->slot_idx]);
        if (u->fill) {
            gtk_style_context_add_class(ctx, "slot-filled");
            gtk_style_context_remove_class(ctx, "slot-empty");
        } else {
            gtk_style_context_add_class(ctx, "slot-empty");
            gtk_style_context_remove_class(ctx, "slot-filled");
        }
        gtk_widget_queue_draw(crate_slots_box[u->slot_idx]); /* FIX: repaint trigger */
    }
    free(u);
    return G_SOURCE_REMOVE;
}
/* Resets all crate slot boxes to empty CSS class when a new crate is provided. */
static gboolean cb_reset_slots(gpointer data) {
    (void)data;
    for (int i = 0; i < CRATE_CAPACITY; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(crate_slots_box[i]);
        gtk_style_context_remove_class(ctx, "slot-filled");
        gtk_style_context_add_class(ctx, "slot-empty");
        gtk_widget_queue_draw(crate_slots_box[i]); /* FIX: repaint trigger */
    }
    return G_SOURCE_REMOVE;
}
/* Updates truck label with total crates loaded count. */
static gboolean cb_update_truck(gpointer data) {
    TruckUpdate *u = (TruckUpdate *)data;
    char txt[64];
    snprintf(txt, sizeof(txt), "Truck: %d crate(s) loaded", u->count);
    gtk_label_set_text(GTK_LABEL(truck_label), txt);
    free(u);
    return G_SOURCE_REMOVE;
}
/* Updates status bar label with latest simulation state message. */
static gboolean cb_update_status(gpointer data) {
    StatusUpdate *u = (StatusUpdate *)data;
    gtk_label_set_text(GTK_LABEL(status_label), u->text);
    free(u->text);
    free(u);
    return G_SOURCE_REMOVE;
}
/* Renders final simulation summary to label and re-enables the start button. */
static gboolean cb_show_summary(gpointer data) {
    SummaryData *s = (SummaryData *)data;
    // average crate filling time
    double avg_crate_ms = crates_timed > 0
                    ? total_crate_time / crates_timed : 0;
    char buf[512];
    snprintf(buf, sizeof(buf),
        "Fruits on tree  : %d\n"
        "Fruits picked   : %d\n"
        "Crates on truck : %d\n"
        "Picker-1 picked : %d\n"
        "Picker-2 picked : %d\n"
        "Picker-3 picked : %d\n"
        "Correctness     : %s\n"
        "Fairness spread : %d fruit(s)\n"
        "Avg crate time  : %.2f ms",
        s->total_fruits, s->total_picked, s->crates,
        s->p[0], s->p[1], s->p[2],
        s->passed ? "PASSED" : "FAILED",
        s->spread, 
        avg_crate_ms);
    gtk_label_set_text(GTK_LABEL(summary_label), buf);
    gtk_widget_set_sensitive(start_btn, TRUE);
    free(s);
    return G_SOURCE_REMOVE;
}

/* ── Worker threads use these macros to safely update GUI from non-main threads ─── */
#define GUI_LOG(msg_str, pid) do {                          \
    LogMsg *_m = malloc(sizeof(LogMsg));                    \
    _m->msg = strdup(msg_str); _m->picker_id = (pid);      \
    g_idle_add(cb_append_log, _m); } while(0)

#define GUI_PICKER_BAR(pid, cnt) do {                       \
    PickerBarUpdate *_u = malloc(sizeof(PickerBarUpdate));  \
    _u->picker_id=(pid); _u->count=(cnt);                   \
    _u->total=tree_size;                                    \
    g_idle_add(cb_update_picker_bar, _u); } while(0)

#define GUI_CRATE_BAR(ns, nc) do {                          \
    CrateBarUpdate *_u = malloc(sizeof(CrateBarUpdate));    \
    _u->nslots=(ns); _u->ncid=(nc);                         \
    g_idle_add(cb_update_crate_bar, _u); } while(0)

#define GUI_SLOT(idx, filled_val) do {                      \
    SlotUpdate *_u = malloc(sizeof(SlotUpdate));            \
    _u->slot_idx=(idx); _u->fill=(filled_val);              \
    g_idle_add(cb_update_slot, _u); } while(0)

#define GUI_TRUCK(cnt) do {                                 \
    TruckUpdate *_u = malloc(sizeof(TruckUpdate));          \
    _u->count=(cnt);                                        \
    g_idle_add(cb_update_truck, _u); } while(0)

#define GUI_STATUS(txt) do {                                \
    StatusUpdate *_u = malloc(sizeof(StatusUpdate));        \
    _u->text = strdup(txt);                                 \
    g_idle_add(cb_update_status, _u); } while(0)

/* ═══════════════ PICKER THREAD ═══════════════ */
// 5
static void *picker_thread(void *arg) {
    int id = *(int *)arg;  // picker id (0-based)
    free(arg);             // free the allocated thread id

    char ts[32], msg[256];  

    get_time_str(ts, sizeof(ts));
    snprintf(msg, sizeof(msg), "%s Picker-%d  Started - heading to the tree.", ts, id+1);
    GUI_LOG(msg, id);
    ms_sleep(150);

    while (1) {
        /* CS-1: pick fruit */
        int fruit_val, fruit_idx, remaining; 

        // lock before checking tree state and picking fruit to ensure mutual exclusion and consistent view of shared state
        pthread_mutex_lock(&tree_mutex);
        if (tree_bare || next_fruit >= tree_size) { // tree bare condition check
            tree_bare = 1;
            pthread_mutex_unlock(&tree_mutex);
            break;  // loop exit
        }
        fruit_val        = tree[next_fruit]; // pick the next fruit value
        fruit_idx        = next_fruit; // for logging (1-based index)
        tree[next_fruit] = 0; // zero out — no copy back needed since we won't revisit this index
        next_fruit++;  // advance to next fruit for next picker
        remaining = tree_size - next_fruit;  // for logging thats shows remaining fruits on tree
        
        // unlock as soon as we have the fruit value and updated shared state, to minimize time spent in critical section and allow other pickers to access tree
        pthread_mutex_unlock(&tree_mutex);
        /* CS-1: ends */    
        ms_sleep(DELAY_PICK_MS);

        picker_count[id]++; // each picker ki productivity
        get_time_str(ts, sizeof(ts));
        snprintf(msg, sizeof(msg),
            "%s Picker-%d  Picked fruit #%d (val=%d) | remaining: %d",
            ts, id+1, fruit_idx+1, fruit_val, remaining);
        GUI_LOG(msg, id);
        GUI_PICKER_BAR(id, picker_count[id]);

        /* CS-2: place in crate */
        pthread_mutex_lock(&crate_mutex);
        
        // Wait for the crate to be open
        while (crate_state != CRATE_OPEN)
            pthread_cond_wait(&cond_crate_open, &crate_mutex);  // wait for loader to signal that crate is open

        crate_slots++;  // place fruit in crate (advance slot count)
        int slot_no    = crate_slots;  // for logging (1-based slot number)
        int this_crate = crate_id;  // for logging (current crate id)

        // for avg crate filling time: if this was the last slot to fill the crate, we set crate state to full and 
        // calculate elapsed time for this crate before unlocking, to ensure accurate timing and that loader sees 
        // the correct crate state when it wakes up
        if (slot_no == CRATE_CAPACITY) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            // Calculate elapsed time since we started filling this crate, and accumulate for average calculation later
            double elapsed = (now.tv_sec  - crate_start_time.tv_sec)  * 1000.0
                        + (now.tv_nsec - crate_start_time.tv_nsec) / 1e6;
            total_crate_time += elapsed; // accumulate total time for all crates
            crates_timed++; // increment count of crates for which we have timing data

            // ── state + signal ──
            crate_state = CRATE_FULL;
            GUI_SLOT(slot_no - 1, 1);
            GUI_CRATE_BAR(crate_slots, crate_id);
            GUI_STATUS("Crate FULL - loader working...");
            pthread_cond_signal(&cond_crate_full);  // loader ko signal

            // ── log before waiting ──
            get_time_str(ts, sizeof(ts));
            snprintf(msg, sizeof(msg),
                "%s Picker-%d  Crate-%d FULL! Signalling loader...",
                ts, id+1, this_crate);
            GUI_LOG(msg, id);

            // ── wait for new crate ──
            while (crate_state != CRATE_OPEN)
                pthread_cond_wait(&cond_crate_open, &crate_mutex);

            get_time_str(ts, sizeof(ts));
            snprintf(msg, sizeof(msg),
                "%s Picker-%d  New Crate-%d received. Resuming.", ts, id+1, crate_id);
            GUI_LOG(msg, id);

        } else {
            GUI_SLOT(slot_no - 1, 1);
            GUI_CRATE_BAR(crate_slots, crate_id);
        }

        pthread_mutex_unlock(&crate_mutex);
        ms_sleep(DELAY_PLACE_MS);

        // ── placed log (normal case) ──
        get_time_str(ts, sizeof(ts));
        snprintf(msg, sizeof(msg),
            "%s Picker-%d  Placed into Crate-%d | Slot %2d/%d filled.",
            ts, id+1, this_crate, slot_no, CRATE_CAPACITY);
        GUI_LOG(msg, id);

        ms_sleep(80);
        /* CS-2: ends */
    }
    // final lock when all pickers are done to safely update shared state and 
    // check if we are the last picker to finish
    pthread_mutex_lock(&crate_mutex);  
    pickers_done++;
    int all_done = (pickers_done == NUM_PICKERS);
    pthread_mutex_unlock(&crate_mutex); // unlock after updating shared state

    // Each picker done its individual log
    get_time_str(ts, sizeof(ts));
    snprintf(msg, sizeof(msg),
        "%s Picker-%d  Done. (Picked %d fruits)",
        ts, id+1, picker_count[id]);
    GUI_LOG(msg, id);

    // Sirf last picker tree bare announce kare
    if (all_done) {
        get_time_str(ts, sizeof(ts));
        snprintf(msg, sizeof(msg),
            "%s Picker-%d  Tree is now BARE. Signalling loader.",
            ts, id+1);
        GUI_LOG(msg, id);

        pthread_mutex_lock(&crate_mutex);  // lock before signaling to ensure loader sees the updated pickers_done and tree_bare state
        pthread_cond_signal(&cond_crate_full);   // loader ko
        pthread_cond_broadcast(&cond_crate_open); // pickers unblock
        pthread_mutex_unlock(&crate_mutex);  // unlock after signaling
    }
    return NULL;
}

/* ═════════════ LOADER THREAD ══════════════════ */
// 6
static void *loader_thread(void *arg) {
    (void)arg;  // 
    char ts[32], msg[256];

    get_time_str(ts, sizeof(ts));
    snprintf(msg, sizeof(msg), "%s Loader Started. Empty Crate-1 ready.", ts);
    GUI_LOG(msg, 99);

    // Loader thread starts with an empty crate already available, 
    // so we just need to set the start time for crate filling
    pthread_mutex_lock(&crate_mutex);  

    while (1) {
        while (crate_state != CRATE_FULL && pickers_done < NUM_PICKERS)
            // wait for either condition: crate full signal from pickers OR all pickers done signal (tree bare)
            pthread_cond_wait(&cond_crate_full, &crate_mutex);

        /* Partial crate at end OR tree bare condition */
        if (pickers_done == NUM_PICKERS && crate_state != CRATE_FULL) {
            int partial = crate_slots; // current filled slots of partial crate (if any)
            int cid     = crate_id; // for logging before we reset crate state for new crate
            pthread_mutex_unlock(&crate_mutex); // unlock if the above condition is true to allow pickers to exit if they're still waiting

            if (partial > 0) {
                get_time_str(ts, sizeof(ts));
                snprintf(msg, sizeof(msg),
                    "%s Loader    All pickers done. Loading PARTIAL Crate-%d (%d/%d)...",
                    ts, cid, partial, CRATE_CAPACITY);
                GUI_LOG(msg, 99);
                GUI_STATUS("Loading partial crate...");
                ms_sleep(DELAY_LOAD_MS);
                truck_crates++;
                GUI_TRUCK(truck_crates);
                get_time_str(ts, sizeof(ts));
                snprintf(msg, sizeof(msg),
                    "%s Loader    Partial Crate-%d on truck. Total: %d crate(s).",
                    ts, cid, truck_crates);
                GUI_LOG(msg, 99);
            }
            break;
        }

        /* Full crate */
        int full_crate = crate_id;  // for logging before we reset crate state for new crate
        crate_state    = CRATE_SWAPPING;  
        // unlock before simulating loading time to allow GUI updates and 
        // avoid blocking pickers unnecessarily while loader is "busy"
        pthread_mutex_unlock(&crate_mutex);

        get_time_str(ts, sizeof(ts));
        snprintf(msg, sizeof(msg),
            "%s Loader    Loading FULL Crate-%d onto truck...", ts, full_crate);
        GUI_LOG(msg, 99);
        GUI_STATUS("Loader: loading full crate onto truck...");

        ms_sleep(DELAY_LOAD_MS);

        pthread_mutex_lock(&crate_mutex); // lock to update shared state after loading simulation
        truck_crates++;
        pthread_mutex_unlock(&crate_mutex); // unlock after updating shared state to allow pickers to proceed with next crate 

        GUI_TRUCK(truck_crates);

        get_time_str(ts, sizeof(ts));
        snprintf(msg, sizeof(msg),
            "%s Loader    Crate-%d loaded. Truck total: %d crate(s).",
            ts, full_crate, truck_crates);
        GUI_LOG(msg, 99);

        GUI_STATUS("Loader: fetching new empty crate...");
        ms_sleep(DELAY_NEW_CRATE_MS);

        g_idle_add(cb_reset_slots, NULL);   /* clear slot visuals */

        // lock to update crate state for new crate and signal pickers that new crate is open, 
        // but unlock before logging to allow GUI updates and avoid blocking pickers unnecessarily
        pthread_mutex_lock(&crate_mutex); 
        crate_slots = 0;
        crate_id++;
        crate_state = CRATE_OPEN;

        pthread_cond_broadcast(&cond_crate_open); // signal all waiting pickers that new crate is open
        clock_gettime(CLOCK_MONOTONIC, &crate_start_time); // start time for filling new crate

        GUI_CRATE_BAR(0, crate_id); // reset crate bar for new crate

        get_time_str(ts, sizeof(ts));
        snprintf(msg, sizeof(msg),
            "%s Loader    New empty Crate-%d provided.", ts, crate_id);
        GUI_LOG(msg, 99);
        GUI_STATUS("Pickers working...");
    }

    get_time_str(ts, sizeof(ts));
    snprintf(msg, sizeof(msg), "%s Loader    All done. Exiting.", ts);
    GUI_LOG(msg, 99);
    GUI_STATUS("Simulation complete!");

    /* Build and post summary */
    int total = 0;
    for (int i = 0; i < NUM_PICKERS; i++) total += picker_count[i];
    int mx = picker_count[0], mn = picker_count[0];
    for (int i = 1; i < NUM_PICKERS; i++) {
        if (picker_count[i] > mx) mx = picker_count[i];
        if (picker_count[i] < mn) mn = picker_count[i];
    }
    SummaryData *s = malloc(sizeof(SummaryData));
    s->total_fruits = tree_size;
    s->total_picked = total;
    s->crates       = truck_crates;
    for (int i = 0; i < NUM_PICKERS; i++) s->p[i] = picker_count[i];
    s->passed = (total == tree_size); // Correctness check
    s->spread = mx - mn; // fairness metric
    g_idle_add(cb_show_summary, s);

    return NULL;
}

/* ── Simulation runner thread ────────────────────────────── */
typedef struct { int n; } SimArgs;
// here n is no. of fruits on the tree

// Resets all shared simulation state to initial values for a new run
// 7 (only if we want to run again the window)
static void reset_sim_state(int n) {
    free(tree); // free previous tree if any
    tree         = malloc(n * sizeof(int)); // Tree globally shared — no zero copy 
    tree_size    = n;
    next_fruit   = 0;
    tree_bare    = 0;
    crate_state  = CRATE_OPEN;
    crate_slots  = 0;
    crate_id     = 1;
    truck_crates = 0;
    pickers_done = 0;
    total_crate_time = 0;
    crates_timed     = 0;
    for (int i = 0; i < NUM_PICKERS; i++) picker_count[i] = 0;
    srand((unsigned)time(NULL));
    for (int i = 0; i < n; i++) tree[i] = rand() % 100 + 1;
}

// Main simulation runner function that initializes state, spawns threads, and waits for completion
// 4
static void *sim_runner(void *arg) {
    SimArgs *sa = (SimArgs *)arg; // unpack arguments
    int n = sa->n; // number of fruits to pick (tree size)
    free(sa);

    reset_sim_state(n);

    pthread_t pickers[NUM_PICKERS], loader;
    for (int i = 0; i < NUM_PICKERS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        pthread_create(&pickers[i], NULL, picker_thread, id); // pickers created
    }
    pthread_create(&loader, NULL, loader_thread, NULL); // loader created

    for (int i = 0; i < NUM_PICKERS; i++) pthread_join(pickers[i], NULL);
    pthread_join(loader, NULL);

    free(tree);
    tree = NULL;
    return NULL;
}

/* ── Start button callback ───────────────────────────────── */
// 3
static void on_start_clicked(GtkWidget *btn, gpointer data) {
    (void)data;
    const char *txt = gtk_entry_get_text(GTK_ENTRY(entry_fruits));
    int n = atoi(txt);
    if (n <= 0) {
        gtk_label_set_text(GTK_LABEL(status_label),
                           "Please enter a valid positive number!");
        return;
    }

    gtk_text_buffer_set_text(log_buf, "", -1);
    gtk_label_set_text(GTK_LABEL(summary_label), "");
    gtk_label_set_text(GTK_LABEL(truck_label), "Truck: 0 crate(s) loaded");

    for (int i = 0; i < NUM_PICKERS; i++) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bars[i]), 0.0);
        char t[64];
        snprintf(t, sizeof(t), "%s: 0 fruits", PICKER_NAME[i]);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bars[i]), t);
    }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(crate_bar), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(crate_bar), "Crate-1: 0/12");

    for (int i = 0; i < CRATE_CAPACITY; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(crate_slots_box[i]);
        gtk_style_context_remove_class(ctx, "slot-filled");
        gtk_style_context_add_class(ctx, "slot-empty");
        gtk_widget_queue_draw(crate_slots_box[i]); /* repaint on reset */
    }

    gtk_widget_set_sensitive(btn, FALSE);

    // start simulation in a separate thread to avoid blocking the GUI and allow real-time updates
    SimArgs *sa = malloc(sizeof(SimArgs)); // allocate arguments for sim runner
    sa->n = n;
    pthread_t t;
    pthread_create(&t, NULL, sim_runner, sa); // spawn sim runner thread (here: pickers and loaders are created)
    pthread_detach(t); // bcz we want make GUI responsive not freeeze & it will clean up after itself when done
}

/* ────────────── Build GUI Window ──────────────────────── */
// 2
static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background-color: #0f172a; }"
        "label  { color: #e2e8f0; font-family: 'Courier New', monospace; }"
        ".title { color: #f8fafc; font-size: 18px; font-weight: bold;"
        "         font-family: 'Courier New', monospace; }"
        ".sub   { color: #94a3b8; font-size: 11px;"
        "         font-family: 'Courier New', monospace; }"
        ".sec   { color: #7dd3fc; font-size: 15px; font-weight: bold;"
        "         font-family: 'Courier New', monospace; }"
        ".stat  { color: #86efac; font-size: 12px; font-weight: bold;"
        "         font-family: 'Courier New', monospace; }"
        ".sum   { color: #fde68a; font-size: 15px;"
        "         font-family: 'Courier New', monospace; }"
        ".truck { color: #fb923c; font-size: 13px; font-weight: bold;"
        "         font-family: 'Courier New', monospace; }"
        "textview { background-color: #0a0f1e; color: #e2e8f0;"
        "           font-family: 'Courier New', monospace; font-size: 13.5px; }"
        "textview text { background-color: #0a0f1e; }"
        "entry  { background-color: #1e293b; color: #f8fafc;"
        "         font-family: 'Courier New', monospace;"
        "         border: 1px solid #334155; border-radius: 4px;"
        "         padding: 4px 8px; }"
        "button { background: #1d4ed8; color: #f8fafc;"
        "         font-family: 'Courier New', monospace; font-weight: bold;"
        "         border: none; border-radius: 6px; padding: 6px 18px; }"
        "button:hover    { background: #2563eb; }"
        "button:disabled { background: #334155; color: #64748b; }"
        "progressbar trough  { background-color: #1e293b; min-height: 16px;"
        "                       border-radius: 4px; }"
        "progressbar progress{ background-color: #3b82f6; border-radius: 7px; }"
        "progressbar text    { color: #f8fafc; font-size: 13px;"
        "                       font-family: 'Courier New', monospace; }"
        ".slot-empty  { background-color: #1e293b; border: 1px solid #334155;"
        "               border-radius: 3px; min-width: 26px; min-height: 26px; }"
        ".slot-filled { background-color: #16a34a; border: 1px solid #22c55e;"
        "               border-radius: 3px; min-width: 26px; min-height: 26px; }"
        ".panel { background-color: #1e293b; border: 1px solid #334155;"
        "         border-radius: 8px; padding: 10px; }",
        -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "Spring Workers — Fruit Picking Simulation");
    gtk_window_set_default_size(GTK_WINDOW(win), 960, 680);
    gtk_container_set_border_width(GTK_CONTAINER(win), 14);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(win), root);

    /* Header */
    GtkWidget *title = gtk_label_new("SPRING WORKERS — Fruit Picking Simulation");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "title");
    gtk_box_pack_start(GTK_BOX(root), title, FALSE, FALSE, 0);

    GtkWidget *sub = gtk_label_new("3 Pickers + 1 Loader | POSIX Threads | Two-Mutex Design");
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "sub");
    gtk_box_pack_start(GTK_BOX(root), sub, FALSE, FALSE, 0);

    /* Input row */
    GtkWidget *irow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(root), irow, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(irow), gtk_label_new("Number of fruits:"), FALSE, FALSE, 0);

    entry_fruits = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry_fruits), "20");
    gtk_entry_set_width_chars(GTK_ENTRY(entry_fruits), 6);
    gtk_box_pack_start(GTK_BOX(irow), entry_fruits, FALSE, FALSE, 0);

    start_btn = gtk_button_new_with_label("Start Simulation");
    g_signal_connect(start_btn, "clicked", G_CALLBACK(on_start_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(irow), start_btn, FALSE, FALSE, 0);

    status_label = gtk_label_new("Enter fruit count and press Start.");
    gtk_style_context_add_class(gtk_widget_get_style_context(status_label), "stat");
    gtk_box_pack_start(GTK_BOX(irow), status_label, FALSE, FALSE, 8);

    /* Main content */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(root), hbox, TRUE, TRUE, 0);

    /* LEFT: log */
    GtkWidget *lpanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(lpanel), "panel");
    gtk_box_pack_start(GTK_BOX(hbox), lpanel, TRUE, TRUE, 0);

    GtkWidget *llbl = gtk_label_new("Activity Log");
    gtk_style_context_add_class(gtk_widget_get_style_context(llbl), "sec");
    gtk_widget_set_halign(llbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(lpanel), llbl, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(lpanel), scroll, TRUE, TRUE, 0);

    log_view = gtk_text_view_new();
    log_buf  = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(log_view), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(log_view), 6);
    gtk_container_add(GTK_CONTAINER(scroll), log_view);

    /* RIGHT: stats */
    GtkWidget *rpanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(rpanel), "panel");
    gtk_widget_set_size_request(rpanel, 270, -1);
    gtk_box_pack_start(GTK_BOX(hbox), rpanel, FALSE, FALSE, 0);

    GtkWidget *plbl = gtk_label_new("Picker Progress");
    gtk_style_context_add_class(gtk_widget_get_style_context(plbl), "sec");
    gtk_widget_set_halign(plbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(rpanel), plbl, FALSE, FALSE, 0);

    for (int i = 0; i < NUM_PICKERS; i++) {
        progress_bars[i] = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bars[i]), TRUE);
        char t[64];
        snprintf(t, sizeof(t), "%s: 0 fruits", PICKER_NAME[i]);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bars[i]), t);
        gtk_box_pack_start(GTK_BOX(rpanel), progress_bars[i], FALSE, FALSE, 2);
    }

    gtk_box_pack_start(GTK_BOX(rpanel),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *clbl = gtk_label_new("Current Crate");
    gtk_style_context_add_class(gtk_widget_get_style_context(clbl), "sec");
    gtk_widget_set_halign(clbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(rpanel), clbl, FALSE, FALSE, 0);

    crate_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(crate_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(crate_bar), "Crate-1: 0/12");
    gtk_box_pack_start(GTK_BOX(rpanel), crate_bar, FALSE, FALSE, 2);

    /* Slot grid 4x3 */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_box_pack_start(GTK_BOX(rpanel), grid, FALSE, FALSE, 4);

    for (int i = 0; i < CRATE_CAPACITY; i++) {
        crate_slots_box[i] = gtk_event_box_new();
        gtk_widget_set_size_request(crate_slots_box[i], 28, 28);
        GtkStyleContext *ctx = gtk_widget_get_style_context(crate_slots_box[i]);
        gtk_style_context_add_class(ctx, "slot-empty");
        gtk_grid_attach(GTK_GRID(grid), crate_slots_box[i], i % 3, i / 3, 1, 1);
    }

    gtk_box_pack_start(GTK_BOX(rpanel),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    truck_label = gtk_label_new("Truck: 0 crate(s) loaded");
    gtk_style_context_add_class(gtk_widget_get_style_context(truck_label), "truck");
    gtk_widget_set_halign(truck_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(rpanel), truck_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(rpanel),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *slbl = gtk_label_new("Summary");
    gtk_style_context_add_class(gtk_widget_get_style_context(slbl), "sec");
    gtk_widget_set_halign(slbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(rpanel), slbl, FALSE, FALSE, 0);

    summary_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(summary_label), "sum");
    gtk_widget_set_halign(summary_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(summary_label), TRUE);
    gtk_box_pack_start(GTK_BOX(rpanel), summary_label, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
}

/* ═════════════════════════ MAIN ════════════════════════════ */
// 1 
int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint); // For fault tolerance and graceful exit on Ctrl+C

    GtkApplication *app = gtk_application_new(
        "com.springworkers.sim", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}