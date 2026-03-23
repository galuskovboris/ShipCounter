#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "lodepng.h"

// принимаем на вход: имя файла, указатели на int для хранения прочитанной ширины и высоты картинки
// возвращаем указатель на выделенную память для хранения картинки
// Если память выделить не смогли, отдаем нулевой указатель и пишем сообщение об ошибке
unsigned char* load_png(const char* filename, unsigned int* width, unsigned int* height) 
{
  unsigned char* image = NULL; 
  int error = lodepng_decode32_file(&image, width, height, filename);
  if(error != 0) {
    printf("error %u: %s\n", error, lodepng_error_text(error)); 
  }
  return (image);
}

// принимаем на вход: имя файла для записи, указатель на массив пикселей,  ширину и высоту картинки
// Если преобразовать массив в картинку или сохранить не смогли,  пишем сообщение об ошибке
void write_png(const char* filename, const unsigned char* image, unsigned width, unsigned height)
{
  unsigned char* png;
  long unsigned int pngsize;
  int error = lodepng_encode32(&png, &pngsize, image, width, height);
  if(error == 0) {
      lodepng_save_file(png, pngsize, filename);
  } else { 
    printf("error %u: %s\n", error, lodepng_error_text(error));
  }
  free(png);
}


// контрастирование
void contrast(unsigned char *col, int bw_size)
{
    int i;
    for(i=0; i < bw_size; i++)
    {
        if(col[i] < 55)
        col[i] = 0; 
        if(col[i] > 195)
        col[i] = 255;
    } 
    return; 
} 

// Гауссово размытие
void Gauss_blur(unsigned char *col, unsigned char *blr_pic, int width, int height)
{
    int i, j;
    for(i = 1; i < height - 1; i++)
        for(j = 1; j < width - 1; j++)
        {
            int sum = 0;

            sum += col[width*(i-1) + (j-1)] * 1;
            sum += col[width*(i-1) + j]     * 2;   // верхняя строка
            sum += col[width*(i-1) + (j+1)] * 1;

            sum += col[width*i + (j-1)] * 2;
            sum += col[width*i + j]     * 4;       // средняя строка
            sum += col[width*i + (j+1)] * 2;

            sum += col[width*(i+1) + (j-1)] * 1;
            sum += col[width*(i+1) + j]     * 2;   // нижнняя строка
            sum += col[width*(i+1) + (j+1)] * 1;

            blr_pic[width*i + j] = sum / 16;
        }

    for(i = 0; i < height; i++)
    {
        blr_pic[width*i + 0] = col[width*i + 0];
        blr_pic[width*i + (width-1)] = col[width*i + (width-1)];
    }

    for(j = 0; j < width; j++)
    {
        blr_pic[0*width + j] = col[0*width + j];
        blr_pic[(height-1)*width + j] = col[(height-1)*width + j];
    }

    return;
}
// проверка на белый цвет
int is_white(unsigned char *binary, int width, int height, int x, int y)
{
    if(x < 0 || x >= width || y < 0 || y >= height) return 0;
    int idx = y * width + x;
    return (binary[idx] == 255);
}

typedef struct {
    int *row_ptr;      // указатели на начало строк
    int *col_idx;      // индексы столбцов (соседей)
    int nnz;           // количество ненулевых элементов
} CSRGraph;

// Структура для компоненты связности (корабля)
typedef struct {
    int *nodes;        // индексы пикселей в компоненте
    int size;          // количество пикселей
} Component;

// Построение графа
CSRGraph* build_graph(unsigned char *binary, int width, int height) {
    int total_pixels = width * height;
    int *degrees = (int*)calloc(total_pixels, sizeof(int));

    // степень каждого узла
    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            int idx = y * width + x;
            if(!is_white(binary, width, height, x, y)) continue;

            // проверяем 8 соседей
            for(int dy = -1; dy <= 1; dy++) {
                for(int dx = -1; dx <= 1; dx++) {
                    if(dx == 0 && dy == 0) continue;
                    if(is_white(binary, width, height, x + dx, y + dy)) {
                        degrees[idx]++;
                    }
                }
            }
        }
    }
    CSRGraph *graph = (CSRGraph*)malloc(sizeof(CSRGraph));

    graph->row_ptr = (int*)malloc((total_pixels + 1) * sizeof(int));
    graph->row_ptr[0] = 0;
    for(int i = 0; i < total_pixels; i++) {
        graph->row_ptr[i + 1] = graph->row_ptr[i] + degrees[i];
    }

    graph->nnz = graph->row_ptr[total_pixels];
    graph->col_idx = (int*)malloc(graph->nnz * sizeof(int));

    int *current_pos = (int*)calloc(total_pixels, sizeof(int));
    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            int idx = y * width + x;
            if(!is_white(binary, width, height, x, y)) continue;

            int pos = graph->row_ptr[idx] + current_pos[idx];

            for(int dy = -1; dy <= 1; dy++) {
                for(int dx = -1; dx <= 1; dx++) {
                    if(dx == 0 && dy == 0) continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    if(is_white(binary, width, height, nx, ny)) {
                        int nidx = ny * width + nx;
                        graph->col_idx[pos++] = nidx;
                    }
                }
            }
            current_pos[idx] += degrees[idx];
        }
    }

    free(degrees);
    free(current_pos);

    return graph;
}

// Освобождение графа
void free_graph(CSRGraph *graph)
{
    if(graph) {
        free(graph->row_ptr);
        free(graph->col_idx);
        free(graph);
    }
}

// Поиск компонент связности
Component* find_components(CSRGraph *graph, int total_pixels, int *comp_count)
{
    int *visited = (int*)calloc(total_pixels, sizeof(int));
    Component *components = (Component*)malloc(total_pixels * sizeof(Component));
    int comp_idx = 0;

    // Стек для DFS
    int *stack = (int*)malloc(total_pixels * sizeof(int));

    for(int i = 0; i < total_pixels; i++) {
        if(visited[i] || graph->row_ptr[i] == graph->row_ptr[i + 1]) {
            continue;
        }

        int stack_top = 0;
        stack[stack_top++] = i;
        visited[i] = 1;

        int *comp_nodes = (int*)malloc(total_pixels * sizeof(int));
        int node_count = 0;

        while(stack_top > 0) {
            int node = stack[--stack_top];
            comp_nodes[node_count++] = node;

            for(int j = graph->row_ptr[node]; j < graph->row_ptr[node + 1]; j++) {
                int neighbor = graph->col_idx[j];
                if(!visited[neighbor]) {
                    visited[neighbor] = 1;
                    stack[stack_top++] = neighbor;
                }
            }
        }

        components[comp_idx].nodes = (int*)malloc(node_count * sizeof(int));
        memcpy(components[comp_idx].nodes, comp_nodes, node_count * sizeof(int));
        components[comp_idx].size = node_count;
        comp_idx++;

        free(comp_nodes);
    }

    *comp_count = comp_idx;
    free(visited);
    free(stack);

    return components;
}

//  Место для экспериментов
void color(unsigned char *blr_pic, unsigned char *res, int size)
{ 
  int i;
    for(i=1;i<size;i++) 
    { 
        res[i*4]=40+blr_pic[i]+0.35*blr_pic[i-1]; 
        res[i*4+1]=65+blr_pic[i]; 
        res[i*4+2]=170+blr_pic[i]; 
        res[i*4+3]=255; 
    } 
    return; 
} 
  
int main() 
{ 
    const char* filename = "skull.png"; 
    unsigned int width, height;
    int size;
    int bw_size;
    
    // Прочитали картинку
    unsigned char* picture = load_png("skull.png", &width, &height);
    if (picture == NULL)
    { 
        printf("Problem reading picture from the file %s. Error.\n", filename); 
        return -1; 
    } 

    size = width * height * 4;
    bw_size = width * height;
    
    unsigned char* bw_pic = (unsigned char*)malloc(bw_size*sizeof(unsigned char)); 
    unsigned char* blr_pic = (unsigned char*)malloc(bw_size*sizeof(unsigned char)); 
    unsigned char* finish = (unsigned char*)malloc(size*sizeof(unsigned char)); 
 
    // Например, поиграли с  контрастом
    contrast(bw_pic, bw_size); 
        // посмотрим на промежуточные картинки
    write_png("contrast.png", finish, width, height);
    
    // поиграли с Гауссом
    Gauss_blur(bw_pic, blr_pic, width, height); 
    // посмотрим на промежуточные картинки
    write_png("gauss.png", finish, width, height);
    
    // сделали еще что-нибудь
    // .....
    // ....
    // ....
    // ....
    // ....
    // ....
    // ....
    //
    
    write_png("intermediate_result.png", finish, width, height);
    color(blr_pic, finish, bw_size); 
    
    // выписали результат
    write_png("picture_out.png", finish, width, height); 
    
    // забыли помыться!
    free(bw_pic);
    free(blr_pic);
    free(finish);
    free(picture);

    return 0; 
}
