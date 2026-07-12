/* =========================================================================
   PROJETO: SEA QUEST (EDICAO HORIZONTE DO ATARI - VERSAO ULTRA FLUIDA)
   CONTROLES: Move no WASD, atira no Espaco, Q para Sair.
   ========================================================================= */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
#else
    #include <termios.h>
    #include <unistd.h>
#endif

/* --- CONFIGURACOES GERAIS --- */
#define LARGURA 70
#define ALTURA 20
#define LINHA_HORIZONTE 10
#define MAX_INIMIGOS 6
#define MAX_TIROS 5
#define MAX_MERGULHADORES 3
#define MAX_CARGA_HUMANA 6
#define OXIGENIO_MAX 150
#define VIDAS_INICIAIS 3
#define ATRASO_QUADRO_MS 90
#define FRAMES_POR_INIMIGO 25
#define FRAMES_POR_MERGULHADOR 60

/* --- CONSTANTES DE SPRITES E ENGENHARIA --- */
#define TAMANHO_SPRITE_JOGADOR 5
#define TAMANHO_SPRITE_INIMIGO 3
#define TAMANHO_SPRITE_TORPEDO 2
#define TAMANHO_SPRITE_MERGULHADOR 3
#define DURACAO_EXPLOSAO 7
#define RECARGA_OXIGENIO_TAXA 4
#define BARRA_HUD_MAX 20

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
    COR_OXIGENIO_BAIXO,
    COR_MERGULHADOR,
    COR_CEU
} Cor;

typedef enum {
    FUNDO_PADRAO = 0,
    FUNDO_AGUA,
    FUNDO_CEU
} CorFundo;

/* --- ESTRUTURAS DE DADOS --- */
typedef struct {
    int x, y;
    int direcao;
    int vidas;
    int oxigenio;
    int invencivel;
    int mergulhadores_carregados;
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
    int quadro_explosao;
} Inimigo;

typedef struct {
    int x, y;
    int direcao;
    int ativo;
} Mergulhador;

/* --- VARIAVEIS GLOBAIS --- */
Jogador jogador;
Torpedo torpedos[MAX_TIROS];
Inimigo inimigos[MAX_INIMIGOS];
Mergulhador mergulhadores[MAX_MERGULHADORES];
int pontuacao = 0;
int jogo_rodando = 1;
int contador_quadros = 0;

#ifdef _WIN32
    HANDLE console_saida;
    static WORD win_fundo_atual = 0;
#else
    struct termios termios_original;
    static int tecla_em_buffer = 0;
    static char tecla_buffer;
#endif

/* =========================================================================
   FUNCOES DE TERMINAL (sem chamadas system() lentas no game loop)
   ========================================================================= */
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
void resetar_cursor(void) { posicionar_cursor(0, 0); }
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
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    return (int)c;
}
void esperar_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

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
    fwrite(dados, 1, (size_t)n, f); fclose(f);
}
void tocar_som_wav(const char *path, const Nota *ns, int qtd) {
    static unsigned char buf[TAXA * 2];
    int n = 0;
    for (int i = 0; i < qtd; i++) {
        int amostras = ns[i].ms * TAXA / 1000;
        int meio = ns[i].freq > 0 ? (TAXA / ns[i].freq) / 2 : 1;
        if (meio <= 0) meio = 1;
        for (int s = 0; s < amostras && n < (int)sizeof(buf); s++, n++) {
            if (ns[i].freq == 0) buf[n] = 128;
            else if (ns[i].freq < 0) buf[n] = (unsigned char)((rand() & 1) ? 196 : 60);
            else buf[n] = (unsigned char)(((s / meio) & 1) ? 196 : 60);
        }
    }
    escreve_wav(path, buf, n);
}
void tocar_som(int frequencia, int duracao_ms) {
    Nota ns[1];
    ns[0].freq = frequencia; ns[0].ms = duracao_ms;
    const char *tmp = "/tmp/sea_quest_sound.wav";
    tocar_som_wav(tmp, ns, 1);
#ifdef __APPLE__
    if (system("afplay /tmp/sea_quest_sound.wav >/dev/null 2>&1 &") != 0) { /* som opcional */ }
#else
    if (system("aplay -q /tmp/sea_quest_sound.wav >/dev/null 2>&1 &") != 0) { /* som opcional */ }
#endif
}
void limpar_tela(void) { if (system("clear") != 0) { /* ignora falha */ } }
void resetar_cursor(void) { printf("\033[H"); }
#endif

void definir_cor(Cor cor) {
#ifdef _WIN32
    static const int tabela_windows[] = { 7, 9, 10, 12, 13, 15, 11, 6, 10, 10, 12, 14, 12 };
    SetConsoleTextAttribute(console_saida, (WORD)(tabela_windows[cor] | win_fundo_atual));
#else
    static const char *tabela_ansi[] = {
        "\033[22;39m", "\033[1;34m", "\033[1;38;5;46m", "\033[1;38;5;208m", "\033[1;38;5;201m",
        "\033[1;37m", "\033[1;36m", "\033[0;33m", "\033[1;32m", "\033[1;32m", "\033[1;31m", "\033[1;38;5;226m", "\033[1;31m"
    };
    printf("%s", tabela_ansi[cor]);
#endif
}

void definir_cor_fundo(CorFundo fundo) {
#ifdef _WIN32
    static const int fundos_windows[] = { 0, 1 << 4, 0 };
    win_fundo_atual = (WORD)fundos_windows[fundo];
    SetConsoleTextAttribute(console_saida, (WORD)(15 | win_fundo_atual));
#else
    static const char *fundos_ansi[] = { "\033[0m", "\033[44m", "\033[40m" };
    printf("%s", fundos_ansi[fundo]);
#endif
}

void desenhar_texto_centralizado(int y, const char *texto, Cor cor) {
    int x = (LARGURA - (int)strlen(texto)) / 2;
    if (x < 0) x = 0;
    posicionar_cursor(x, y);
    definir_cor(cor);
    printf("%s", texto);
    definir_cor(COR_PADRAO);
}

void aplicar_fundo_por_posicao(int y) {
    if (y < LINHA_HORIZONTE) {
        definir_cor_fundo(FUNDO_CEU);
    } else {
        definir_cor_fundo(FUNDO_AGUA);
    }
}

/* =========================================================================
   LOGICA DO JOGO
   ========================================================================= */
void inicializar_jogo(void) {
    int i;
    jogador.x = LARGURA / 4;
    jogador.y = LINHA_HORIZONTE + 2;
    jogador.direcao = 1;
    jogador.vidas = VIDAS_INICIAIS;
    jogador.oxigenio = OXIGENIO_MAX;
    jogador.invencivel = 0;
    jogador.mergulhadores_carregados = 0;

    for (i = 0; i < MAX_TIROS; i++) torpedos[i].ativo = 0;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        inimigos[i].ativo = 0;
        inimigos[i].quadro_explosao = 0;
    }
    for (i = 0; i < MAX_MERGULHADORES; i++) mergulhadores[i].ativo = 0;

    pontuacao = 0;
    contador_quadros = 0;
    srand((unsigned int)time(NULL));
}

void criar_inimigo(void) {
    int i;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].ativo) {
            int vem_da_esquerda = (rand() % 2);
            inimigos[i].ativo = 1;
            inimigos[i].quadro_explosao = 0;
            inimigos[i].y = LINHA_HORIZONTE + 1 + rand() % (ALTURA - LINHA_HORIZONTE - 3);
            if (vem_da_esquerda) {
                inimigos[i].x = 1;
                inimigos[i].direcao = 1;
            } else {
                inimigos[i].x = LARGURA - TAMANHO_SPRITE_INIMIGO - 1;
                inimigos[i].direcao = -1;
            }
            if (rand() % 2) {
                inimigos[i].simbolo = 'S';
                inimigos[i].cor = COR_TUBARAO;
            } else {
                inimigos[i].simbolo = 'X';
                inimigos[i].cor = COR_SUBMARINO_INIMIGO;
            }
            return;
        }
    }
}

void criar_mergulhador(void) {
    int i;
    for (i = 0; i < MAX_MERGULHADORES; i++) {
        if (!mergulhadores[i].ativo) {
            int vem_da_esquerda = (rand() % 2);
            mergulhadores[i].ativo = 1;
            mergulhadores[i].y = ALTURA - 3 - (rand() % 2);
            if (vem_da_esquerda) {
                mergulhadores[i].x = 1;
                mergulhadores[i].direcao = 1;
            } else {
                mergulhadores[i].x = LARGURA - TAMANHO_SPRITE_MERGULHADOR - 1;
                mergulhadores[i].direcao = -1;
            }
            return;
        }
    }
}

void atirar_torpedo(void) {
    for (int i = 0; i < MAX_TIROS; i++) {
        if (!torpedos[i].ativo) {
            torpedos[i].ativo = 1;
            torpedos[i].direcao = jogador.direcao;
            torpedos[i].y = jogador.y;
            torpedos[i].x = jogador.x + (jogador.direcao == 1 ? TAMANHO_SPRITE_JOGADOR : -TAMANHO_SPRITE_TORPEDO);
            tocar_som(1200, 40);
            return;
        }
    }
}

void processar_entrada(void) {
    if (!tecla_disponivel()) return;
    switch (ler_tecla()) {
        case 'w': case 'W':
            if (jogador.y > LINHA_HORIZONTE) jogador.y--;
            break;
        case 's': case 'S':
            if (jogador.y < ALTURA - 2) jogador.y++;
            break;
        case 'a': case 'A':
            if (jogador.x > 1) jogador.x--;
            jogador.direcao = -1;
            break;
        case 'd': case 'D':
            if (jogador.x < LARGURA - TAMANHO_SPRITE_JOGADOR - 1) jogador.x++;
            jogador.direcao = 1;
            break;
        case ' ':
            atirar_torpedo();
            break;
        case 'q': case 'Q':
            jogo_rodando = 0;
            break;
        default:
            break;
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
        if (inimigos[i].quadro_explosao > 0) {
            inimigos[i].quadro_explosao--;
            if (inimigos[i].quadro_explosao == 0) inimigos[i].ativo = 0;
            continue;
        }
        if (contador_quadros % 2 == 0) {
            inimigos[i].x += inimigos[i].direcao;
            if (inimigos[i].x <= 0 || inimigos[i].x >= LARGURA - 2) inimigos[i].ativo = 0;
        }
    }
}

void atualizar_mergulhadores(void) {
    int i;
    for (i = 0; i < MAX_MERGULHADORES; i++) {
        if (!mergulhadores[i].ativo) continue;
        if (contador_quadros % 3 == 0) {
            mergulhadores[i].x += mergulhadores[i].direcao;
            if (mergulhadores[i].x <= 0 || mergulhadores[i].x >= LARGURA - 3) mergulhadores[i].ativo = 0;
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
                ((torpedos[i].x + TAMANHO_SPRITE_TORPEDO - 1 >= inimigos[j].x) &&
                 (torpedos[i].x <= inimigos[j].x + TAMANHO_SPRITE_INIMIGO - 1))) {
                inimigos[j].quadro_explosao = DURACAO_EXPLOSAO;
                torpedos[i].ativo = 0;
                pontuacao += 10;
                tocar_som(600, 80);
            }
        }
    }

    /* Colisao submarino/mergulhador: comparacao completa das duas caixas (AABB),
       independente da direcao do jogador -- a versao anterior so testava um
       unico ponto da borda do mergulhador e perdia sobreposicoes parciais. */
    for (i = 0; i < MAX_MERGULHADORES; i++) {
        if (mergulhadores[i].ativo && mergulhadores[i].y == jogador.y) {
            int colidiu = (mergulhadores[i].x <= jogador.x + TAMANHO_SPRITE_JOGADOR - 1) &&
                          (mergulhadores[i].x + TAMANHO_SPRITE_MERGULHADOR - 1 >= jogador.x);

            if (colidiu && jogador.mergulhadores_carregados < MAX_CARGA_HUMANA) {
                jogador.mergulhadores_carregados++;
                mergulhadores[i].ativo = 0;
                pontuacao += 50;
                tocar_som(900, 150);
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
            (jogador.x <= inimigos[j].x + TAMANHO_SPRITE_INIMIGO - 1 && jogador.x + TAMANHO_SPRITE_JOGADOR - 1 >= inimigos[j].x)) {

            jogador.vidas--;
            jogador.invencivel = 20;
            tocar_som(150, 300);
            jogador.x = LARGURA / 4;
            jogador.y = LINHA_HORIZONTE + 2;
            inimigos[j].ativo = 0;
            jogador.mergulhadores_carregados = 0;
            jogador.oxigenio = OXIGENIO_MAX;
        }
    }
}

void atualizar_oxigenio(void) {
    if (jogador.y == LINHA_HORIZONTE) {
        if (jogador.mergulhadores_carregados > 0) {
            pontuacao += jogador.mergulhadores_carregados * 100;
            jogador.mergulhadores_carregados = 0;
            tocar_som(2000, 400);
        }
        jogador.oxigenio += RECARGA_OXIGENIO_TAXA;
        if (jogador.oxigenio > OXIGENIO_MAX) jogador.oxigenio = OXIGENIO_MAX;
    } else {
        jogador.oxigenio--;
        if (jogador.oxigenio <= 0) {
            jogador.vidas--;
            jogador.oxigenio = OXIGENIO_MAX;
            jogador.mergulhadores_carregados = 0;
            jogador.x = LARGURA / 4;
            jogador.y = LINHA_HORIZONTE + 2;
            tocar_som(100, 500);
        }
    }
}

/* =========================================================================
   REPRODUCAO GRAFICA
   ========================================================================= */
void desenhar_jogo(void) {
    int x, i;
    int tamanho_barra;
    char barra_oxigenio[BARRA_HUD_MAX + 1];

    resetar_cursor();

    for (i = 0; i < ALTURA - 1; i++) {
        aplicar_fundo_por_posicao(i);
        definir_cor(COR_PADRAO);

        if (i == LINHA_HORIZONTE) {
            definir_cor(COR_SUPERFICIE);
            for (x = 0; x < LARGURA; x++) {
                if ((x + contador_quadros / 4) % 4 == 0) putchar('-');
                else putchar('~');
            }
        } else if (i < LINHA_HORIZONTE) {
            for (x = 0; x < LARGURA; x++) {
                if (i == 3 && (x == 15 || x == 45) && (x < LARGURA - 6)) {
                    definir_cor(COR_SUPERFICIE); printf("____"); x += 3;
                }
                else if (i == 4 && (x == 14 || x == 44) && (x < LARGURA - 6)) {
                    definir_cor(COR_SUPERFICIE); printf("(____)"); x += 5;
                }
                else if (i == 2 && (x + contador_quadros) % 50 == 0) {
                    definir_cor(COR_TEXTO); putchar('v');
                }
                else {
                    definir_cor(COR_PADRAO); putchar(' ');
                }
            }
        } else {
            for (x = 0; x < LARGURA; x++) {
                if ((x * i + contador_quadros * 2) % 233 == 0) {
                    definir_cor(COR_SUPERFICIE); putchar('.');
                } else {
                    definir_cor(COR_PADRAO); putchar(' ');
                }
            }
        }
        putchar('\n');
    }

    definir_cor(COR_FUNDO_MAR);
    definir_cor_fundo(FUNDO_AGUA);
    for (x = 0; x < LARGURA; x++) {
        putchar('_');
    }
    putchar('\n');
    definir_cor_fundo(FUNDO_PADRAO);

    for (i = 0; i < MAX_TIROS; i++) {
        if (torpedos[i].ativo) {
            posicionar_cursor(torpedos[i].x, torpedos[i].y);
            aplicar_fundo_por_posicao(torpedos[i].y);
            definir_cor(COR_TORPEDO);
            printf("%s", torpedos[i].direcao == 1 ? "->" : "<-");
        }
    }

    for (i = 0; i < MAX_MERGULHADORES; i++) {
        if (mergulhadores[i].ativo) {
            posicionar_cursor(mergulhadores[i].x, mergulhadores[i].y);
            aplicar_fundo_por_posicao(mergulhadores[i].y);
            definir_cor(COR_MERGULHADOR);
            printf("%s", mergulhadores[i].direcao == 1 ? "o->" : "<-o");
        }
    }

    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (inimigos[i].ativo) {
            posicionar_cursor(inimigos[i].x, inimigos[i].y);
            aplicar_fundo_por_posicao(inimigos[i].y);
            if (inimigos[i].quadro_explosao > 0) {
                definir_cor(COR_TORPEDO);
                if (inimigos[i].quadro_explosao > 4) printf("***");
                else if (inimigos[i].quadro_explosao > 1) printf("+ +");
                else printf(" . ");
            } else {
                definir_cor(inimigos[i].cor);
                if (inimigos[i].simbolo == 'S') {
                    int boca_aberta = (contador_quadros % 4 < 2);
                    if (inimigos[i].direcao == 1) printf("%s", boca_aberta ? "G>" : "G=");
                    else printf("%s", boca_aberta ? "<G" : "=G");
                } else {
                    printf("(X)");
                }
            }
        }
    }

    if (jogador.invencivel == 0 || (contador_quadros % 4 < 2)) {
        posicionar_cursor(jogador.x, jogador.y);
        aplicar_fundo_por_posicao(jogador.y);
        definir_cor(COR_JOGADOR);
        printf("%s", jogador.direcao == 1 ? "+==>>" : "<<==+");
    }

    definir_cor_fundo(FUNDO_PADRAO);
    definir_cor(COR_TEXTO);
    posicionar_cursor(0, ALTURA);

    printf("PONTOS: %-6d VIDAS: ", pontuacao);
    definir_cor(COR_TORPEDO);
    for (i = 0; i < VIDAS_INICIAIS; i++) {
        if (i < jogador.vidas) printf("V ");
        else printf("  ");
    }

    definir_cor(COR_TEXTO);
    printf(" CARONA: [");
    definir_cor(COR_MERGULHADOR);
    for (i = 0; i < MAX_CARGA_HUMANA; i++) {
        if (i < jogador.mergulhadores_carregados) putchar('o');
        else putchar(' ');
    }
    definir_cor(COR_TEXTO);
    printf("]");

    tamanho_barra = (jogador.oxigenio * BARRA_HUD_MAX) / OXIGENIO_MAX;
    if (tamanho_barra < 0) tamanho_barra = 0;
    if (tamanho_barra > BARRA_HUD_MAX) tamanho_barra = BARRA_HUD_MAX;
    for (i = 0; i < BARRA_HUD_MAX; i++) {
        if (i < tamanho_barra) barra_oxigenio[i] = '#';
        else barra_oxigenio[i] = '=';
    }
    barra_oxigenio[BARRA_HUD_MAX] = '\0';

    posicionar_cursor(0, ALTURA + 1);
    definir_cor(COR_TEXTO);
    printf("OXIGENIO: [");
    definir_cor(jogador.oxigenio < 45 ? COR_OXIGENIO_BAIXO : COR_OXIGENIO_OK);
    printf("%s", barra_oxigenio);
    definir_cor(COR_TEXTO); printf("] ");
    definir_cor(COR_PADRAO);
    fflush(stdout);
}

void tela_inicial(void) {
    limpar_tela();
    desenhar_texto_centralizado(ALTURA / 2 - 6, "=== SEA QUEST ===", COR_JOGADOR);
    desenhar_texto_centralizado(ALTURA / 2 - 4, "CONTROLES: WASD movem, ESPACO atira, Q sai.", COR_TEXTO);

    desenhar_texto_centralizado(ALTURA / 2 - 2, "--- IDENTIFICACAO DAS ENTIDADES ---", COR_SUPERFICIE);

    posicionar_cursor(LARGURA / 4, ALTURA / 2 - 1);
    definir_cor(COR_JOGADOR); printf("+==>>"); definir_cor(COR_TEXTO); printf("  : Seu Submarino");

    posicionar_cursor(LARGURA / 4, ALTURA / 2);
    definir_cor(COR_TUBARAO); printf("G>   "); definir_cor(COR_TEXTO); printf("  : Tubarao (Inimigo)");

    posicionar_cursor(LARGURA / 4, ALTURA / 2 + 1);
    definir_cor(COR_SUBMARINO_INIMIGO); printf("(X)  "); definir_cor(COR_TEXTO); printf("  : Submarino Inimigo");

    posicionar_cursor(LARGURA / 4, ALTURA / 2 + 2);
    definir_cor(COR_MERGULHADOR); printf("o->  "); definir_cor(COR_TEXTO); printf("  : Mergulhador (Resgate!)");

    desenhar_texto_centralizado(ALTURA / 2 + 4, "Leve os mergulhadores para a superficie para pontuar!", COR_OXIGENIO_OK);
    desenhar_texto_centralizado(ALTURA / 2 + 6, "Pressione qualquer tecla para comecar...", COR_SUPERFICIE);

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

        if (contador_quadros % FRAMES_POR_INIMIGO == 0) criar_inimigo();
        if (contador_quadros % FRAMES_POR_MERGULHADOR == 0) criar_mergulhador();

        atualizar_torpedos();
        atualizar_inimigos();
        atualizar_mergulhadores();
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
