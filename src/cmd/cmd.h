#ifndef CMD_H
#define CMD_H

int init();
int catFile(int argc, char *argv[]);
int hashObject(int argc, char *argv[]);
int LSTree(int argc, char *argv[]);
char* writeTree(char *dirname);
int commitTree(int argc, char *argv[]);

#endif // CMD_H