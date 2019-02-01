#include <stdio.h>
#include <rga/RgaApi.h>
#include <linux/videodev2.h>
#include "include/rknn_runtime.h"
#include "rknn_msg.h"
#include "yuv.h"
#include "joint.h"
#include "joint_post.h"
#include "v4l2camera.h"

#define DEV_NAME      "/dev/video0"
#define MODEL_NAME    "/usr/share/rknn_demo/cpm.rknn"

#define SRC_W         640
#define SRC_H         480
#define SRC_FPS       30
#define SRC_BPP       24
#define DST_W         192
#define DST_H         192
#define DST_BPP       24
#define CPM_NUM       14

#define SRC_V4L2_FMT  V4L2_PIX_FMT_YUV420
#define SRC_RKRGA_FMT RK_FORMAT_YCbCr_420_P
#define DST_RKRGA_FMT RK_FORMAT_RGB_888

float g_fps;
bo_t g_rga_buf_bo;
int g_rga_buf_fd;
char *g_mem_buf;
rknn_context ctx;
float cpm_result[CPM_NUM * 2];
volatile int send_count;

extern
int yuv_draw(char *src_ptr, int src_fd, int format, int src_w, int src_h);
extern void joint_paint_object_msg();

inline float *joint_get_joint_group()
{
    return cpm_result;
}

#define CHECK_STATUS(func) do { \
    status = func; \
    if (status < 0)  { \
        goto final; \
    }   \
} while(0)

static unsigned char *load_model(const char *filename, int *model_size)
{
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL) {
        printf("fopen %s fail!\n", filename);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    int model_len = ftell(fp);
    unsigned char *model = (unsigned char*)malloc(model_len);
    fseek(fp, 0, SEEK_SET);
    if(model_len != fread(model, 1, model_len, fp)) {
        printf("fread %s fail!\n", filename);
        free(model);
        return NULL;
    }
    *model_size = model_len;
    if(fp) {
        fclose(fp);
    }
    return model;
}

long getCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int joint_buffer_init(int width, int height, int bpp)
{
    int ret = -1;

    ret = c_RkRgaInit();
    if (ret) {
        printf("c_RkRgaInit error : %s\n", strerror(errno));
        return ret;
    }
    ret = c_RkRgaGetAllocBuffer(&g_rga_buf_bo, width, height, bpp);
    if (ret) {
        printf("c_RkRgaGetAllocBuffer error : %s\n", strerror(errno));
        return ret;
    }
    printf("cur_bo->size = %d\n",g_rga_buf_bo.size);
    ret = c_RkRgaGetMmap(&g_rga_buf_bo);
    if (ret) {
        printf("c_RkRgaGetMmap error : %s\n", strerror(errno));
        return ret;
    }
    ret = c_RkRgaGetBufferFd(&g_rga_buf_bo, &g_rga_buf_fd);
    if (ret) {
        printf("c_RkRgaGetBufferFd error : %s\n", strerror(errno));
        return ret;
    }

    if (g_mem_buf == NULL) {
        g_mem_buf = (char *)malloc(width * height * bpp / 8);
    }

    return ret;
}

int joint_buffer_deinit()
{
    int ret = -1;
    printf("func = %s, line = %d\n", __func__, __LINE__);
    if (g_mem_buf)
        free(g_mem_buf);
    close(g_rga_buf_fd);
    ret = c_RkRgaUnmap(&g_rga_buf_bo);
    if (ret)
        printf("c_RkRgaUnmap error : %s\n", strerror(errno));
    ret = c_RkRgaFree(&g_rga_buf_bo);
}

int joint_rknn_process(char* in_data, int w, int h, int c)
{
    int status = 0;
    int in_size;
    float *out_data0 = NULL;
    int out_size0;
    float *out_data1 = NULL;
    int out_size1;
    // printf("camera callback w=%d h=%d c=%d\n", w, h, c);
	
    long runTime1 = getCurrentTime();

    long setinputTime1 = getCurrentTime();
    // Set Input Data
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = w*h*c/8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].buf = in_data;

    status = rknn_inputs_set(ctx, 1, inputs);
    if(status < 0) {
        printf("rknn_input_set fail! ret=%d\n", status);
        return -1;
    }
    long setinputTime2 = getCurrentTime();

    /* Verify and Process graph */
    status = rknn_run(ctx, NULL);
    if(status < 0) {
        printf("rknn_run fail! ret=%d\n", status);
        return -1;
    }

    rknn_output outputs[1];
    memset(outputs, 0, sizeof(outputs));
    outputs[0].want_float = 1;
    status = rknn_outputs_get(ctx, 1, outputs, NULL);
    if(status < 0) {
        printf("rknn_outputs_get fail! ret=%d\n", status);
        return -1;
    }

    out_size0 = CPM_NUM * 96 * 96;
    out_data0 = (float *) malloc(out_size0 * sizeof(float));
    
    memcpy(out_data0, outputs[0].buf, out_size0 * sizeof(float));

    rknn_outputs_release(ctx, 1, outputs);

    long runTime2 = getCurrentTime();
	
    long postprocessTime1 = getCurrentTime();
    rknn_msg_send(out_data0, out_data1, w, h, (void *)&cpm_result);

    while(send_count >= 5) {
        usleep(200);
    }
	
    long postprocessTime2 = getCurrentTime();
    send_count++;
}

void joint_camera_callback(void *p, int w, int h)
{
    unsigned char* srcbuf = (unsigned char *)p;

    // Send camera data to minigui layer
    yuv_draw(srcbuf, 0, SRC_RKRGA_FMT, w, h);
    YUV420toRGB24_RGA(SRC_RKRGA_FMT, srcbuf, w, h,
                      DST_RKRGA_FMT, g_rga_buf_fd, DST_W, DST_H);
    memcpy(g_mem_buf, g_rga_buf_bo.ptr, DST_W * DST_H * DST_BPP / 8);
    joint_rknn_process(g_mem_buf, DST_W, DST_H, DST_BPP);
}

int joint_post(void *flag)
{
    int width;
    int heigh;
    float *predictions;
    float *output_classes;
    float *group;

    while(*(int *)flag) {
        rknn_msg_recv(&predictions, &output_classes, &width, &heigh, (void *)&group);
        send_count--;
        postProcessCPM(predictions, group, CPM_NUM);
        if (predictions)
            free(predictions);
        if (output_classes)
            free(output_classes);
        joint_paint_object_msg();
    }
}

int joint_run(void *flag)
{
    int status = 0;

    // Init
    int model_len = 0;
    unsigned char* model = load_model(MODEL_NAME, &model_len);
    status = rknn_init(&ctx, model, model_len, 0);
    if(status < 0) {
        printf("rknn_init fail! ret=%d\n", status);
        return -1;
    }

    // Get Model Input Output Info
    rknn_input_output_num io_num;
    status = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (status != RKNN_SUCC) {
        printf("rknn_query fail! ret=%d\n", status);
        return -1;
    }
    printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

    printf("input tensors:\n");
    rknn_tensor_attr input_attrs[io_num.n_input];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (int i = 0; i < io_num.n_input; i++) {
        input_attrs[i].index = i;
        status = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (status != RKNN_SUCC) {
            printf("rknn_query fail! ret=%d\n", status);
            return -1;
        }
        printRKNNTensor(&(input_attrs[i]));
    }

    printf("output tensors:\n");
    rknn_tensor_attr output_attrs[io_num.n_output];
    memset(output_attrs, 0, sizeof(output_attrs));
    for (int i = 0; i < io_num.n_output; i++) {
        output_attrs[i].index = i;
        status = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
        if (status != RKNN_SUCC) {
            printf("rknn_query fail! ret=%d\n", status);
            return -1;
        }
        printRKNNTensor(&(output_attrs[i]));
    }

    printf("start camera run\n");

 // Open Camera and Run
    cameraRun(DEV_NAME, SRC_W, SRC_H, SRC_FPS, SRC_V4L2_FMT,
              joint_camera_callback, (int*)flag);
    printf("exit camera run\n");

 // Release
    if(model) {
        free(model);
    }
    if(ctx > 0) {
        rknn_destroy(ctx);
    }

    return status;
}

int joint_init(int arg)
{
    rknn_msg_init();
    joint_buffer_init(DST_W, DST_H, DST_BPP);
}

int joint_deinit()
{
    joint_buffer_deinit();
    rknn_msg_deinit();
}
