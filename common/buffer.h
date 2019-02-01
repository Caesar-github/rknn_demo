#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "include/rknn_runtime.h"

unsigned char *load_model(const char *filename, int *model_size);
long getCurrentTime(void);
void print_rknn_tensor(rknn_tensor_attr *attr);
int buffer_init(int width, int height, int bpp, bo_t *g_rga_buf_bo,
		int *g_rga_buf_fd);
int buffer_deinit(bo_t *g_rga_buf_bo, int g_rga_buf_fd);
#endif

