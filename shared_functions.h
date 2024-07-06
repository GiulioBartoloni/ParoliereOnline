#ifndef SHARED_FUNCTIONS_H
#define SHARED_FUNCTIONS_H

int isNumber(char *string);
void requiredArgs(char *arg2, int *port);
void composeMessage(char **buffer, char messageType, char *message);

#endif
