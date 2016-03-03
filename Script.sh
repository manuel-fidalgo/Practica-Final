#!/bin/bash
clear
echo -e "\nShell Script de compilacion y debuggeo\n"
#Declaramos los colores de la salida estandar
red='\e[0;31m'
purple='\e[0;35m' 
green='\e[0;32m'
yellow='\e[0;33m' 
endColor='\e[0m'

while true 
do
	echo -e "\t 1.${purple}Compilar.${endColor}"
	echo -e "\t 2.${purple}Ejecutar.${endColor}"
	echo -e "\t 3.${purple}Debuggear (gdb).${endColor}"
	echo -e "\t 4.${purple}Debuggear (ddd).${endColor}"
	echo -e "\t *.${yellow}Salir${endColor}"
	read option
	case $option in
		1)	clear
			gcc PracticaFinal.c -lpthread -o Practica_Final  #Compilamos, mostramos un mensaje con el resultado de la compilacion
			  	if test $? -eq 0 
			  	then
					echo -e "\t${green}Compilacion realizada con exito${endColor}\n\n"
				else
					echo -e "\t${red}Error en la Compilacion${endColor}\n\n"
				fi 
				;;

		2)	clear
			./Practica_Final #Intentamos ejecutar, error 126 falta permisos, error 127 no se encuentra el archivo
				case $? in
					126) echo -e "\t${red}Este usuario no tiene permiso para ejecutar el programa. Intente \"chmod u=x Coche\" ${endColor}\n\n"	
						;;
			 		127) echo -e "\t${red}No se encuentra el archivo ejecutable, compile el codigo primero ${endColor}\n\n"	
						;;
				esac
				;;
		3) 	clear
			#Recompilamos con la librerias de debuggeo.
			gcc -g PracticaFinal.c -o Practica_Final -lpthread -DTEST --debug
			gdb Practica_Final ;;
		4) 	clear
			ddd Practica_Final;;
		*) break ;;
	esac
done
exit 0