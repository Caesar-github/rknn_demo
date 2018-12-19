#include <stdio.h>
#include <rga/RgaApi.h>
#include <linux/videodev2.h>
#include "include/rknn_runtime.h"
#include "rknn_msg.h"
#include "yuv.h"
#include "ssd.h"
#include "ssd_post.h"
#include "v4l2camera.h"

#define DEV_NAME      "/dev/video0"
#define MODEL_NAME    "/usr/share/rknn_demo/ssd_mobilenet_v1_coco.rknn"

#define SRC_W         640
#define SRC_H         480
#define SRC_FPS       30
#define SRC_BPP       24
#define DST_W         300
#define DST_H         300
#define DST_BPP       24

#define SRC_V4L2_FMT  V4L2_PIX_FMT_YUV420
#define SRC_RKRGA_FMT RK_FORMAT_YCbCr_420_P
#define DST_RKRGA_FMT RK_FORMAT_RGB_888

float g_fps;
bo_t g_rga_buf_bo;
int g_rga_buf_fd;
char *g_mem_buf;
rknn_context_t ctx;
struct ssd_group g_ssd_group;
volatile int send_count;

extern
int yuv_draw(char *src_ptr, int src_fd, int format, int src_w, int src_h);
extern void ssd_paint_object_msg();
extern void ssd_paint_fps_msg();

inline struct ssd_group* ssd_get_ssd_group()
{
    return &g_ssd_group;
}

inline float ssd_get_fps()
{
    return g_fps;
}

#define CHECK_STATUS(func) do { \
    status = func; \
    if (status < 0)  { \
        goto final; \
    }   \
} while(0)


long getCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

long cal_fps(float *cal_fps)
{
    static int count = 0;
    static float fps = 0;
    static long begin_time = 0;
    static long end_time = 0;

    count++;
    if (begin_time == 0)
        begin_time = getCurrentTime();
    if (count >= 10) {
        end_time = getCurrentTime();
        fps = (float)10000 / (float)(end_time - begin_time);
        begin_time = end_time;
        count = 0;
        *cal_fps = fps;
    }
    ssd_paint_fps_msg();
}

int ssd_buffer_init(int width, int height, int bpp)
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

int ssd_buffer_deinit()
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

int ssd_rknn_process(char* in_data, int w, int h, int c)
{
    int status = 0;
    int in_size;
    int out_size0;
    int out_size1;
    float *out_data0 = NULL;
    float *out_data1 = NULL;
  //   printf("camera callback w=%d h=%d c=%d\n", w, h, c);
    cal_fps(&g_fps);

    long runTime1 = getCurrentTime();

    long setinputTime1 = getCurrentTime();
    RKNNSetInput(ctx, 0, in_data, w * h * c / 8);
    long setinputTime2 = getCurrentTime();
   //  printf("set input time:%0.2ldms\n", setinputTime2-setinputTime1);

    RKNNRun(ctx);

    out_size0 = RKNNGetOutputSize(ctx, 0);
    out_data0 = (float *) malloc(out_size0  * sizeof(float));
    RKNNGetOutput(ctx, 0, out_data0, out_size0);

    out_size1 = RKNNGetOutputSize(ctx, 1);
    out_data1 = (float *) malloc(out_size1  * sizeof(float));
    RKNNGetOutput(ctx, 1, out_data1, out_size1);

    long runTime2 = getCurrentTime();
   // printf("rknn run time:%0.2ldms\n", runTime2 - runTime1);

    long postprocessTime1 = getCurrentTime();
    rknn_msg_send(out_data1, out_data0, w, h, &g_ssd_group);
    while(send_count >= 5) {
        printf("sleep now \n");
        usleep(200);
    }
    long postprocessTime2 = getCurrentTime();
    send_count++;
    //printf("post process time:%0.2ldms\n", postprocessTime2 - postprocessTime1);
    // if (out_data1)
    //     free(out_data1);
    // if (out_data0)
    //     free(out_data0);
    // ssd_paint_object_msg();
}

void ssd_camera_callback(void *p, int w, int h)
{
    unsigned char* srcbuf = (unsigned char *)p;
    // Send camera data to minigui layer
    yuv_draw(srcbuf, 0, SRC_RKRGA_FMT, w, h);
    YUV420toRGB24_RGA(SRC_RKRGA_FMT, srcbuf, w, h,
                      DST_RKRGA_FMT, g_rga_buf_fd, DST_W, DST_H);
    memcpy(g_mem_buf, g_rga_buf_bo.ptr, DST_W * DST_H * DST_BPP / 8);
    ssd_rknn_process(g_mem_buf, DST_W, DST_H, DST_BPP);
}

int ssd_post(void *flag)
{
    int width;
    int heigh;
    float *predictions;
    float *output_classes;
    struct ssd_group *group;

    while(*(int *)flag) {
        rknn_msg_recv(&predictions, &output_classes, &width, &heigh, (void *)&group);
        send_count--;
        postProcessSSD(predictions, output_classes, width, heigh, group);
        if (predictions)
            free(predictions);
        if (output_classes)
            free(output_classes);
        ssd_paint_object_msg();
    }
}

int ssd_run(void *flag)
{
    int status = 0;
    /* Create Context */
    ctx = RKNNCreateContext();
    if (ctx == 0)
    {
        goto final;
    }

    /* Create the neural network */
    printf("RKNNBuildGraph...\n");
    CHECK_STATUS(RKNNBuildGraph(ctx, MODEL_NAME));
    cameraRun(DEV_NAME, SRC_W, SRC_H, SRC_FPS, SRC_V4L2_FMT,
              ssd_camera_callback, (int*)flag);
final:
    RKNNReleaseContext(ctx);
    return status;
}


int ssd_init(int arg)
{
    rknn_msg_init();
    ssd_buffer_init(DST_W, DST_H, DST_BPP);
}

int ssd_deinit()
{
    ssd_buffer_deinit();
    rknn_msg_deinit();
}
