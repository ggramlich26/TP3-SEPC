#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <complex.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>

#include "tsp-types.h"
#include "tsp-job.h"
#include "tsp-genmap.h"
#include "tsp-print.h"
#include "tsp-tsp.h"
#include "tsp-lp.h"
#include "tsp-hkbound.h"


/* macro de mesure de temps, retourne une valeur en nanosecondes */
#define TIME_DIFF(t1, t2) \
  ((t2.tv_sec - t1.tv_sec) * 1000000000ll + (long long int) (t2.tv_nsec - t1.tv_nsec))


/* tableau des distances */
tsp_distance_matrix_t tsp_distance ={};

/** Paramètres **/

/* nombre de villes */
int nb_towns=10;
/* graine */
long int myseed= 0;
/* nombre de threads */
int nb_threads=1;
/* best solution*/
tsp_path_t sol;
int sol_len;
pthread_mutex_t sol_mutex;
/* Job queue */
struct tsp_queue q;
pthread_mutex_t q_mutex;
/* minimum */
extern pthread_mutex_t minimum_mutex;
/* cuts */
long long int cuts = 0;
extern pthread_mutex_t cuts_mutex;

/* affichage SVG */
bool affiche_sol= false;
bool affiche_progress=false;
bool quiet=false;

static void generate_tsp_jobs (struct tsp_queue *q, int hops, int len, uint64_t vpres, tsp_path_t path, long long int *cuts, tsp_path_t sol, int *sol_len, int depth)
{
    if (len >= minimum) {
        (*cuts)++ ;
        return;
    }
    
    if (hops == depth) {
        /* On enregistre du travail à faire plus tard... */
      add_job (q, path, hops, len, vpres);
    } else {
        int me = path [hops - 1];        
        for (int i = 0; i < nb_towns; i++) {
	  if (!present (i, hops, path, vpres)) {
                path[hops] = i;
		vpres |= (1<<i);
                int dist = tsp_distance[me][i];
                generate_tsp_jobs (q, hops + 1, len + dist, vpres, path, cuts, sol, sol_len, depth);
		vpres &= (~(1<<i));
            }
        }
    }
}

static void usage(const char *name) {
  fprintf (stderr, "Usage: %s [-s] <ncities> <seed> <nthreads>\n", name);
  exit (-1);
}

void *main_tsp(void *arg){
    unsigned long long perf;
    struct timespec t1, t2;
    uint64_t vpres=0;
	long long int myCuts = 0;

    clock_gettime (CLOCK_REALTIME, &t1);

    /* calculer chacun des travaux */
    tsp_path_t solution;
    memset (solution, -1, MAX_TOWNS * sizeof (int));
    solution[0] = 0;
	pthread_mutex_lock(&q_mutex);
    while (!empty_queue (&q, &q_mutex)) {
        int hops = 0, len = 0;
        get_job (&q, solution, &hops, &len, &vpres, &q_mutex);
		pthread_mutex_unlock(&q_mutex);

		//printf("Thread %ld\n", syscall(SYS_gettid));
	
	// le noeud est moins bon que la solution courante
	if (minimum < INT_MAX
	    && (nb_towns - hops) > 10
	    && ( (lower_bound_using_hk(solution, hops, len, vpres)) >= minimum 
		|| (lower_bound_using_lp(solution, hops, len, vpres)) >= minimum)){

		pthread_mutex_lock(&q_mutex);
		continue;
	}

	tsp (hops, len, vpres, solution, &myCuts, sol, &sol_len);
	pthread_mutex_lock(&q_mutex);
    }
	pthread_mutex_unlock(&q_mutex);

	/* update cuts */
	pthread_mutex_lock(&cuts_mutex);
	cuts += myCuts;
	pthread_mutex_unlock(&cuts_mutex);

    clock_gettime (CLOCK_REALTIME, &t2);
    perf = TIME_DIFF (t1,t2);
	printf("Son thread %ld finished after %lld.%03lld ms\n\n", syscall(SYS_gettid), perf/1000000ll, perf%1000000ll);
    
    return 0 ;

}

int main (int argc, char **argv)
{
    unsigned long long perf;
    tsp_path_t path;
    uint64_t vpres=0;
    struct timespec t1, t2;

	pthread_mutex_init(&q_mutex, NULL);
	pthread_mutex_init(&sol_mutex, NULL);
	pthread_mutex_init(&minimum_mutex, NULL);
	pthread_mutex_init(&cuts_mutex, NULL);

    /* lire les arguments */
    int opt;
    while ((opt = getopt(argc, argv, "spq")) != -1) {
      switch (opt) {
      case 's':
	affiche_sol = true;
	break;
      case 'p':
	affiche_progress = true;
	break;
      case 'q':
	quiet = true;
	break;
      default:
	usage(argv[0]);
	break;
      }
    }

    if (optind != argc-3)
      usage(argv[0]);

    nb_towns = atoi(argv[optind]);
    myseed = atol(argv[optind+1]);
    nb_threads = atoi(argv[optind+2]);
    assert(nb_towns > 0);
    assert(nb_threads > 0);
   
    minimum = INT_MAX;
      
    /* generer la carte et la matrice de distance */
    if (! quiet)
      fprintf (stderr, "ncities = %3d\n", nb_towns);
    genmap ();

    init_queue (&q);

    clock_gettime (CLOCK_REALTIME, &t1);

    memset (path, -1, MAX_TOWNS * sizeof (int));
    path[0] = 0;
    vpres=1;

    /* mettre les travaux dans la file d'attente */
    generate_tsp_jobs (&q, 1, 0, vpres, path, &cuts, sol, & sol_len, 3);
    no_more_jobs (&q);

	/* generate other threads */
	pthread_t *tids = malloc((nb_threads-1)*sizeof(pthread_t));
	for(int i = 0; i < nb_threads-1; i++){
		pthread_create(&(tids[i]), NULL, main_tsp, NULL);
	}
   
	main_tsp(NULL);
//    /* calculer chacun des travaux */
//	long long int myCuts = 0;
//    tsp_path_t solution;
//    memset (solution, -1, MAX_TOWNS * sizeof (int));
//    solution[0] = 0;
//    while (!empty_queue (&q, &q_mutex)) {
//        int hops = 0, len = 0;
//        get_job (&q, solution, &hops, &len, &vpres, &q_mutex);
//	
//	// le noeud est moins bon que la solution courante
//	if (minimum < INT_MAX
//	    && (nb_towns - hops) > 10
//	    && ( (lower_bound_using_hk(solution, hops, len, vpres)) >= minimum
//		 || (lower_bound_using_lp(solution, hops, len, vpres)) >= minimum)
//	    )
//
//	  continue;
//
//	tsp (hops, len, vpres, solution, &myCuts, sol, &sol_len);
//    }
//
//	/* update cuts */
//	pthread_mutex_lock(&cuts_mutex);
//	cuts += myCuts;
//	pthread_mutex_unlock(&cuts_mutex);

//    clock_gettime (CLOCK_REALTIME, &t2);
//    perf = TIME_DIFF (t1,t2);
//	printf("Main thread finished after %lld.%03lld ms\n\n", perf/1000000ll, perf%1000000ll);

	/* wait for other threads */
	for(int i = 0; i < nb_threads-1; i++){
		pthread_join(tids[i], NULL);
	}
    
    clock_gettime (CLOCK_REALTIME, &t2);

    if (affiche_sol)
      //print_solution_svg (sol, sol_len);
      print_solution_svg_to_file (sol, sol_len);

    perf = TIME_DIFF (t1,t2);
    printf("<!-- # = %d seed = %ld len = %d threads = %d time = %lld.%03lld ms ( %lld coupures ) -->\n",
	   nb_towns, myseed, sol_len, nb_threads,
	   perf/1000000ll, perf%1000000ll, cuts);

	pthread_mutex_destroy(&q_mutex);
	pthread_mutex_destroy(&sol_mutex);
	pthread_mutex_destroy(&minimum_mutex);
	pthread_mutex_destroy(&cuts_mutex);

    return 0 ;
}
