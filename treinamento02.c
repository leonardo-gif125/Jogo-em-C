/* Move no WASD, atira no Espaço, tubarão S, submarino X*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#pragma comment(lib, "winmm.lib")
#include <unistd.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

/* CONFIGURACOES DO JOGO */
#define LARGURA 70
#define ALTURA 20
#define MAX_INIMIGOS 6
#define MAX_TIROS 5
#define OXIGENIO_MAX 150
#define VIDAS_INICIAIS 3
#define ATRASO_QUADRO_MS 90
#define FRAMES_POR_INIMIGO 25

typedef enum {
    COR_PADRAO = 0,
    COR_AGUA,
    COR_JOGADOR,
    COR_TUBARAO,
    COR_SUBMARINO_INIMIGO,
    COR_TORPEDO,
    COR_SUPERFICIE,
    COR_FUNDO_MAR,
    COR_TEXTO,
    COR_OXIGENIO_OK,
    COR_OXIGENIO_BAIXO
} Cor;

typedef enum {
    FUNDO_PADRAO = 0,
    FUNDO_AGUA
} CorFundo;

typedef struct {
    int x, y;
    int direcao;
    int vidas;
    int oxigenio;
    int invencivel;
} Jogador;

typedef struct {
    int x, y;
    int direcao;
    int ativo;
} Torpedo;

typedef struct {
    int x, y;
    int direcao;
    int ativo;
    char simbolo;
    Cor cor;
    int quadro_explosao; /* 0 = normal, >0 indica que esta explodindo */
} Inimigo;

/* VARIAVEIS GLOBAIS */
Jogador jogador;
Torpedo torpedos[MAX_TIROS];
Inimigo inimigos[MAX_INIMIGOS];
int pontuacao = 0;
int jogo_rodando = 1;
int contador_quadros = 0;

#ifdef _WIN32
HANDLE console_saida;
#else
struct termios termios_original;
static int tecla_em_buffer = 0;
static char tecla_buffer;
#endif

/* FUNCOES DE TERMINAL */
#ifdef _WIN32
void inicializar_console(void) {
    CONSOLE_CURSOR_INFO info;
    console_saida = GetStdHandle(STD_OUTPUT_HANDLE);
    info.dwSize = 100;
    info.bVisible = FALSE;
    SetConsoleCursorInfo(console_saida, &info);
}
void restaurar_console(void) {
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = TRUE;
    SetConsoleCursorInfo(console_saida, &info);
}
void posicionar_cursor(int x, int y) {
    COORD coord;
    coord.X = (SHORT)x;
    coord.Y = (SHORT)y;
    SetConsoleCursorPosition(console_saida, coord);
}
int tecla_disponivel(void) { return _kbhit(); }
int ler_tecla(void) { return _getch(); }
void esperar_ms(int ms) { Sleep(ms); }
void tocar_som(int frequencia, int duracao_ms) { Beep(frequencia, duracao_ms); }
void limpar_tela(void) { system("cls"); }
#else
void inicializar_console(void) {
    struct termios novo;
    tcgetattr(STDIN_FILENO, &termios_original);
    novo = termios_original;
    novo.c_lflag &= ~(ICANON | ECHO);
    novo.c_cc[VMIN] = 0;
    novo.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &novo);
    printf("\033[?25l");
    fflush(stdout);
}
void restaurar_console(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &termios_original);
    printf("\033[?25h");
    fflush(stdout);
}
void posicionar_cursor(int x, int y) {
    printf("\033[%d;%dH", y + 1, x + 1);
}
int tecla_disponivel(void) {
    char c;
    if (tecla_em_buffer) return 1;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        tecla_buffer = c;
        tecla_em_buffer = 1;
        return 1;
    }
    return 0;
}
int ler_tecla(void) {
    if (tecla_em_buffer) {
        tecla_em_buffer = 0;
        return (int)tecla_buffer;
    }
    char c = 0;
    read(STDIN_FILENO, &c, 1);
    return (int)c;
}
void esperar_ms(int ms) { usleep(ms * 1000); }

#define TAXA 11025
typedef struct { int freq, ms; } Nota;
void escreve_wav(const char *path, const unsigned char *dados, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int taxa = TAXA, byterate = TAXA, sub1 = 16, datalen = n, riff = 36 + n;
    short fmt = 1, canais = 1, blockalign = 1, bits = 8;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&sub1, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&canais, 2, 1, f);
    fwrite(&taxa, 4, 1, f); fwrite(&byterate, 4, 1, f);
    fwrite(&blockalign, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&datalen, 4, 1, f);
    fwrite(dados, 1, n, f); fclose(f);
}
void tocar_som_wav(const char *path, const Nota *ns, int qtd) {
    static unsigned char buf[TAXA * 2];
    int n = 0;
    for (int i = 0; i < qtd; i++) {
        int amostras = ns[i].ms * TAXA / 1000;
        int meio = ns[i].freq > 0 ? (TAXA / ns[i].freq) / 2 : 1;
        for (int s = 0; s < amostras && n < (int)sizeof(buf); s++, n++) {
            if (ns[i].freq == 0) buf[n] = 128;
            else if (ns[i].freq < 0) buf[n] = (rand() & 1) ? 196 : 60;
            else buf[n] = ((s / meio) & 1) ? 196 : 60;
        }
    }
    escreve_wav(path, buf, n);
}
void tocar_som(int frequencia, int duracao_ms) {
    Nota ns[1];
    ns[0].freq = frequencia; ns[0].ms = duracao_ms;
    const char *tmp = "/tmp/sea_quest_sound.wav";
    tocar_som_wav(tmp, ns, 1);
#ifdef _APPLE_
    system("afplay /tmp/sea_quest_sound.wav >/dev/null 2>&1 &");
#else
    system("aplay -q /tmp/sea_quest_sound.wav >/dev/null 2>&1 &");
#endif
}
void limpar_tela(void) { system("clear"); }
#endif

void definir_cor(Cor cor) {
#ifdef _WIN32
    static const int tabela_windows[] = { 7, 9, 14, 12, 13, 15, 11, 6, 10, 10, 12 };
    SetConsoleTextAttribute(console_saida, tabela_windows[cor]);
#else
    static const char *tabela_ansi[] = {
        "\033[0m", "\033[1;34m", "\033[1;33m", "\033[1;31m", "\033[1;35m",
        "\033[1;37m", "\033[1;36m", "\033[0;33m", "\033[1;32m", "\033[1;32m", "\033[1;31m"
    };
    printf("%s", tabela_ansi[cor]);
#endif
}

void definir_cor_fundo(CorFundo fundo) {
#ifdef _WIN32
    static const int fundos_windows[] = { 0, 1 << 4 };
    SetConsoleTextAttribute(console_saida, 15 | fundos_windows[fundo]);
#else
    static const char *fundos_ansi[] = { "\033[0m", "\033[1;44m" };
    printf("%s", fundos_ansi[fundo]);
#endif
}

void desenhar_texto_centralizado(int y, const char *texto, Cor cor) {
    int x = (LARGURA - (int)strlen(texto)) / 2;
    if (x < 0) x = 0;
    posicionar_cursor(x, y);
    definir_cor(cor);
    printf("%s", texto);
}

/* LOGICA DO JOGO */
void inicializar_jogo(void) {
    int i;
    jogador.x = LARGURA / 4;
    jogador.y = ALTURA / 2;
    jogador.direcao = 1;
    jogador.vidas = VIDAS_INICIAIS;
    jogador.oxigenio = OXIGENIO_MAX;
    jogador.invencivel = 0;
    for (i = 0; i < MAX_TIROS; i++) torpedos[i].ativo = 0;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        inimigos[i].ativo = 0;
        inimigos[i].quadro_explosao = 0;
    }
    pontuacao = 0;
    contador_quadros = 0;
    srand((unsigned int)time(NULL));
}

void criar_inimigo(void) {
    int i;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].ativo) {
            int vem_da_esquerda = rand() % 2;
            inimigos[i].ativo = 1;
            inimigos[i].quadro_explosao = 0;
            inimigos[i].y = 2 + rand() % (ALTURA - 4);
            if (vem_da_esquerda) {
                inimigos[i].x = 1;
                inimigos[i].direcao = 1;
            } else {
                inimigos[i].x = LARGURA - 3;
                inimigos[i].direcao = -1;
            }
            if (rand() % 2) {
                inimigos[i].simbolo = 'S'; /* Tubarao */
                inimigos[i].cor = COR_TUBARAO;
            } else {
                inimigos[i].simbolo = 'X'; /* Submarino */
                inimigos[i].cor = COR_SUBMARINO_INIMIGO;
            }
            return;
        }
    }
}

void atirar_torpedo(void) {
    int i;
    for (i = 0; i < MAX_TIROS; i++) {
        if (!torpedos[i].ativo) {
            torpedos[i].ativo = 1;
            torpedos[i].direcao = jogador.direcao;
            torpedos[i].y = jogador.y;
            torpedos[i].x = jogador.x + (jogador.direcao == 1 ? 5 : -2);
            tocar_som(1200, 40);
            return;
        }
    }
}

void processar_entrada(void) {
    if (!tecla_disponivel()) return;
    switch (ler_tecla()) {
        case 'w': case 'W': if (jogador.y > 0) jogador.y--; break;
        case 's': case 'S': if (jogador.y < ALTURA - 2) jogador.y++; break;
        case 'a': case 'A': if (jogador.x > 1) jogador.x--; jogador.direcao = -1; break;
        case 'd': case 'D': if (jogador.x < LARGURA - 6) jogador.x++; jogador.direcao = 1; break;
        case ' ': atirar_torpedo(); break;
        case 'q': case 'Q': jogo_rodando = 0; break;
    }
}

void atualizar_torpedos(void) {
    int i;
    for (i = 0; i < MAX_TIROS; i++) {
        if (!torpedos[i].ativo) continue;
        torpedos[i].x += torpedos[i].direcao * 2;
        if (torpedos[i].x <= 0 || torpedos[i].x >= LARGURA - 1) {
            torpedos[i].ativo = 0;
        }
    }
}

void atualizar_inimigos(void) {
    int i;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].ativo) continue;
        
        /* Se estiver em processo de explosao, reduz o tempo */
        if (inimigos[i].quadro_explosao > 0) {
            inimigos[i].quadro_explosao--;
            if (inimigos[i].quadro_explosao == 0) {
                inimigos[i].ativo = 0;
            }
            continue;
        }

        if (contador_quadros % 2 == 0) {
            inimigos[i].x += inimigos[i].direcao;
            if (inimigos[i].x <= 0 || inimigos[i].x >= LARGURA - 2) {
                inimigos[i].ativo = 0;
            }
        }
    }
}

void verificar_colisoes(void) {
    int i, j;
    for (i = 0; i < MAX_TIROS; i++) {
        if (!torpedos[i].ativo) continue;
        for (j = 0; j < MAX_INIMIGOS; j++) {
            if (inimigos[j].ativo && inimigos[j].quadro_explosao == 0 &&
                inimigos[j].y == torpedos[i].y &&
                abs(inimigos[j].x - torpedos[i].x) <= 1) {
                
                inimigos[j].quadro_explosao = 3; /* Fica ativo por mais 3 quadros exibindo '*' */
                torpedos[i].ativo = 0;
                pontuacao += 10;
                tocar_som(600, 80);
            }
        }
    }

    if (jogador.invencivel > 0) {
        jogador.invencivel--;
        return;
    }

    for (j = 0; j < MAX_INIMIGOS; j++) {
        if (inimigos[j].ativo && inimigos[j].quadro_explosao == 0 &&
            inimigos[j].y == jogador.y &&
            abs(inimigos[j].x - jogador.x) <= 2) {
            
            jogador.vidas--;
            jogador.invencivel = 20;
            tocar_som(150, 300);
            jogador.x = LARGURA / 4;
            jogador.y = ALTURA / 2;
            inimigos[j].ativo = 0;
        }
    }
}

void atualizar_oxigenio(void) {
    if (jogador.y == 0) {
        jogador.oxigenio += 4;
        if (jogador.oxigenio > OXIGENIO_MAX) jogador.oxigenio = OXIGENIO_MAX;
    } else {
        jogador.oxigenio--;
        if (jogador.oxigenio <= 0) {
            jogador.vidas--;
            jogador.oxigenio = OXIGENIO_MAX;
            jogador.x = LARGURA / 4;
            jogador.y = ALTURA / 2;
            tocar_som(100, 500);
        }
    }
}

/* DESENHO NA TELA */
void desenhar_jogo(void) {
    int x, i;
    int tamanho_barra;
    char barra_oxigenio[21];

    /* Linha da superficie animada */
    definir_cor(COR_SUPERFICIE);
    definir_cor_fundo(FUNDO_PADRAO);
    posicionar_cursor(0, 0);
    for (x = 0; x < LARGURA; x++) {
        /* Alterna o padrão das ondas com base nos quadros */
        if ((x + contador_quadros / 4) % 4 == 0) putchar('-');
        else putchar('~');
    }

    /* Area de agua com gerador de bolhas aleatorias */
    definir_cor_fundo(FUNDO_AGUA);
    for (i = 1; i < ALTURA - 1; i++) {
        posicionar_cursor(0, i);
        for (x = 0; x < LARGURA; x++) {
            /* Semente baseada na posicao para criar pequenas bolhas subindo */
            if ((x * i + contador_quadros) % 197 == 0) {
                definir_cor(COR_SUPERFICIE);
                putchar('o');
            } else if ((x * i + contador_quadros * 2) % 233 == 0) {
                definir_cor(COR_SUPERFICIE);
                putchar('.');
            } else {
                putchar(' ');
            }
        }
    }
    definir_cor_fundo(FUNDO_PADRAO);

    /* Fundo do mar */
    definir_cor(COR_FUNDO_MAR);
    definir_cor_fundo(FUNDO_AGUA);
    posicionar_cursor(0, ALTURA - 1);
    for (x = 0; x < LARGURA; x++) putchar('_');

    /* Torpedos direcionais */
    definir_cor(COR_TORPEDO);
    definir_cor_fundo(FUNDO_AGUA);
    for (i = 0; i < MAX_TIROS; i++) {
        if (torpedos[i].ativo) {
            posicionar_cursor(torpedos[i].x, torpedos[i].y);
            if (torpedos[i].direcao == 1) printf("->");
            else printf("<-");
        }
    }

    /* Inimigos com sprites direcionais e efeito de explosao */
    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (inimigos[i].ativo) {
            posicionar_cursor(inimigos[i].x, inimigos[i].y);
            definir_cor_fundo(FUNDO_AGUA);
            if (inimigos[i].quadro_explosao > 0) {
                definir_cor(COR_TORPEDO);
                printf("***");
            } else {
                definir_cor(inimigos[i].cor);
                if (inimigos[i].simbolo == 'S') { /* Tubarao */
                    if (inimigos[i].direcao == 1) printf("G>");
                    else printf("<G");
                } else { /* Submarino Inimigo */
                    if (inimigos[i].direcao == 1) printf("(X)");
                    else printf("(X)");
                }
            }
        }
    }

    /* Jogador pisca se estiver invencivel */
    if (jogador.invencivel == 0 || (contador_quadros % 4 < 2)) {
        definir_cor(COR_JOGADOR);
        definir_cor_fundo(FUNDO_AGUA);
        posicionar_cursor(jogador.x, jogador.y);
        if (jogador.direcao == 1) printf("+==>>");
        else printf("<<==+");
    }

    /* HUD: Pontuação e Vidas representadas por graficos simples (<3) */
    definir_cor(COR_TEXTO);
    definir_cor_fundo(FUNDO_PADRAO);
    posicionar_cursor(0, ALTURA);
    printf("PONTOS: %-6d VIDAS: ", pontuacao);
    definir_cor(COR_TUBARAO);
    for (i = 0; i < VIDAS_INICIAIS; i++) {
        if (i < jogador.vidas) printf("<3 ");
        else printf("   ");
    }

    /* HUD: Barra de oxigenio estilizada com caracteres simples */
    tamanho_barra = jogador.oxigenio / 7.5; /* Escala de 0 a 20 blocos */
    if (tamanho_barra < 0) tamanho_barra = 0;
    if (tamanho_barra > 20) tamanho_barra = 20;
    
    for (i = 0; i < 20; i++) {
        if (i < tamanho_barra) barra_oxigenio[i] = '#'; /* Cheio */
        else barra_oxigenio[i] = '='; /* Vazio */
    }
    barra_oxigenio[20] = '\0';

    posicionar_cursor(0, ALTURA + 1);
    definir_cor(COR_TEXTO);
    printf("OXIGENIO: [");
    definir_cor(jogador.oxigenio < 45 ? COR_OXIGENIO_BAIXO : COR_OXIGENIO_OK);
    printf("%s", barra_oxigenio);
    definir_cor(COR_TEXTO);
    printf("] ");
    definir_cor(COR_PADRAO);
    fflush(stdout);
}

void tela_inicial(void) {
    limpar_tela();
    desenhar_texto_centralizado(ALTURA / 2 - 3, "=== SEA QUEST (ASCII TOTAL) ===", COR_JOGADOR);
    desenhar_texto_centralizado(ALTURA / 2 - 1, "WASD para mover, ESPACO para atirar, Q para sair", COR_TEXTO);
    desenhar_texto_centralizado(ALTURA / 2,     "Fique de olho no oxigenio! Suba para respirar.", COR_OXIGENIO_OK);
    desenhar_texto_centralizado(ALTURA / 2 + 2, "Pressione qualquer tecla para comecar...", COR_SUPERFICIE);
    definir_cor(COR_PADRAO);
    fflush(stdout);
    while (!tecla_disponivel()) esperar_ms(50);
    ler_tecla();
}

void tela_fim_de_jogo(void) {
    char texto_pontos[64];
    limpar_tela();
    desenhar_texto_centralizado(ALTURA / 2 - 2, "FIM DE JOGO", COR_TUBARAO);
    sprintf(texto_pontos, "Pontuacao final: %d", pontuacao);
    desenhar_texto_centralizado(ALTURA / 2, texto_pontos, COR_TEXTO);
    definir_cor(COR_PADRAO);
    fflush(stdout);
    esperar_ms(2000);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    inicializar_console();
    tela_inicial();
    inicializar_jogo();
    limpar_tela();

    while (jogo_rodando && jogador.vidas > 0) {
        processar_entrada();
        if (contador_quadros % FRAMES_POR_INIMIGO == 0) {
            criar_inimigo();
        }
        atualizar_torpedos();
        atualizar_inimigos();
        verificar_colisoes();
        atualizar_oxigenio();
        desenhar_jogo();
        
        contador_quadros++;
        esperar_ms(ATRASO_QUADRO_MS);
    }

    tela_fim_de_jogo();
    restaurar_console();
    return 0;
}