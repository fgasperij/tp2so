#include <signal.h>
#include <errno.h>

#include "biblioteca.h"

/* Estructura que almacena los datos de una reserva. */
typedef struct {
	int posiciones[ANCHO_AULA][ALTO_AULA];
	pthread_mutex_t locks[ANCHO_AULA][ALTO_AULA];
	pthread_mutex_t lock_cantidad;
	int cantidad_de_personas;
	int tanda;
	
	int rescatistas_disponibles;
} t_aula;

typedef struct thread_data {
	int thread_no;
	int socketfd_cliente;
	t_aula *el_aula;
} thdata;

int rescatistas = RESCATISTAS;
pthread_cond_t cv_rescatistas;
pthread_mutex_t mutex_cv_rescatistas;
pthread_cond_t cv_tanda;
pthread_mutex_t mutex_tanda;

void t_aula_iniciar_vacia(t_aula *un_aula)
{
	int i, j;
	for(i = 0; i < ANCHO_AULA; i++)
	{
		for (j = 0; j < ALTO_AULA; j++)
		{
			un_aula->posiciones[i][j] = 0;
			if (pthread_mutex_init(&(un_aula->locks[i][j]), NULL) != 0) {
				printf("\n mutex init failed\n");        		
			}
		}
	}	
	if (pthread_mutex_init(&(un_aula->lock_cantidad), NULL) != 0) {
		printf("\n mutex init failed\n");        		
	}
	un_aula->cantidad_de_personas = 0;
	un_aula->tanda = 0;
	
	un_aula->rescatistas_disponibles = RESCATISTAS;
}

void t_aula_ingresar(t_aula *un_aula, t_persona *alumno)
{
	pthread_mutex_lock(&(un_aula->lock_cantidad));		
	un_aula->cantidad_de_personas++;
	pthread_mutex_unlock(&(un_aula->lock_cantidad));
	
	pthread_mutex_lock(&(un_aula->locks[alumno->posicion_fila][alumno->posicion_columna]));	
	un_aula->posiciones[alumno->posicion_fila][alumno->posicion_columna]++;
	pthread_mutex_unlock(&(un_aula->locks[alumno->posicion_fila][alumno->posicion_columna]));
}

void pseudo () {
	int grupo;
	pthread_mutex_lock(&(un_aula->lock_cantidad));
	if (un_aula->cantidad_de_personas >= 5) {
		grupo = 5;
	} else {
		grupo = un_aula->cantidad_de_personas;
	}
	pthread_mutex_unlock(&(un_aula->lock_cantidad));
	
	pthread_mutex_lock(&(un_aula->lock_esperando));
	++esperando_en_grupo;
	while (esperando_en_grupo < grupo) {
		pthread_cond_wait(&cv_grupo_formado, &(un_aula->lock_esperando));
	}
	pthread_mutex_lock(&(un_aula->lock_esperando));
		
}
void t_aula_liberar(t_aula *un_aula, t_persona *alumno)
{
	bool ultimo_del_grupo = false;
	printf("%s: Pido lock de tanda\n", alumno->nombre);
	pthread_mutex_lock(&mutex_tanda);		
	printf("%s: Obtuve lock de tanda\n", alumno->nombre);
		while(un_aula->tanda == 5) {
			printf("%s: Tanda es igual a 5, voy a esperar que el grupo termine de salir.\n", alumno->nombre);
			pthread_cond_wait(&cv_tanda, &mutex_tanda);
		}
		if (un_aula->tanda == 4) {
			printf("%s: Soy el ultimo del grupo\n", alumno->nombre);
			ultimo_del_grupo = true;
		}
		printf("%s: Ingreso a la tanda\n", alumno->nombre);
		++un_aula->tanda;
		printf("%s: Libero lock de tanda\n", alumno->nombre);
	pthread_mutex_unlock(&mutex_tanda);			
	printf("%s: Signal de cond_wait de tanda\n", alumno->nombre);
	pthread_cond_signal(&cv_tanda);
	printf("%s: Pido lock de tanda\n", alumno->nombre);
	pthread_mutex_lock(&mutex_tanda);			
	printf("%s: Obtuve lock de tanda\n", alumno->nombre);
		while (un_aula->tanda < 5) {
			printf("%s: Tanda es menor a 5, voy a esperar que el grupo termine de formarse.\n", alumno->nombre);
			pthread_cond_wait(&cv_tanda, &mutex_tanda);
		}
	printf("%s: Se llenó la tanda\n", alumno->nombre);	
	pthread_mutex_unlock(&mutex_tanda);
	if (ultimo_del_grupo) {
		printf("%s: Soy el último del grupo, despierto al resto\n", alumno->nombre);
		pthread_cond_broadcast(&cv_tanda);
	}
	printf("%s: Pido lock de cantidad de personas\n", alumno->nombre);
	pthread_mutex_lock(&(un_aula->lock_cantidad));
		printf("%s: Obtuve lock de cantidad de personas\n", alumno->nombre);
		printf("%s: Disminuyo la cantidad de personas del aula\n", alumno->nombre);		
		un_aula->cantidad_de_personas--;
		printf("%s: Libero lock de cantidad de personas\n", alumno->nombre);					
	pthread_mutex_unlock(&(un_aula->lock_cantidad));		
	if (ultimo_del_grupo) {
		printf("%s: Soy el último del grupo, pido lock de tanda para resetearla\n", alumno->nombre);
		pthread_mutex_lock(&mutex_tanda);
		printf("%s: Soy el último y obtuve lock de tanda para resetearlo.\n", alumno->nombre);
			un_aula->tanda = 0;
		printf("%s: Libero el lock de tanda que obtuve para resetearla\n", alumno->nombre);
		pthread_mutex_unlock(&mutex_tanda);
		pthread_cond_signal(&cv_tanda);
	}
}

static void terminar_servidor_de_alumno(int socket_fd, t_aula *aula, t_persona *alumno) {
	printf(">> Se interrumpió la comunicación con una consola.\n");
		
	close(socket_fd);
	
	t_aula_liberar(aula, alumno);
	//exit(-1);
	pthread_exit(0);
}

t_comando intentar_moverse(t_aula *el_aula, t_persona *alumno, t_direccion dir)
{
	int fila = alumno->posicion_fila;
	int columna = alumno->posicion_columna;
	alumno->salio = direccion_moverse_hacia(dir, &fila, &columna);

	///char buf[STRING_MAXIMO];
	///t_direccion_convertir_a_string(dir, buf);
	///printf("%s intenta moverse hacia %s (%d, %d)... ", alumno->nombre, buf, fila, columna);
	
	
	bool entre_limites = (fila >= 0) && (columna >= 0) &&
	     (fila < ANCHO_AULA) && (columna < ALTO_AULA);
	    
	bool pudo_moverse = false;
	 
	if (alumno->salio) {
		pthread_mutex_lock(&(el_aula->locks[alumno->posicion_fila][alumno->posicion_columna]));
		el_aula->posiciones[alumno->posicion_fila][alumno->posicion_columna]--;
		pthread_mutex_unlock(&(el_aula->locks[alumno->posicion_fila][alumno->posicion_columna]));
		
		alumno->posicion_fila = fila;
		alumno->posicion_columna = columna;	
		pudo_moverse = true;
	} else if (entre_limites) {
		//lockear_posiciones_en_orden(el_aula, alumno, dir);
		// lockear las posiciones en orden
		int fila1 = alumno->posicion_fila;
		int fila2 = alumno->posicion_fila;
		int columna1 = alumno->posicion_columna;
		int columna2 = alumno->posicion_columna;
		
		if (dir == ARRIBA) {
			fila1--;
		} else if (dir == DERECHA) {
			columna2++;
		} else if (dir == ABAJO) {
			fila2++;
		} else if (dir == IZQUIERDA) {
			columna1--;
		}
		printf("%s: Pido lock sobre la celda (%d, %d)\n", alumno->nombre, fila1, columna1);
		pthread_mutex_lock(&(el_aula->locks[fila1][columna1]));
		printf("%s: Obtuve lock sobre la celda (%d, %d)\n", alumno->nombre, fila1, columna1);
		printf("%s: Pido lock sobre la celda (%d, %d)\n", alumno->nombre, fila2, columna2);
		pthread_mutex_lock(&(el_aula->locks[fila2][columna2]));
		printf("%s: Obtuve lock sobre la celda (%d, %d)\n", alumno->nombre, fila2, columna2);
		
		
		if (el_aula->posiciones[fila][columna] < MAXIMO_POR_POSICION) {
			el_aula->posiciones[fila][columna]++;
			el_aula->posiciones[alumno->posicion_fila][alumno->posicion_columna]--;
		}
		pthread_mutex_unlock(&(el_aula->locks[alumno->posicion_fila][alumno->posicion_columna]));
		pthread_mutex_unlock(&(el_aula->locks[fila][columna]));
		
		alumno->posicion_fila = fila;
		alumno->posicion_columna = columna;	
		pudo_moverse = true;
	}	
	
	//~ if (pudo_moverse)
		//~ printf("OK!\n");
	//~ else
		//~ printf("Ocupado!\n");


	return pudo_moverse;
}

void colocar_mascara(t_aula *el_aula, t_persona *alumno)
{
	printf("Esperando rescatista. Ya casi %s!\n", alumno->nombre);
	// esperar rescatista libre
	alumno->tiene_mascara = true;
	
	printf("%s: Pido lock de rescatistas\n", alumno->nombre);
	pthread_mutex_lock(&mutex_cv_rescatistas);
	printf("%s: Obtuve lock de rescatistas\n", alumno->nombre);
		while (rescatistas == 0) {
			printf("%s: rescatistas es igual a 0\n", alumno->nombre);
			if (rescatistas < 0) {
				printf("La cantidad de rescatistas es menor a 0\n");				
			}			
			printf("%s: Hago cond_wait de rescatistas\n", alumno->nombre);
			pthread_cond_wait(&cv_rescatistas, &mutex_cv_rescatistas);
			printf("%s: Me desperté del cond_wait de rescatistas\n", alumno->nombre);
		}			
		printf("%s: Conseguí un rescatista\n", alumno->nombre);
		--rescatistas;
		printf("%s: Libero el lock de rescatistas\n", alumno->nombre);
	pthread_mutex_unlock(&mutex_cv_rescatistas);
		printf("%s: Me colocan la máscara\n", alumno->nombre);
		alumno->tiene_mascara = true;
	printf("%s: Pido lock de rescatistas\n", alumno->nombre);		
	pthread_mutex_lock(&mutex_cv_rescatistas);
	printf("%s: Obtuve lock de rescatistas\n", alumno->nombre);
		++rescatistas;
		printf("%s: Libero el lock de rescatistas\n", alumno->nombre);
	pthread_mutex_unlock(&mutex_cv_rescatistas);
	printf("%s: Signal del cond_wait de rescatistas\n", alumno->nombre);
	pthread_cond_signal(&cv_rescatistas);
	
}


void *atendedor_de_alumno(void *data_ptr)
{
	thdata *data;
	data = (thdata *) data_ptr;
	int socket_fd = data->socketfd_cliente;
	t_aula *el_aula = data->el_aula;
	
	t_persona alumno;
	t_persona_inicializar(&alumno);
	
	if (recibir_nombre_y_posicion(socket_fd, &alumno) != 0) {
		/* O la consola cortó la comunicación, o hubo un error. Cerramos todo. */
		terminar_servidor_de_alumno(socket_fd, NULL, NULL);
	}
	
	printf("Atendiendo a %s en la posicion (%d, %d)\n", 
			alumno.nombre, alumno.posicion_fila, alumno.posicion_columna);
		
	t_aula_ingresar(el_aula, &alumno);
	
	/// Loop de espera de pedido de movimiento.
	for(;;) {
		t_direccion direccion;
		
		/// Esperamos un pedido de movimiento.
		printf("%s: Esperando movimiento\n", alumno.nombre);
		if (recibir_direccion(socket_fd, &direccion) != 0) {
			/* O la consola cortó la comunicación, o hubo un error. Cerramos todo. */
			terminar_servidor_de_alumno(socket_fd, el_aula, &alumno);
		}
		printf("%s: Movimiento recibido\n", alumno.nombre);
		
		/// Tratamos de movernos en nuestro modelo
		printf("%s: Voy a intenar moverme\n", alumno.nombre);
		bool pudo_moverse = intentar_moverse(el_aula, &alumno, direccion);

		printf("%s se movio a: (%d, %d)\n", alumno.nombre, alumno.posicion_fila, alumno.posicion_columna);

		/// Avisamos que ocurrio
		enviar_respuesta(socket_fd, pudo_moverse ? OK : OCUPADO);		
		//printf("aca3\n");
		
		if (alumno.salio)
			break;
	}
	
	colocar_mascara(el_aula, &alumno);

	t_aula_liberar(el_aula, &alumno);
	enviar_respuesta(socket_fd, LIBRE);
	
	printf("Listo, %s es libre!\n", alumno.nombre);
	
	return NULL;

}

int main(void)
{
	//signal(SIGUSR1, signal_terminar);
	int socketfd_cliente, socket_servidor, socket_size;
	struct sockaddr_in local, remoto;

	

	/* Crear un socket de tipo INET con TCP (SOCK_STREAM). */
	if ((socket_servidor = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("creando socket");
	}

	/* Crear nombre, usamos INADDR_ANY para indicar que cualquiera puede conectarse aquí. */
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(PORT);
	
	if (bind(socket_servidor, (struct sockaddr *)&local, sizeof(local)) == -1) {
		perror("haciendo bind");
	}

	/* Escuchar en el socket y permitir 5 conexiones en espera. */
	if (listen(socket_servidor, 5) == -1) {
		perror("escuchando");
	}
	
	t_aula el_aula;
	t_aula_iniciar_vacia(&el_aula);
	
		
	if (pthread_mutex_init(&mutex_cv_rescatistas, NULL) != 0 ) {
		printf("La inicialización de la variable de condición de los rescatistas falló.");
		return 1;
	}
	if (pthread_cond_init(&cv_rescatistas, NULL) != 0 ) {
		printf("La inicialización de la variable de condición de los rescatistas falló.");
		return 1;
	}
	if (pthread_mutex_init(&mutex_tanda, NULL) != 0 ) {
		printf("La inicialización de la variable de condición de los rescatistas falló.");
		return 1;
	}
	if (pthread_cond_init(&cv_tanda, NULL) != 0 ) {
		printf("La inicialización de la variable de condición de los rescatistas falló.");
		return 1;
	}
	
	/// Aceptar conexiones entrantes.
	socket_size = sizeof(remoto);
	int thread_count = 1;	
	for(;;){		
		if (-1 == (socketfd_cliente = 
					accept(socket_servidor, (struct sockaddr*) &remoto, (socklen_t*) &socket_size)))
		{			
			printf("!! Error al aceptar conexion\n");
		}
		else {						
			// initialize thread
			pthread_t current_thread;
			thdata current_data;
			current_data.thread_no = thread_count;
			current_data.el_aula = &el_aula;
			current_data.socketfd_cliente = socketfd_cliente;
			thread_count++;
			
			// atendedor_de_alumno(socketfd_cliente, &el_aula);
			pthread_create(&current_thread, NULL, (void *) &atendedor_de_alumno, (void *) &current_data);
		}
	}


	return 0;
}


