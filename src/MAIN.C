#include <alloc.h>
#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int                old_mode, lives = 5;
long long          score = 0;
clock_t            aclock, amoveclock, bclock;
unsigned char      BG            = 106;
const unsigned int screen_width  = 320;
const unsigned int screen_height = 200;
const unsigned int screen_size   = 64000;

unsigned char far *
              chr;  // asta e un pointer catre adresa placii video unde sunt stocate caracterele
unsigned char far * screen;
unsigned char far * buff;

typedef struct
{
  int           width, height;
  unsigned char far * content;
} surface;

surface nava;
int     navax, navay;
surface bullet;
surface alien;
surface heart;

struct lista
{
  int            x, y;
  struct lista * next;
};

struct lista *bullets = NULL, *bullend = NULL;
struct lista *aliens = NULL, *aliend = NULL;

// este pentru modul 13h adica 255 de culor si rezolutia 320x200
void start_mode13(void)
{
  union REGS in, out;
  // for old_mode
  in.h.ah = 0xf;
  int86(0x10, &in, &out);
  old_mode = out.h.al;
  // start mode 13
  in.h.ah = 0;
  in.h.al = 0x13;
  int86(0x10, &in, &out);
}

// stop 13h
void stop_mode13(void)
{
  union REGS in, out;
  in.h.ah = 0;
  in.h.al = old_mode;
  int86(0x10, &in, &out);
}

// alocarea memoriei pentru buffer si pornirea modului 13h
// incercare alocare memorie pentru buffer
// Daca rulati programul in borland C va rugam sa setati memoria heam mai mare de 64K
int start_video_mode(void)
{
  buff = (unsigned char far *)farmalloc(screen_size);
  if (buff)
  {
    screen = MK_FP(0xa000, 0);
    chr    = (unsigned char far *)0xF000FA6EL;
    start_mode13();
    _fmemset(buff, BG, screen_size);
    return 1;
  }
  else
  {
    printf("Out of memory!\n");
    getch();
    return 0;
  }
}

void update_screen(void)
{
  // copierea bufferului pe memoria video
  while (inportb(0x3da) & 8)
    ;

  // pentru monitoarele crt astepta vertical retrace
  while (!(inport(0x3da) & 8))
    ;

  _fmemcpy(screen, buff, screen_size);
}

// incarca extraterestru, inimile si gloantele din harta de biti care este
// scrisa in fisiere .nik
void createImg(char * filename, surface * comp)
{
  int    i, c;
  FILE * f = fopen(filename, "r");
  fscanf(f, "%d %d", &comp->width, &comp->height);

  comp->content = farmalloc(comp->width * comp->height);
  for (i = 0; i < comp->width * comp->height; i++)
  {
    fscanf(f, "%d", &c);
    if (c == 0)
      comp->content[i] = BG;
    else
      comp->content[i] = c;
  }
  fclose(f);
}

// pentru a inscrie un obect in buffer functia aceasta trebue sa fie optimizata la maxim pentru ca
// este folosita foarte des
void blitSurface(int x, int y, surface * s)
{
  unsigned char far * p;
  unsigned int        i, bp = 0;
  p = (buff + (y << 8) + (y << 6) + x);
  for (i = 0; i < s->height; i++)
  {
    _fmemcpy(p, s->content + bp, s->width);
    bp += s->width;
    p += screen_width;
  }
}

// asta extrage un caracter din memoria video si o inregistreaza in buffer in memoria video un
// caracter are dimensiunile 8x8 si ocupa doar 8 octeti adica 1 octet pentru o linie si un bit
// pentru un pixel
void printChr(int x, int y, char c)
{
  int           i, j;
  unsigned char far *p, far *sc;
  p  = chr + c * 8;
  sc = (buff + (y << 8) + (y << 6) + x);
  for (i = 0; i < 8; i++)
  {
    for (j = 0; j < 8; j++)
    {
      if ((1 << (7 - j)) & (*p))
        *(sc + j) = 3;
      else
        *(sc + j) = BG;
    }
    p++;
    sc += screen_width;
  }
}

// asta e pentru a printa un sir (i<<3) => (i*8) dar am pus i<<3 pentru ca este mai rapid
void printS(char * str)
{
  int i;
  for (i = 0; i < strlen(str); i++)
  {
    printChr((i << 3), 1, str[i]);
  }
}

// aici se atribue BG la tot bufferul
void clear_buffer(void)
{
  _fmemset(buff, BG, screen_size);
}

// aici se adauga un nod la o lista simplu inlantuita adica ori un extraterestru ori un glonte
void add_comp(struct lista ** comp, struct lista ** compend, int x, int y)
{
  struct lista * temp;

  if (*comp == NULL)
  {
    *comp         = malloc(sizeof(struct lista));
    (*comp)->next = NULL;
    (*comp)->x    = x;
    (*comp)->y    = y;
    (*compend)    = (*comp);
  }
  else
  {
    temp             = malloc(sizeof(struct lista));
    temp->next       = NULL;
    temp->x          = x;
    temp->y          = y;
    (*compend)->next = temp;
    (*compend)       = temp;
  }
}

// adauga in buffer un obect pe coordonatele
// x,y care sunt in nodurile listei
void blitComp(struct lista * comp, surface * scomp)
{
  struct lista * it = comp;
  while (it != NULL)
  {
    blitSurface(it->x, it->y, scomp);
    it = it->next;
  }
}

// lipeste inimioarele in buffer
void blitLives(void)
{
  int i, x, y;
  x = 320;
  y = 1;
  for (i = 1; i <= lives; i++)
  {
    x -= heart.width + 1;
    blitSurface(x, y, &heart);
  }
}

// se calculeaza miscarea gloantelor si eliminarea unui nod daca glontele ese in afara ecranului
void moveBullets(void)
{
  struct lista *it = bullets, *it2, *del;
  while (it != NULL)
  {
    if (it->y - 3 < 0)
    {
      if (it == bullets)
      {
        del     = bullets;
        bullets = bullets->next;
        it      = bullets;
        free(del);
      }
      else
      {
        del       = it;
        it        = it->next;
        it2->next = it;
        free(del);
      }
    }
    else
    {
      it->y -= 3;
      it2 = it;
      it  = it->next;
    }
  }
}

// aici se misca extraterestri pina la marginea ecranului
void moveAliens(void)
{
  struct lista *it = aliens, *it2, *del;
  if (clock() - amoveclock < 1)
    return;
  amoveclock = clock();

  while (it != NULL)
  {
    if (it->y + 1 > screen_height - alien.height)
    {
      if (it == aliens)
      {
        del    = aliens;
        aliens = aliens->next;
        it     = aliens;
        free(del);
      }
      else
      {
        del       = it;
        it        = it->next;
        it2->next = it;
        free(del);
      }
    }
    else
    {
      it->y += 1;
      it2 = it;
      it  = it->next;
    }
  }
  if (clock() - aclock > 30)
  {
    aclock = clock();
    add_comp(&aliens, &aliend, rand() % (screen_width - alien.width), 0);
  }
}

// se calculeaza coleziunile si se elimina nodurile
void boom(void)
{
  struct lista *it1 = bullets, *it2 = aliens, *del, *dit1, *dit2;
  char          stop;
  while (it2 != NULL)
  {
    if (it2->y > navay - alien.height && ((navax >= it2->x && it2->x + alien.width > navax) ||
                                          (it2->x > navax && navax + nava.width > it2->x)))
    {
      if (it2 == aliens)
      {
        del    = aliens;
        aliens = aliens->next;
        it2    = aliens;
        free(del);
      }
      else
      {
        del        = it2;
        it2        = it2->next;
        dit2->next = it2;
        free(del);
      }
      lives--;
      continue;
    }
    it1  = bullets;
    stop = 0;
    while (stop != 1 && it1 != NULL)
    {
      if ((it1->x >= it2->x && it1->x - it2->x < alien.width) ||
          (it2->x > it1->x && it2->x - it1->x < bullet.width))
      {
        if ((it2->y <= it1->y && it1->y - it2->y < alien.height) ||
            (it1->y < it2->y && it2->y - it1->y < bullet.height))
        {
          if (it2 == aliens)
          {
            del    = aliens;
            aliens = aliens->next;
            it2    = aliens;
            free(del);
          }
          else
          {
            del        = it2;
            it2        = it2->next;
            dit2->next = it2;
            free(del);
          }
          if (it1 == bullets)
          {
            del     = bullets;
            bullets = bullets->next;
            it1     = bullets;
            free(del);
          }
          else
          {
            del        = it1;
            it1        = it1->next;
            dit1->next = it1;
            free(del);
          }
          stop = 1;
          score += 10;
        }
      }
      if (stop != 1)
      {
        dit1 = it1;
        it1  = it1->next;
      }
    }
    if (stop != 1)
    {
      dit2 = it2;
      it2  = it2->next;
    }
  }
}

// chind se termina jocul defapt cand se termina din cauza ca sau perdut toate vetile
void game_over(void)
{
  delay(2000);
  stop_mode13();
  printf("GAME OVER!!!\n");
  printf("Score:%d\n", score);
  printf("Press ESC to exit!\n");
  while (getch() != 27)
    ;
}

// initializarea initiala
int start(void)
{
  clrscr();
  printf("Welcome to SPACE INVADERS\n");
  printf("Telechi Nicolae all right reserved!\n");
  printf("Press any key to continue or ESC to exit...\n");
  if (getch() == 27)
    exit(0);

  if (!start_video_mode())
    return 0;

  createImg("nava.nik", &nava);
  createImg("bullet.nik", &bullet);
  createImg("alien.nik", &alien);
  createImg("heart.nik", &heart);

  navax  = (screen_width - nava.width) >> 1;
  navay  = (screen_height - nava.height);
  aclock = bclock = amoveclock = clock();

  return 1;
}

// compunerea tuturor componentelor si crearea actiunilor
void run(void)
{
  int  done = 0;
  char c, str[15];
  while (!done)
  {
    clear_buffer();
    moveBullets();
    moveAliens();
    if (kbhit())
    {
      c = getch();
      if (c == 0)
      {
        switch (getch())
        {
        case 77:
          navax += 4;
          if (navax + nava.width > screen_width)
            navax = screen_width - nava.width;
          break;
        case 75:
          navax -= 4;
          if (navax < 0)
            navax = 0;
          break;
        }
      }
      else if (c == 32 && clock() - bclock > 2)
      {
        add_comp(&bullets, &bullend, navax + ((nava.width - bullet.width) >> 1),
                 navay - bullet.height);
        bclock = clock();
      }
      else if (c == 27)
        done = 1;
    }
    boom();
    blitComp(aliens, &alien);
    blitComp(bullets, &bullet);
    sprintf(str, "Score:%d", score);
    printS(str);
    blitLives();
    blitSurface(navax, navay, &nava);
    update_screen();
    if (lives <= 0)
    {
      game_over();
      done = 1;
    }
  }
}

// sfirsit de program eliberarea de memorie si esirea din 13h returnarea in text mode 03 adica
void stop(void)
{
  stop_mode13();
  farfree(buff);
  free(bullets);
  free(aliens);
}

// main :D
int main()
{
  if (!start())
    return 1;

  run();

  stop();
  return 0;
}
