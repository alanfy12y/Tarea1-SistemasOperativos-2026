#include <stdio.h>  // printf(), fgets()
#include <stdlib.h> // exit() y funciones generales
#include <unistd.h> // fork(), execvp(), getcwd(), chdir()
#include <sys/wait.h> // waitpid()
#include <string.h> // strtok(), strcmp()

int main(void) {

    char directorio[1024]; //crea un espacio donde guardaremos la ruta.
    char linea[1024]; //crea un espacio donde guardaremos la linea de comando.
    char *args[64]; //crea un espacio donde guardaremos los argumentos de la linea de comando.

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


        pid_t pid = fork(); // Crea un nuevo proceso

        if(pid < 0) { // Error al crear el proceso

            perror("fork");
            exit(EXIT_FAILURE);

        } else if (pid == 0) { // Proceso hijo
            
            if (execvp(args[0], args) == -1) { // Ejecuta el comando
                
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        } else { // Proceso padre
            
            waitpid(pid, NULL, 0);
        }
    }


    return 0;
}