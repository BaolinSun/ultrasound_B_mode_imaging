#include "../data_process/compress.h"

#include <stdlib.h>
#include "string.h"
#include <math.h>
#include "xil_cache.h"

#define DIFF_DATA_RANGE 2048

int compare(const void *a, const void *b) {
	node **nodeA = (node **) a;
	node **nodeB = (node **) b;
	return ((*nodeB)->freq) - ((*nodeA)->freq);
}

u32 memory_check(void *memory_ptr) {
	if (memory_ptr == NULL) {
		xil_printf("memory allocate error\r\n");
		return XST_FAILURE;
	}
	return XST_SUCCESS;
}

node *create_node(int value, int freq) {
	node *new_node = malloc(sizeof(node));
	memory_check(new_node);
	new_node->value = value;
	new_node->freq = freq;
	new_node->left = NULL;
	new_node->right = NULL;
	new_node->parent = NULL;
	return new_node;
}

node *merge_nodes(node *left, node *right) {
	node *new_node = create_node(-1, left->freq + right->freq);
	new_node->left = left;
	new_node->right = right;
	left->parent = new_node;
	right->parent = new_node;
	return new_node;
}

// 创建新的栈节点
StackNode* newStackNode(node *node) {
	static int i = 0;
	i++;
	StackNode* stackNode = malloc(sizeof(StackNode));
	memory_check(stackNode);
	stackNode->node = node;
	stackNode->next = NULL;
	stackNode->id = i;
	return stackNode;
}

// 判断栈是否为空
int isStackEmpty(StackNode *root) {
	return !root;
}

// 入栈操作
void push(StackNode** root, node *node, const char *code) {
	StackNode* stackNode = newStackNode(node);
	strcpy(stackNode->node->code, code);
	stackNode->next = *root;
	*root = stackNode;
}

// 出栈操作
StackNode* pop(StackNode** root) {
	if (isStackEmpty(*root))
		return NULL;
	StackNode* temp = *root;
	*root = (*root)->next;
	return temp;
}
node *build_huffman_tree(node *nodes[], int count) {
	int temp_count = count;
	while (temp_count > 1) {
		// 首先排序
		qsort(nodes, temp_count, sizeof(node*), compare);
		// for (int i = 0; i < temp_count; i++)
		// {
		//     printf("%d,%d\n",nodes[i]->value,nodes[i]->freq);
		// }
		// printf(" -----%d----\n" ,temp_count);
		// 然后合并
		node *left = nodes[temp_count - 2];
		node *right = nodes[temp_count - 1];
		node *new_node = merge_nodes(left, right);
		// printf(" left:%d,%d  right:%d,%d  merge node: :%d,%d\n",left->value,left->freq,right->value,right->freq,new_node->value,new_node->freq);
		nodes[temp_count - 2] = new_node;
		temp_count--;
	}
	return nodes[0];

}

void generate_huffman_codes(node* tree_root, char *codes[]) {
	if (tree_root == NULL)
		return;

	StackNode *stack = NULL;
	char scode[100] = "";
	push(&stack, tree_root, scode);

	while (!isStackEmpty(stack)) {
		StackNode *current = pop(&stack);
		node *node = current->node;

		if (node->left == NULL && node->right == NULL) {
			// 叶子节点，输出编码
//			xil_printf("%d: %s\r\n", node->value, current->node->code);
			strcpy(codes[node->value], current->node->code);
		}

		if (node->right != NULL) {
			char newCode[20];
			strcpy(newCode, current->node->code);
			strcat(newCode, "0");
			push(&stack, node->right, newCode);
		}

		if (node->left != NULL) {
			char newCode[20];
			strcpy(newCode, current->node->code);
			strcat(newCode, "1");
			push(&stack, node->left, newCode);
		}
		free(current);
	}

}
char *encode_data(u16 *data, int length, char *codes[]) {
	char *encoded_data = malloc(length * 15 * sizeof(char));
	memory_check(encoded_data);
	int index = 0;
	for (int i = 0; i < length; i++) {
		strcpy(encoded_data + index, codes[data[i]]);
		index += strlen(codes[data[i]]);
	}
	return encoded_data;
}
u8 *str_to_int(char *str, int length) {
	u8 *data = malloc(length * sizeof(u8));
	memory_check(data);
	int index = 0;
	for (int i = 0; i < strlen(str); i += 8) {
		u8 num = 0;
		for (int j = 0; j < 8; j++) {
			num += (str[i + j] - '0') << (7 - j);
		}
		data[index] = num;
		index++;
	}
	return data;
}

u16 *diff(u16 *data, u16 length) {
	u16 *diff_data = malloc(length * sizeof(u16));
	memory_check(diff_data);
	diff_data[0] = data[0];
	int i = 1;
	while (i < length) {
		diff_data[i] = data[i] - data[i - 1] + 1023;
		i = i + 1;
	}
	return diff_data;
}

void free_hulfman_tree(node *node) {
	if (node == NULL) {
		return;
	}
	free_hulfman_tree(node->left);
	free_hulfman_tree(node->right);
	if (node->value == -1) {
		free(node);
	}
}

hulfman_encode_data *hulfman_encode(u16 *data, u16 data_length) {
	//1.原始数据转为差分数据
	u16 *diff_data = diff(data, data_length);
	//2,统计数据频率
	static u16 freq[DIFF_DATA_RANGE] = { 0 };
	memset(freq, 0, DIFF_DATA_RANGE * sizeof(u16));
	for (int i = 0; i < data_length; i++) {
		freq[diff_data[i]] += 1;
	}
	u16 count = 0;

	for (int i = 0; i < data_length; i++) {
		if (freq[i] != 0) {
			count += 1;
		}
	}
	u16 *index_to_value = malloc(count * sizeof(u16));
	count = 0;
	// 然后只统计出现过的数字的频率,同时建立一个映射表
	for (int i = 0; i < 2048; i++) {
		if (freq[i] != 0) {
			index_to_value[count] = i;
			count++;
		}
	}
//	xil_printf("data freq count  = %d\r\n", count);
	//3,创建节点数组
	node *nodes = malloc(count * sizeof(node));
	node **node_ptr = (node **) malloc(count * sizeof(node*));

	for (int j = 0; j < count; j++) {

		nodes[j].value = index_to_value[j];
		nodes[j].freq = freq[index_to_value[j]];
//		xil_printf("node value: %d   freq: %d \r\n", nodes[j].value,
//				nodes[j].freq);
		nodes[j].left = NULL;
		nodes[j].right = NULL;
		node_ptr[j] = &nodes[j];
	}
	// 4,构建霍夫曼树
	node *huffman_tree = build_huffman_tree(node_ptr, count);
//	xil_printf("Hulfman tree built\r\n");
//	xil_printf("tree root node value: %d   freq: %d \r\n", huffman_tree->value,huffman_tree->freq);
	// 5,创建霍夫曼编码表
	char *codes[DIFF_DATA_RANGE];
	for (int i = 0; i < DIFF_DATA_RANGE; i++) {
		codes[i] = malloc(20 * sizeof(char));
		memory_check(codes[i]);
	}
	// 然后开始生成编码
	char *current_code = (char *) malloc(20 * sizeof(char));
	*current_code = '\0';
	generate_huffman_codes(huffman_tree, codes);
//	xil_printf("Huffman codes generated\r\n");

	// 编码生成之后可以释放霍夫曼树了

	free_hulfman_tree(huffman_tree);
	free(nodes);
	// 同时释放
	free(node_ptr);
	// 然后开始压缩数据
	char *encoded_data = encode_data(diff_data, data_length, codes);
//	xil_printf("Data encoded\r\n");
	// 最后输出压缩后的数据

//	xil_printf("encode data length =%d\r\n", strlen(encoded_data));
	int padding = strlen(encoded_data) % 8;
	// 补0来完成字符串向字节的转换
	char *padding_code = malloc((padding + 1) * sizeof(char));
	memory_check(padding_code);
	for (int i = 0; i < padding; i++) {
		padding_code[i] = '0';
	}
	padding_code[padding] = '\0';
	strcat(encoded_data, padding_code);
	// 释放内存
	free(padding_code);
	for (int i = 0; i < DIFF_DATA_RANGE; i++) {
		free(codes[i]);
	}
	free(diff_data);
//	xil_printf("encode data: %s\r\n", encoded_data);
	// 将字符串转换为字节
	int encoded_data_length = strlen(encoded_data) / 8;
	u8 *encode_data = str_to_int(encoded_data, encoded_data_length);
	free(encoded_data);
	hulfman_encode_data * h_f_data = (hulfman_encode_data *) malloc(
			sizeof(hulfman_encode_data));
	h_f_data->encode_data = encode_data;
	h_f_data->freq_table = index_to_value;
	h_f_data->freq_table_length = count;
	h_f_data->padding_length = padding;

	return h_f_data;
}

u32 data_bits_compress(u16 *data, u32 data_length) {
	int outputIndex = 0;
	int bitOffset = 0;
	int i = 0;
//	Xil_DCacheInvalidateRange((u8 *) data, 262144);
	u8* data_compress = (u8 *) data;
	while (i < data_length) {
		// 提取低10位有效数据
		uint16_t validData = data[i] & 0x03FF;
		// 将有效数据放入输出缓冲区
		for (int j = 0; j < 10; j++) {
			if (bitOffset == 8) {
				// 当一个字节填满时，移动到下一个字节
				outputIndex++;
				bitOffset = 0;
			}
			// 将当前位放入输出字节的相应位置
			data_compress[outputIndex] |= (((validData >> j) & 1) << bitOffset);
			bitOffset++;
		}
		i += 1;
	}
	return outputIndex;
}
u32 fast_data_bits_compress(u16 *data, u32 data_length) {
	int outputIndex = 0;
	int inputIndex = 0;
	int bitOffset = 0;
	int i = 0;
	u8* data_compress = (u8 *) data;
	u8 data_buffer[10] = { 0 };
	while (i < data_length) {

		data_buffer[0] = data[i] & 0xFF;	// 8 D0[7:0]
		data_buffer[1] = ((data[i + 1] & 0x3F) << 2) | ((data[i] >> 8) & 0x03); //D1[5:0] D0[9:8]
		data_buffer[2] = ((data[i + 2] & 0x0F) << 4)
				| ((data[i + 1] >> 6) & 0x0F); //  D2[3:0] D1[9:6]
		data_buffer[3] = ((data[i + 3] & 0x03) << 6)
				| ((data[i + 2] >> 4) & 0x3F); // D3[1:0] D2[9:4]
		data_buffer[4] = ((data[i + 3] >> 2) & 0xFF); //D3[9:2]
		data_buffer[5] = data[i + 4] & 0xFF; //D4[7:0]
		data_buffer[6] = ((data[i + 5] & 0x3F) << 2)
				| ((data[i + 4] >> 8) & 0x03); //D5[5:0] D4[9:8]
		data_buffer[7] = ((data[i + 6] & 0x0F) << 4)
				| ((data[i + 5] >> 6) & 0x0F); // D6[3:0] D5[9:6]
		data_buffer[8] = ((data[i + 7] & 0x03) << 6)
				| ((data[i + 6] >> 4) & 0x3F); // D7[1:0] D6[9:4]
		data_buffer[9] = (data[i + 7] >> 2) & 0xFF; // D7[9:2]

//		data_buffer[10] = data[i + 8] & 0xFF;	// 8 D0[7:0]
//		data_buffer[11] = ((data[i + 9] & 0x3F) << 2)
//				| ((data[i + 8] >> 8) & 0x03); //D1[5:0] D0[9:8]
//		data_buffer[12] = ((data[i + 10] & 0x0F) << 4)
//				| ((data[i + 9] >> 6) & 0x0F); //  D2[3:0] D1[9:6]
//		data_buffer[13] = ((data[i + 11] & 0x03) << 6)
//				| ((data[i + 10] >> 4) & 0x3F); // D3[1:0] D2[9:4]
//		data_buffer[14] = ((data[i + 11] >> 2) & 0xFF); //D3[9:2]
//		data_buffer[15] = data[i + 12] & 0xFF; //D4[7:0]
//		data_buffer[16] = ((data[i + 13] & 0x3F) << 2)
//				| ((data[i + 12] >> 8) & 0x03); //D5[5:0] D4[9:8]
//		data_buffer[17] = ((data[i + 14] & 0x0F) << 4)
//				| ((data[i + 13] >> 6) & 0x0F); // D6[3:0] D5[9:6]
//		data_buffer[18] = ((data[i + 15] & 0x03) << 6)
//				| ((data[i + 14] >> 4) & 0x3F); // D7[1:0] D6[9:4]
//		data_buffer[19] = (data[i + 15] >> 2) & 0xFF; // D7[9:2]
		// 将有效数据放入输出缓冲区
		for (int j = 0; j < 10; j++) {
			data_compress[outputIndex + j] = data_buffer[j];
		}
		outputIndex += 10;
		i += 8;
	}
	return outputIndex;
}

