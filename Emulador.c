/*
 * Simulador de arquitetura de computador com microprogramação
 * Melhorias aplicadas: modularização conceitual, tipos fixos, tratamento de erros e documentação.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>  // Para tipos de tamanho fixo

// ----- Definições e constantes -----

#define TAM_MICROPROG 512
#define TAM_MEMORIA 100000000
#define TAM_PROGRAMA_INICIAL 20

// Tipos base utilizando tipos do stdint.h para tamanho fixo

typedef uint8_t byte;              // 8 bits
typedef uint32_t palavra;          // 32 bits
typedef uint64_t microinstrucao;   // 64 bits (utiliza só 36 bits no microprograma)

// ----- Registradores -----

palavra MAR = 0, MDR = 0, PC = 0;  // Registradores para acesso a memória
byte MBR = 0;                      // Registrador byte para acesso memória

palavra SP = 0, LV = 0, TOS = 0,  // Registradores da ULA
        OPC = 0, CPP = 0, H = 0;

microinstrucao MIR = 0;            // Microinstrução atual
palavra MPC = 0;                   // Endereço da próxima microinstrução

// ----- Barramentos -----

palavra barramento_B = 0, barramento_C = 0;

// ----- Flip-Flops -----

byte N = 0, Z = 0;

// ----- Auxiliares para decodificação da microinstrução -----

byte MIR_B = 0, MIR_Operacao = 0, MIR_Deslocador = 0, MIR_MEM = 0, MIR_pulo = 0;
palavra MIR_C = 0;

// ----- Armazenamento de Controle -----

microinstrucao armazenamento[TAM_MICROPROG];

// ----- Memória Principal -----

byte memoria[TAM_MEMORIA];

// ----- Protótipos das funções -----

void carregar_microprograma_de_controle(void);
void carregar_programa(const char *nome_arquivo);
void exibir_processos(void);
void decodificar_microinstrucao(void);
void atribuir_barramento_B(void);
void realizar_operacao_ALU(void);
void atribuir_barramento_C(void);
void operar_memoria(void);
void pular(void);
void imprimir_binario(void* valor, int tipo);

// ----- Função principal -----

int main(int argc, const char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <arquivo_programa>\n", argv[0]);
        return EXIT_FAILURE;
    }

    carregar_microprograma_de_controle();
    carregar_programa(argv[1]);

    while (1) {
        exibir_processos();

        MIR = armazenamento[MPC];

        decodificar_microinstrucao();
        atribuir_barramento_B();
        realizar_operacao_ALU();
        atribuir_barramento_C();
        operar_memoria();
        pular();
    }

    return 0;
}

// ----- Implementação das funções -----

/**
 * Carrega o microprograma de controle a partir do arquivo 'microprog.rom'
 * Termina o programa em caso de erro na abertura do arquivo.
 */
void carregar_microprograma_de_controle(void) {
    FILE *arquivo_microprog = fopen("microprog.rom", "rb");
    if (arquivo_microprog == NULL) {
        fprintf(stderr, "Erro: Não foi possível abrir microprog.rom\n");
        exit(EXIT_FAILURE);
    }

    size_t lidos = fread(armazenamento, sizeof(microinstrucao), TAM_MICROPROG, arquivo_microprog);
    if (lidos != TAM_MICROPROG) {
        fprintf(stderr, "Erro: Falha ao ler microprograma de controle.\n");
        fclose(arquivo_microprog);
        exit(EXIT_FAILURE);
    }

    fclose(arquivo_microprog);
}

/**
 * Carrega o programa principal a partir do arquivo dado
 * Faz verificações de erro para a abertura e leitura.
 */
void carregar_programa(const char* nome_arquivo) {
    FILE *arquivo_prog = fopen(nome_arquivo, "rb");
    if (arquivo_prog == NULL) {
        fprintf(stderr, "Erro: Não foi possível abrir o arquivo do programa: %s\n", nome_arquivo);
        exit(EXIT_FAILURE);
    }

    byte tamanho_temp[4];
    palavra tamanho;

    // Leitura do tamanho do programa (4 bytes)
    if (fread(tamanho_temp, sizeof(byte), 4, arquivo_prog) != 4) {
        fprintf(stderr, "Erro: Falha ao ler o tamanho do programa.\n");
        fclose(arquivo_prog);
        exit(EXIT_FAILURE);
    }
    memcpy(&tamanho, tamanho_temp, sizeof(palavra));

    if (tamanho > TAM_MEMORIA || tamanho < TAM_PROGRAMA_INICIAL) {
        fprintf(stderr, "Erro: Tamanho de programa inválido.\n");
        fclose(arquivo_prog);
        exit(EXIT_FAILURE);
    }

    // Leitura dos 20 bytes de inicialização
    if (fread(memoria, sizeof(byte), TAM_PROGRAMA_INICIAL, arquivo_prog) != TAM_PROGRAMA_INICIAL) {
        fprintf(stderr, "Erro: Falha ao ler bytes iniciais do programa.\n");
        fclose(arquivo_prog);
        exit(EXIT_FAILURE);
    }

    // Leitura do restante do programa - carga a partir do endereço 0x0401 (1025 decimal)
    size_t restante = tamanho - TAM_PROGRAMA_INICIAL;
    if (fread(&memoria[0x0401], sizeof(byte), restante, arquivo_prog) != restante) {
        fprintf(stderr, "Erro: Falha ao ler o programa completo.\n");
        fclose(arquivo_prog);
        exit(EXIT_FAILURE);
    }

    fclose(arquivo_prog);
}

/**
 * Decodifica a microinstrução atual MIR nos sinais de controle
 */
void decodificar_microinstrucao(void) {
    MIR_B = (MIR) & 0b1111;
    MIR_MEM = (MIR >> 4) & 0b111;
    MIR_C = (MIR >> 7) & 0b111111111;
    MIR_Operacao = (MIR >> 16) & 0b111111;
    MIR_Deslocador = (MIR >> 22) & 0b11;
    MIR_pulo = (MIR >> 24) & 0b111;
    MPC = (MIR >> 27) & 0b111111111;
}

/**
 * Define o valor do barramento B conforme sinal MIR_B
 */
void atribuir_barramento_B(void) {
    switch (MIR_B) {
        case 0: barramento_B = MDR; break;
        case 1: barramento_B = PC; break;
        // Extensão de sinal para o MBR
        case 2:
            barramento_B = (palavra)MBR;
            if (MBR & 0b10000000)  // bit mais significativo do byte
                barramento_B |= 0xFFFFFF00;  // Extensão de sinal para 32 bits
            break;
        case 3: barramento_B = (palavra)MBR; break;
        case 4: barramento_B = SP; break;
        case 5: barramento_B = LV; break;
        case 6: barramento_B = CPP; break;
        case 7: barramento_B = TOS; break;
        case 8: barramento_B = OPC; break;
        default: barramento_B = 0xFFFFFFFF; break;  // Valor inválido
    }
}

/**
 * Realiza a operação da ULA conforme MIR_Operacao e ajusta flags N e Z
 * Também aplica deslocamento conforme MIR_Deslocador
 */
void realizar_operacao_ALU(void) {
    switch (MIR_Operacao) {
        case 12: barramento_C = H & barramento_B; break;
        case 17: barramento_C = 1; break;
        case 18: barramento_C = (palavra)-1; break;
        case 20: barramento_C = barramento_B; break;
        case 24: barramento_C = H; break;
        case 26: barramento_C = ~H; break;
        case 28: barramento_C = H | barramento_B; break;
        case 44: barramento_C = ~barramento_B; break;
        case 53: barramento_C = barramento_B + 1; break;
        case 54: barramento_C = barramento_B - 1; break;
        case 57: barramento_C = H + 1; break;
        case 59: barramento_C = (palavra)(-((int32_t)H)); break;
        case 60: barramento_C = H + barramento_B; break;
        case 61: barramento_C = H + barramento_B + 1; break;
        case 63: barramento_C = barramento_B - H; break;
        default: barramento_C = 0; break;
    }

    if (barramento_C == 0) {
        N = 0; Z = 1;
    } else {
        N = 1; Z = 0;
    }

    switch (MIR_Deslocador) {
        case 1: barramento_C <<= 8; break;
        case 2: barramento_C >>= 1; break;
        default: break;
    }
}

/**
 * Atribui o resultado barramento_C aos registradores conforme bits setados em MIR_C
 */
void atribuir_barramento_C(void) {
    if (MIR_C & 0b000000001) MAR = barramento_C;
    if (MIR_C & 0b000000010) MDR = barramento_C;
    if (MIR_C & 0b000000100) PC = barramento_C;
    if (MIR_C & 0b000001000) SP = barramento_C;
    if (MIR_C & 0b000010000) LV = barramento_C;
    if (MIR_C & 0b000100000) CPP = barramento_C;
    if (MIR_C & 0b001000000) TOS = barramento_C;
    if (MIR_C & 0b010000000) OPC = barramento_C;
    if (MIR_C & 0b100000000) H = barramento_C;
}

/**
 * Operações de memória conforme bits setados em MIR_MEM
 */
void operar_memoria(void) {
    // Multiplicação por 4 pois os registradores MAR e MDR trabalham com palavras (4 bytes)
    if (MIR_MEM & 0b001) MBR = memoria[PC];

    if (MIR_MEM & 0b010) memcpy(&MDR, &memoria[MAR * 4], sizeof(palavra));

    if (MIR_MEM & 0b100) memcpy(&memoria[MAR * 4], &MDR, sizeof(palavra));
}

/**
 * Ajusta o MPC conforme bits de pulo e flags N, Z, e valor de MBR
 */
void pular(void) {
    palavra complemento = 0;

    if (MIR_pulo & 0b001) complemento |= (N << 8);
    if (MIR_pulo & 0b010) complemento |= (Z << 8);
    if (MIR_pulo & 0b100) complemento |= MBR;

    MPC |= complemento;
}

/**
 * Exibe estado atual da pilha, programa e registradores
 */
void exibir_processos(void) {
    // Exibição da pilha de operandos
    if (LV && SP) {
        printf("\n\t\tPilha de Operandos\n");
        printf("========================================\n");
        printf(" END    \tBinário do valor        \tValor\n");
        for (int i = SP; i >= (int)LV; i--) {
            palavra valor = 0;
            memcpy(&valor, &memoria[i * 4], sizeof(palavra));

            if (i == (int)SP) printf("SP ->");
            else if (i == (int)LV) printf("LV ->");
            else printf("     ");

            printf("0x%X \t", i);
            imprimir_binario(&valor, 1);
            printf("\t%d\n", valor);
        }
        printf("========================================\n");
    }

    // Exibição área do programa
    if (PC >= 0x0401) {
        printf("\n\t\tÁrea do Programa\n");
        printf("========================================\n");
        printf("        Binário\t\tHEX\tEndereço de byte\n");
        for (int i = PC - 2; i <= PC + 3; i++) {
            if (i == (int)PC) printf("Em execução >>\t");
            else printf("\t\t");

            imprimir_binario(&memoria[i], 2);
            printf(" 0x%02X \t0x%X\n", memoria[i], i);
        }
        printf("========================================\n\n");
    }

    // Exibição dos registradores
    printf("\t\tRegistradores\n");
    printf("\tBinário\t\t\t\t HEX\n");

    printf("MAR: "); imprimir_binario(&MAR, 3); printf("\t0x%X\n", MAR);
    printf("MDR: "); imprimir_binario(&MDR, 3); printf("\t0x%X\n", MDR);
    printf("PC:  "); imprimir_binario(&PC, 3); printf("\t0x%X\n", PC);
    printf("MBR: "); imprimir_binario(&MBR, 2); printf("\t\t0x%X\n", MBR);
    printf("SP:  "); imprimir_binario(&SP, 3); printf("\t0x%X\n", SP);
    printf("LV:  "); imprimir_binario(&LV, 3); printf("\t0x%X\n", LV);
    printf("CPP: "); imprimir_binario(&CPP, 3); printf("\t0x%X\n", CPP);
    printf("TOS: "); imprimir_binario(&TOS, 3); printf("\t0x%X\n", TOS);
    printf("OPC: "); imprimir_binario(&OPC, 3); printf("\t0x%X\n", OPC);
    printf("H:   "); imprimir_binario(&H, 3); printf("\t0x%X\n", H);

    printf("MPC: "); imprimir_binario(&MPC, 5); printf("\t0x%X\n", MPC);
    printf("MIR: "); imprimir_binario(&MIR, 4); printf("\n");

    printf("Pressione Enter para continuar...\n");
    getchar();
}

/**
 * Imprime o valor passado em binário, conforme o tipo:
 * Tipo 1: palavra (4 bytes, 32 bits) - imprime 4 bytes em binário
 * Tipo 2: byte (1 byte, 8 bits)
 * Tipo 3: palavra (4 bytes, imprime os 32 bits)
 * Tipo 4: microinstrução (36 bits)
 * Tipo 5: bits do MPC (9 bits)
 */
void imprimir_binario(void* valor, int tipo) {
    switch (tipo) {
        case 1: {
            byte aux;
            byte* valorAux = (byte*)valor;
            for (int i = 3; i >= 0; i--) {
                aux = *(valorAux + i);
                for (int j = 0; j < 8; j++) {
                    printf("%d", (aux >> 7) & 0b1);
                    aux <<= 1;
                }
                printf(" ");
            }
            break;
        }
        case 2: {
            byte aux = *((byte*)valor);
            for (int j = 0; j < 8; j++) {
                printf("%d", (aux >> 7) & 0b1);
                aux <<= 1;
            }
            break;
        }
        case 3: {
            palavra aux = *((palavra*)valor);
            for (int j = 0; j < 32; j++) {
                printf("%d", (aux >> 31) & 0b1);
                aux <<= 1;
            }
            break;
        }
        case 4: {
            microinstrucao aux = *((microinstrucao*)valor);
            for (int j = 0; j < 36; j++) {
                if (j == 9 || j == 12 || j == 20 || j == 29 || j == 32) printf(" ");
                printf("%ld", (aux >> 35) & 0b1);
                aux <<= 1;
            }
            break;
        }
        case 5: {
            palavra aux = *((palavra*)valor) << 23;
            for (int j = 0; j < 9; j++) {
                printf("%d", (aux >> 31) & 0b1);
                aux <<= 1;
            }
            break;
        }
        default:
            printf("Tipo inválido para imprimir binário.\n");
            break;
    }
}

