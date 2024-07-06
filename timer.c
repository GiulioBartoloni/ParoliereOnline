//la libreria contiene funzioni per la gestione del timer del server
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "macros.h"
#include "timer.h"

//la funzione inizializza un timer creando la mutex e impostando la durata a 0
void initTimer(Timer *timer){
    int retval;
    SYSC(retval, pthread_mutex_init(&timer->timerMutex, NULL), "pthread_mutex_init:");
    timer->duration = 0;
}

//la funzione avvia il timer impostando la durata corretta
void startTimer(Timer *timer, int duration){
    int retval;

    SYSC(retval, pthread_mutex_lock(&timer->timerMutex), "pthread_mutex_lock:");
    timer->duration = duration;
    timer->startTime = time(NULL);
    
    SYSC(retval, pthread_mutex_unlock(&timer->timerMutex), "pthread_mutex_unlock:");
}

//la funzione permette di recuperare il tempo rimanente ad un timer
int getRemainingTime(Timer *timer){
    int retval;

    SYSC(retval, pthread_mutex_lock(&timer->timerMutex), "pthread_mutex_lock:");

    time_t currentTime = time(NULL);
    double remainingTime = timer->duration - difftime(currentTime, timer->startTime);
    if (remainingTime < 0)
        remainingTime = 0;

    SYSC(retval, pthread_mutex_unlock(&timer->timerMutex), "pthread_mutex_unlock:");

    return (int)remainingTime;
}