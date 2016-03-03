#include <stdio.h>
#include <sys/syscall.h>
#include <stdlib.h> 
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
/*@manuel_fidalgo practica final*/

////////////////////////////////
/*
Por temas de indentacion se ha restringido el numero de carateres
por lina a menos de 90 como recomiendan la mayoria de las 
guias de estilo de C, llamadas a funciones con parametros largos se han
escrito en varias lineas para facilitar su lectura en todo tipo de editores.
*/
/////////////////////////////////
/*
Partes opcionales:
-Memoria estatica hecha y fucionando
-Memoria dinamica implementada, fallo:
*** Error in `./Practica_Final': realloc(): invalid pointer: 0x0000000002204250 ***

/*Punteros a la cola de clientes y cajeros*/
struct client* clients_queue;
struct cashier* cashiers_queue;

/*Numero de cajeros y clientes, para la reserva de memoria estatica*/
int NUM_MAX_CLIENTES;
int NUM_CASHIERS;
int SEGUIR;
int TOTAL_CLIENTS;

/*identificador del siguiente y numero clientes esperando*/
int identificador;
int necesita_reponedor=0;

/*Mutex & variables de condicion*/
pthread_mutex_t access_control_queue;
pthread_mutex_t access_control_log;
pthread_mutex_t worker_control;
pthread_cond_t condicion;

/*Log*/
FILE * logFile;

/*Structs para clientes y para empleados*/
struct client{
	pthread_t tid;
	int atendido; /*0 atendido, 1 sin atender, 2 atendiendo,-1 inicializado*/
	int id;
};
struct cashier{
	pthread_t tid;
	int clientes_atendidos;
	int id;
};
pthread_t worker_queue;

/*Definicion de las funciones, C estandar*/

void inputs(void);
void printMemory(void);
void error(char zz);
void freeMemory(int posicion);
void writelogMessage(char *id, char *msg);
void createClient(void);
void resizeMemory(void);
void * client(void *arg);
void * cashier(void *arg);
void * warehouseWorker(void *arg);
int searchOldest(void);
int searchPosition(void);
int terminateProgram(void);
int buscarId(int idt);
pid_t gettid(void);



int main(void){
	if(signal(SIGUSR1,createClient) == SIG_ERR) error('s');
	if(signal(SIGINT,terminateProgram) == SIG_ERR) error('s');
	if(signal(SIGUSR2,resizeMemory)== SIG_ERR) error('s');

	/*leemos el tamaño por la entrada estandar NO PUEDE ESTAR EN EXCLUSION MUTUA*/
	inputs();

	/*cremaos el primer mutex y lo cerramos*/
	if(pthread_mutex_init(&access_control_queue, NULL)!=0) error('m');
	pthread_mutex_lock(&access_control_queue);

	/*inicializamos algunas variables...*/
	int i;
	TOTAL_CLIENTS=0;
	identificador = 0;

	logFile = fopen("registro.log", "w"); /*Abre el fichero del log y lo reescribe*/
	srand (getpid()*time(NULL)); /*rand() % (N+1) N = lim maximo*/

	/*Reservamos memoria para clientes y cajeros*/
	clients_queue = (struct client *)malloc(NUM_MAX_CLIENTES*sizeof(struct client));
	cashiers_queue = (struct cashier *)malloc(NUM_CASHIERS*sizeof(struct cashier));
	
	if(clients_queue==NULL || cashiers_queue==NULL){
		error('a');
	}

	/*Inicializamos los structs de la cola de clientes*/
	for(i=0;i<NUM_MAX_CLIENTES;i++){
		freeMemory(i);
	}

	/*Inicializamos los structs de cajeros y creamos los procesos cajeros*/
	for(i=0; i<NUM_CASHIERS; i++){
		cashiers_queue[i].id=i+1;
		cashiers_queue[i].tid=0;
		/*El hilo del cliente tomara su struct como argumento*/
		if(pthread_create (&cashiers_queue[i].tid ,NULL, cashier, &cashiers_queue[i])!=0) error('t');
	}

	/*iniciamos el resto de mutex y controlamos en error*/
	if(pthread_mutex_init(&access_control_log, NULL)!=0) error('m');
	if(pthread_mutex_init(&worker_control, NULL)!=0) error('m');
	if(pthread_cond_init(&condicion,NULL)!=0) error('m');
	
	/*Creamos el hilo del reponedor*/
	if(pthread_create(&worker_queue,NULL,warehouseWorker,NULL)!=0) error('t');

	/*volvemos a abrir el mutex y dejamos que la cosa fluya*/
	pthread_mutex_unlock(&access_control_queue);
	
	/*Se queda esperando por las señales*/
	while(1){
		pause();

	}
	/*Instruccion inutil, por seguir la guia de estilo de C*/
	return -1;
}

/*Controla los tipos de errores*/
void error(char zz){
	if(zz=='s') perror("Error al recibir la señal.\n");
	else if(zz=='t') perror("Error al crear un thread.\n");
	else if(zz=='m') perror("Error al inicializar un mutex.\n");
	else if(zz=='c') perror("Error en el aceso a la cola.\n");
	else if(zz=='k') perror("Error al enviar kill.\n");
	else if(zz=='a') perror("Error al alojar o realojar memoria\n");
	exit(-1);
}

/*Escribe en el log*/
/*EXCLUSION MUTUA*/
void writelogMessage(char *id, char *msg) {
	// Calculamos la hora actual
	time_t now = time(0);
	struct tm *tlocal = localtime(&now);
	char stnow[19];
	strftime(stnow, 19, "%d/%m/%y %H:%M:%S", tlocal);
	// Escribimos en el log con los mutex cerrador para que no se pisen caracters
	fprintf(logFile, "[%s] %s %s\n", stnow, id, msg);
}

/*Funcion de tipo halder que crea un nuevo cliente*/
void createClient(void){
	if(signal(SIGUSR1,createClient) == SIG_ERR) error('s');
	int posicion;
	/*Buscamos la primera posicion libre y añadimos el cliente a la cola*/
	pthread_mutex_lock(&access_control_queue);
	posicion = searchPosition();
	if(posicion>=0){	
		clients_queue[posicion].id = ++identificador;
		clients_queue[posicion].atendido = 1;
		if(pthread_create(&clients_queue[posicion].tid,NULL,client,&clients_queue[posicion])!=0)error('t');
	}
	pthread_mutex_unlock(&access_control_queue);
	if(posicion<0){
		pthread_mutex_lock(&access_control_log);
			writelogMessage("Ha llegado otro cliente pero la cola estaba llena.","");
		pthread_mutex_unlock(&access_control_log);
	}
}

/*Funciones del cliente*/
void *client(void *arg) {
	int posicion;
	int aleat;
	int seg_esp;
	char message[20];

	sprintf(message,"EL Cliente_%d",(*(struct client *)arg).id);
	pthread_mutex_lock(&access_control_log);
		writelogMessage(message,"se pone a la cola");
	pthread_mutex_unlock(&access_control_log);

	aleat = rand() % (100+1);
	while(1){
		/*Comprobamos si esta sendo atendido o si se cansa de esperar*/
		sleep(1);
		seg_esp++;
		if((*(struct client *)arg).atendido==2||(*(struct client *)arg).atendido==0){
			/*Esta siendo atendido o ha sido atendido, terminamos el proceso y dejamos que el cajerlo libere la memoria*/
			pthread_exit(NULL);
		}
		if(seg_esp%10==0){
			/*liberamos memoria para que ningun otro cajero lo pueda usar*/
			pthread_mutex_lock(&access_control_queue);
				freeMemory(buscarId((*(struct client *)arg).id));
			pthread_mutex_unlock(&access_control_queue);
			pthread_mutex_lock(&access_control_log);
				writelogMessage(message,"se ha cansado de esperar y se ha ido.");
			pthread_mutex_unlock(&access_control_log);
			pthread_exit(NULL);
		}
	}
	pthread_exit(NULL);
}

void *cashier(void *arg){
	int problemas;
	int atender_a;
	int precio_compra;
	int ex_time;
	/*Cadenas para la impresion del mensaje*/
	char message[20];
	char message_2[30];
	atender_a = -1;

	sprintf(message,"Cajero_%d",(*(struct cashier*)arg).id);
	pthread_mutex_lock(&access_control_log);
		writelogMessage(message,"ha entrado a trabajar");
	pthread_mutex_unlock(&access_control_log);

	while(1){
		ex_time = rand() % (5+1);
		/*Exlcusion mutua, luchan todo el rato por el primer cliente de la cola*/
		while(1){
			sleep(1);
			pthread_mutex_lock(&access_control_queue);
			atender_a = searchOldest();
			clients_queue[atender_a].atendido=2;
			pthread_mutex_unlock(&access_control_queue);
			if(atender_a>=0) break; /*Si devuelve -1 es que no hay clientes*/
		}

		sprintf(message_2,"El %s comienza a atender al Cliente_%d",
			message,clients_queue[atender_a].id);

		pthread_mutex_lock(&access_control_log);
			writelogMessage(message_2,"");
		pthread_mutex_unlock(&access_control_log);

		problemas=(rand()%100)+1;

		if(problemas<5){ 
			/*Escribimos en el log que no se puede realizar*/
			sprintf(message_2,"El Cliente_%d atendido por %s no puede hacer la compra por algun motivo.",
				clients_queue[atender_a].id,message);

			pthread_mutex_lock(&access_control_log);
				writelogMessage(message_2,"");
			pthread_mutex_unlock(&access_control_log);
			
			/*liberamos el espacio en la memoria*/
			pthread_mutex_lock(&access_control_queue);
				freeMemory(atender_a);
			pthread_mutex_unlock(&access_control_queue);
			
			continue; /*para atneder al siguiente*/

		}else if(problemas<30){/*hay algun tipo de probkemas y se necesita al reponer*/
			sprintf(message_2,"El Cliente_%d atendido por %s tiene algun problema y necestia al reponedor.",
				clients_queue[atender_a].id,message);

			pthread_mutex_lock(&access_control_log);
				writelogMessage(message_2,"");
			pthread_mutex_unlock(&access_control_log);
			/*llamamos al reponedor y nos quedamos esperando a que termine(variable condicion)*/
			pthread_mutex_lock(&worker_control);
			necesita_reponedor++;
				pthread_cond_wait(&condicion,&worker_control);
				necesita_reponedor=0;
			pthread_mutex_unlock(&worker_control);

			/*reflejamos en el log lo que ha pasado*/
			sprintf(message_2,
				"El Cliente_%d, antendido por el %s ha terminado de consultar al reponedor.",
				clients_queue[atender_a].id,message);

			pthread_mutex_lock(&access_control_log);
				writelogMessage(message_2,"");
			pthread_mutex_unlock(&access_control_log);
		}
		sleep(ex_time);
		precio_compra = rand()%(100+1);
		sprintf(message_2,
			"El %s ha terminado de antender al Cliente_%d, precio de la compra: %d$.",
			message,clients_queue[atender_a].id,precio_compra);
		
		/*loggeamos*/
		pthread_mutex_lock(&access_control_log);
			writelogMessage(message_2,"");
		pthread_mutex_unlock(&access_control_log);
		
		/*liberaos la memoria del clente atendido y actualizamos los clientes*/
		pthread_mutex_lock(&access_control_queue);
			freeMemory(atender_a);
			TOTAL_CLIENTS++;
			(*(struct cashier*)arg).clientes_atendidos++;
		pthread_mutex_unlock(&access_control_queue);

		if((*(struct cashier*)arg).clientes_atendidos%10==0){
			pthread_mutex_lock(&access_control_log);
				writelogMessage(message,"se toma un descanso");
			pthread_mutex_unlock(&access_control_log);
			sleep(20);
			pthread_mutex_lock(&access_control_log);
				writelogMessage(message,"vuelve al trabajo");
			pthread_mutex_unlock(&access_control_log);
		}
	}
}

 /*funciones del reponedor*/
void * warehouseWorker(void * arg){
	pthread_mutex_lock(&access_control_log);
		writelogMessage("El Reponedor entra a trabajar.","");
	pthread_mutex_unlock(&access_control_log);
	while(1){
		/*Evita la espera activa echando al hilo del procesador*/
		/*puede sustituirse por sleep(1)*/
		pthread_yield();
		if(necesita_reponedor==1){
			sleep((rand()%5)+1); /*t_consulta€[1,5]*/
			pthread_mutex_lock(&access_control_log);
				writelogMessage("El Reponedor acaba la consulta que le habian asignado.","");
			pthread_mutex_unlock(&access_control_log);
			/*Señalamos condicon*/
			pthread_mutex_lock(&worker_control);
				pthread_cond_signal(&condicion);
			pthread_mutex_unlock(&worker_control);

		}
	}
}


/*Devuelve la posicion de donde esta el cliente mas viejo(Menor id)
si no hay cliente devielve -1*/
/*	EXCLUSION MUTUA */
int searchOldest(){
	int posicion;
	int min;
	int i;
	posicion=-1;
	min=identificador;
	for(i=0; i<NUM_MAX_CLIENTES;i++){
		if(clients_queue[i].id<=min && clients_queue[i].id !=0 && clients_queue[i].atendido==1){
			min = clients_queue[i].id;
			posicion = i;
		}
	}
	return posicion;
}

/*devuelve el TID de un proceso*/
pid_t gettid(void) {
	return syscall(__NR_gettid);
}

/*Busca la primera posicion en la cola libre
 si no hay devuelve -1*/
/*EXCLUSION_MUTUA*/
int searchPosition(void){
	int i;
	for(i=0;i<NUM_MAX_CLIENTES;i++){
		if(clients_queue[i].atendido==0){
			return i;
		}
	}
	return -1;
}

/*Termina el programa acabando con todos los hilos y liberando la memoria\n*/
int terminateProgram(void){
	if(signal(SIGINT,terminateProgram)==SIG_ERR) error('s');

	int i;
	int ret;
	char final_mess[20];
	char partial_mess[40];

	for(i=0;i<NUM_CASHIERS;i++){
		sprintf(
				partial_mess,
				"El Cajero_%d ha atendido a un total de:%d Clientes",
				cashiers_queue[i].id,cashiers_queue[i].clientes_atendidos
				);

		pthread_mutex_lock(&access_control_log);
			writelogMessage(partial_mess,"");
		pthread_mutex_unlock(&access_control_log);
	}

	sprintf(final_mess,"Se ha atendido a un total de %d Clientes",TOTAL_CLIENTS);
	pthread_mutex_lock(&access_control_log);
		writelogMessage(final_mess,"");
		writelogMessage("\n---MEMORY MAP---","");
		printMemory();
	pthread_mutex_unlock(&access_control_log);
	fclose(logFile);
	printf("terminamos.\n");
	exit(0);
}

/*Imrimer el estado de la memoria que estamos usando*/
void printMemory(void){
	int i;
	char memory_segmet[50];
	for(i=0;i<NUM_MAX_CLIENTES;i++){

		sprintf(memory_segmet,
			"\tid:%d \ttid:%d \tatendido:%d, \tdirecion_memoria:%p",
			clients_queue[i].id,
			clients_queue[i].tid,
			clients_queue[i].atendido,&clients_queue[i]);

		writelogMessage(memory_segmet,"");
	}
	for(i=0; i<NUM_CASHIERS; i++){

		sprintf(memory_segmet,
			"\tid:%d \ttid:%d \t clientes atendidos:%d\tdirecion_memoria:%p",
			cashiers_queue[i].id,
			cashiers_queue[i].tid,cashiers_queue[i].clientes_atendidos,
			&cashiers_queue[i]);

		writelogMessage(memory_segmet,"");	
	}
}

/*busca en toda la cola de clientes la poscion del que tiene este id*/
/*EXLCUSION MUTUA*/
int buscarId(int idt){
	int i;
	for(i=0; i<NUM_MAX_CLIENTES;i++){
		if(clients_queue[i].id==idt){
			return i;
		}
	}
	return -1;
}

/*Libera la cola es la posicion dada*/
/*EXLUSION MUTUA*/
void freeMemory(int posicion){
	clients_queue[posicion].id=0;
	clients_queue[posicion].tid=0;
	clients_queue[posicion].atendido=0;

}
/*Amplia en 1 en numero de cajeros y en 5 en numero de clientes*/
void resizeMemory(void){
	if(signal(SIGUSR2,resizeMemory) == SIG_ERR) error('s');
	struct client * ptr_cli;
	struct cashier * ptr_cash;
	char cad[60];
	int iterator;
	int last_id;

	/*Grantizamos la eclusion mutua*/
	sprintf(cad,"El tamaño de la cola ha aumentado en 5 y el numeros de cajeros en 1");
	
	pthread_mutex_lock(&access_control_queue);

		last_id = cashiers_queue[NUM_CASHIERS-1].id;
		iterator=NUM_CASHIERS;

		NUM_MAX_CLIENTES=NUM_MAX_CLIENTES+5;
		NUM_CASHIERS=NUM_CASHIERS+2;
		
		/*Modificacion de la memoria de los clientes*/
		ptr_cli = (struct client *)realloc(clients_queue,sizeof(struct client)*NUM_MAX_CLIENTES);
		if(ptr_cli!=NULL){
			clients_queue = ptr_cli;
			printf("2_clients_queue: %p\n",clients_queue);
		}else{
			error('a');
		}

		/*Modificacion la memoria de los cajeros*/
		ptr_cash = (struct client *)realloc(cashiers_queue,sizeof(struct cashier)*NUM_MAX_CLIENTES);
		if(ptr_cash!=NULL){
			cashiers_queue = ptr_cash;
		}else{
			error('a');
		}

		/*Creamos el nuevo cajero*/
		cashiers_queue[iterator].id=++last_id;
		cashiers_queue[iterator].tid=0;
		if(pthread_create (&cashiers_queue[iterator].tid ,NULL, cashier, &cashiers_queue[iterator])!=0) error('t');
		

	pthread_mutex_unlock(&access_control_queue);

	pthread_mutex_lock(&access_control_log);
		writelogMessage(cad,"");
	pthread_mutex_unlock(&access_control_log);
}
void inputs(void){
	printf("Tamaño cola: \n");
	scanf("%d",&NUM_MAX_CLIENTES);
	printf("Numero cajeros: \n");
	scanf("%d",&NUM_CASHIERS);
}