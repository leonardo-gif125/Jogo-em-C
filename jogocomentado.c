/* =========================================================================
   PROJETO: SEA QUEST (ASCII TOTAL)
   DESCRIÇÃO: Jogo de subaquático multiplataforma rodando via terminal.
   CONTROLES: Move no WASD, atira no Espaço, Q para Sair.
   ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/* --- DIRETIVAS DE COMPILAÇÃO MULTIPLATAFORMA ---
   O código abaixo detecta o Sistema Operacional para incluir as bibliotecas corretas. */
#ifdef _WIN32
    #include <windows.h>  /* Manipulação de console no Windows (Cores, Cursor) */
    #include <conio.h>    /* Entrada de teclado não-bloqueante (_kbhit, _getch) */
    #include <unistd.h>
#else
    #include <termios.h>  /* Controle de terminal POSIX (Linux/Mac) */
    #include <unistd.h>   /* Funções padrão do sistema como read() e usleep() */
#endif

/* --- CONFIGURAÇÕES GERAIS DO JOGO (CONSTANTES) --- */
#define LARGURA 70              /* Largura da tela do aquário */
#define ALTURA 20               /* Altura da tela do aquário */
#define MAX_INIMIGOS 6          /* Máximo de inimigos simultâneos na tela */
#define MAX_TIROS 5             /* Máximo de torpedos simultâneos na tela */
#define OXIGENIO_MAX 150        /* Capacidade máxima do tanque de oxigênio */
#define VIDAS_INICIAIS 3        /* Quantidade de vidas com que o jogador inicia */
#define ATRASO_QUADRO_MS 90     /* Tempo de espera entre os quadros (Velocidade do Jogo) */
#define FRAMES_POR_INIMIGO 25   /* Taxa/Frequência de nascimento de novos inimigos */

/* --- ENUMERAÇÕES (ORGANIZAÇÃO DE CORES) --- */
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

/* --- ESTRUTURAS DE DADOS (STRUCTS) --- */
typedef struct {
    int x, y;          /* Coordenadas cartesianas do jogador */
    int direcao;       /* Direção do olhar: -1 para Esquerda, 1 para Direita */
    int vidas;         /* Contador de vidas atuais */
    int oxigenio;      /* Nível atual do tanque */
    int invencivel;    /* Contador de quadros de invencibilidade após tomar dano */
} Jogador;

typedef struct {
    int x, y;          /* Posição do torpedo */
    int direcao;       /* Direção do projétil (-1 ou 1) */
    int ativo;         /* Flag booleana: 1 se está na tela, 0 se pode ser reutilizado */
} Torpedo;

typedef struct {
    int x, y;          /* Posição do inimigo */
    int direcao;       /* Direção de movimento (-1 ou 1) */
    int ativo;         /* Flag booleana de controle de existência */
    char simbolo;      /* Tipo do inimigo ('S' para Tubarão, 'X' para Submarino) */
    Cor cor;           /* ID de cor correspondente */
    int quadro_explosao; /* Animação de morte: se > 0, desenha uma explosão */
} Inimigo;

/* --- VARIÁVEIS GLOBAIS --- */
Jogador jogador;
Torpedo torpedos[MAX_TIROS];
Inimigo inimigos[MAX_INIMIGOS];
int pontuacao = 0;
int jogo_rodando = 1;
int contador_quadros = 0;

#ifdef _WIN32
    HANDLE console_saida; /* Handle necessário para gerenciar o console do Windows */
#else
    struct termios termios_original; /* Guarda o estado original do terminal Linux */
    static int tecla_em_buffer = 0;
    static char tecla_buffer;
#endif

/* =========================================================================
   FUNÇÕES DE TERMINAL (Abstração de Sistema Operacional)
   ========================================================================= */
#ifdef _WIN32
/* --- IMPLEMENTAÇÃO WINDOWS --- */
void inicializar_console(void) {
    CONSOLE_CURSOR_INFO info;
    console_saida = GetStdHandle(STD_OUTPUT_HANDLE);
    info.dwSize = 100;
    info.bVisible = FALSE; /* Esconde o cursor piscante para melhorar o visual */
    SetConsoleCursorInfo(console_saida, &info);
}
void restaurar_console(void) {
    CONSOLE_CURSOR_INFO info;
    info.dwSize = 100;
    info.bVisible = TRUE; /* Devolve o cursor piscante ao encerrar o app */
    SetConsoleCursorInfo(console_saida, &info);
}
void posicionar_cursor(int x, int y) {
    COORD coord;
    coord.X = (SHORT)x;
    coord.Y = (SHORT)y;
    SetConsoleCursorPosition(console_saida, coord); /* Move o cursor para desenho direto */
}
int tecla_disponivel(void) { return _kbhit(); } /* Checa se alguma tecla foi pressionada */
int ler_tecla(void) { return _getch(); }       /* Lê o caractere sem exigir "Enter" */
void esperar_ms(int ms) { Sleep(ms); }
void tocar_som(int frequencia, int duracao_ms) { Beep(frequencia, duracao_ms); }
void limpar_tela(void) { system("cls"); }
#else
/* --- IMPLEMENTAÇÃO LINUX / MAC --- */
void inicializar_console(void) {
    struct termios novo;
    tcgetattr(STDIN_FILENO, &termios_original);
    novo = termios_original;
    novo.c_lflag &= ~(ICANON | ECHO); /* Desativa o eco no terminal e o modo canônico (linha) */
    novo.c_cc[VMIN] = 0;
    novo.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &novo);
    printf("\033[?25l"); /* Código ANSI para esconder o cursor */
    fflush(stdout);
}
void restaurar_console(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &termios_original);
    printf("\033[?25h"); /* Código ANSI para restaurar o cursor */
    fflush(stdout);
}
void posicionar_cursor(int x, int y) {
    printf("\033[%d;%dH", y + 1, x + 1); /* Escapes ANSI usam base 1 para linha/coluna */
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

/* --- GERAÇÃO DE SINTETIZADOR DE ÁUDIO (WAV) PARA LINUX --- */
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
#ifdef __APPLE__
    system("afplay /tmp/sea_quest_sound.wav >/dev/null 2>&1 &");
#else
    system("aplay -q /tmp/sea_quest_sound.wav >/dev/null 2>&1 &");
#endif
}
void limpar_tela(void) { system("clear"); }
#endif

/* --- SISTEMA DE CORES --- */
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

/* Auxiliar para centralizar títulos nas telas de Menu */
void desenhar_texto_centralizado(int y, const char *texto, Cor cor) {
    int x = (LARGURA - (int)strlen(texto)) / 2;
    if (x < 0) x = 0;
    posicionar_cursor(x, y);
    definir_cor(cor);
    printf("%s", texto);
}

/* =========================================================================
   LÓGICA E REGRAS DO JOGO
   ========================================================================= */

/* Reseta o estado do jogo e as variáveis de controle */
void inicializar_jogo(void) {
    int i;
    jogador.x = LARGURA / 4;
    jogador.y = ALTURA / 2;
    jogador.direcao = 1;
    jogador.vidas = VIDAS_INICIAIS;
    jogador.oxigenio = OXIGENIO_MAX;
    jogador.invencivel = 0;
    
    /* Inicializa os arrays (limpando lixo de memória) */
    for (i = 0; i < MAX_TIROS; i++) torpedos[i].ativo = 0;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        inimigos[i].ativo = 0;
        inimigos[i].quadro_explosao = 0;
    }
    pontuacao = 0;
    contador_quadros = 0;
    srand((unsigned int)time(NULL)); /* Alimenta a semente do gerador aleatório */
}

/* Instancia um inimigo em um espaço vago ("slot") do array global */
void criar_inimigo(void) {
    int i;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].ativo) {
            int vem_da_esquerda = rand() % 2;
            inimigos[i].ativo = 1;
            inimigos[i].quadro_explosao = 0;
            inimigos[i].y = 2 + rand() % (ALTURA - 4); /* Evita spawn colado no teto/chão */
            
            if (vem_da_esquerda) {
                inimigos[i].x = 1;
                inimigos[i].direcao = 1;
            } else {
                inimigos[i].x = LARGURA - 3;
                inimigos[i].direcao = -1;
            }
            
            /* Sorteia de forma aleatória se nasce Tubarão ou Submarino */
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

/* Gerencia o disparo de torpedos do submarino do jogador */
void atirar_torpedo(void) {
    int i;
    for (i = 0; i < MAX_TIROS; i++) {
        if (!torpedos[i].ativo) {
            torpedos[i].ativo = 1;
            torpedos[i].direcao = jogador.direcao;
            torpedos[i].y = jogador.y;
            /* Faz o tiro nascer logo à frente do nariz do submarino */
            torpedos[i].x = jogador.x + (jogador.direcao == 1 ? 5 : -2);
            tocar_som(1200, 40); /* Som característico de disparo agudo */
            return;
        }
    }
}

/* Captura e interpreta a entrada do teclado do usuário */
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

/* Move os torpedos ativos e os desativa se baterem nas bordas */
void atualizar_torpedos(void) {
    int i;
    for (i = 0; i < MAX_TIROS; i++) {
        if (!torpedos[i].ativo) continue;
        torpedos[i].x += torpedos[i].direcao * 2; /* Torpedo anda 2 blocos (mais rápido) */
        if (torpedos[i].x <= 0 || torpedos[i].x >= LARGURA - 1) {
            torpedos[i].ativo = 0;
        }
    }
}

/* Move os inimigos e gerencia a contagem regressiva do efeito visual de explosão */
void atualizar_inimigos(void) {
    int i;
    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (!inimigos[i].ativo) continue;
        
        /* Se o inimigo foi atingido, reduz o tempo da animação de morte */
        if (inimigos[i].quadro_explosao > 0) {
            inimigos[i].quadro_explosao--;
            if (inimigos[i].quadro_explosao == 0) {
                inimigos[i].ativo = 0;
            }
            continue;
        }

        /* Redutor de velocidade: Inimigos só andam em quadros pares (metade da velocidade do jogo) */
        if (contador_quadros % 2 == 0) {
            inimigos[i].x += inimigos[i].direcao;
            if (inimigos[i].x <= 0 || inimigos[i].x >= LARGURA - 2) {
                inimigos[i].ativo = 0;
            }
        }
    }
}

/* Sistema de Detecção de Colisão por Proximidade Euclidiana (Eixo X e Y) */
void verificar_colisoes(void) {
    int i, j;
    
    /* 1. Colisão: Torpedo acertando Inimigo */
    for (i = 0; i < MAX_TIROS; i++) {
        if (!torpedos[i].ativo) continue;
        for (j = 0; j < MAX_INIMIGOS; j++) {
            if (inimigos[j].ativo && inimigos[j].quadro_explosao == 0 &&
                inimigos[j].y == torpedos[i].y &&
                abs(inimigos[j].x - torpedos[i].x) <= 1) {
                
                inimigos[j].quadro_explosao = 3; /* Ativa visual de explosão por 3 frames */
                torpedos[i].ativo = 0;
                pontuacao += 10;
                tocar_som(600, 80);
            }
        }
    }

    /* Se o jogador está no período de invulnerabilidade (pós-dano), ignora colisões com ele */
    if (jogador.invencivel > 0) {
        jogador.invencivel--;
        return;
    }

    /* 2. Colisão: Inimigo atingindo o Jogador */
    for (j = 0; j < MAX_INIMIGOS; j++) {
        if (inimigos[j].ativo && inimigos[j].quadro_explosao == 0 &&
            inimigos[j].y == jogador.y &&
            abs(inimigos[j].x - jogador.x) <= 2) {
            
            jogador.vidas--;
            jogador.invencivel = 20; /* Concede invencibilidade temporária */
            tocar_som(150, 300);     /* Som grave indicando avaria */
            jogador.x = LARGURA / 4;  /* Reseta jogador para a posição inicial */
            jogador.y = ALTURA / 2;
            inimigos[j].ativo = 0;
        }
    }
}

/* Controla o consumo e reabastecimento do oxigênio */
void atualizar_oxigenio(void) {
    if (jogador.y == 0) {
        /* Se estiver encostado no topo (y=0), está respirando na superfície */
        jogador.oxigenio += 4;
        if (jogador.oxigenio > OXIGENIO_MAX) jogador.oxigenio = OXIGENIO_MAX;
    } else {
        /* Se estiver submerso, gasta combustível respiratório a cada ciclo */
        jogador.oxigenio--;
        if (jogador.oxigenio <= 0) {
            jogador.vidas--;
            jogador.oxigenio = OXIGENIO_MAX;
            jogador.x = LARGURA / 4;
            jogador.y = ALTURA / 2;
            tocar_som(100, 500); /* Som longo e grave simulando afogamento */
        }
    }
}

/* =========================================================================
   SISTEMA GRÁFICO (REPRODUÇÃO NA TELA)
   ========================================================================= */
void desenhar_jogo(void) {
    int x, i;
    int tamanho_barra;
    char barra_oxigenio[21];

    /* 1. Desenha a Superfície da Água Animada */
    definir_cor(COR_SUPERFICIE);
    definir_cor_fundo(FUNDO_PADRAO);
    posicionar_cursor(0, 0);
    for (x = 0; x < LARGURA; x++) {
        if ((x + contador_quadros / 4) % 4 == 0) putchar('-');
        else putchar('~');
    }

    /* 2. Desenha a Massa de Água Interna (com partículas/bolhas aleatórias matematicamente) */
    definir_cor_fundo(FUNDO_AGUA);
    for (i = 1; i < ALTURA - 1; i++) {
        posicionar_cursor(0, i);
        for (x = 0; x < LARGURA; x++) {
            if ((x * i + contador_quadros) % 197 == 0) {
                definir_cor(COR_SUPERFICIE);
                putchar('o'); /* Bolha maior */
            } else if ((x * i + contador_quadros * 2) % 233 == 0) {
                definir_cor(COR_SUPERFICIE);
                putchar('.'); /* Bolha menor */
            } else {
                putchar(' '); /* Água limpa */
            }
        }
    }
    definir_cor_fundo(FUNDO_PADRAO);

    /* 3. Desenha o Solo do Fundo Marinho */
    definir_cor(COR_FUNDO_MAR);
    definir_cor_fundo(FUNDO_AGUA);
    posicionar_cursor(0, ALTURA - 1);
    for (x = 0; x < LARGURA; x++) putchar('_');

    /* 4. Desenha Projéteis (Torpedos) */
    definir_cor(COR_TORPEDO);
    definir_cor_fundo(FUNDO_AGUA);
    for (i = 0; i < MAX_TIROS; i++) {
        if (torpedos[i].ativo) {
            posicionar_cursor(torpedos[i].x, torpedos[i].y);
            if (torpedos[i].direcao == 1) printf("->");
            else printf("<-");
        }
    }

    /* 5. Desenha Inimigos (com Sprites Orientados e Efeito de Morte) */
    for (i = 0; i < MAX_INIMIGOS; i++) {
        if (inimigos[i].ativo) {
            posicionar_cursor(inimigos[i].x, inimigos[i].y);
            definir_cor_fundo(FUNDO_AGUA);
            if (inimigos[i].quadro_explosao > 0) {
                definir_cor(COR_TORPEDO);
                printf("***"); /* Renderiza impacto visual */
            } else {
                definir_cor(inimigos[i].cor);
                if (inimigos[i].simbolo == 'S') {
                    if (inimigos[i].direcao == 1) printf("G>");
                    else printf("<G");
                } else {
                    printf("(X)");
                }
            }
        }
    }

    /* 6. Desenha Submarino do Jogador (Gera efeito estroboscópico/piscando se invencível) */
    if (jogador.invencivel == 0 || (contador_quadros % 4 < 2)) {
        definir_cor(COR_JOGADOR);
        definir_cor_fundo(FUNDO_AGUA);
        posicionar_cursor(jogador.x, jogador.y);
        if (jogador.direcao == 1) printf("+==>>");
        else printf("<<==+");
    }

    /* 7. Interface Textual (HUD): Pontuação e Vidas Dinâmicas via ASCII (<3) */
    definir_cor(COR_TEXTO);
    definir_cor_fundo(FUNDO_PADRAO);
    posicionar_cursor(0, ALTURA);
    printf("PONTOS: %-6d VIDAS: ", pontuacao);
    definir_cor(COR_TUBARAO);
    for (i = 0; i < VIDAS_INICIAIS; i++) {
        if (i < jogador.vidas) printf("<3 ");
        else printf("   ");
    }

    /* 8. Interface Textual (HUD): Barra de Carga de Oxigênio Graficamente Estilizada */
    tamanho_barra = jogador.oxigenio / 7.5; /* Mapeia a escala de oxigênio proporcionalmente a 20 slots */
    if (tamanho_barra < 0) tamanho_barra = 0;
    if (tamanho_barra > 20) tamanho_barra = 20;
    
    for (i = 0; i < 20; i++) {
        if (i < tamanho_barra) barra_oxigenio[i] = '#'; /* Marcador cheio */
        else barra_oxigenio[i] = '=';                  /* Marcador vazio */
    }
    barra_oxigenio[20] = '\0';

    posicionar_cursor(0, ALTURA + 1);
    definir_cor(COR_TEXTO);
    printf("OXIGENIO: [");
    /* Alerta visual: se o oxigênio cair abaixo de 30%, troca a cor do marcador para vermelho */
    definir_cor(jogador.oxigenio < 45 ? COR_OXIGENIO_BAIXO : COR_OXIGENIO_OK);
    printf("%s", barra_oxigenio);
    definir_cor(COR_TEXTO);
    printf("] ");
    definir_cor(COR_PADRAO);
    fflush(stdout); /* Força a renderização imediata do stream de texto */
}

/* Telas informativas padrão */
void tela_inicial(void) {
    limpar_tela();
    desenhar_texto_centralizado(ALTURA / 2 - 3, "=== SEA QUEST (ASCII TOTAL) ===", COR_JOGADOR);
    desenhar_texto_centralizado(ALTURA / 2 - 1, "WASD para mover, ESPACO para atirar, Q para sair", COR_TEXTO);
    desenhar_texto_centralizado(ALTURA / 2,     "Fique de olho no oxigenio! Suba para respirar.", COR_OXIGENIO_OK);
    desenhar_texto_centralizado(ALTURA / 2 + 2, "Pressione qualquer tecla para comecar...", COR_SUPERFICIE);
    definir_cor(COR_PADRAO);
    fflush(stdout);
    while (!tecla_disponivel()) esperar_ms(50);
    ler_tecla(); /* Limpa a tecla pressionada */
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

/* =========================================================================
   FUNÇÃO PRINCIPAL (CORAÇÃO DO PROGRAMA)
   ========================================================================= */
int main(void) {
    /* Desativa o buffer do terminal para que as atualizações de cor apareçam sem atraso (flickering) */
    setvbuf(stdout, NULL, _IONBF, 0);
    
    inicializar_console();
    tela_inicial();
    inicializar_jogo();
    limpar_tela();

    /* --- GAME LOOP (Loop Principal de Processamento) --- */
    while (jogo_rodando && jogador.vidas > 0) {
        processar_entrada(); /* 1. Escuta o Teclado */
        
        /* Sistema de Spawn por Tempo */
        if (contador_quadros % FRAMES_POR_INIMIGO == 0) {
            criar_inimigo(); /* 2. Sorteia novo perigo na tela */
        }
        
        atualizar_torpedos(); /* 3. Atualiza Vetores Físicos */
        atualizar_inimigos();
        verificar_colisoes(); /* 4. Checa Intersecções (Regras de Dano/Pontos) */
        atualizar_oxigenio(); /* 5. Atualiza Estado Vital */
        desenhar_jogo();      /* 6. Renderiza a matriz final na Tela */
        
        contador_quadros++;
        esperar_ms(ATRASO_QUADRO_MS); /* 7. Aguarda o tempo necessário para travar o FPS */
    }

    tela_fim_de_jogo();
    restaurar_console(); /* Restaura as configurações nativas de cursor do terminal */
    return 0;
}