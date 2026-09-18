#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MAXLINE 1024
#define MAXARGS 10

static int
fork1(void)
{
  int pid = fork();
  if(pid < 0){
    fprintf(2, "nsh: fork failed\n");
    exit(1);
  }
  return pid;
}

static char *
skip(char *s)
{
  while(*s == ' ' || *s == '\t')
    s++;
  return s;
}

// Run one command string.  The caller has already arranged any pipe ends.
static void run(char *s);

static void
runpipe(char *left, char *right)
{
  int p[2];
  if(pipe(p) < 0){ fprintf(2, "nsh: pipe failed\n"); exit(1); }
  if(fork1() == 0){
    close(1); dup(p[1]); close(p[0]); close(p[1]); run(left);
  }
  if(fork1() == 0){
    close(0); dup(p[0]); close(p[0]); close(p[1]); run(right);
  }
  close(p[0]); close(p[1]);
  wait(0); wait(0);
  exit(0);
}

static void
run(char *s)
{
  char *argv[MAXARGS], *in = 0, *out = 0, *p;
  int argc = 0;

  s = skip(s);
  for(p = s; *p; p++){
    if(*p == '|'){
      *p = 0;
      runpipe(s, p + 1);
    }
  }

  p = s;
  while(*(p = skip(p))){
    if(*p == '<' || *p == '>'){
      int output = *p++ == '>';
      p = skip(p);
      char *name = p;
      while(*p && *p != ' ' && *p != '\t') p++;
      if(*p) *p++ = 0;
      if(*name == 0){ fprintf(2, "nsh: missing file\n"); exit(1); }
      if(output) out = name; else in = name;
      continue;
    }
    if(argc >= MAXARGS - 1){ fprintf(2, "nsh: too many arguments\n"); exit(1); }
    argv[argc++] = p;
    while(*p && *p != ' ' && *p != '\t' && *p != '<' && *p != '>') p++;
    if(*p) *p++ = 0;
  }
  argv[argc] = 0;
  if(argc == 0) exit(0);
  if(in){
    close(0);
    if(open(in, O_RDONLY) != 0){ fprintf(2, "nsh: cannot open %s\n", in); exit(1); }
  }
  if(out){
    close(1);
    if(open(out, O_CREATE|O_WRONLY) != 1){ fprintf(2, "nsh: cannot open %s\n", out); exit(1); }
  }
  exec(argv[0], argv);
  fprintf(2, "nsh: exec %s failed\n", argv[0]);
  exit(1);
}

int
main(void)
{
  char line[MAXLINE];
  int fd;
  while((fd = open("console", O_RDWR)) >= 0){ if(fd >= 3){ close(fd); break; } }
  for(;;){
    fprintf(2, "@ ");
    memset(line, 0, sizeof(line));
    if(gets(line, sizeof(line)) == 0) break;
    if(line[0] == 0) break;
    line[strlen(line)-1] = 0;
    if(line[0] == 0) continue;
    if(fork1() == 0) run(line);
    wait(0);
  }
  exit(0);
}
