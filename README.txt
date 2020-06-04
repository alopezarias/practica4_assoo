El conjunto de archivos conforman un sistema de ficheros assoofs con las funcionalidades básicas
de la práctica.
A pesar de éstas, he implementado:
	
	- Cache de inodos
	- Semáforos en las escrituras del superbloque
	- Semáforos en las escrituras del almacén de inodos
	- Función de rm para ficheros
	- Función de mv para ficheros y directorios

Para que las partes adicionales funcionaran, he añadido algunos campos en las estructuras
definidas en assoofs.h.
Para que la practica funcione completamente se deben usar los 3 ficheros que se adjuntan:

	- assoofs.c
	- assoofs.h
	- mkassoofs.c

Esto es debido a que hay cambios relevante en todos los archivos.

Como informacion adicional, comento que me ha llevado mucho tiempo y dedicacion
para que las partes opcionales funcionen como es debido.

Por último, me gustaría decir que funciona bien, aunque no he podido
confirmarlo completamente. He realizado pruebas exaustivas de borrado de ficheros
y directorios, comprobando cada traza, para ver si se hacen bien y, en esta
ultima versión, me agrada decir que ha pasado mis pruebas. Claro está que ésto
no garantiza que funcione en todos los casos posibles. Igualmente espero que se
tenga en cuenta el esfuerzo.

Un saludo

		Ángel

 ____________________________
|                            |
|      DATOS PERSONALES      |
|____________________________|
|                            |
|     Ángel López Arias      |
|      DNI: 54130259-N       |
|____________________________|