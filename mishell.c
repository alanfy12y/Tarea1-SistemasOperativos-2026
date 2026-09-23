#include <stdio.h>  // printf(), fgets()
#include <stdlib.h> // exit() y funciones generales
#include <unistd.h> // fork(), execvp(), getcwd(), chdir(), dup2(), close(), pipe()
#include <sys/wait.h> // waitpid()
#include <string.h> // strtok(), strcmp()
#include <fcntl.h> // open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, O_APPEND

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


	//zona de pipes de largo arbitrario y redirecciones
	char *comando[64];		//arreglo que tendra los argumentos del subcomando actual
	int inicio = 0;			//indice para el subcomando actual del args[]
	int entrada_anterior = -1;	//descriptores de archivo del extrema de lectura del pipe

	for (int k=0; k<=argc; k++) {
	    if (args[k] != NULL && strcmp(args[k], "|") != 0) continue;	//si no es "|" ni "NULL": acumulamos

	    int es_ultimo = (args[k] == NULL);
	    int largo = k-inicio;				//cantidad de tokens en el subcomando
	    if (largo == 0) {					//un simple detector de errores
		fprintf(stderr, "Error de sintaxis\n");
	        if (entrada_anterior != -1) close(entrada_anterior);
		break;
	    }

	    for (int m=0; m<largo; m++) {		//ciclo para extraer el subcomando acual del arreglo
		comando[m] = args[inicio+m];
	    }
	    comando[largo] = NULL;
	    inicio = k+1;


	    //variables del redireccionamiemto.
	    char *infile = NULL;	//nombre del archivo de entrada ("<")
	    char *outfile = NULL;	//nombre del archivo de salida (">" o ">>")
	    int append = 0;		//variable para decidir aplicar trunc o append.



	    //identificacion para redireccionamiento
	    int i=0,j=0;
	    while (comando[i] != NULL) {
	        if (strcmp(comando[i], "<")==0 && comando[i+1] != NULL) {	//caso1: redireccion de entrada
		    infile = comando[i+1];
		    i+=2;
	        } else if (strcmp(comando[i], ">")==0 && comando[i+1] != NULL) {	//caso2: redireccion de salida y trunc
		    outfile = comando[i+1];
		    append = 0;
		    i+=2;
	        } else if (strcmp(comando[i], ">>")==0 && comando[i+1] != NULL) { //caso3: redireccion de salida y append
		    outfile = comando[i+1];
		    append = 1;
		    i+=2;
	        } else {							//caso4: comando o argumento.
		    comando[j++] = comando[i++];
	        }
	    }
	    comando[j] = NULL;	//verificamos que no quede ninguna basura al final que pueda afectar el execvp()


	    if (comando[0] == NULL) { //para casos donde el usuario no escriba comando, tipo "< datos.txt"
		fprintf(stderr, "Error de sintaxis\n");
		if (entrada_anterior != -1) close(entrada_anterior);
		break;
	    }

	    int tuberia[2]; //pipe hacia el siguiente  comando, solo se crea si no somos el ultimo de la tuberia.
	    if (!es_ultimo) {
		if (pipe(tuberia) < 0) {
		    perror("pipe");
		    exit(EXIT_FAILURE);
		}
	    }


	    pid_t pid = fork(); // Crea un nuevo proceso

	    if(pid < 0) { // Error al crear el proceso
		perror("fork");
		exit(EXIT_FAILURE);

	    } else if (pid == 0) { // Proceso hijo

		if (entrada_anterior != -1) {	//no es el primer comando: la entrada viene del pipe anterior
		    dup2(entrada_anterior, 0);
		    close(entrada_anterior);
		} else if (infile) {			//es el primer comando y hay redireccion de entrada (caso "<")
		    int fd = open(infile, O_RDONLY);	//abrimos el archivo solo en modo lectura.
		    if (fd<0) {
		       perror("open");
		       exit(EXIT_FAILURE);
		    }
		    dup2(fd, 0);			//stdin ahora apunta al mismo archivo que fd
		    close(fd);				//cerramos archivos ya innecesarios
		}

		if (!es_ultimo) {		//no es el ultimo comando: la salida va hacia el pipe siguiente
		    close(tuberia[0]);		//el hijo no necesita el extremo de lectura de su propio pipe de salida
		    dup2(tuberia[1], 1);
		    close(tuberia[1]);
		} else if (outfile) {		//es el ultimo comando y hay redireccion de salida (caso ">" o ">>")
		    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);	//lectura/crear y append/trunc
		    int fd = open(outfile, flags, 0644);	//se abre con las flags, y 0644 son los permisos para crear
		    if (fd<0) {
		       perror("open");
		       exit(EXIT_FAILURE);
		    }
		    dup2(fd, 1);		//stdout apunta al archivo.
		    close(fd);
		}

		if (execvp(comando[0], comando) == -1) { // Ejecuta el comando

		    perror("execvp");
		    exit(EXIT_FAILURE);
		}
	    } else { // Proceso padre

		if (entrada_anterior != -1) close(entrada_anterior); //el padre ya no necesita el extremo de lectura anterior, el hijo se quedo con su propia copia

		if (!es_ultimo) {
		    close(tuberia[1]);			//el padre no escribe en el pipe, solo lo hereda el hijo
		    entrada_anterior = tuberia[0];	//el proximo comando de la tuberia leera desde aqui
		}
	    }
	}

	while (wait(NULL) > 0); //esperamos a TODOS los hijos de la tuberia, sin importar cuantos fueron (largo arbitrario)
    }


    return 0;
}
