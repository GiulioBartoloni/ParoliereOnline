#ifndef MATRIX_H
#define MATRIX_H

#include <stdio.h>

void retrieveMatrix(char matrix[4][4],char *result);
void readLine(FILE *matrixFile, char matrix[4][4]);
void printMatrix(char matrix[4][4]);
void randomizeMatrix(char matrix[4][4]);
int isCellValid(int i, int j, int visited[4][4]);
int crawlMatrix(char matrix[4][4], char *word, int wordIndex, int row, int col, int visited[4][4]);
int searchWord(char matrix[4][4], char* word);
int quParse(char *string);

#endif