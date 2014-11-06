#include <signal.h>
#include <errno.h>

#include "biblioteca.h"

/* Estructura que almacena los datos de una reserva. */
typedef struct {
	int posiciones[ANCHO_AULA][ALTO_AULA];
	pthread_mutex_t locks[ANCHO_AULA][ALTO_AULA];
	pthread_mutex_t lock_cantidad;
	int cantidad_de_personas;
	
	int rescatistas_disponibles;
} t_aula;

typedef struct thread_data {
	int thread_no;
	int socketfd_cliente;
	t_aula *el_aula;
} thdata;

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

void t_aula_liberar(t_aula *un_aula, t_persona *alumno)
{
	pthread_mutex_lock(&(un_aula->lock_cantidad));		
	un_aula->cantidad_de_personas--;
	pthread_mutex_unlock(&(un_aula->lock_cantidad));		
}

static void terminar_servidor_de_alumno(int socket_fd, t_aula *aula, t_persona *alumno) {
	printf(">> Se interrumpió la comunicación con una consola.\n");
		
	close(socket_fd);
	
	t_aula_liberar(aula, alumno);
	//exit(-1);
	pthread_exit(0);
}

void lockear_posiciones_en_orden(t_aula *el_aula, t_persona *alumno, t_direccion dir)
{
	int fila1 = alumno->posicion_fila;
	int fila2 = alumno->posicion_fila;
	int columna1 = alumno->posicion_columna;
	int columna2 = alumno->posicion_columna;
	
	if (dir == ARRIBA) {
		columna1--;
	} else if (dir == DERECHA) {
		columna2++;
	} else if (dir == ABAJO) {
		fila2++;
	} else if (dir == IZQUIERDA) {
		columna1--;
	}
	
	pthread_mutex_lock(&(el_aula->locks[fila1][columna1]));
	pthread_mutex_lock(&(el_aula->locks[fila2][columna2]));
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
		
		pthread_mutex_lock(&(el_aula->locks[fila1][columna1]));
		pthread_mutex_lock(&(el_aula->locks[fila2][columna2]));
		
		
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
		
	alumno->tiene_mascara = true;
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
		if (recibir_direccion(socket_fd, &direccion) != 0) {
			/* O la consola cortó la comunicación, o hubo un error. Cerramos todo. */
			terminar_servidor_de_alumno(socket_fd, el_aula, &alumno);
		}
		
		/// Tratamos de movernos en nuestro modelo
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

