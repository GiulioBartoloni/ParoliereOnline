#ifndef TIMER_H
#define TIMER_H

#include <pthread.h>

typedef struct{
    time_t startTime;
    int duration;
    pthread_mutex_t timerMutex;
} Timer;

void initTimer(Timer *timer);
void startTimer(Timer *timer, int duration);
int getRemainingTime(Timer *timer);

#endif