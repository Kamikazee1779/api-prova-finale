#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Constantes de hashing*/
#define BASE 2
static const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
static const uint64_t FNV_PRIME = 1099511628211ULL;

static const size_t TOKEN_INITIAL_CAPACITY = 16;
static const size_t MENU_INITIAL_CAPACITY = 32;
static const size_t CLAUSES_INITIAL_CAPACITY = 32;
static const size_t LITERALS_INITIAL_CAPACITY = 32;

/* Estructuras de datos */
typedef struct {
    size_t offset;
    size_t length;
    bool tautological;
} Clause;

typedef struct {
    char *name;    /* apunta dentro de menu_buffer, no memoria propia */
    int variable;
} HashEntry;

/* Funciones de APOYO */
size_t nextPowerOfTwo(size_t n); /* Generales */

uint64_t fnv_update(uint64_t hash, char c); /* De hashing */
void hash_insert(HashEntry *hash_table, size_t capacity, char *name, int variable, uint64_t hash);
int hash_lookup(HashEntry *hash_table, size_t capacity, char *token_buffer, uint64_t hash);

/* Funciones principales --> el músculo */
bool parse_menu(char **menu_buffer, FILE *streamType, int *n, HashEntry **hash_table, size_t *hash_capacity);
void process_word(int8_t *support, HashEntry *hash_table, size_t hash_capacity, char *token_buffer, uint64_t hash, int sign, size_t *touched_count, int *touched, bool *tautological);
bool finish_clause(int8_t *support, int *touched, size_t *touched_count, bool tautological, Clause **clauses, size_t *clauses_size, size_t *clauses_capacity, int **literals, size_t *literals_size, size_t *literals_capacity);
bool parse_employees(char **token_buffer, size_t *token_capacity, size_t *touched_count, int8_t *support, int *touched, Clause **clauses, size_t *clauses_size, size_t *clauses_capacity, int **literals, size_t *literals_size, size_t *literals_capacity, HashEntry *hash_table, size_t hash_capacity);

int main(void)
{
    int n = 0;
    size_t m = 0;
    bool ok = true;

    size_t hash_capacity = 0;
    size_t touched_count = 0;
    size_t literals_size = 0;
    size_t literals_capacity = LITERALS_INITIAL_CAPACITY;
    size_t clauses_size = 0;
    size_t clauses_capacity = CLAUSES_INITIAL_CAPACITY;
    size_t token_capacity = TOKEN_INITIAL_CAPACITY;

    char *menu_buffer = NULL;
    HashEntry *hash_table = NULL;
    int8_t *support = NULL;
    int *touched = NULL;
    int *literals = NULL;
    Clause *clauses = NULL;
    char *token_buffer = NULL;

    /* Fase 1: menú/universo. */
    if (!parse_menu(&menu_buffer, stdin, &n, &hash_table, &hash_capacity)) {
        ok = false; /*Error fatal*/
    }

    /* Fase 2: estructuras de apoyo, pueden ser creadas con tranquilidad dado que ya disponemos de n */
    if (ok) {
        support = calloc((size_t)n + 1, sizeof *support);
        touched = malloc((size_t)n * sizeof *touched);
        clauses = malloc(clauses_capacity * sizeof *clauses);
        literals = malloc(literals_capacity * sizeof *literals);
        token_buffer = malloc(token_capacity * sizeof *token_buffer);

        if (!support || !touched || !clauses || !literals || !token_buffer) {
            fprintf(stderr, "Fatal malloc error!! (apoyos)\n");
            ok = false;
        }
    }

    if(ok){
        if (!parse_employees(&token_buffer, &token_capacity, &touched_count, support, touched, &clauses, &clauses_size, &clauses_capacity, &literals, &literals_size, &literals_capacity,hash_table, hash_capacity)) {
            ok = false;
        }
    }

    if(ok){
        m = clauses_size;
    }

    /* El parser terminó, por lo que esta memoria puede morir para siempre*/
    free(hash_table);
    free(menu_buffer);
    free(token_buffer);
    free(support);
    free(touched);

    /* Estos sobreviven para el solver, pensando en futuro se deja listo el espacio para su futura liberación */
    free(clauses);
    free(literals);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Desarrollo funciones apoyo */
size_t nextPowerOfTwo(size_t n){
    size_t capacity = 1;
    while (capacity < n) {
        capacity *= BASE;
    }
    return capacity;
}

uint64_t fnv_update(uint64_t hash, char c){
    hash ^= (unsigned char)c;
    hash *= FNV_PRIME;
    return hash;
}

void hash_insert(HashEntry *hash_table, size_t capacity, char *name, int variable, uint64_t hash){
    size_t slot = hash & (capacity - 1);
    bool ok = false;

    while (!ok) {
        if (hash_table[slot].name == NULL) { /* condición que significa, este slot está vacío*/
            hash_table[slot].variable = variable; /* No uso -> porque paso la hash_table como un puntador simple */
            hash_table[slot].name = name;
            ok = true;
        } else {
            slot = (slot + 1) & (capacity - 1); /* linear probing en caso de falla*/
        }
    }
}

int hash_lookup(HashEntry *hash_table, size_t capacity, char *token_buffer, uint64_t hash){
    size_t slot = hash & (capacity - 1);
    bool found = false;

    while (!found) {
        if (hash_table[slot].name != NULL && !strcmp(hash_table[slot].name, token_buffer)) {
            found = true; /*Lo encontré*/
        } else {
            slot = (slot + 1) & (capacity - 1); /* linear probing */
        }
    }
    return hash_table[slot].variable;
}

bool parse_menu(char **menu_buffer, FILE *streamType, int *n, HashEntry **hash_table, size_t *hash_capacity){
    size_t i;
    int variable;
    char *c = NULL;       /* exactamente done comienza el nombre actual*/
    char *tmp_buffer;       /*por si hay problemas de realloc*/
    size_t length = 0;
    size_t menu_capacity = MENU_INITIAL_CAPACITY;
    uint64_t hash = FNV_OFFSET_BASIS;
    bool complete_line = false, error = false, change;

    *menu_buffer = malloc(menu_capacity * sizeof(char));
    if (!*menu_buffer) {
        fprintf(stderr, "Error malloc menu_buffer\n");
        return false;
    }
    (*menu_buffer)[0] = '\0';

    /* Lectura de TODO el universo*/
    while (!error && !complete_line) {
        char *piece = fgets(*menu_buffer + length, (int)(menu_capacity - length), streamType);
        if (!piece) {
            if (ferror(streamType)) {
                error = true;
                fprintf(stderr, "Error de streaming\n");
            } else {
                complete_line = true; /*Es decir EOF sin '\n' final --> ya completé la línea */
            }
        } else {
            length += strlen(piece); /* Para no solapar re-lecturas */
            if ((*menu_buffer)[length - 1] == '\n') { /* menú nunca vacío --> length > 0 */
                (*menu_buffer)[length - 1] = '\0';
                if (length >= 2 && (*menu_buffer)[length - 2] == '\r') { /*Por los errores extraños que no podíamos entender en Windows*/
                    (*menu_buffer)[length - 2] = '\0';
                }
                complete_line = true;
            } else {
                menu_capacity *= BASE;
                tmp_buffer = realloc(*menu_buffer, menu_capacity * sizeof(char));
                if (!tmp_buffer) {
                    fprintf(stderr, "Error realloc menu_buffer redimensionamiento\n");
                    error = true;
                } else {
                    *menu_buffer = tmp_buffer;
                }
            }
        }
    }
    if (error) {
        return false;
    }

    /* Primera pasada: contar nombres recorriendo todo menu_buffer y calcular n de este modo*/
    for (i = 0, *n = 0, change = false; (*menu_buffer)[i] != '\0'; i++) {
        if ((*menu_buffer)[i] != ' ' && !change) {
            change = true;
            (*n)++;
        } else if ((*menu_buffer)[i] == ' ' && change) {
            change = false;
        }
    }

    *hash_capacity = nextPowerOfTwo(2 * (size_t)(*n)); /*Buscando tener un buen factor de carga para evitar clusters demasiado agresivos*/
    *hash_table = calloc(*hash_capacity, sizeof(HashEntry)); /* Porque nos interesa la inicialización a 0*/
    if (!*hash_table) {
        fprintf(stderr, "Error malloc hash\n");
        return false;
    }

    /*Segunda pasada*/
    for (i = 0, change = false, variable = 0; (*menu_buffer)[i] != '\0'; i++) {
        if ((*menu_buffer)[i] != ' ' && !change) {
            c = *menu_buffer + i;
            variable++;
            change = true;
            hash = FNV_OFFSET_BASIS; /* reset del proceso de hashing por nombre */
            hash = fnv_update(hash, (*menu_buffer)[i]);
        } else if ((*menu_buffer)[i] != ' ' && change) {
            hash = fnv_update(hash, (*menu_buffer)[i]);
        } else if ((*menu_buffer)[i] == ' ' && change) {
            (*menu_buffer)[i] = '\0';
            change = false;
            hash_insert(*hash_table, *hash_capacity, c, variable, hash);
        }
    }
    if (c != NULL && variable && change) { /* último nombre*/
        hash_insert(*hash_table, *hash_capacity, c, variable, hash);
    }

    return true;
}

void process_word(int8_t *support, HashEntry *hash_table, size_t hash_capacity, char *token_buffer, uint64_t hash, int sign,size_t *touched_count, int *touched, bool *tautological){
    int v = hash_lookup(hash_table, hash_capacity, token_buffer, hash);
    if (support[v] == 0) {
        support[v] = (int8_t)sign;
        touched[(*touched_count)++] = v;
    } else if (support[v] == -sign) {
        *tautological = true;
    }
}

bool finish_clause(int8_t *support, int *touched, size_t *touched_count, bool tautological,Clause **clauses, size_t *clauses_size, size_t *clauses_capacity,int **literals, size_t *literals_size, size_t *literals_capacity){
    size_t i, required;
    int lit, v;
    int *literals_tmp;
    Clause *clauses_tmp;

    /* Antes que nada, reviso si hay espacio para la cláusula nueva */
    if (*clauses_size >= *clauses_capacity) {
        *clauses_capacity *= BASE;
        clauses_tmp = realloc(*clauses, *clauses_capacity * sizeof (Clause));
        if (!clauses_tmp) {
            fprintf(stderr, "Error realloc clauses redimensionamiento\n");
            return false;
        }
        *clauses = clauses_tmp;
    }

    if (tautological) { /*Tratación más simple... */
        (*clauses)[*clauses_size].offset = *literals_size;
        (*clauses)[*clauses_size].length = 0;
        (*clauses)[*clauses_size].tautological = true;

        for (i = 0; i < *touched_count; i++) {
            support[touched[i]] = 0;
        }
    } else {
        required = *literals_size + *touched_count;
        if (*literals_capacity < required) {
            while (*literals_capacity < required) {
                *literals_capacity *= BASE;
            }
            literals_tmp = realloc(*literals, *literals_capacity * sizeof (int));
            if (!literals_tmp) {
                fprintf(stderr, "Error realloc literals redimensionamiento\n");
                return false;
            }
            *literals = literals_tmp;
        }

        (*clauses)[*clauses_size].offset = *literals_size;
        (*clauses)[*clauses_size].length = *touched_count;
        (*clauses)[*clauses_size].tautological = false;

        for (i = 0; i < *touched_count; i++) {
            v = touched[i];
            lit = support[v] > 0 ? v : -v;
            (*literals)[*literals_size + i] = lit;
            support[v] = 0; /* dejo listo para la siguiente cláusula */
        }
        *literals_size += *touched_count;
    }

    (*clauses_size)++;
    *touched_count = 0;
    return true;
}

bool parse_employees(char **token_buffer, size_t *token_capacity,size_t *touched_count, int8_t *support, int *touched,Clause **clauses, size_t *clauses_size, size_t *clauses_capacity,int **literals, size_t *literals_size, size_t *literals_capacity,HashEntry *hash_table, size_t hash_capacity)
{
    /* centro de control del estado de la cláusula actual */
    bool tautological = false;
    bool clause_started = false;

    /* centro de control del estado del token actual */
    bool token_active = false;
    size_t token_length = 0;
    int sign = +1;
    uint64_t hash = FNV_OFFSET_BASIS;

    int c;
    char *token_buffer_tmp;
    *touched_count = 0;

    while ((c = fgetc(stdin)) != EOF) {
        if (c == '\n' || c == '\r') {
            if (token_active && !tautological) {
                (*token_buffer)[token_length] = '\0';
                process_word(support, hash_table, hash_capacity, *token_buffer, hash, sign,touched_count, touched,&tautological);
            }
            if (clause_started) {
                if (!finish_clause(support, touched, touched_count, tautological,clauses, clauses_size, clauses_capacity, literals, literals_size, literals_capacity)) {
                    return false;
                }
            }
            /* reset*/
            tautological = false;
            clause_started = false;
            token_active = false;
            token_length = 0;
            sign = +1;
            hash = FNV_OFFSET_BASIS;

        } else if (c == ' ') {
            if (token_active && !tautological) {
                (*token_buffer)[token_length] = '\0';
                process_word(support, hash_table, hash_capacity, *token_buffer, hash, sign, touched_count, touched, &tautological);
            }
            /* reset pero esta vez ÚNICAMENTE DEL TOKEN */
            token_active = false;
            token_length = 0;
            sign = +1;
            hash = FNV_OFFSET_BASIS;

        } else if (tautological) { /*No haga un puto culo (fast-skipping)*/

        } else {
            clause_started = true;

            if (!token_active) {
                token_active = true; /* Es decir, me encuentro al comienzo de un token */
                if (c == '-') {
                    sign = -1; /* asignación directa, no toggle */
                } else {
                    if (token_length + 1 >= *token_capacity) { /* reservar sitio para '\0' */
                        *token_capacity *= BASE;
                        token_buffer_tmp = realloc(*token_buffer, *token_capacity * sizeof(char));
                        if (!token_buffer_tmp) {
                            fprintf(stderr, "Error realloc token_buffer redimensionamiento\n");
                            return false;
                        }
                        *token_buffer = token_buffer_tmp;
                    }
                    (*token_buffer)[token_length++] = (char)c; /*Porque lo tenía como int para poder detectar bien el EOF... hago casting directo y sale*/
                    hash = fnv_update(hash, (char)c);
                }
            } else { /* tiempo ordinario*/
                if (token_length + 1 >= *token_capacity) {
                    *token_capacity *= BASE;
                    token_buffer_tmp = realloc(*token_buffer, *token_capacity * sizeof(char));
                    if (!token_buffer_tmp) {
                        fprintf(stderr, "Error realloc token_buffer redimensionamiento\n");
                        return false;
                    }
                    *token_buffer = token_buffer_tmp;
                }
                (*token_buffer)[token_length++] = (char)c;
                hash = fnv_update(hash, (char)c);
            }
        }
    }

    if (token_active && !tautological) {
        (*token_buffer)[token_length] = '\0';
        process_word(support, hash_table, hash_capacity, *token_buffer, hash, sign, touched_count, touched, &tautological);
    }
    if (clause_started) { /*Para no comerme la eventual palabra que acabo de procesar*/
        if (!finish_clause(support, touched, touched_count, tautological, clauses, clauses_size, clauses_capacity, literals, literals_size, literals_capacity)) {
            return false;
        }
    }

    return true;
}
