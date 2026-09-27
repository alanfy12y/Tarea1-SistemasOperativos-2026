#define _GNU_SOURCE
#include <stdio.h>  // printf(), fgets()
#include <stdlib.h> // exit() y funciones generales
#include <unistd.h> // fork(), execvp(), getcwd(), chdir()
#include <sys/wait.h> // waitpid()
#include <string.h> // strtok(), strcmp()
#include <signal.h> // sigaction(), señales
#define MAX_JOBS 100

volatile sig_atomic_t pmon_refresh =0;
volatile sig_atomic_t pmon_exit= 0;
typedef struct{
	pid_t pid;
	char comando[256]; // para guardar el texto del comando ej: "sleep 30"
	int activo; // 1 para indicar si el proceso esta ejecutandose y 0 si ya terminó
}Job; 
Job tabla_jobs[MAX_JOBS];

// Variable global para contar jobs en background 
int contador_jobs = 1;
int total_jobs =0;

//Manejador asincrono para limpiar procesos Zombie
void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    
    // Recoge todos los hijos muertos en background sin bloquear la shell
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        
        for (int i=0; i< total_jobs; i++){
            if(tabla_jobs[i].pid == pid){
                tabla_jobs[i].activo=0; //Marcarlo como terminado para pmon
                break;
            }
        }

        char msg[60];
        int len = snprintf(msg, sizeof(msg), "\n[Background job terminado: PID %d]\n", pid);
        write(STDOUT_FILENO, msg, len); // write es seguro dentro de señales
    }
}

void manejador_alerta(int signum){
	(void) signum;
    pmon_refresh=1;
}
void manejador_interrupcionPMON(int signal){
	(void) signal;
    pmon_exit=1;
}
void ejecutar_pmon(char **args); //la funcion esta abajo del main, esto es solo la declaracion para que no haya errores al compilar

int main(void) {

    char directorio[1024]; //crea un espacio donde guardaremos la ruta.
    char linea[1024]; //crea un espacio donde guardaremos la linea de comando.
    char *args[64]; //crea un espacio donde guardaremos los argumentos de la linea de comando.

    // Configuración inicial de señales en la Shell
  
    // 1) La shell ignora ctrl + c (SIGINT) y ctrl + \ (SIGQUIT)
    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = SA_RESTART; // para que fgets no falle al recibir señales
    sigaction(SIGINT, &sa_ignore, NULL);
    sigaction(SIGQUIT, &sa_ignore, NULL);

    // 2) Activar el recolector automático de procesos en background
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);
    while (1) {

        getcwd(directorio, sizeof(directorio));

        printf("miSHell:%s$ ", directorio);
        fflush(stdout);

        if (fgets(linea, sizeof(linea), stdin) == NULL) { //Lee una línea desde la entrada estándar y guárdala en linea.
            printf("\n");
            break;
        }
        
        int argc = 0;
        char *token = strtok(linea, " \n"); // Divide la línea en tokens

        while (token != NULL && argc < 63) {

            args[argc++] = token; // Almacena cada token en el arreglo args
            token = strtok(NULL, " \n");
        }  

        args[argc] = NULL; // Asegura que el último elemento del arreglo args sea NULL
        
        if (argc == 0) { // Si no hay argumentos, continuar con la siguiente iteración
            continue;
        }

        if (strcmp(args[0], "exit") == 0) { // Comando para salir del shell
            break;
        }

        if (strcmp(args[0], "cd") == 0) { // Comando para cambiar de directorio

            if (argc < 2) {
                fprintf(stderr, "cd: falta el argumento\n");
            } else {

                if (chdir(args[1]) != 0) {
                    perror("cd");
                }
            }
            continue;
        }


      
        //Deteccion de ejecucion en Background (&)
       
        int es_background = 0;
        if (argc > 0 && strcmp(args[argc - 1], "&") == 0) {
            es_background = 1;
            args[argc - 1] = NULL; // Quitamos el '&' para que execvp no lo interprete como argumento
            argc--;                // Actualizamos la cantidad de argumentos
        }

        if (argc == 0) { // Si despues de quitar el '&' no queda nada, continuar
            continue;
        }


        pid_t pid = fork(); // Crea un nuevo proceso

        if(pid < 0) { // Error al crear el proceso

            perror("fork");
            exit(EXIT_FAILURE);

        } else if (pid == 0) { // Proceso hijo
            

            // Restaurar ctr + c solo si esta en primer plano
    
            if (!es_background) {
                struct sigaction sa_default;
                sa_default.sa_handler = SIG_DFL; // Comportamiento por defecto (morir al recibir ctrl + c)
                sigemptyset(&sa_default.sa_mask);
                sa_default.sa_flags = 0;
                sigaction(SIGINT, &sa_default, NULL);
                sigaction(SIGQUIT, &sa_default, NULL);
            }
            // Si es background, hereda SIG_IGN y es inmune al ctrl + c de la terminal


            if (execvp(args[0], args) == -1) { // Ejecuta el comando
                
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else { // Proceso padre
            ;
            //Comportamiento del Padre segun sea Foreground o Background
            if (es_background) {
                tabla_jobs[total_jobs].pid=pid;
                strncpy(tabla_jobs[total_jobs].comando, args, 255);
                tabla_jobs[total_jobs].activo =1;

                // No espera al hijo de inmediato sino imprime el job y el PID
                printf("[%d] %d\n", contador_jobs, pid);
                contador_jobs++;
            }else 
            {
            waitpid(pid, NULL, 0);
            }
        }
    }

    return 0;
}

void ejecutar_pmon(char **args){
    int intervalo = 5; //5 segundos por defecto si no se ingresa un tiempo al ejecutar el comando
    if (args[1] != NULL){
        intervalo = atoi(args[1]);
        if (intervalo <= 0) intervalo=5; // en caso de que se ingrese texto o tiempo invalido
    }
    
    //Configuracion de señales exclusivas de pmon
    struct sigaction sa_alarm, sa_int, old_int;

    sa_alarm.sa_handler = manejador_alerta;
    sigemptyset(&sa_alarm.sa_mask);
    sa_alarm.sa_flags =0;
    sigaction(SIGALRM, &sa_alarm, NULL);

    sa_int.sa_handler = manejador_interrupcionPMON;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags=0;
    sigaction(SIGINT, &sa_int, NULL);

    pmon_exit=0;

    long hertz = sysconf(_SC_CLK_TCK); //Cuantos ciclos por segundo tiene la CPU en el sistema operativo
    unsigned long cpu_ticks_previos[MAX_JOBS] = {0}; //arreglo para guardar los tiempos de CPU en el ciclo anterior y calcular el delta

    while (!pmon_exit){
        printf("\e[1;1H\e[2J"); // Limpiador de pantalla
        printf("PID\t| COMANDO\t| ESTADO\t| %%CPU (aprox)\t| RSS (KB)\n");

        for(int i=0; i< total_jobs; i++){
            if (tabla_jobs[i].activo == 0) continue; // si el job terminó se ignora

            pid_t pid = tabla_jobs[i].pid;
            char ruta_stat[256], ruta_status[256];
            snprintf(ruta_stat, sizeof(ruta_stat), "/prox/%d/stat", pid);
            snprintf(ruta_status, sizeof(ruta_status), "/prox/%d/status", pid);

            //Abrir y leer proc/[pid]/stat
            FILE *f_stat = fopen(ruta_stat, "r");
            if (!f_stat){ //Si el archivo no existe, osea el proceso acaba de morir.
                tabla_jobs[i].activo=0;
                continue;
            }
            char stat_line[1024];
            if(!fgets(stat_line, sizeof(stat_line), f_stat)){ //Seguro en caso de error y evitar fugas de memoria
                f_close(f_stat);
                continue;;
            }
            fclose(f_stat);

            char estado= "?";
            unsigned long utime = 0, stime= 0;

            char *p = strrchr(stat_line, ")");
            if (p){
                // Se ubican los valores pedidos
                sscanf(p, "%*s %*s %c %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu", &estado, &utime, &stime);   
            }  //                 aqui                                        y aqui  

            //Abrir y leer proc/[pid]/status
            FILE *f_status = fopen(ruta_status, "r");
            long rss= 0;
            if (f_status){
                char rss_line[256];
                while(fgets(rss_line, sizeof(rss_line), f_status)){// Se busca linea a linea VmRSS y se extrae el numero
                    if(strncmp(rss_line, "VmRSS;", 6)==0) {
                        sscanf(rss_line, "VmRSS: %ld", rss);
                        break;
                    }

            }
            fclose(f_status);

            //Calcular %CPU (delta)
            unsigned long total_ticks_actuales = utime+stime;
            float cpu_usage = 0.0;

            if (cpu_ticks_previos[i]>0){
                unsigned long delta_ticks = total_ticks_actuales - cpu_ticks_previos[i];
                //Formula (100*delta)/(ticks por segundo / segundos transcurridos);
                cpu_usage = (100.0 * delta_ticks)/ (float)(hertz*intervalo);
            }
            cpu_ticks_previos[i]= total_ticks_actuales; //Guardar para el sgte ciclo
            // Imprimir fila
            printf("%d\t| %s\t| %c\t\t| %.1f\t\t| %ld\n", pid, tabla_jobs[i].comando, estado, cpu_usage, rss);
        }

        fflush(stdout);
        
        //Refresco de pantalla cada x segundos ingresados al ejecutar el pmon
        alarm(intervalo);
        pmon_refresh=0;

        while (!pmon_refresh && !pmon_exit){ // duerme hasta que llegue una señal
            pause();
        }

    }

    //Limpieza al salir de pmon con Ctrl+C
    alarm(0); // cancela la alarma
    sigaction(SIGINT, &old_int, NULL); //Se devuelve la configuracion original de SIGINT de la shell
    printf("\n");
}