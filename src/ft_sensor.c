#define _GNU_SOURCE

#include <ecrt.h>
#include <stdio.h>
#include <unistd.h>      // for usleep
#include <linux/sched.h>
#include <sys/syscall.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <string.h>      // für memset, CPU_ZERO, CPU_SET
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "../include/ns.h"

#define ATI_VENDOR_ID    0x00000732
#define ATI_PRODUCT_CODE 0x26483052 // CHECK AGAIN 0X26483052

ssize_t hist_size = 1000;
ssize_t hist_loop_size = 2000;

unsigned long long hist[1000] = {0};
unsigned long long hist_loop[2000] = {0};

// char *time_stamp(){

//     char *timestamp = (char *)malloc(sizeof(char) * 16);
//     time_t ltime;
//     ltime=time(NULL);
//     struct tm *tm;
//     tm=localtime(&ltime);
    
//     sprintf(timestamp,"%02d.%02d.%04d %02d:%02d:%02d", 
//         tm->tm_mday, tm->tm_mon, tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec);
//     return timestamp;
// }


// Für gettid: Falls nicht im Header definiert, als Wrapper verwenden.
pid_t gettid() {
    return (pid_t)syscall(SYS_gettid);
}

void set_CPU(size_t cpu)
{
    cpu_set_t mask;
    CPU_ZERO(&mask);

    CPU_SET(cpu, &mask);

    // pid = 0 means "calling process"
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
    {
        perror("Error setting CPU affinity");
    }
}

void set_realtime_priority(int priority)
{
    struct sched_param schedParam;
    schedParam.sched_priority = priority;

    // Set the thread to real-time FIFO scheduling
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &schedParam) != 0)
    {
        perror("Failed to set real-time priority");
        exit(EXIT_FAILURE);
    }
}

void sigxcpu_handler(int signum) {
    // Use thread-local storage or other mechanisms to identify the thread
    // For example, using pthread_self()
    printf("Runtime overrun detected: Task exceeded allocated runtime");
}

void setup_signal_handler() {
    struct sigaction sa;
    sa.sa_handler = sigxcpu_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGXCPU, &sa, NULL) != 0) {
        perror("Failed to set SIGXCPU handler");
        exit(EXIT_FAILURE);
    }
}

struct sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

void set_realtime_deadline(unsigned long runtime, unsigned long deadline, unsigned long period)
{
    struct sched_attr attr;
    int ret;

    // Zero out the structure
    memset(&attr, 0, sizeof(attr));

    // Set the scheduling policy to SCHED_DEADLINE
    attr.size = sizeof(attr);
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = runtime;
    attr.sched_deadline = deadline;
    attr.sched_period = period;

    // Enable Overrun detection
    attr.sched_flags |= SCHED_FLAG_DL_OVERRUN;

    // Use syscall to set the scheduling policy and parameters
    ret = syscall(SYS_sched_setattr, gettid(), &attr, 0);
    if (ret != 0)
    {
        perror("Failed to set SCHED_DEADLINE");
        exit(EXIT_FAILURE);
    }
}

void add_to_hist(unsigned long long* hist, size_t hist_size, uint64_t t, double res) {
    int idx = (int)(t / res);
    if (idx >= 0 && idx < hist_size) {
        hist[idx]++;
    }
    else {
        // printf("Time outside Histogram size\nHistogram size: %ld\n%s,%ld\n", hist_size, time_stamp(), t);
        printf("Time outside Histogram size\nHistogram size: %ld\ntime: %ld\n", hist_size, t);
    }
}

int32_t fx_raw;
int32_t fy_raw;
int32_t fz_raw;
int32_t tx_raw;
int32_t ty_raw;
int32_t tz_raw;

void *main_loop() {

    set_CPU(0);

    ec_master_t       *master   = NULL;
    ec_domain_t       *domain1  = NULL;
    uint8_t           *domain1_pd = NULL;  // Pointer auf Prozessdaten

    master = ecrt_request_master(0);
    if (!master) {
        fprintf(stderr, "EtherCAT Master nicht verfügbar\n");
        return NULL;
    }
    domain1 = ecrt_master_create_domain(master);
    if (!domain1) {
        fprintf(stderr, "Konnte Domain nicht anlegen\n");
        return NULL;
    }

    ec_slave_config_t *sc;
    sc = ecrt_master_slave_config(master, 0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE);
    if (!sc) {
        fprintf(stderr, "Slave nicht gefunden (Vendor/Product mismatch)\n");
        return NULL;
    }

    // PDO-Mapping konfigurieren (Sync-Manager 2 und 3 laut PDO-Definition)
    ec_pdo_entry_info_t slave_pdo_entries[] = {
        {0x7010, 0x01, 32}, // Control 1 (DINT)
        {0x7010, 0x02, 32}, // Control 2 (DINT)
        {0x6000, 0x01, 32}, // Fx
        {0x6000, 0x02, 32}, // Fy
        {0x6000, 0x03, 32}, // Fz
        {0x6000, 0x04, 32}, // Tx
        {0x6000, 0x05, 32}, // Ty
        {0x6000, 0x06, 32}, // Tz
        {0x6010, 0x00, 32}, // Status Code
        {0x6020, 0x00, 32}  // Sample Counter
    };
    ec_pdo_info_t slave_pdos[] = {
        {0x1601, 2, slave_pdo_entries + 0},  // 2 Outputs (Control1, Control2)
        {0x1A00, 8, slave_pdo_entries + 2}   // 8 Inputs  (Fx..Tz, Status, Counter)
    };
    ec_sync_info_t slave_syncs[] = {
        {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},      // SM0 (Mailbox) – unused
        {1, EC_DIR_INPUT,  0, NULL, EC_WD_DISABLE},      // SM1 (Mailbox) – unused
        {2, EC_DIR_OUTPUT, 1, slave_pdos + 0, EC_WD_ENABLE},  // SM2 Outputs
        {3, EC_DIR_INPUT,  1, slave_pdos + 1, EC_WD_DISABLE}, // SM3 Inputs
        {0xFF}
    };
    if (ecrt_slave_config_pdos(sc, EC_END, slave_syncs)) {
        fprintf(stderr, "PDO-Konfiguration fehlgeschlagen\n");
        return NULL;
    }

    // PDO-Entries in der Domain registrieren und Offsets bestimmen
    unsigned int off_fx, off_fy, off_fz, off_tx, off_ty, off_tz, off_status, off_counter;
    ec_pdo_entry_reg_t domain_regs[] = {
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6000, 0x01, &off_fx},
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6000, 0x02, &off_fy},
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6000, 0x03, &off_fz},
    uint64_t start = ns();
    uint64_t start_before;
    uint64_t end;
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6000, 0x04, &off_tx},
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6000, 0x05, &off_ty},
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6000, 0x06, &off_tz},
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6010, 0x00, &off_status},
        {0, 0, ATI_VENDOR_ID, ATI_PRODUCT_CODE, 0x6020, 0x00, &off_counter},
        {}
    };
    if (ecrt_domain_reg_pdo_entry_list(domain1, domain_regs)) {
        fprintf(stderr, "PDO Entry Registrierung fehlgeschlagen\n");
        return NULL;
    }

    // Master in OP nehmen
    if (ecrt_master_activate(master)) {
        fprintf(stderr, "Master konnte nicht aktiviert werden\n");
        return NULL;
    }
    if (!(domain1_pd = ecrt_domain_data(domain1))) {
        fprintf(stderr, "Prozessabbild konnte nicht abgefragt werden\n");
        return NULL;
    }

    unsigned long runtime =     1000 * 1000;
    unsigned long deadline =    1000 * 1000;
    unsigned long period =      1000 * 1000;

    setup_signal_handler();
    set_realtime_deadline(runtime, deadline, period);

    uint64_t start = ns();
    uint64_t start_before;
    uint64_t end;
    // Zyklus: alle 100 ms Messwerte abrufen und ausgeben
    while (1) {

        // struct timespec start, end;

        // clock_gettime(CLOCK_MONOTONIC, &start);

        start_before = start;
        start = ns();
        double loop_time1 = (start - start_before) / 1000.0;
        add_to_hist(hist_loop, hist_loop_size, loop_time1, 1);

        // Prozessdaten-Austausch
        ecrt_master_receive(master);
        ecrt_domain_process(domain1);
        
        // Rohwerte lesen
        fx_raw = EC_READ_S32(domain1_pd + off_fx);
        fy_raw = EC_READ_S32(domain1_pd + off_fy);
        fz_raw = EC_READ_S32(domain1_pd + off_fz);
        tx_raw = EC_READ_S32(domain1_pd + off_tx);
        ty_raw = EC_READ_S32(domain1_pd + off_ty);
        tz_raw = EC_READ_S32(domain1_pd + off_tz);
        
        // (Status und Counter könnten hier ebenfalls gelesen/überwacht werden)
        
        // Optional: Umrechnung in N/Nm
        // double fx = fx_raw / 1000000.0;
        // double fy = fy_raw / 1000000.0;
        // double fz = fz_raw / 1000000.0;
        // double tx = tx_raw / 1000000.0;
        // double ty = ty_raw / 1000000.0;
        // double tz = tz_raw / 1000000.0;
        
        // Ausgabe der Werte
        // printf("F/T raw: Fx=%d, Fy=%d, Fz=%d, Tx=%d, Ty=%d, Tz=%d\n",
        //     fx_raw, fy_raw, fz_raw, tx_raw, ty_raw, tz_raw);
        // printf("F/T (SI): Fx=%.3f N, Fy=%.3f N, Fz=%.3f N, Tx=%.3f Nm, Ty=%.3f Nm, Tz=%.3f Nm\n",
        //        fx, fy, fz, tx, ty, tz);
        
        ecrt_domain_queue(domain1);
        ecrt_master_send(master);

        // clock_gettime(CLOCK_MONOTONIC, &end);

        end = ns();

        // double loop_time = 1.0e9 * (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000.0;
        double loop_time2 = (end - start) / 1000.0;
        add_to_hist(hist, hist_size, loop_time2, 1);

        sched_yield();
        
        
    }
}

void *lb_send() {
    // send data via loopback and show data live in a python script
    set_CPU(1);
    set_realtime_priority(99);

    //open server socket
    struct sockaddr_in clientaddr;

    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation error");
        return NULL;
    }
    memset(&clientaddr, 0, sizeof(clientaddr));
    clientaddr.sin_family = AF_INET;
    clientaddr.sin_port = htons(6008);
    clientaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    while (1) {
        // send fx, fy, fz, tx, ty, tz via loopback
        char buffer[1024];
        sprintf(buffer, "%d,%d,%d,%d,%d,%d", fx_raw, fy_raw, fz_raw, tx_raw, ty_raw, tz_raw);
        ssize_t bytes = 0;
        bytes = sendto(sock, buffer, strlen(buffer), 0, (struct sockaddr *)&clientaddr, sizeof(clientaddr));
        if (bytes < 0) {
            perror("Error in sending data");
        }

        usleep(1000);
    }
    return NULL;
}

// Struktur, um Argumente für den CSV-Thread zu bündeln
typedef struct {
    FILE* csv;
    unsigned long long* hist;
    size_t hist_size;
} HistToCSVArgs;

// Thread-Funktion, die in regelmäßigen Abständen den Histogramm-Inhalt in die CSV-Datei schreibt
void* hist_to_csv(void* arg) {
    HistToCSVArgs* args = (HistToCSVArgs*) arg;
    FILE* csv = args->csv;
    unsigned long long* hist = args->hist;
    size_t hist_size = args->hist_size;

    set_CPU(1);
    set_realtime_priority(99);

    while (1) {
        // Schreibe alle Histogrammwerte in eine Zeile (CSV-Format)
        for (size_t i = 0; i < hist_size; i++) {
            fprintf(csv, "%llu", hist[i]);
            if (i < hist_size - 1) {
                fprintf(csv, ",");
            }
        }
        fprintf(csv, "\n");
        fflush(csv);

        // 300 Sekunden warten
        sleep(300);
    }
    return NULL;
}

int main() {
    pthread_t ec_thread, lb_thread;
    int ret;

    ret = pthread_create(&ec_thread, NULL, main_loop, NULL);
    if (ret != 0) {
        fprintf(stderr, "Fehler beim Erstellen des main_loop Threads: %d\n", ret);
        exit(EXIT_FAILURE);
    }

    ret = pthread_create(&lb_thread, NULL, lb_send, NULL);
    if (ret != 0) {
        fprintf(stderr, "Fehler beim Erstellen des main_loop Threads: %d\n", ret);
        exit(EXIT_FAILURE);
    }

    const char* filename1 = "/home/urc/ethercat_example/analysis/04_04_1/hist.csv";
    FILE* hist_csv = fopen(filename1, "w");

    pthread_t hist_thread1;
    HistToCSVArgs args1;
    args1.csv = hist_csv;
    args1.hist = hist;
    args1.hist_size = hist_size;
    if (pthread_create(&hist_thread1, NULL, hist_to_csv, &args1) != 0) {
        fprintf(stderr, "Error creating thread\n");
        fclose(hist_csv);
        return EXIT_FAILURE;
    }

    const char* filename2 = "/home/urc/ethercat_example/analysis/04_04_1/hist_loop.csv";
    FILE* hist_loop_csv = fopen(filename2, "w");

    pthread_t hist_thread2;
    HistToCSVArgs args2;
    args2.csv = hist_loop_csv;
    args2.hist = hist_loop;
    args2.hist_size = hist_loop_size;
    if (pthread_create(&hist_thread2, NULL, hist_to_csv, &args2) != 0) {
        fprintf(stderr, "Error creating thread\n");
        fclose(hist_loop_csv);
        return EXIT_FAILURE;
    }

    pthread_join(ec_thread, NULL);
    pthread_join(lb_thread, NULL);
    pthread_join(hist_thread1, NULL);
    pthread_join(hist_thread2, NULL);

    fclose(hist_csv);
    fclose(hist_loop_csv);

    return 0;
}

