#ifndef __RKNN_MSG_H__
#define __RKNN_MSG_H__

typedef struct _RKNN_MSG {
	float *out_data0;
	float *out_data1;
	int w;
	int h;
	void *group;
} RKNN_MSG;

int rknn_msg_init();
void rknn_msg_deinit();
int rknn_msg_send(float * predictions, float *output_classes,
	              int width, int heigh, void *group);
int rknn_msg_recv(float ** predictions, float **output_classes,
	              int *width, int *heigh, void **group);
#endif