#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lodepng.h"

typedef struct {
    unsigned char color;
} Vertex;

typedef struct {
    int size;
    int width;
    int height;
    Vertex* vertices;
    int* edges;
} Graph;

// стек для DFS
typedef struct {
    int* data;
    int top;
    int capacity;
} Stack;

Stack* create_stack(int cap) {
    Stack* s = (Stack*)malloc(sizeof(Stack));
    s->data = (int*)malloc(cap * sizeof(int));
    s->top = -1;
    s->capacity = cap;
    return s;
}

void push(Stack* s, int val) {
    s->data[++s->top] = val;
}

int pop(Stack* s) {
    return s->data[s->top--];
}

int is_empty(Stack* s) {
    return s->top == -1;
}

void free_stack(Stack* s) {
    free(s->data);
    free(s);
}

unsigned char* load_png(const char* filename, unsigned int* w, unsigned int* h) {
    unsigned char* img = NULL;
    int err = lodepng_decode32_file(&img, w, h, filename);
    if (err) printf("load error %u: %s\n", err, lodepng_error_text(err));
    return img;
}

void write_png(const char* filename, const unsigned char* img, unsigned w, unsigned h) {
    unsigned char* png;
    size_t pngsize;
    int err = lodepng_encode32(&png, &pngsize, img, w, h);
    if (err == 0) {
        lodepng_save_file(png, pngsize, filename);
    } else {
        printf("encode error %u: %s\n", err, lodepng_error_text(err));
    }
    free(png);
}

void rgba_to_gray(unsigned char* rgba, unsigned char* gray, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char r = rgba[i*4], g = rgba[i*4+1], b = rgba[i*4+2];
        gray[i] = (r*299 + g*587 + b*114) / 1000;
    }
}

void gray_to_rgba(unsigned char* gray, unsigned char* rgba, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char v = gray[i];
        rgba[i*4] = v; rgba[i*4+1] = v; rgba[i*4+2] = v; rgba[i*4+3] = 255;
    }
}

void binarize(unsigned char* img, int n, int threshold) {
    for (int i = 0; i < n; i++) {
        img[i] = (img[i] > threshold) ? 255 : 0;
    }
}

void init_graph(Graph* g, int w, int h) {
    g->width = w;
    g->height = h;
    g->size = w * h;
    g->vertices = (Vertex*)malloc((g->size + 1) * sizeof(Vertex));
    g->edges = (int*)malloc(4 * g->size * sizeof(int));
    memset(g->edges, -1, 4 * g->size * sizeof(int));
    g->vertices[0].color = 0;
}

void add_pixel(Graph* g, int idx, unsigned char color) {
    g->vertices[idx + 1].color = color;
    //  0-верх,1-лево,2-право,3-низ
    if (idx >= g->width)          g->edges[4*idx]   = idx - g->width;
    if (idx % g->width != 0)      g->edges[4*idx+1] = idx - 1;
    if ((idx+1) % g->width != 0)  g->edges[4*idx+2] = idx + 1;
    if (idx < g->size - g->width) g->edges[4*idx+3] = idx + g->width;
}

// DFS
void dfs_component(Graph* g, char* visited, int start) {
    Stack* st = create_stack(g->size);
    push(st, start);
    visited[start] = 1;

    while (!is_empty(st)) {
        int v = pop(st);
        for (int d = 0; d < 4; d++) {
            int nb = g->edges[4*v + d];
            if (nb != -1 && !visited[nb] && g->vertices[nb+1].color == 255) {
                visited[nb] = 1;
                push(st, nb);
            }
        }
    }
    free_stack(st);
}

// подсчёт компонент
int count_ships_in_graph(Graph* g) {
    char* visited = (char*)calloc(g->size, sizeof(char));
    int ships = 0;
    for (int v = 0; v < g->size; v++) {
        if (!visited[v] && g->vertices[v+1].color == 255) {
            dfs_component(g, visited, v);
            ships++;
        }
    }
    free(visited);
    return ships;
}

void free_graph(Graph* g) {
    free(g->vertices);
    free(g->edges);
}

int main() {
    unsigned int w, h;
    unsigned char* img = load_png("skull.png", &w, &h);
    if (!img) return 1;

    int n = w * h;
    unsigned char* gray = (unsigned char*)malloc(n);
    rgba_to_gray(img, gray, n);

    // порог контраста
    binarize(gray, n, 76);

    // секторы для поиска кораблей
    struct { int x1, x2, y1, y2, w, h; } sectors[] = {
        {536, 710, 0,   273, 175, 273},
        {564, 1114, 275, 603, 551, 328},
        {825, 1114, 602, 647, 290, 45},
        {501, 564, 301, 347, 64, 46},
        {509, 564, 347, 411, 56, 64},
        {521, 564, 452, 513, 44, 61},
        {540, 564, 514, 560, 31, 46}
    };
    int sector_count = 7;
    Graph graphs[7];

    int total_ships = 0;
    for (int s = 0; s < sector_count; s++) {
        int sw = sectors[s].w;
        int sh = sectors[s].h;
        init_graph(&graphs[s], sw, sh);

        // копируем пиксели из общей области в граф
        for (int y = sectors[s].y1; y < sectors[s].y2; y++) {
            for (int x = sectors[s].x1; x < sectors[s].x2; x++) {
                int src = y * w + x;
                int dst = (y - sectors[s].y1) * sw + (x - sectors[s].x1);
                add_pixel(&graphs[s], dst, gray[src]);
            }
        }

        int ships = count_ships_in_graph(&graphs[s]);
        printf("Sector %d: %d ships\n", s+1, ships);
        total_ships += ships;
    }

    total_ships -= 2;
    printf("Total ships: %d\n", total_ships);

    // выходная картинка
    unsigned char* out_rgba = (unsigned char*)malloc(n * 4);
    gray_to_rgba(gray, out_rgba, n);
    write_png("result.png", out_rgba, w, h);

    free(img);
    free(gray);
    free(out_rgba);
    for (int i = 0; i < sector_count; i++) {
        free_graph(&graphs[i]);
    }

    return 0;
}