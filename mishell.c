#include <stdio.h>  // printf(), fgets()
#include <stdlib.h> // exit() y funciones generales
#include <unistd.h> // fork(), execvp(), getcwd(), chdir()
#include <sys/wait.h> // waitpid()
#include <string.h> // strtok(), strcmp()
#include <signal.h> // sigaction(), señales

// Variable global para contar jobs en background 
int contador_jobs = 1;


//Manejador asincrono para limpiar procesos Zombie

void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    
    // Recoge todos los hijos muertos en background sin bloquear la shell
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        char msg[60];
        int len = snprintf(msg, sizeof(msg), "\n[Background job terminado: PID %d]\n", pid);
        write(STDOUT_FILENO, msg, len); // write es seguro dentro de señales
    }
}


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
            
            //Comportamiento del Padre segun sea Foreground o Background
            if (es_background) {
                // No espera al hijo de inmediato sino imprime el job y el PID
                printf("[%d] %d\n", contador_jobs++, pid);
            }else 
            {
            waitpid(pid, NULL, 0);
            }
        }
    }

    return 0;
}