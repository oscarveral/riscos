# xv6 Modificado.

Modificación de xv6 implementando las siguientes mejoras:
* Scheduler alternativo mediante un sistema de loteria.
* Llamadas al sistema mmap y munmap para el mapeo de ficheros en memoria.
* Sistema de copia en escritura para optimizar el uso de memoria y rendimiento.
* Páginas físicas compartidas entre procesos padre e hijo en mapeos de memoria compartidos.
* Reserva de páginas físicas bajo demanada en los mapeos de memoria.
* Ejecución de binarios con paginación bajo demanda.
* Soporte para Device Tree.

## Autores.
* Beatriz Pérez Garnica.
* Óscar Vera López.