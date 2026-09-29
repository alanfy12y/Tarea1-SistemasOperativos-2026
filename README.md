# Tarea 1 - Sistemas Operativos 2026

Esta es una implementacion de una shell de texto simplificada para Linux, desarrollada como parte de la Tarea 1 de Sistemas Operativos. Soporta ejecucion de comandos, tuberias (pipes) de largo arbitrario, redireccion de entrada/salida, ejecucion en segundo plano (background) y un monitor de procesos integrado.

## Shell simple con pipes, redirección y señales - Sistemas Operativos 2026

### Requisitos Previos:

- Entorno Linux (o WSL en Windows).
- Compilador gcc.
- Herramienta make.


### Instrucciones de Compilacion y Ejecucion:
**1. Compilacion:**

El proyecto incluye un Makefile preconfigurado. Para compilar el codigo fuente y generar el ejecutable, abre tu terminal en el directorio raiz del proyecto y escribe el comando:
```powershell
make
```

**2. Ejecucion:**

Una vez compilado, inicia la shell ejecutando el siguiente comando:
```powershell
./mishell
```
Veras aparecer el prompt personalizado indicando tu directorio actual, por ejemplo: miShell:/home/estudiante$

**3. Limpieza del entorno:**

Para eliminar el archivo ejecutable generado y mantener limpio el directorio , ejecuta el comando:
```powershell
make clean
```
