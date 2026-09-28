#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_JOBS 100

// Variables globales para pmon y jobs
volatile sig_atomic_t pmon_refresh = 0;
volatile sig_atomic_t pmon_exit = 0;

typedef struct {
    pid_t pid;
    char comando[256];
    int activo;
} Job;

Job tabla_jobs[MAX_JOBS];
int total_jobs = 0;
int contador_jobs = 1;

// Manejador asincrono para limpiar procesos Zombie
void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < total_jobs; i++) {
            if (tabla_jobs[i].pid == pid) {
                tabla_jobs[i].activo = 0; // Marcarlo como terminado para pmon
                break;
            }
        }
        char msg[60];
        int len = snprintf(msg, sizeof(msg), "\n[Background job terminado: PID %d]\n", pid);
        write(STDOUT_FILENO, msg, len);
    }
}

// Señales para pmon
void manejador_alerta(int signum) {
    (void)signum;
    pmon_refresh = 1;
}

void manejador_interrupcionPMON(int signal) {
    (void)signal;
    pmon_exit = 1;
}

void ejecutar_pmon(char **args);
int main(void) {
    char directorio[1024];
    char linea[1024];
    char linea_original[1024];
    char *args[64];

    // 1) La shell ignora ctrl + c y ctrl + 
    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_ignore, NULL);
    sigaction(SIGQUIT, &sa_ignore, NULL);

    // 2) Activar el recolector de hijos (zombies)
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    while (1) {
        getcwd(directorio, sizeof(directorio));
        printf("miSHell:%s$ ", directorio);
        fflush(stdout);

        if (fgets(linea, sizeof(linea), stdin) == NULL) {
            printf("\n");
            break;
        }

        strncpy(linea_original, linea, sizeof(linea_original) - 1);
        linea_original[sizeof(linea_original) - 1] = '\0';

        int argc = 0;
        char *token = strtok(linea, " \n");
        while (token != NULL && argc < 63) {
            args[argc++] = token;
            token = strtok(NULL, " \n");
        }
        args[argc] = NULL;

        if (argc == 0) continue;

        // Comandos Built-in (R2)
        if (strcmp(args[0], "exit") == 0) break;
        if (strcmp(args[0], "cd") == 0) {
            if (argc < 2) {
                fprintf(stderr, "cd: falta el argumento\n");
            } else {
                if (chdir(args[1]) != 0) perror("cd");
            }
            continue;
        }
        if (strcmp(args[0], "pmon") == 0) {
            ejecutar_pmon(args);
            continue;
        }
        if (strcmp(args[0], "jobs") == 0) {
            for(int i = 0; i < total_jobs; i++){
                if(tabla_jobs[i].activo) {
                    printf("[%d] Ejecutando PID: %d\n", i+1, tabla_jobs[i].pid);
                }
            }
            continue;
        }

        // Detección de Background (&)
        int es_background = 0;
        if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
            es_background = 1;
            args[argc - 1] = NULL;
            argc--;
        }
        
        if (argc == 0) continue;

        // Variables para pipes y redirecciones
        char *comando[64];
        int inicio = 0;
        int entrada_anterior = -1;
        pid_t ultimo_pid = -1;

        for (int k = 0; k <= argc; k++) {
            if (args[k] != NULL && strcmp(args[k], "|") != 0) continue;

            int es_ultimo = (args[k] == NULL);
            int largo = k - inicio;

            if (largo == 0) {
                fprintf(stderr, "Error de sintaxis\n");
                if (entrada_anterior != -1) close(entrada_anterior);
                break;
            }

            for (int m = 0; m < largo; m++) {
                comando[m] = args[inicio + m];
            }
            comando[largo] = NULL;
            inicio = k + 1;

            char *infile = NULL;
            char *outfile = NULL;
            int append = 0;

            int i = 0, j = 0;
            while (comando[i] != NULL) {
                if (strcmp(comando[i], "<") == 0 && comando[i+1] != NULL) {
                    infile = comando[i+1];
                    i += 2;
                } else if (strcmp(comando[i], ">") == 0 && comando[i+1] != NULL) {
                    outfile = comando[i+1];
                    append = 0;
                    i += 2;
                } else if (strcmp(comando[i], ">>") == 0 && comando[i+1] != NULL) {
                    outfile = comando[i+1];
                    append = 1;
                    i += 2;
                } else {
                    comando[j++] = comando[i++];
                }
            }
            comando[j] = NULL;

            if (comando[0] == NULL) {
                fprintf(stderr, "Error de sintaxis\n");
                if (entrada_anterior != -1) close(entrada_anterior);
                break;
            }

            int tuberia[2];
            if (!es_ultimo) {
                if (pipe(tuberia) < 0) {
                    perror("pipe");
                    break;
                }
            }

            pid_t pid = fork();

            if (pid < 0) {
                perror("fork");
                break;
            } else if (pid == 0) {
                // Restaurar Ctrl+C solo si NO es background
                if (!es_background) {
                    struct sigaction sa_default;
                    sa_default.sa_handler = SIG_DFL;
                    sigemptyset(&sa_default.sa_mask);
                    sa_default.sa_flags = 0;
                    sigaction(SIGINT, &sa_default, NULL);
                    sigaction(SIGQUIT, &sa_default, NULL);
                }

                if (entrada_anterior != -1) {
                    dup2(entrada_anterior, 0);
                    close(entrada_anterior);
                } else if (infile) {
                    int fd = open(infile, O_RDONLY);
                    if (fd < 0) { perror("open"); exit(EXIT_FAILURE); }
                    dup2(fd, 0);
                    close(fd);
                }

                if (!es_ultimo) {
                    close(tuberia[0]);
                    dup2(tuberia[1], 1);
                    close(tuberia[1]);
                } else if (outfile) {
                    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
                    int fd = open(outfile, flags, 0644);
                    if (fd < 0) { perror("open"); exit(EXIT_FAILURE); }
                    dup2(fd, 1);
                    close(fd);
                }

                if (execvp(comando[0], comando) == -1) {
                    perror("execvp");
                    exit(EXIT_FAILURE);
                }
            } else {
                ultimo_pid = pid; // Guardamos el pid para registrarlo
                if (entrada_anterior != -1) close(entrada_anterior);
                if (!es_ultimo) {
                    close(tuberia[1]);
                    entrada_anterior = tuberia[0];
                }
            }
        }

        // Manejo del padre al finalizar la linea de comandos
        if (es_background && ultimo_pid != -1) {
            tabla_jobs[total_jobs].pid = ultimo_pid;
            strncpy(tabla_jobs[total_jobs].comando, args[0], 255);
            tabla_jobs[total_jobs].activo = 1;
            printf("[%d] %d\n", contador_jobs++, ultimo_pid);
            total_jobs++;
        } else if (!es_background) {
            // Si es foreground, espera a todos los hijos (tuberia completa)
            while (wait(NULL) > 0);
        }
    }
    return 0;
}

void ejecutar_pmon(char **args) {
    int intervalo = 5;
    if (args[1] != NULL) {
        intervalo = atoi(args[1]);
        if (intervalo <= 0) intervalo = 5;
    }

    struct sigaction sa_alarm, sa_int, old_int;
    
    sa_alarm.sa_handler = manejador_alerta;
    sigemptyset(&sa_alarm.sa_mask);
    sa_alarm.sa_flags = 0;
    sigaction(SIGALRM, &sa_alarm, NULL);

    sa_int.sa_handler = manejador_interrupcionPMON;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, &old_int);

    pmon_exit = 0;
    long hertz = sysconf(_SC_CLK_TCK);
    unsigned long cpu_ticks_previos[MAX_JOBS] = {0};

    while (!pmon_exit) {
        printf("\e[1;1H\e[2J"); // Limpiador de pantalla
        printf("PID\t| COMANDO\t| ESTADO\t| %%CPU (aprox)\t| RSS (KB)\n");

        for (int i = 0; i < total_jobs; i++) {
            if (tabla_jobs[i].activo == 0) continue;

            pid_t pid = tabla_jobs[i].pid;
            char ruta_stat[256], ruta_status[256];
            snprintf(ruta_stat, sizeof(ruta_stat), "/proc/%d/stat", pid);
            snprintf(ruta_status, sizeof(ruta_status), "/proc/%d/status", pid);

            FILE *f_stat = fopen(ruta_stat, "r");
            if (!f_stat) {
                tabla_jobs[i].activo = 0;
                continue;
            }
            char stat_line[1024];
            if (!fgets(stat_line, sizeof(stat_line), f_stat)) {
                fclose(f_stat);
                continue;
            }
            fclose(f_stat);

            char estado = '?';
            unsigned long utime = 0, stime = 0;
            char *p = strrchr(stat_line, ')');
            if (p) {
                sscanf(p, ") %c %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu", &estado, &utime, &stime);
            }

            FILE *f_status = fopen(ruta_status, "r");
            long rss = 0;
            if (f_status) {
                char rss_line[256];
                while (fgets(rss_line, sizeof(rss_line), f_status)) {
                    if (strncmp(rss_line, "VmRSS:", 6) == 0) {
                        sscanf(rss_line, "VmRSS: %ld", &rss);
                        break;
                    }
                }
                fclose(f_status);
            }

            unsigned long total_ticks_actuales = utime + stime;
            float cpu_usage = 0.0;
            if (cpu_ticks_previos[i] > 0) {
                unsigned long delta_ticks = total_ticks_actuales - cpu_ticks_previos[i];
                cpu_usage = (100.0 * delta_ticks) / (float)(hertz * intervalo);
            }
            cpu_ticks_previos[i] = total_ticks_actuales;

            printf("%d\t| %s\t\t| %c\t\t| %.1f\t\t| %ld\n", pid, tabla_jobs[i].comando, estado, cpu_usage, rss);
        }
        fflush(stdout);

        alarm(intervalo);
        pmon_refresh = 0;

        while (!pmon_refresh && !pmon_exit) {
            pause();
        }
    }

    alarm(0); // Cancela la alarma
    sigaction(SIGINT, &old_int, NULL); // Restaurar SIGINT
    printf("\n");
}